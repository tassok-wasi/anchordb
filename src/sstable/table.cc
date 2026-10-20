// Copyright 2026 Tassok Imam Wasi

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sstable/table.h"
#include "anchordb/env.h"
#include "anchordb/status.h"
#include "file/filename.h"
#include "sstable/data_record.h"
#include "sstable/empty_iter.h"
#include "sstable/filter_block.h"
#include "sstable/idx_block_reader.h"
#include "sstable/idx_block_writer.h"
#include "sstable/idx_summary.h"
#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

namespace {

// Fixed byte overhead charged to the table cache per open table, on top of the
// resident summary bytes.
constexpr size_t kTableCacheEntryOverhead = 256;

// Upper bound on a summary file we are willing to load into memory.
constexpr uint64_t kMaxSummaryFileSize = 64ull * 1024 * 1024;

// Upper bound on a filter file we are willing to load into memory.
constexpr uint64_t kMaxFilterFileSize = 64ull * 1024 * 1024;

// Reads exactly `n` bytes at `offset` into *buf and points *out at them.
// Fails if fewer than `n` bytes are available.
Status read_exact(RandomAccessFileReader *reader, uint64_t offset, size_t n,
                  std::string *buf, std::string_view *out) {
    *out = std::string_view();
    if (n == 0) {
        buf->clear();
        return Status::ok();
    }

    buf->resize(n);
    std::string_view view;
    Status s = reader->read(offset, n, &view, buf->data());
    if (!s.is_ok()) { return s; }
    if (view.size() < n) {
        return Status::corruption("short read", std::to_string(view.size()));
    }
    if (view.data() != buf->data()) {
        std::memmove(buf->data(), view.data(), n);
    }
    *out = std::string_view(buf->data(), n);
    return Status::ok();
}

// First slot of `reader` whose key is >= target (lower_bound within a block).
size_t lower_bound(const Comparator &cmp, const IndexBlockReader &reader,
                   std::string_view target) {
    size_t lo = 0;
    size_t hi = reader.size();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (cmp.compare(reader.key(mid), target) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

} // namespace

// All of a Table's state and block/data accessors.  Keeping them here (rather
// than in the header) hides the SSTable formats from every includer of table.h.
struct Table::Rep {
    Rep() = default;

    Rep(const Rep &) = delete;
    Rep &operator=(const Rep &) = delete;

    ~Rep() {
        delete summary;
        delete index_reader;
        delete data_reader;
    }

    // Reads the index block at `block_offset` (on-disk length
    // `compressed_size`, both from the summary) and decompresses it when a
    // compressor is configured.  *out points into *stored_buf or *plain_buf,
    // whichever holds the parseable block, so both must outlive its use.
    Status read_index_block(uint64_t block_offset, uint32_t compressed_size,
                            std::string *stored_buf, std::string *plain_buf,
                            std::string_view *out) const {
        *out = std::string_view();
        if (block_offset > index_file_size ||
            compressed_size > index_file_size - block_offset) {
            return Status::corruption("index block out of range",
                                      std::to_string(block_offset));
        }

        std::string_view stored;
        Status s = read_exact(index_reader, block_offset, compressed_size,
                              stored_buf, &stored);
        if (!s.is_ok()) { return s; }

        if (compressor.uncompress == nullptr) {
            // Blocks are stored verbatim as full block_size segments; anything
            // else means the summary and index disagree.
            if (stored.size() != block_size) {
                return Status::corruption("index block not block sized",
                                          std::to_string(stored.size()));
            }
            *out = stored;
            return Status::ok();
        }
        if (compressor.get_uncompressed_length == nullptr) {
            return Status::corruption("index block: compressor lacks length");
        }

        size_t uncompressed_size = 0;
        if (!compressor.get_uncompressed_length(stored.data(), stored.size(),
                                                &uncompressed_size)) {
            return Status::corruption("index block: bad compressed length");
        }
        // Every index block decompresses to exactly the block size recorded in
        // the summary header.
        if (uncompressed_size != block_size) {
            return Status::corruption(
                "index block not block sized after decompression",
                std::to_string(uncompressed_size));
        }
        plain_buf->resize(uncompressed_size);
        if (!compressor.uncompress(stored.data(), stored.size(),
                                   plain_buf->data())) {
            return Status::corruption("index block decompression failed");
        }
        *out = std::string_view(plain_buf->data(), uncompressed_size);
        return Status::ok();
    }

    // Reads and validates the data record referenced by `handle`,
    // cross-checking it against `expected_key` and the handle metadata. Returns
    // Status::not_found for tombstones, Status::not_supported for blob values.
    Status read_record(const ValueHandle &handle, std::string_view expected_key,
                       std::string *value, std::string *scratch) const {
        if (value == nullptr || scratch == nullptr) {
            return Status::invalid_argument(
                "Table::read_record: null argument");
        }
        // The index stores the exact layout size of the data record, so the
        // whole record can be fetched with a single I/O and no probing.
        if (handle.data_size < kDataRecordHeaderSize) {
            return Status::corruption("data record size too small",
                                      std::to_string(handle.data_size));
        }
        if (handle.offset >= data_file_size ||
            handle.data_size > data_file_size - handle.offset) {
            return Status::corruption("data record out of range",
                                      std::to_string(handle.offset));
        }

        std::string_view view;
        Status s = read_exact(data_reader, handle.offset, handle.data_size,
                              scratch, &view);
        if (!s.is_ok()) { return s; }

        ParsedDataRecord rec;
        std::string_view cursor = view;
        s = decode_data_record(&cursor, &rec);
        if (!s.is_ok()) { return s; }
        if (!cursor.empty()) {
            return Status::corruption("trailing bytes after data record");
        }
        if (rec.key != expected_key) {
            return Status::corruption("data record key mismatch");
        }
        if (rec.type != handle.type || rec.flag != handle.flag) {
            return Status::corruption("index/data record metadata mismatch");
        }
        if (rec.type == ValueType::deletion) {
            return Status::not_found("tombstone", std::string(expected_key));
        }
        if (rec.flag == ValueFlag::blob) {
            return Status::not_supported("vLog read not implemented");
        }
        value->assign(rec.value.data(), rec.value.size());
        return Status::ok();
    }

    Comparator comparator;
    // Copied from Options; a null compressor means index blocks are stored
    // verbatim and are parsed directly (see read_index_block).
    Compressor compressor;
    std::string summary_buffer;
    SummaryIndexReader *summary = nullptr;
    // Resident filter file bytes and its parsed reader.  `filter` is null when
    // the table has no (usable) filter.
    std::string filter_buffer;
    std::unique_ptr<FilterBlockReader> filter;
    RandomAccessFileReader *index_reader = nullptr;
    RandomAccessFileReader *data_reader = nullptr;
    uint64_t index_file_size = 0;
    uint64_t data_file_size = 0;
    size_t block_count = 0;
    // Index-block segment size, recovered from the summary header. Every index
    // block is read and validated against it.
    size_t block_size = 0;
};

// Bidirectional iterator over a Table, keeping one (possibly compressed) index
// block resident at a time and spanning block boundaries.  Tombstone entries
// are filtered out; blob (vLog) values surface an error via status().  Internal
// to this translation unit.
class TableIterator : public Iterator {
  public:
    TableIterator(Table *table, ReadOptions options)
        : table_(table), comparator_(table->rep_->comparator),
          block_count_(table->rep_->block_count) {
        (void)options;
        table_->ref();
    }

    ~TableIterator() override { table_->unref(); }

    TableIterator(const TableIterator &) = delete;
    TableIterator &operator=(const TableIterator &) = delete;

    bool valid() const override { return valid_; }

    void seek_to_first() override {
        status_ = Status::ok();
        valid_ = false;
        if (block_count_ == 0) {
            invalidate();
            return;
        }
        load_block(0);
        if (!status_.is_ok()) { return; }
        slot_ = 0;
        skip_forward();
    }

    void seek_to_last() override {
        status_ = Status::ok();
        valid_ = false;
        if (block_count_ == 0) {
            invalidate();
            return;
        }
        load_block(block_count_ - 1);
        if (!status_.is_ok()) { return; }
        slot_ = reader_->size();
        skip_backward();
    }

    void seek(std::string_view target) override {
        status_ = Status::ok();
        valid_ = false;
        if (block_count_ == 0) {
            invalidate();
            return;
        }

        const size_t block_index = table_->rep_->summary->lower_bound(target);
        if (block_index >= table_->rep_->summary->size()) {
            invalidate();
            return;
        }
        load_block(block_index);
        if (!status_.is_ok()) { return; }
        slot_ = lower_bound(comparator_, *reader_, target);
        skip_forward();
    }

    void next() override {
        assert(valid_);
        ++slot_;
        skip_forward();
    }

    void prev() override {
        assert(valid_);
        if (slot_ > 0) {
            --slot_;
            skip_backward();
            return;
        }
        if (block_index_ == 0) {
            invalidate();
            return;
        }
        load_block(block_index_ - 1);
        if (!status_.is_ok()) {
            invalidate();
            return;
        }
        slot_ = reader_->size();
        skip_backward();
    }

    std::string_view key() const override {
        assert(valid_);
        return key_;
    }

    std::string_view value() const override {
        assert(valid_);
        return value_;
    }

    Status status() const override { return status_; }

  private:
    bool visible(size_t slot) const {
        return reader_->entry(slot).handle.type != ValueType::deletion;
    }

    void invalidate() {
        valid_ = false;
        delete reader_;
        reader_ = nullptr;
        key_ = std::string_view();
        value_ = std::string_view();
    }

    void load_block(size_t block_index) {
        block_index_ = block_index;
        delete reader_;
        reader_ = nullptr;
        // The summary gives this block's absolute offset and on-disk length.
        const SummaryEntry &entry = table_->rep_->summary->entry(block_index);
        std::string_view block;
        Status s = table_->rep_->read_index_block(
            entry.block_offset, entry.compressed_size, &stored_buf_,
            &plain_buf_, &block);
        if (!s.is_ok()) {
            status_ = s;
            invalidate();
            return;
        }
        reader_ = new IndexBlockReader(block, table_->rep_->block_size);
        if (!reader_->valid()) {
            status_ = Status::corruption("invalid index block");
            invalidate();
        }
    }

    void materialize_current() {
        const IndexBlockEntry entry = reader_->entry(slot_);
        key_ = entry.key;
        value_buf_.clear();
        Status s = table_->rep_->read_record(entry.handle, key_, &value_buf_,
                                             &scratch_);
        if (!s.is_ok()) {
            status_ = s;
            invalidate();
            return;
        }
        value_ = value_buf_;
        valid_ = true;
    }

    void skip_forward() {
        while (true) {
            if (reader_ == nullptr) {
                invalidate();
                return;
            }
            if (slot_ < reader_->size()) {
                if (visible(slot_)) {
                    materialize_current();
                    return;
                }
                ++slot_;
                continue;
            }
            if (block_index_ + 1 >= block_count_) {
                invalidate();
                return;
            }
            load_block(block_index_ + 1);
            if (!status_.is_ok()) {
                invalidate();
                return;
            }
            slot_ = 0;
        }
    }

    void skip_backward() {
        while (true) {
            if (reader_ == nullptr) {
                invalidate();
                return;
            }
            if (slot_ >= reader_->size()) {
                // Positioned just past the end of a block: step to its last
                // entry, or move to the previous block if it is empty.
                if (reader_->size() == 0) {
                    if (block_index_ == 0) {
                        invalidate();
                        return;
                    }
                    load_block(block_index_ - 1);
                    if (!status_.is_ok()) {
                        invalidate();
                        return;
                    }
                    continue;
                }
                slot_ = reader_->size() - 1;
                continue;
            }
            if (visible(slot_)) {
                materialize_current();
                return;
            }
            if (slot_ == 0) {
                if (block_index_ == 0) {
                    invalidate();
                    return;
                }
                load_block(block_index_ - 1);
                if (!status_.is_ok()) {
                    invalidate();
                    return;
                }
                slot_ = reader_->size();
                continue;
            }
            --slot_;
        }
    }

    Table *table_;
    Comparator comparator_;
    size_t block_count_;

    size_t block_index_ = 0;
    IndexBlockReader *reader_ = nullptr;
    // On-disk (compressed) bytes of the resident block and, when decompressed,
    // the plain bytes the reader parses.
    std::string stored_buf_;
    std::string plain_buf_;

    size_t slot_ = 0;
    bool valid_ = false;

    std::string value_buf_;
    std::string scratch_;
    std::string_view key_;
    std::string_view value_;
    Status status_;
};

TableFileSet make_table_file_set(std::string_view db_dir,
                                 uint64_t table_number) {
    TableFileSet set;
    set.data = TableDataFileName(db_dir, table_number);
    set.index = TableIndexFileName(db_dir, table_number);
    set.summary = TableSummaryFileName(db_dir, table_number);
    set.filter = TableFilterFileName(db_dir, table_number);
    return set;
}

Status Table::open(const Options &options, const TableFileSet &files,
                   Table **result) {
    if (result == nullptr) {
        return Status::invalid_argument("Table::open: null result");
    }
    if (options.env == nullptr) {
        return Status::invalid_argument("Table::open: options.env is null");
    }

    uint64_t summary_size = 0;
    uint64_t index_size = 0;
    uint64_t data_size = 0;
    Status s = options.env->get_file_size(files.summary, &summary_size);
    if (!s.is_ok()) { return s; }
    s = options.env->get_file_size(files.index, &index_size);
    if (!s.is_ok()) { return s; }
    s = options.env->get_file_size(files.data, &data_size);
    if (!s.is_ok()) { return s; }
    if (summary_size > kMaxSummaryFileSize) {
        return Status::corruption("summary file too large",
                                  std::to_string(summary_size));
    }
    Rep *rep = new Rep();
    rep->comparator = options.comparator;
    if (rep->comparator.compare == nullptr) {
        rep->comparator = new_byte_wise_comparator();
    }
    rep->compressor = options.compressor;
    rep->index_file_size = index_size;
    rep->data_file_size = data_size;

    RandomAccessFileReader *raw_index = nullptr;
    s = options.env->new_random_access_file_reader(files.index, &raw_index);
    if (!s.is_ok()) {
        delete rep;
        return s;
    }
    rep->index_reader = raw_index;

    RandomAccessFileReader *raw_data = nullptr;
    s = options.env->new_random_access_file_reader(files.data, &raw_data);
    if (!s.is_ok()) {
        delete rep;
        return s;
    }
    rep->data_reader = raw_data;

    RandomAccessFileReader *raw_summary = nullptr;
    s = options.env->new_random_access_file_reader(files.summary, &raw_summary);
    if (!s.is_ok()) {
        delete rep;
        return s;
    }

    std::string_view summary_view;
    s = read_exact(raw_summary, 0, static_cast<size_t>(summary_size),
                   &rep->summary_buffer, &summary_view);
    // The summary bytes are fully resident; close the reader.
    delete raw_summary;
    if (!s.is_ok()) {
        delete rep;
        return s;
    }

    rep->summary = new SummaryIndexReader(rep->summary_buffer, rep->comparator);
    if (!rep->summary->valid()) {
        delete rep;
        return Status::corruption("invalid summary index");
    }
    // The summary header pins the index-block segment size for this table.
    rep->block_size = rep->summary->block_size();
    // The summary is the source of truth for the block layout: it names the
    // block count and, per block, the absolute offset and on-disk length.
    // Validate every entry maps inside index.bin.
    for (size_t i = 0; i < rep->summary->size(); ++i) {
        const SummaryEntry &entry = rep->summary->entry(i);
        if (entry.compressed_size == 0 || entry.block_offset > index_size ||
            entry.compressed_size > index_size - entry.block_offset) {
            delete rep;
            return Status::corruption("summary entry out of range",
                                      std::to_string(entry.block_offset));
        }
    }
    rep->block_count = rep->summary->size();

    // The per-table filter is optional: a table may have been built without
    // one, so a missing or unreadable filter disables filtering instead of
    // failing the open.  Skip the file entirely when no usable filter is
    // configured.
    const bool filter_configured = options.filter.key_may_match != nullptr &&
                                   options.filter.name != nullptr;
    if (filter_configured && !files.filter.empty()) {
        uint64_t filter_size = 0;
        Status fs = options.env->get_file_size(files.filter, &filter_size);
        if (fs.is_ok() && filter_size > 0) {
            if (filter_size > kMaxFilterFileSize) {
                delete rep;
                return Status::corruption("filter file too large",
                                          std::to_string(filter_size));
            }
            RandomAccessFileReader *raw_filter = nullptr;
            fs = options.env->new_random_access_file_reader(files.filter,
                                                            &raw_filter);
            if (!fs.is_ok()) {
                delete rep;
                return fs;
            }
            std::string_view filter_view;
            fs = read_exact(raw_filter, 0, static_cast<size_t>(filter_size),
                            &rep->filter_buffer, &filter_view);
            delete raw_filter;
            if (!fs.is_ok()) {
                delete rep;
                return fs;
            }
            auto reader = std::make_unique<FilterBlockReader>(
                options.filter, rep->filter_buffer);
            if (reader->valid()) { rep->filter = std::move(reader); }
        }
    }

    Table *table = new Table(rep);
    table->ref();
    *result = table;
    return Status::ok();
}

Table::~Table() {
    assert(refs_ == 0);
    delete rep_;
}

void Table::unref() {
    --refs_;
    assert(refs_ >= 0);
    if (refs_ <= 0) { delete this; }
}

size_t Table::cache_charge() const {
    return rep_->summary_buffer.size() + rep_->filter_buffer.size() +
           kTableCacheEntryOverhead;
}

Status Table::get(const ReadOptions &options, std::string_view key,
                  std::string *value, std::string *scratch) const {
    (void)options;
    if (value == nullptr) {
        return Status::invalid_argument("Table::get: null value");
    }
    if (scratch == nullptr) {
        return Status::invalid_argument("Table::get: null scratch");
    }

    // A definite filter miss means the key is absent, so skip all I/O.
    if (rep_->filter != nullptr && !rep_->filter->key_may_match(key)) {
        return Status::not_found("key not in table filter", std::string(key));
    }

    const size_t block_index = rep_->summary->lower_bound(key);
    if (block_index >= rep_->summary->size()) {
        return Status::not_found("key not in table range", std::string(key));
    }
    const SummaryEntry &entry = rep_->summary->entry(block_index);

    std::string stored_buf;
    std::string plain_buf;
    std::string_view block;
    Status s = rep_->read_index_block(entry.block_offset, entry.compressed_size,
                                      &stored_buf, &plain_buf, &block);
    if (!s.is_ok()) { return s; }

    IndexBlockReader reader(block, rep_->block_size);
    if (!reader.valid()) { return Status::corruption("invalid index block"); }

    const size_t slot = lower_bound(rep_->comparator, reader, key);
    if (slot >= reader.size() ||
        rep_->comparator.compare(reader.key(slot), key) != 0) {
        return Status::not_found("key not found", std::string(key));
    }

    return rep_->read_record(reader.entry(slot).handle, key, value, scratch);
}

Iterator *Table::new_iterator(const ReadOptions &options) {
    if (rep_->summary == nullptr || !rep_->summary->valid()) {
        return new_error_iterator(Status::corruption("invalid table"));
    }
    if (rep_->block_count == 0) { return new_empty_iterator(); }
    return new TableIterator(this, options);
}

} // namespace ANCHORDB_NAMESPACE

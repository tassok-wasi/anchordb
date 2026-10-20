# AnchorDB

> **Work in progress.** AnchorDB is under active development and is **not** ready for use. The public API, on-disk formats, and build setup may change without notice.

AnchorDB is a from-scratch LSM-tree storage engine written in C++20, organized as a WiscKey-style key-value/blob store. It follows the LevelDB/RocksDB lineage: a log-structured merge tree with an in-memory memtable, persistent write-ahead log, SSTables on disk, and a separate log for large values.

## Status

The storage primitives are largely in place (SSTables, memtable, WAL, block cache, filters, file and environment layers). The layers that tie them together are still being built:

- Compaction
- vLog data path and garbage collection
- Versioning / MANIFEST
- Database layer and public API
- Tests and benchmarks

No tests exist yet, and the API should be considered unstable.

## Requirements

- A C++20 compiler
- CMake 3.23+
- [vcpkg](https://github.com/microsoft/vcpkg) at `$HOME/vcpkg` (see `CMakePresets.json`); dependencies `zstd`, `snappy`, `lz4`, and `zlib` are installed by the preset
- `liburing` on Linux (required by the default build)

## Build

```sh
cmake --preset default
cmake --build build
```

This produces a static library, `build/libanchordb.a`.

## Layout

- `include/anchordb/` — public headers
- `src/` — implementation, grouped by subsystem (`sstable/`, `memtable/`, `db/`, `cache/`, `file/`, `env/`, `memory/`, `util/`)

## License

Apache-2.0. See `LICENSE` for details.

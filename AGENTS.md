# AGENTS.md

`anchordb` is a from-scratch C++20 LSM-tree storage engine (LevelDB/RocksDB lineage), organized as a WiscKey-style KV/blob store.

## Status
- Pre-alpha. **No tests, benchmarks, lint, or typecheck commands exist.** The CMake test block is commented out at the end of `CMakeLists.txt`; do not claim a test suite or invent a test command.
- Do not add files under `src/` without also adding them to `target_sources(anchordb PRIVATE ...)` in `CMakeLists.txt`; otherwise they are silently never compiled.
- `readorder`, `.rules`, `.zed/`, and `ARCHITECTURE.md` are gitignored local-only notes. They may be absent or stale; verify against code/config.
- The DB layer itself is not implemented yet: no compaction, vLog data path/GC, versioning/MANIFEST, recovery, snapshots, merge iterator, or public `db.h`. `include/anchordb/` holds only the primitive interfaces.

## Build
- Configure: `cmake --preset default` → Unix Makefiles, `Debug`, `build/`, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`.
- Build: `cmake --build build -j`; output is the static lib `build/libanchordb.a`.
- The preset hardcodes `CMAKE_TOOLCHAIN_FILE=$env{HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake`. `vcpkg.json` pulls `zstd`, `snappy`, `lz4`, `zlib`, `gperftools` into `build/vcpkg_installed/`.
- Linux: `liburing` is optional (`ANCHORDB_USE_IO_URING` defaults ON); if `pkg_check_modules(liburing)` fails the build warns and uses synchronous I/O.
- `.clangd` reads the compilation database from `build/`, so configure before expecting clangd to resolve headers.
- x86_64 builds add `-msse4.2 -mavx2`; IPO/LTO is enabled when supported.

## Formatting / style
- Run `clang-format -i <file>` after edits. `.clang-format`: LLVM base, 4-space indent, `IndentPPDirectives: BeforeHash`, short if/loop/function/block allowed on one line.
- Headers use `#pragma once`.
- New sources need the `// Copyright 2026 Tassok Imam Wasi` + Apache-2.0 header block, matching neighboring files.

## Architecture (verified non-obvious points)
- Public API lives in `include/anchordb/*.h`; private code under `src/` is reachable via the `PRIVATE` include dir `src/`.
- Core extension points `Comparator`, `Compressor`, and `Filter` are **plain function-pointer structs**, not virtual classes; follow that idiom. `Env`/`Logger`/readers/writers are the exception and use virtual classes (`EnvWrapper` is the decorator base).
- Internal key encoding (memtable + SSTable): `| user_key (variable) | 8-byte trailer |`, trailer = `(seq << 8) | type` with a 56-bit sequence. `ValueType::deletion` is a tombstone. Memtable `LookUpKey` prepends `varint32(internal_key_len)`. See `src/db/db_format.h`.
- Memtable ordering (`InternalKeyComparator`): user_key ascending, then sequence **descending** (newest first).
- Allocator: only `Arena` exists (`src/memory/arena.h`), a block bump allocator over global `operator new[]`; there is no `Allocator` interface or wrapper type. tcmalloc is linked process-wide by CMake.
- Memtable storage is one arena-backed single-writer/multi-reader skiplist: `src/memtable/skiplist.h`. Writer serialization is the caller's responsibility.
- Platform I/O split: `src/env/env.cc` + `posix.cc` (io_uring for random access, pread/pwrite fallback) on Unix, `src/port/win/env_win.cc` + `io_win.cc` (IOCP) on Windows. File handles are `*_reader`/`*_writer` abstractions under `src/file/`.
- **O_DIRECT is implemented and on by default** (`ANCHORDB_DIRECT_IO_DEFAULT=ON`); buffers/offsets are realigned to `k_o_direct_alignment` (4096) in `src/file/`.
- Trust `include/anchordb/options.h` for defaults: `block_size = 4 KB`, `write_buffer_size`/`max_file_size` = 4 MB/2 MB, `table_cache_size` = 8 MB, `max_open_files` = 1000. Cache replacement policy and hash-map backend are selected by `CacheType`/`MapType` enums.

## Gotchas
- `HAS_ZSTD/HAS_SNAPPY/HAS_LZ4/HAS_ZLIB` are set by CMake `find_package`; `src/port/port_stdcxx.h` defaults any unset one to 0. Compression sources always compile, so a missing library silently disables that codec (pass-through) instead of failing the build.
- tcmalloc (`ANCHORDB_USE_TCMALLOC`, default ON) is linked `PUBLIC` so its global new/delete override reaches every consumer; if not found CMake only warns.
- `build/`, `.cache/`, and `compile_commands.json` (symlink into `build/`) are generated — never edit them.
- CMake lists `src/sstable/empty_iter.h` and `src/sstable/iterator_wrapper.h` twice; harmless but don't copy the pattern.
- Large values are not readable yet: `BlobHandle` is defined but `Table::get` returns `not_supported` for the vLog path.

cmake_minimum_required(VERSION 3.23)

project(anchordb VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(anchordb SHARED)

target_sources(anchordb
    PRIVATE
        src/memtable/arena.h
        src/memtable/arena.cc
        src/memtable/struct.h
        src/memtable/mwmr_skiplist.h
        src/memtable/swmr_skiplist.h
        src/memtable/swsr_skiplist.h
        src/util/log_writer.h
        src/util/log_writer.cc
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS
            include
        FILES
            include/anchordb/macro.h
            include/anchordb/export.h
            include/anchordb/slice.h
)

cmake_minimum_required(VERSION 3.23)

project(anchordb VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# add_compile_definitions(MWMR_MEMTABLE_SKIPLIST = 1) # only one can be selected
# add_compile_definitions(SWMR_MEMTABLE_SKIPLIST = 1)
# add_compile_definitions(SWSR_MEMTABLE_SKIPLIST = 1)

add_library(anchordb SHARED)

target_sources(anchordb
    PRIVATE
        src/memtable/arena.h
        src/memtable/arena.cc
        src/memtable/mwmr_skiplist.h
        src/memtable/swmr_skiplist.h
        src/memtable/swsr_skiplist.h
        src/export.h
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS
            include
        FILES
            include/anchordb/macro.h
)

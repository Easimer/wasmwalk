cmake_minimum_required(VERSION 3.16)
project(wasmwalk)

add_executable(wasmwalk)

target_sources(wasmwalk
  PRIVATE
    src/entry.cpp
    src/wasmobj.cpp
    src/wasmobj.h
    src/reader.cpp
    src/reader.h

    src/types.h
    src/wasm.h
)
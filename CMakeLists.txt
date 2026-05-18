cmake_minimum_required(VERSION 3.14)
project(btor2rw LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(btor2rw
  src/main.cpp
  src/IR.cpp
  src/IR.h
  src/Btor2Loader.cpp
  src/Btor2Loader.h
  src/Smt2Emitter.cpp
  src/Smt2Emitter.h
)
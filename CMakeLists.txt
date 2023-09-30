cmake_minimum_required(VERSION 3.21)
project(MemAnalyzer)

set(CMAKE_CXX_STANDARD 20)

add_executable(MemAnalyzer main.cpp Scanner/Scanner.h Scanner/AddressRange.h Scanner/Value.h)

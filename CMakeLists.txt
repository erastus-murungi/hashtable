cmake_minimum_required(VERSION 3.15)
project(hashtable C)

set(CMAKE_C_STANDARD 99)

add_compile_options(-O3 -ffast-math -march=native -Wall -pedantic -Werror -Wno-unused-function)

add_executable(hashtable main.c dict.c dict.h common.c array.c hashes.h)
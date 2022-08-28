cmake_minimum_required(VERSION 3.14)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_C_EXTENSIONS ON)

project(hashtable C)

set(CMAKE_C_STANDARD 99)

add_compile_options(-O3 -ffast-math -march=native -Wall -pedantic -Werror -Wno-unused-function)


include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        release-1.12.1
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(CTest)
enable_testing()
# add_subdirectory(tests)

file(GLOB sources "${PROJECT_SOURCE_DIR}/*.c")

add_executable(hashtable main.c dict.c dict.h common.c array.c hashes.h)

include_directories("${PROJECT_SOURCE_DIR}")

file(GLOB sources "${PROJECT_SOURCE_DIR}/*.c")
list(REMOVE_ITEM sources "${PROJECT_SOURCE_DIR}/main.c")

file(GLOB tests "${PROJECT_SOURCE_DIR}/tests/*.cpp")
list(REMOVE_ITEM tests "${PROJECT_SOURCE_DIR}/tests/main.cpp")

foreach(file ${tests})
  set(name)
  get_filename_component(name ${file} NAME_WE)
  add_executable("${name}_tests"
    ${sources}
    ${file}
    "${PROJECT_SOURCE_DIR}/tests/main.cpp")
  target_link_libraries("${name}_tests" gtest_main)
  add_test(NAME ${name} COMMAND "${name}_tests")
endforeach()
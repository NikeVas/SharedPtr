project(BIGINTEGER)
cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 20)

add_definitions(-DCMAKE_EXPORT_COMPILE_COMMANDS=ON)

set(ASAN_COMPILE_FLAGS -fsanitize=address,undefined)
set(ASAN_LINK_FLAGS -fsanitize=address,undefined)

add_compile_options(${ASAN_COMPILE_FLAGS})
add_link_options(${ASAN_LINK_FLAGS})

add_compile_options(-Wall -Wextra -pedantic -g)

add_executable(main main.cpp)

# --------------------

function (make_test name)
    add_executable("${name}" ${ARGN})
    add_test(NAME "${name}" COMMAND "${name}")
    target_include_directories("${name}" PUBLIC ${CMAKE_SOURCE_DIR})
endfunction()

# --------------------

enable_testing()
include_directories(${CMAKE_SOURCE_DIR})
add_subdirectory(test)
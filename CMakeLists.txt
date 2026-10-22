cmake_minimum_required(VERSION 3.20)
project(asm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE COMPILER_SRC CONFIGURE_DEPENDS compiler/*.cpp)
file(GLOB_RECURSE EMULATOR_SRC CONFIGURE_DEPENDS emulator/*.cpp)

add_executable(compiler ${COMPILER_SRC})
add_executable(emulator ${EMULATOR_SRC})

foreach(target compiler emulator)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/includes)

    if (MSVC) # EWW
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

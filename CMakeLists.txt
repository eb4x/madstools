cmake_minimum_required(VERSION 3.16)
project(madstools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# CXX_EXTENSIONS stays at its default (gnu++17): the code uses VLAs throughout.

# mads: reads a MADSCONCAT 1.0 container or a MADSPACK 2.0 archive
add_executable(mads
	src/font.cc
	src/lzss_fab.cc
	src/sprite.cc
	src/strerror.cc
	src/util/tgaify.cc
	tools/cli.cc
)
target_include_directories(mads PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(mads PRIVATE -Wall -Wextra)

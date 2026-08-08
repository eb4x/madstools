cmake_minimum_required(VERSION 3.16)
project(madstools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# CXX_EXTENSIONS stays at its default (gnu++17): the code uses VLAs throughout.

# mconcat: splits a MADSCONCAT 1.0 container into its members.
add_executable(mconcat
	src/madsconcat.cc
	src/util/dump.cc
)
target_compile_options(mconcat PRIVATE -Wall -Wextra)

# mpack: extracts images from a MADSPACK 2.0 archive.
add_executable(mpack
	src/madspack.cc
	src/font.cc
	src/sprite.cc
	src/unpack.cc
	src/util/dump.cc
	src/util/tgaify.cc
)
target_compile_options(mpack PRIVATE -Wall -Wextra)

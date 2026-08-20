cmake_minimum_required(VERSION 3.16)
project(madstools VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# mads: the MADS format library. Consumers add_subdirectory this
# project and link the target; the include dir is PUBLIC for their
# #include <mads.h>.
add_library(mads
	src/font.cc
	src/lzss_fab.cc
	src/madsconcat.cc
	src/madspack.cc
	src/palette.cc
	src/picture.cc
	src/rle.cc
	src/sprite.cc
	src/strerror.cc
)
target_include_directories(mads PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(mads PRIVATE -Wall -Wextra)

# mads-cli: reads a MADSCONCAT 1.0 container or a MADSPACK 2.0
# archive. The binary is still mads; only the CMake target name moved
# aside for the library.
add_executable(mads-cli
	tools/cli.cc
	tools/util/tgaify.cc
)
set_target_properties(mads-cli PROPERTIES OUTPUT_NAME mads)
target_link_libraries(mads-cli PRIVATE mads)
target_compile_options(mads-cli PRIVATE -Wall -Wextra)
target_compile_definitions(mads-cli PRIVATE MADS_VERSION="${PROJECT_VERSION}")

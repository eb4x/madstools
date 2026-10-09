cmake_minimum_required(VERSION 3.21)
project(madstools VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# mads: the MADS format library. Consumers add_subdirectory this
# project and link the target; the include dir is PUBLIC for their
# #include <mads.h>.
add_library(mads
	src/font.c
	src/lzss_fab.c
	src/madsconcat.c
	src/madspack.c
	src/palette.c
	src/picture.c
	src/rle.c
	src/sprite.c
	src/strerror.c
)
target_include_directories(mads PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(mads PRIVATE -Wall -Wextra -Wpedantic)

# The CLI is C++; built by default only when this is the top-level
# project, so a consumer of the library needs no C++ toolchain.
option(MADS_BUILD_CLI "build the mads command-line tool" ${PROJECT_IS_TOP_LEVEL})

if(MADS_BUILD_CLI)
	enable_language(CXX)

	set(CMAKE_CXX_STANDARD 23)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_CXX_EXTENSIONS OFF)

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
endif()

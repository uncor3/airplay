cmake_minimum_required(VERSION 3.10.0)

option(DISABLE_GSTREAMER_RENDERER "" OFF)
option(ENABLE_FFMPEG_SDL2_RENDERER "" ON)

set(default_build_type "Release")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
		STRING "Choose the type of build." FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
		"Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

project(rpiplay)

set (CMAKE_CXX_STANDARD 11)

set (RENDERER_FLAGS "")

add_subdirectory(lib/playfair)
add_subdirectory(lib/llhttp)
add_subdirectory(lib)
add_subdirectory(renderers)

# Create library instead of executable
add_library(airplay STATIC rpiplay.cpp airplay_server.cpp)
target_link_libraries(airplay renderers airplay_lib)

# Make sure the library is aware of the available renderers
target_compile_definitions(airplay PRIVATE "${RENDERER_FLAGS}")

# Pass renderer flags to parent scope
set(RENDERER_FLAGS "${RENDERER_FLAGS}" PARENT_SCOPE)

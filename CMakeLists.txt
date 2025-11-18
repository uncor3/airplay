cmake_minimum_required(VERSION 3.10.0)

set(default_build_type "Release")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
		STRING "Choose the type of build." FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
		"Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

project(rpiplay)

set (CMAKE_CXX_STANDARD 17)

# Find Qt components needed for renderers
find_package(Qt6 REQUIRED COMPONENTS Core Multimedia)

set (RENDERER_FLAGS "-DHAS_QT_RENDERER")

add_subdirectory(lib/playfair)
add_subdirectory(lib/llhttp)
add_subdirectory(lib)
add_subdirectory(renderers)

# Create library instead of executable
add_library(airplay STATIC rpiplay.cpp)
target_link_libraries(airplay renderers airplay_lib Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Multimedia)

# Make sure the library is aware of the available renderers
target_compile_definitions(airplay PRIVATE "${RENDERER_FLAGS}")

# Pass renderer flags to parent scope
set(RENDERER_FLAGS "${RENDERER_FLAGS}" PARENT_SCOPE)


# Find pkg-config, providing a hint for Windows/MSYS2
if(WIN32)
    set(PKG_CONFIG_EXECUTABLE "C:/msys64/mingw64/bin/pkg-config.exe" CACHE FILEPATH "Path to pkg-config executable")
endif()
find_package(PkgConfig REQUIRED)

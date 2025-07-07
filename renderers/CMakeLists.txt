cmake_minimum_required(VERSION 3.10.0)

# Common Linux cflags
if( UNIX AND NOT APPLE )
    set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -fPIC -DPIC -D_REENTRANT   -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -Wall -g" )
endif()

# Common x86/x86_64 cflags
if( CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)" )
    set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Ofast -march=native" )
endif()

# Always compile the dummy renderers
list(APPEND RENDERER_FLAGS "HAS_DUMMY_RENDERER" )
set( RENDERER_SOURCES audio_renderer_dummy.c video_renderer_dummy.c )
set( RENDERER_LINK_LIBS "" )
set( RENDERER_INCLUDE_DIRS "" )

# Check for availability of gstreamer
find_package( PkgConfig )
if( PKG_CONFIG_FOUND AND NOT DISABLE_GSTREAMER_RENDERER )
  pkg_check_modules( GST gstreamer-1.0>=1.4
                         gstreamer-sdp-1.0>=1.4
                         gstreamer-video-1.0>=1.4
                         gstreamer-app-1.0>=1.4 )
  if( GST_FOUND )
    list(APPEND RENDERER_FLAGS "HAS_GSTREAMER_RENDERER" )
    set( RENDERER_SOURCES ${RENDERER_SOURCES} audio_renderer_gstreamer.c video_renderer_gstreamer.c )
    set( RENDERER_LINK_LIBS ${RENDERER_LINK_LIBS} ${GST_LIBRARIES} )
    set( RENDERER_INCLUDE_DIRS ${RENDERER_INCLUDE_DIRS} ${GST_INCLUDE_DIRS} )
  else()
    message( STATUS "GStreamer not found, skipping compilation of GStreamer renderer" )
  endif()
else()
  message( STATUS "pkg-config not found, skipping compilation of GStreamer renderer" )
endif()

# Find FFmpeg for Qt renderer
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED 
    libavcodec>=58.0.0 
    libavutil>=56.0.0 
    libswscale>=5.0.0)

# Qt renderer support
list(APPEND RENDERER_FLAGS "HAS_QT_RENDERER")
list(APPEND RENDERER_SOURCES video_renderer_qt.cpp audio_renderer_qt.cpp)
list(APPEND RENDERER_LINK_LIBS Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Multimedia)
list(APPEND RENDERER_LINK_LIBS ${LIBAV_LIBRARIES})
list(APPEND RENDERER_INCLUDE_DIRS ${LIBAV_INCLUDE_DIRS})

# Create the renderers library and link against everything
add_library( renderers STATIC ${RENDERER_SOURCES})
target_link_libraries ( renderers ${RENDERER_LINK_LIBS} )
target_include_directories( renderers PUBLIC ${RENDERER_INCLUDE_DIRS} )

# Pass the final renderer flags up to the parent scope so it knows which renderers
# will be available to use.
set( RENDERER_FLAGS "${RENDERER_FLAGS}" PARENT_SCOPE )

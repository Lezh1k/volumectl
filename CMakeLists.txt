cmake_minimum_required(VERSION 3.16)

SET(CMAKE_C_COMPILER clang)
SET(CMAKE_CXX_COMPILER clang++)
project(volumectl VERSION 0.1 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_BUILD_TYPE MATCHES Debug)
  message("Debug build.")
elseif(CMAKE_BUILD_TYPE MATCHES Release)
  message("Release build.")
else()
  message("Some other build type. Setting up default = Release")
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-pipe -O2 -march=native -mtune=native")
set(CMAKE_CXX_FLAGS_DEBUG "-pipe -O0 -g")

find_package( raylib REQUIRED )
find_package( cJSON REQUIRED )
find_package( PulseAudio REQUIRED )

# these messages are just to be sure I uses variables correctly
message( "raylib include dirs: ${raylib_INCLUDE_DIRS}" )
message( "raylib libs: ${raylib_LIBRARIES}" )
message( "cjson include dirs: ${CJSON_INCLUDE_DIRS}" )
message( "cjson libs: ${CJSON_LIBRARIES}" )
message( "PA include dirs: ${PULSEAUDIO_INCLUDE_DIR}" )
message( "PA libs: ${PULSEAUDIO_LIBRARY}" )

if(LINUX)
    message(STATUS "Configuring for Linux")
elseif(BSD)
    message(STATUS "Configuring for FreeBSD or another BSD system")
else()
    message(FATAL_ERROR "Unsupported operating system")
endif()


include_directories( ${raylib_INCLUDE_DIRS} )
include_directories( ${CJSON_INCLUDE_DIRS} )
include_directories( ${PULSEAUDIO_INCLUDE_DIR} )

set(sources
  # headers
  inc/dlg.h
  inc/log.h
  inc/out.h
  inc/sys.h

  # sources
  src/dlg.c
  src/main.c
  src/out.c
  src/sys.c

  # other files (like ui forms)
)

add_executable(${PROJECT_NAME} ${sources})
add_library(microui STATIC vendor/microui/src/microui.h vendor/microui/src/microui.c)

target_include_directories(${PROJECT_NAME} PRIVATE 
  inc
  vendor/microui/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  m
  microui
  ${raylib_LIBRARIES}
  ${CJSON_LIBRARIES}
  ${PULSEAUDIO_LIBRARY}
)

if(LINUX)
    message(STATUS "Configuring for Linux")
elseif(BSD)
    message(STATUS "Configuring for FreeBSD or another BSD system")
else()
    message(FATAL_ERROR "Unsupported operating system")
endif()

cmake_minimum_required(VERSION 3.16)

project(volumectl VERSION 0.1 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package( raylib REQUIRED )
find_package( cJSON REQUIRED )
find_package( PulseAudio REQUIRED )

# these messages are just to be sure I uses variables correctly
message( "raylib incs: ${raylib_INCLUDE_DIRS}" )
message( "raylib libs: ${raylib_LIBRARIES}" )
message( "cjson incs: ${CJSON_INCLUDE_DIRS}" )
message( "cjson libs: ${CJSON_LIBRARIES}" )
message( "PA incs: ${PULSEAUDIO_INCLUDE_DIR}" )
message( "PA libs: ${PULSEAUDIO_LIBRARY}" )

if(LINUX)
    message(STATUS "Configuring for Linux")
elseif(BSD)
    message(STATUS "Configuring for FreeBSD or another BSD system")
else()
    message(FATAL_ERROR "Unsupported operating system")
endif()

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

add_executable( ${PROJECT_NAME} ${sources} )
add_library( microui STATIC vendor/microui/src/microui.h vendor/microui/src/microui.c )

target_compile_definitions(${PROJECT_NAME} PRIVATE
  _posix_c_source=200809l
  _POSIX_C_SOURCE=200809L
)

target_compile_options(${PROJECT_NAME} PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  $<$<CONFIG:Release>:-O2>
  $<$<CONFIG:Debug>:-O0 -g>
)

target_compile_options(microui PRIVATE
  $<$<CONFIG:Release>:-O2>
  $<$<CONFIG:Debug>:-O0 -g>
)

target_include_directories( ${PROJECT_NAME} PRIVATE 
  inc
  vendor/microui/src
  ${raylib_INCLUDE_DIRS} 
  ${CJSON_INCLUDE_DIRS} 
  ${PULSEAUDIO_INCLUDE_DIR} 
)

target_link_libraries( ${PROJECT_NAME} PRIVATE
  m
  microui
  ${raylib_LIBRARIES}
  ${CJSON_LIBRARIES}
  ${PULSEAUDIO_LIBRARY}
)

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
  set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-pipe -O2 -march=native -mtune=native")
set(CMAKE_CXX_FLAGS_DEBUG "-pipe -O0 -g")

find_package( raylib REQUIRED )
find_package( cJSON REQUIRED )

if(LINUX)
    message(STATUS "Configuring for Linux")
elseif(BSD)
    message(STATUS "Configuring for FreeBSD or another BSD system")
else()
    message(FATAL_ERROR "Unsupported operating system")
endif()


include_directories( ${Raylib_INCLUDE_DIRS} )
include_directories( ${cjson_INCLUDE_DIRS} )

set(sources
  # sources
  src/main.c
  src/sys.c
  src/dlg.c

  # headers
  inc/sys.h
  inc/dlg.h
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
  raylib
  cjson
)

if(LINUX)
    message(STATUS "Configuring for Linux")
elseif(BSD)
    message(STATUS "Configuring for FreeBSD or another BSD system")
else()
    message(FATAL_ERROR "Unsupported operating system")
endif()

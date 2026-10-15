cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Set VITASDK to the vitasdk folder.")
  endif()
endif()

project(mgshdfixvita C CXX)
include("${VITASDK}/share/vita.cmake" REQUIRED)

add_executable(mgshdfixvita main.cpp)

# game_strings.hpp is shared with the upstream PC code.
target_include_directories(mgshdfixvita PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src/resources)

target_compile_options(mgshdfixvita PRIVATE
  -O2 -Wall -fno-exceptions -fno-rtti
)

option(MGSHDFIX_DEBUG_STRINGS "Log each string hash that the game looks up" OFF)
if(MGSHDFIX_DEBUG_STRINGS)
  target_compile_definitions(mgshdfixvita PRIVATE MGSHDFIX_DEBUG_STRINGS)
endif()

# The self-test list holds original game text, so it is generated into local/ and never committed.
option(MGSHDFIX_SELFTEST "Check each fix against the game's own lookup at start" OFF)
set(MGSHDFIX_SELFTEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../local/re/selftest" CACHE PATH "Folder with selftest_originals.h")
if(MGSHDFIX_SELFTEST)
  target_compile_definitions(mgshdfixvita PRIVATE MGSHDFIX_SELFTEST)
  target_include_directories(mgshdfixvita PRIVATE ${MGSHDFIX_SELFTEST_DIR})
endif()

target_link_libraries(mgshdfixvita
  taihen_stub
  SceLibKernel_stub
  SceIofilemgr_stub
  SceKernelModulemgr_stub
  SceSysmem_stub
  SceKernelThreadMgr_stub
  SceCtrl_stub
)

set_target_properties(mgshdfixvita PROPERTIES LINK_FLAGS "-nostdlib")

vita_create_self(mgshdfixvita.suprx mgshdfixvita CONFIG exports.yml UNSAFE)

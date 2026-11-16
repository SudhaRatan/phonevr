cmake_minimum_required(VERSION 3.20)
project(phonevr_driver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OpenVR driver headers (header-only use). Downloaded as a zip, so git is NOT required.
# To use a local copy instead (offline):  cmake -B build -A x64 -DOPENVR_DIR=C:/path/to/openvr
if(NOT OPENVR_DIR)
  include(FetchContent)
  FetchContent_Declare(openvr
    URL https://github.com/ValveSoftware/openvr/archive/0924064316de3effbcd1acf1e309182a2deb1c05.zip
    SOURCE_SUBDIR headers)  # headers/ has no CMakeLists, so nothing from OpenVR gets built
  FetchContent_MakeAvailable(openvr)
  set(OPENVR_DIR ${openvr_SOURCE_DIR})
endif()

add_library(driver_phonevr SHARED
  src/driver_main.cpp
  src/hmd_device.cpp
  src/net_server.cpp
  src/video_pipeline.cpp)
set_target_properties(driver_phonevr PROPERTIES PREFIX "")
target_include_directories(driver_phonevr PRIVATE ${OPENVR_DIR}/headers)
target_compile_definitions(driver_phonevr PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_link_libraries(driver_phonevr PRIVATE
  d3d11 dxgi d3dcompiler dxguid mfplat mf mfuuid strmiids ole32 oleaut32 ws2_32 winmm)
if(MSVC)
  target_compile_options(driver_phonevr PRIVATE /W3 /utf-8)
endif()

# Assemble the SteamVR driver folder:  <build>/phonevr/{driver.vrdrivermanifest,bin/win64,resources}
set(PKG ${CMAKE_BINARY_DIR}/phonevr)
add_custom_command(TARGET driver_phonevr POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PKG}/bin/win64
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:driver_phonevr> ${PKG}/bin/win64/driver_phonevr.dll
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/package/phonevr ${PKG})

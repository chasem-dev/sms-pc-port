cmake_minimum_required(VERSION 3.16)
project(sms_gx C CXX)

# GX/GD for the Super Mario Sunshine PC port: OpenGL 3.3 core backend.
set(SMS_DECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../decomp" CACHE PATH "Path to the decompilation")
option(SMS_GX_BUILD_TESTS "Build the headless self-test" ON)

set(SMS_GX_SOURCES
    src/gl_loader.cpp
    src/gx_api.cpp
    src/gx_fifo.cpp
    src/gx_render.cpp
    src/gx_shader.cpp
    src/gx_texture.cpp
    src/gx_vert.cpp)

# GD is compiled from the decompiled SDK sources: it only emits command bytes.
set(SMS_GX_GD_SOURCES
    ${SMS_DECOMP_DIR}/src/dolphin/gd/GDBase.c
    ${SMS_DECOMP_DIR}/src/dolphin/gd/GDGeometry.c
    ${SMS_DECOMP_DIR}/src/dolphin/gd/GDLight.c
    ${SMS_DECOMP_DIR}/src/dolphin/gd/GDPixel.c
    ${SMS_DECOMP_DIR}/src/dolphin/gd/GDTev.c
    ${SMS_DECOMP_DIR}/src/dolphin/gd/GDTransform.c)
set_source_files_properties(${SMS_GX_GD_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/src/gd_host_prefix.h;-w;-fcommon")

add_library(sms_gx STATIC ${SMS_GX_SOURCES} ${SMS_GX_GD_SOURCES})
target_include_directories(sms_gx
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${SMS_DECOMP_DIR}/include)
target_compile_features(sms_gx PRIVATE cxx_std_17)
set_target_properties(sms_gx PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sms_gx PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wno-unused-function -fno-strict-aliasing>)
endif()

if(SMS_GX_BUILD_TESTS)
    find_library(SMS_GX_EGL EGL)
    if(SMS_GX_EGL)
        add_executable(gx_selftest tests/gx_selftest.cpp)
        target_include_directories(gx_selftest PRIVATE ${SMS_DECOMP_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(gx_selftest PRIVATE sms_gx ${SMS_GX_EGL})
        target_compile_features(gx_selftest PRIVATE cxx_std_17)
        enable_testing()
        add_test(NAME gx_selftest COMMAND gx_selftest)
    else()
        message(STATUS "sms_gx: libEGL not found, self-test disabled")
    endif()
endif()

cmake_minimum_required(VERSION 3.28)

project(enhanced-flight LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SAFETYHOOK_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
    safetyhook
    GIT_REPOSITORY https://github.com/cursey/safetyhook.git
    GIT_TAG f44cc070a8340f2f26649553c49533475417304d
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(safetyhook)

add_library(EnhancedFlight SHARED
    src/EnhancedFlight/EnhancedFlight.cpp
)

target_include_directories(EnhancedFlight PRIVATE
    src/shared
)

target_link_libraries(EnhancedFlight PRIVATE
    safetyhook::safetyhook
    psapi
    xinput
    hid
    setupapi
)

target_compile_definitions(EnhancedFlight PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(EnhancedFlight PRIVATE
        /utf-8
        /EHsc
        /W4
        /permissive-
    )
endif()

set_target_properties(EnhancedFlight PROPERTIES
    OUTPUT_NAME "EnhancedFlight"
    SUFFIX ".asi"
)

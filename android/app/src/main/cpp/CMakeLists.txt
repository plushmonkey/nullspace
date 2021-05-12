cmake_minimum_required(VERSION 3.10.2)

project("nullspace")

set(CMAKE_CXX_STANDARD 14)
add_definitions(-DIMGUI_IMPL_OPENGL_LOADER_GLAD -DIMGUI_IMPL_OPENGL_ES3)

set(IMGUI_SOURCES
        ../../../../../lib/imgui/imgui.cpp
        ../../../../../lib/imgui/imgui_demo.cpp
        ../../../../../lib/imgui/imgui_draw.cpp
        ../../../../../lib/imgui/imgui_tables.cpp
        ../../../../../lib/imgui/imgui_widgets.cpp
        ../../../../../lib/imgui/backends/imgui_impl_android.cpp
        ../../../../../lib/imgui/backends/imgui_impl_opengl3.cpp
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

set(CMAKE_SHARED_LINKER_FLAGS
    "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate"
)

file(GLOB_RECURSE SOURCES ../../../../../src/*.cpp)
list(FILTER SOURCES EXCLUDE REGEX src/main.cpp$)

add_library(nullspace-android
            SHARED
            ${IMGUI_SOURCES}
            ../../../../../lib/glad/src/glad.cpp
            ${SOURCES}
            nullspace_android.cpp)

target_include_directories(nullspace-android PRIVATE
        ../../../../../lib
        ../../../../../
        ../../../../../lib/imgui
        ../../../../../lib/glad/include
        ${ANDROID_NDK}/sources/android/native_app_glue)

target_link_libraries(nullspace-android
                      android
                      EGL
                      GLESv3
                      log)

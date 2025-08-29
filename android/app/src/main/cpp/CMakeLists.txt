cmake_minimum_required(VERSION 3.10.2)

project("nullspace")

# Define a variable for the project root to make paths cleaner
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)

set(CMAKE_CXX_STANDARD 14)

set(IMGUI_SOURCES
        ${PROJECT_ROOT}/lib/imgui/imgui.cpp
        ${PROJECT_ROOT}/lib/imgui/imgui_demo.cpp
        ${PROJECT_ROOT}/lib/imgui/imgui_draw.cpp
        ${PROJECT_ROOT}/lib/imgui/imgui_tables.cpp
        ${PROJECT_ROOT}/lib/imgui/imgui_widgets.cpp
        ${PROJECT_ROOT}/lib/imgui/backends/imgui_impl_android.cpp
        ${PROJECT_ROOT}/lib/imgui/backends/imgui_impl_opengl3.cpp
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

set(CMAKE_SHARED_LINKER_FLAGS
    "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate"
)

# Add 16KB page size compatibility flags for Android 15+ devices
if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(CMAKE_SHARED_LINKER_FLAGS
        "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384"
    )
endif()

# Note: file(GLOB_RECURSE) is convenient but might not update the build
# system automatically if you add/remove .cpp files without touching CMakeLists.txt.
file(GLOB_RECURSE SOURCES ${PROJECT_ROOT}/src/*.cpp)
list(FILTER SOURCES EXCLUDE REGEX "null/main\\.cpp$") # Exclude the desktop main.cpp that uses GLFW

add_library(nullspace-android
            SHARED
            ${IMGUI_SOURCES}
            ${PROJECT_ROOT}/lib/glad/src/glad.cpp
            ${SOURCES}
            nullspace_android.cpp) # Assuming this is in the same dir as CMakeLists.txt

# Use target_compile_definitions for better practice
target_compile_definitions(nullspace-android PRIVATE
    IMGUI_IMPL_OPENGL_LOADER_GLAD
    IMGUI_IMPL_OPENGL_ES3)

target_include_directories(nullspace-android PRIVATE
        ${PROJECT_ROOT}/lib
        ${PROJECT_ROOT}/src  # Corrected path to find <null/HashMap.h> if it's in <PROJECT_ROOT>/src/null/
        ${PROJECT_ROOT}/lib/imgui
        ${PROJECT_ROOT}/lib/glad/include
        ${ANDROID_NDK}/sources/android/native_app_glue)

target_link_libraries(nullspace-android
                      android
                      EGL
                      GLESv3
                      log)

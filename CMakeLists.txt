cmake_minimum_required(VERSION 3.2)

project(nullspace VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)

set(IMGUI_SOURCES
  lib/imgui/imgui.cpp
  lib/imgui/imgui_demo.cpp
  lib/imgui/imgui_draw.cpp
  lib/imgui/imgui_tables.cpp
  lib/imgui/imgui_widgets.cpp
  lib/imgui/backends/imgui_impl_glfw.cpp
  lib/imgui/backends/imgui_impl_opengl3.cpp)
  
if(WIN32)
add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_GLFW_WIN32 -D_MBCS -DNDEBUG)
set(GLFW_SOURCES
  lib/glfw/src/context.cpp
  lib/glfw/src/egl_context.cpp
  lib/glfw/src/init.cpp
  lib/glfw/src/input.cpp
  lib/glfw/src/monitor.cpp
  lib/glfw/src/osmesa_context.cpp
  lib/glfw/src/vulkan.cpp
  lib/glfw/src/wgl_context.cpp
  lib/glfw/src/win32_init.cpp
  lib/glfw/src/win32_joystick.cpp
  lib/glfw/src/win32_monitor.cpp
  lib/glfw/src/win32_thread.cpp
  lib/glfw/src/win32_time.cpp
  lib/glfw/src/win32_window.cpp
  lib/glfw/src/window.cpp)
elseif(UNIX)
add_definitions(-DNDEBUG)
endif()

include(GNUInstallDirs)

file(GLOB_RECURSE SOURCES src/*.cpp)
list(APPEND SOURCES lib/glad/src/glad.cpp)
list(APPEND SOURCES ${GLFW_SOURCES})
list(APPEND SOURCES ${IMGUI_SOURCES})

add_executable(nullspace ${SOURCES})

target_include_directories(nullspace PRIVATE
                           lib
                           lib/imgui
                           lib/glad/include
                           lib/glfw/include)
                           
if(WIN32)
target_link_libraries(nullspace ws2_32)
else()
find_package(glfw3 3.3 REQUIRED)
target_link_libraries(nullspace glfw dl)
endif()

set(CPACK_PACKAGE_NAME "nullspace")
set(CPACK_PACKAGE_VENDOR "plushmonkey")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "nullspace - SubSpace client")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "nullspace")

set(CMAKE_INSTALL_PREFIX ".")

install(TARGETS nullspace 
        CONFIGURATIONS Debug
        RUNTIME DESTINATION Debug)
        
install(TARGETS nullspace 
        CONFIGURATIONS Release
        RUNTIME DESTINATION Release)

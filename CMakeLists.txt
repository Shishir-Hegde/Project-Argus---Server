cmake_minimum_required(VERSION 3.16)
project(Project_Argus_Server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Project-Argus-Server
    src/main.cpp
    src/ReidMatcher.cpp
    src/CameraManager.cpp
    src/NetworkServer.cpp
    src/GuiRenderer.cpp
)

target_include_directories(Project-Argus-Server PRIVATE include)

if(WIN32)
    target_link_libraries(Project-Argus-Server PRIVATE ws2_32 gdi32 user32)
endif()

if(MINGW)
    target_link_options(Project-Argus-Server PRIVATE -static-libgcc -static-libstdc++)
endif()

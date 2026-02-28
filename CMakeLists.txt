cmake_minimum_required(VERSION 3.20)
project(PelageFurRenderer VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /WX-)
endif()

add_executable(PelageFur WIN32
    src/main.cpp
    src/FurRenderer.cpp
    src/GeometryGen.cpp
)

target_include_directories(PelageFur PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/DirectX-Headers/include
    ${CMAKE_SOURCE_DIR}/third_party/DirectX-Headers/include/directx
)

target_link_libraries(PelageFur PRIVATE
    d3d12
    dxgi
    d3dcompiler
    dxguid
)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/shaders)

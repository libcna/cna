cmake_minimum_required(VERSION 3.10)
project(CNA)

set(CMAKE_CXX_STANDARD 17)

find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)

add_library(CNA STATIC
        src/Microsoft/Xna/Framework/Game.cpp
        src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp
        src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp
        src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp
)

target_include_directories(CNA PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(CNA PRIVATE SDL3::SDL3 SDL3_image)

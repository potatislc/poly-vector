cmake_minimum_required(VERSION 3.30)
project(poly_vector)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
  -Wconversion
  -Wsign-conversion
  -fsanitize=address
  -g
)

add_link_options(-fsanitize=address)

add_executable(test main.cpp)
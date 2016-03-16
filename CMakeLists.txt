project(ffuntar)
cmake_minimum_required(VERSION 2.8)

add_compile_options(-std=c++11)
add_executable(ffuntar ffuntar.cpp)
find_package(Qt5 5.0.0 COMPONENTS Core REQUIRED)
target_link_libraries(ffuntar Qt5::Core archive)

install(TARGETS  ffuntar RUNTIME DESTINATION bin)

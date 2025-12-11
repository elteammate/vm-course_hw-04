cmake_minimum_required(VERSION 3.5)
project(vm_course_04)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(runtime)

add_definitions(-O2 -DNDEBUG)
# add_compile_options(-fsanitize=address)
# add_link_options(-fsanitize=address)
add_executable(hw main.cpp)
target_include_directories(hw PRIVATE runtime)
target_link_libraries(hw PRIVATE runtime)

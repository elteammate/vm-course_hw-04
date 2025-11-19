cmake_minimum_required(VERSION 3.5)
project(runtime C)

set(PROD_FLAGS "${COMMON_FLAGS} -DLAMA_ENV")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${PROD_FLAGS}")

set(RUNTIME_SOURCES
        gc.c
        runtime.c
        printf.S
        vm.h
)

set_source_files_properties(printf.S PROPERTIES COMPILE_FLAGS "-Wa,--noexecstack -x assembler-with-cpp -g")

add_library(runtime STATIC ${RUNTIME_SOURCES})

target_include_directories(runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

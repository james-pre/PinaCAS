cmake_minimum_required(VERSION 3.12)

find_program(CLANG_CXX clang++)
find_program(CLANG_C clang)
if(CLANG_CXX AND NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER ${CLANG_CXX})
endif()
if(CLANG_C AND NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER ${CLANG_C})
endif()

project(PinaCAS C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Werror -Wno-missing-field-initializers)
add_compile_definitions(COMPILE_PC USE_32BIT_WORDS DEBUG)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -fno-exceptions")

include_directories(
    src
    src/cas
    lib
    src/pc
)

file(GLOB SOURCE_FILES CONFIGURE_DEPENDS "src/*.[ch]xx" "src/*/*.[ch]xx" "src/*/*/*.[ch]xx" "lib/imath/imath.[ch]" "lib/imath/imrat.[ch]")

add_executable(pinacas ${SOURCE_FILES})

add_custom_target(test
    COMMAND pinacas test tests.txt
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "run automated tests in ${CMAKE_CURRENT_SOURCE_DIR}"
    SOURCES tests.txt
)
add_dependencies(test pinacas)

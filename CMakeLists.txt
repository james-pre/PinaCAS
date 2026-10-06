cmake_minimum_required(VERSION 3.12)
project(PinaCAS C)

set(CMAKE_C_FLAGS "-std=c23 -Wall -Wextra -Werror -Wno-missing-field-initializers -DCOMPILE_PC -DUSE_32BIT_WORDS")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DDEBUG")

include_directories(
    src
    src/cas
    lib
    src/pc
)

file(GLOB SOURCE_FILES CONFIGURE_DEPENDS "src/*.[ch]" "src/*/*.[ch]" "src/*/*/*.[ch]" "lib/imath/imath.[ch]" "lib/imath/imrat.[ch]")

add_executable(pinacas ${SOURCE_FILES})

add_custom_target(test
    COMMAND pinacas test tests.txt
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "run automated tests in ${CMAKE_CURRENT_SOURCE_DIR}"
    SOURCES tests.txt
)
add_dependencies(test pinacas)

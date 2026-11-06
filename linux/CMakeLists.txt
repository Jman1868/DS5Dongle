cmake_minimum_required(VERSION 3.16)

project(ds5dongle-linux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
find_package(Python3 COMPONENTS Interpreter)

add_library(ds5dongle_protocol STATIC
    ../portable/dualsense_protocol.cpp
)
target_include_directories(ds5dongle_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../portable
)
target_compile_options(ds5dongle_protocol PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ds5dongle_protocol_test
    ../tests/dualsense_protocol_test.cpp
)
target_link_libraries(ds5dongle_protocol_test PRIVATE ds5dongle_protocol)
target_compile_options(ds5dongle_protocol_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME dualsense_protocol_test COMMAND ds5dongle_protocol_test)

if(Python3_Interpreter_FOUND)
    add_test(
        NAME hid_descriptor_parser_test
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_hid_descriptor_parser.py
    )
endif()

add_executable(ds5dongle_hid_probe hid_gadget_probe.cpp)
target_compile_options(ds5dongle_hid_probe PRIVATE -Wall -Wextra -Wpedantic)

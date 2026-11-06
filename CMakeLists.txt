cmake_minimum_required(VERSION 3.16)
project(ds5-bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(BLUEZ REQUIRED bluez)
pkg_check_modules(USBGX REQUIRED libusbgx)

add_executable(ds5-bridge src/ds5_bridge.cpp portable/dualsense_protocol.cpp)
target_compile_options(ds5-bridge PRIVATE -Wall -Wextra -Wpedantic ${BLUEZ_CFLAGS_OTHER})
target_include_directories(ds5-bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/portable
    ${BLUEZ_INCLUDE_DIRS} ${USBGX_INCLUDE_DIRS})
target_link_directories(ds5-bridge PRIVATE ${BLUEZ_LIBRARY_DIRS} ${USBGX_LIBRARY_DIRS})
target_link_libraries(ds5-bridge PRIVATE ${BLUEZ_LIBRARIES} pthread rt)
if (USBGX_FOUND)
    target_link_libraries(ds5-bridge PRIVATE ${USBGX_LIBRARIES})
else()
    message(WARNING "libusbgx was not found; configfs is used directly by the gadget setup script")
endif()

install(TARGETS ds5-bridge RUNTIME DESTINATION bin)

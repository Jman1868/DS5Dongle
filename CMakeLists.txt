cmake_minimum_required(VERSION 3.16)
project(ds5-bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(BLUEZ REQUIRED bluez)
pkg_check_modules(USBGX REQUIRED libusbgx)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

include(CheckIncludeFileCXX)
check_include_file_cxx("linux/usb/g_hid.h" HAVE_G_HID_H)
if (NOT HAVE_G_HID_H)
    message(FATAL_ERROR "linux/usb/g_hid.h is required; install the Linux kernel headers")
endif()

add_executable(ds5-bridge src/ds5_bridge.cpp portable/dualsense_protocol.cpp)
target_compile_options(ds5-bridge PRIVATE -Wall -Wextra -Wpedantic
    ${BLUEZ_CFLAGS_OTHER} ${USBGX_CFLAGS_OTHER} ${SYSTEMD_CFLAGS_OTHER})
target_include_directories(ds5-bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/portable
    ${BLUEZ_INCLUDE_DIRS} ${USBGX_INCLUDE_DIRS} ${SYSTEMD_INCLUDE_DIRS})
target_link_directories(ds5-bridge PRIVATE ${BLUEZ_LIBRARY_DIRS} ${USBGX_LIBRARY_DIRS}
    ${SYSTEMD_LIBRARY_DIRS})
target_link_libraries(ds5-bridge PRIVATE ${BLUEZ_LIBRARIES} ${USBGX_LIBRARIES}
    PkgConfig::SYSTEMD pthread rt)

install(TARGETS ds5-bridge RUNTIME DESTINATION bin)

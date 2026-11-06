# CMake generated Testfile for 
# Source directory: /home/jamal/Coding/pico zero/DS5Dongle/linux
# Build directory: /home/jamal/Coding/pico zero/DS5Dongle/build-linux
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(dualsense_protocol_test "/home/jamal/Coding/pico zero/DS5Dongle/build-linux/ds5dongle_protocol_test")
set_tests_properties(dualsense_protocol_test PROPERTIES  _BACKTRACE_TRIPLES "/home/jamal/Coding/pico zero/DS5Dongle/linux/CMakeLists.txt;26;add_test;/home/jamal/Coding/pico zero/DS5Dongle/linux/CMakeLists.txt;0;")
add_test(hid_descriptor_parser_test "/usr/bin/python3" "/home/jamal/Coding/pico zero/DS5Dongle/linux/../tests/test_hid_descriptor_parser.py")
set_tests_properties(hid_descriptor_parser_test PROPERTIES  _BACKTRACE_TRIPLES "/home/jamal/Coding/pico zero/DS5Dongle/linux/CMakeLists.txt;29;add_test;/home/jamal/Coding/pico zero/DS5Dongle/linux/CMakeLists.txt;0;")

# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

find_package(asio CONFIG REQUIRED)

# Configure stdexec's Asio adapter privately: enabling its upstream CMake option also exports Asio.
set(STDEXEC_ASIO_USES_STANDALONE 1)
set(STDEXEC_ASIO_USES_BOOST 0)
configure_file("${stdexec_SOURCE_DIR}/include/exec/asio/asio_config.hpp.in"
               "${CMAKE_CURRENT_BINARY_DIR}/include/exec/asio/asio_config.hpp")

add_executable(${PROJECT_NAME}.demo.hello_world hello_world.cpp)
target_link_libraries(${PROJECT_NAME}.demo.hello_world PRIVATE ${PROJECT_NAME}.lib)
fnfelt_target_linters(${PROJECT_NAME}.demo.hello_world)

add_test(NAME ${PROJECT_NAME}.demo.hello_world COMMAND ${PROJECT_NAME}.demo.hello_world)
# The demo returns a line count; matching output accepts ordinary nonzero exits.
# Assertion failures still fail the test.
set_tests_properties(
  ${PROJECT_NAME}.demo.hello_world
  PROPERTIES WORKING_DIRECTORY
             "${PROJECT_SOURCE_DIR}"
             PASS_REGULAR_EXPRESSION
             "Docs: [0-9]+ README lines [+] [0-9]+ license lines = [0-9]+ total")

add_executable(${PROJECT_NAME}.demo.tcp_echo stdexec_tcp_echo.cpp)
target_link_libraries(${PROJECT_NAME}.demo.tcp_echo PRIVATE ${PROJECT_NAME}.lib asio::asio)
target_include_directories(${PROJECT_NAME}.demo.tcp_echo
                           PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/include")
fnfelt_target_linters(${PROJECT_NAME}.demo.tcp_echo)

add_test(NAME ${PROJECT_NAME}.demo.tcp_echo COMMAND ${PROJECT_NAME}.demo.tcp_echo)
set_tests_properties(${PROJECT_NAME}.demo.tcp_echo PROPERTIES TIMEOUT 15)

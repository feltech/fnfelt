# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

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

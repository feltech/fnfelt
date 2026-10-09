# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

fnfelt_test_compile_fail(
  monad.io.create.result_box_move_assignment
  SOURCE
  result_box_move_assignment.cpp
  ERROR_REGEX
  "fnfelt: ResultBox move assignment is unsupported")

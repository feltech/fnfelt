# fnfelt
# SPDX-License-Identifier: MIT
# Copyright 2026 David Feltell

include_guard(GLOBAL)

##################################################################################################
# clang-tidy

# TODO(DF): Disabled until clang-tidy supports reflection.
#  find_program(${PROJECT_NAME}_CLANG_TIDY_EXE NAMES clang-tidy)
#  if(${PROJECT_NAME}_CLANG_TIDY_EXE)
#    message(STATUS "clang-tidy found: ${${PROJECT_NAME}_CLANG_TIDY_EXE}")
#  else()
#    message(WARNING "clang-tidy not found — C++ static analysis disabled")
#  endif()

##################################################################################################
# clang-format

find_program(${PROJECT_NAME}_CLANGFORMAT_EXE NAMES clang-format)
if(${PROJECT_NAME}_CLANGFORMAT_EXE)
  message(STATUS "clang-format found: ${${PROJECT_NAME}_CLANGFORMAT_EXE}")
  file(
    GLOB_RECURSE _sources
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS # Ensure we re-scan if files change.
    ${PROJECT_SOURCE_DIR}/include/*.h
    ${PROJECT_SOURCE_DIR}/include/*.hpp
    ${PROJECT_SOURCE_DIR}/src/*.[ch]pp
    ${PROJECT_SOURCE_DIR}/src/*.[ch]
    ${PROJECT_SOURCE_DIR}/tests/*.[ch]pp
    ${PROJECT_SOURCE_DIR}/tests/*.[ch])
  add_custom_target(
    ${PROJECT_NAME}.lint.clang-format
    COMMENT "clang-format"
    COMMAND ${${PROJECT_NAME}_CLANGFORMAT_EXE} --Werror --dry-run --style=file ${_sources})
else()
  message(WARNING "clang-format not found — C++ formatting check disabled")
endif()

##################################################################################################
# cpplint

find_program(${PROJECT_NAME}_CPPLINT_EXE NAMES cpplint)
if(${PROJECT_NAME}_CPPLINT_EXE)
  message(STATUS "cpplint found: ${${PROJECT_NAME}_CPPLINT_EXE}")
  add_custom_target(
    ${PROJECT_NAME}.lint.cpplint
    COMMENT "cpplint"
    COMMAND ${${PROJECT_NAME}_CPPLINT_EXE} --recursive ${PROJECT_SOURCE_DIR}/include
            ${PROJECT_SOURCE_DIR}/src)
else()
  message(WARNING "cpplint not found — C++ google style guide linter check disabled")
endif()

##################################################################################################
# cmake-lint

find_program(${PROJECT_NAME}_CMAKELINT_EXE NAMES cmake-lint)
if(${PROJECT_NAME}_CMAKELINT_EXE)
  message(STATUS "cmake-lint found: ${${PROJECT_NAME}_CMAKELINT_EXE}")
  file(
    GLOB_RECURSE _sources
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS # Ensure we re-scan if files change.
    ${PROJECT_SOURCE_DIR}/cmake/*.cmake
    ${PROJECT_SOURCE_DIR}/src/CMakeLists.txt
    ${PROJECT_SOURCE_DIR}/src/**/CMakeLists.txt
    ${PROJECT_SOURCE_DIR}/tests/CMakeLists.txt
    ${PROJECT_SOURCE_DIR}/tests/**/CMakeLists.txt)
  # Explicitly add top-level CMakeLists.txt, rather than globbing
  # from root, so that hidden/build dirs located at project root
  # are not included.
  list(PREPEND _sources ${PROJECT_SOURCE_DIR}/CMakeLists.txt)

  add_custom_target(
    ${PROJECT_NAME}.lint.cmake-lint
    COMMENT "cmake-lint"
    COMMAND ${${PROJECT_NAME}_CMAKELINT_EXE} --suppress-decorations ${_sources})

else()
  message(WARNING "cmake-lint not found — CMake linting check disabled")
endif()

##################################################################################################
# cmake-format

find_program(${PROJECT_NAME}_CMAKEFORMAT_EXE NAMES cmake-format)
if(${PROJECT_NAME}_CMAKEFORMAT_EXE)
  message(STATUS "cmake-format found: ${${PROJECT_NAME}_CMAKEFORMAT_EXE}")
  file(
    GLOB_RECURSE _sources
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS # Ensure we re-scan if files change.
    ${PROJECT_SOURCE_DIR}/cmake/*.cmake
    ${PROJECT_SOURCE_DIR}/src/CMakeLists.txt
    ${PROJECT_SOURCE_DIR}/src/**/CMakeLists.txt
    ${PROJECT_SOURCE_DIR}/tests/CMakeLists.txt
    ${PROJECT_SOURCE_DIR}/tests/**/CMakeLists.txt)
  # Explicitly add top-level CMakeLists.txt, rather than globbing
  # from root, so that hidden/build dirs located at project root
  # are not included.
  list(PREPEND _sources ${PROJECT_SOURCE_DIR}/CMakeLists.txt)

  add_custom_target(
    ${PROJECT_NAME}.lint.cmake-format
    COMMENT "cmake-format"
    COMMAND ${${PROJECT_NAME}_CMAKEFORMAT_EXE} --check ${_sources})

else()
  message(WARNING "cmake-format not found — CMake formating check disabled")
endif()

##################################################################################################
# doxygen

find_program(${PROJECT_NAME}_DOXYGEN_EXE NAMES doxygen)
if(${PROJECT_NAME}_DOXYGEN_EXE)
  message(STATUS "doxygen found: ${${PROJECT_NAME}_DOXYGEN_EXE}")

  add_custom_target(
    ${PROJECT_NAME}.lint.doxygen
    COMMENT "doxygen"
    COMMAND ${${PROJECT_NAME}_DOXYGEN_EXE} Doxyfile
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

else()
  message(WARNING "doxygen not found — CMake linting check disabled")
endif()

##################################################################################################
# mdformat

find_program(${PROJECT_NAME}_MDFORMAT_EXE NAMES mdformat)
if(${PROJECT_NAME}_MDFORMAT_EXE)
  message(STATUS "mdformat found: ${${PROJECT_NAME}_MDFORMAT_EXE}")
  file(
    GLOB _root_sources
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS *.md)
  file(
    GLOB_RECURSE _sources
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    .opencode/agent/*.md
    include/*.md
    src/*.md
    tests/*.md)
  list(APPEND _sources ${_root_sources})
  add_custom_target(
    ${PROJECT_NAME}.lint.mdformat
    COMMENT "mdformat"
    COMMAND ${${PROJECT_NAME}_MDFORMAT_EXE} --check ${_sources})

else()
  message(WARNING "mdformat not found — markdown formating check disabled")
endif()

##################################################################################################
# Functions.

# Add linter dependencies to a CMake target.
function(fnfelt_target_linters target)
  # Error on compiler warnings.
  set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR ON)

  # Clang-Tidy
  if(${PROJECT_NAME}_CLANG_TIDY_EXE)
    set(clang_tidy_cmd ${${PROJECT_NAME}_CLANG_TIDY_EXE})
    list(APPEND clang_tidy_cmd --use-color)
    list(APPEND clang_tidy_cmd --allow-no-checks)
    list(APPEND clang_tidy_cmd -extra-arg-before=-Wno-unknown-warning-option)
    list(APPEND clang_tidy_cmd -extra-arg-before=-Wno-unknown-argument)
    list(APPEND clang_tidy_cmd -extra-arg-before=-Qunused-arguments)
    list(APPEND clang_tidy_cmd -extra-arg-before=-Wno-ignored-optimization-argument)
    # list(APPEND clang_tidy_cmd --removed-arg=-freflection)
    list(APPEND clang_tidy_cmd -extra-arg=-std=c++${CMAKE_CXX_STANDARD})
    set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${clang_tidy_cmd}")
  endif()

  # cpplint
  if(TARGET ${PROJECT_NAME}.lint.cpplint)
    add_dependencies(${target} ${PROJECT_NAME}.lint.cpplint)
  endif()

  # clang-format
  if(TARGET ${PROJECT_NAME}.lint.clang-format)
    add_dependencies(${target} ${PROJECT_NAME}.lint.clang-format)
  endif()

  # cmake-lint
  if(TARGET ${PROJECT_NAME}.lint.cmake-lint)
    add_dependencies(${target} ${PROJECT_NAME}.lint.cmake-lint)
  endif()

  # cmake-format
  if(TARGET ${PROJECT_NAME}.lint.cmake-format)
    add_dependencies(${target} ${PROJECT_NAME}.lint.cmake-format)
  endif()

  # Doxygen (linting only)
  if(TARGET ${PROJECT_NAME}.lint.doxygen)
    add_dependencies(${target} ${PROJECT_NAME}.lint.doxygen)
  endif()

  # mdformat
  if(TARGET ${PROJECT_NAME}.lint.mdformat)
    add_dependencies(${target} ${PROJECT_NAME}.lint.mdformat)
  endif()

  if(${PROJECT_NAME}_ENABLE_SANITIZER_ASAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined
                                               -fno-sanitize-recover=all -fno-omit-frame-pointer)
      target_link_options(
        ${target}
        PUBLIC
        -fsanitize=address,undefined
        -fno-sanitize-recover=all
        -fno-omit-frame-pointer)
    endif()
  endif()
endfunction()

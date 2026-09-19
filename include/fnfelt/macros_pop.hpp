// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2024-2026 David Feltell
/**
 * @file macros_pop.hpp
 *
 * Restore macros saved by macros_push.hpp.
 *
 * This should be used at the bottom of a file that included macros_push.hpp at the top, so as
 * not to pollute the global macro namespace, which may be used by other library headers.
 */
// push/pop must be allowed to run multiple times:
// ReSharper disable CppMissingIncludeGuard

#pragma pop_macro("FW")

// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
/**
 * @file macros_push.hpp
 *
 * Useful project private macros.
 *
 * This should be used at the top of a file, and macros_pop.hpp add at the bottom, so as not to
 * pollute the global macro namespace, which may be used by other library headers.
 */
// ReSharper disable CppMissingIncludeGuard - push/pop must be allowed to run multiple times.
#pragma push_macro("FW")
#ifdef FW
#undef FW
#endif
/// Shorthand for forwarding.
#define FW(a) std::forward<decltype(a)>(a)

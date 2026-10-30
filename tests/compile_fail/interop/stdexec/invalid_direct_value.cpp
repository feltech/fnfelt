// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell

#include <fnfelt/interop/stdexec.hpp>

#include <utility>

#include <stdexec/execution.hpp>

using sender_type = decltype(stdexec::just(std::in_place));
using outcome_type = fnfelt::interop::sender_outcome_t<sender_type>;
static_assert(sizeof(outcome_type) > 0);

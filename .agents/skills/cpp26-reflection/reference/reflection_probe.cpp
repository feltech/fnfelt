// fnfelt
// SPDX-License-Identifier: MIT
// Copyright 2026 David Feltell
//
// Self-contained C++26 reflection probe/playground for gcc 16.
//
// Compile and run:
//   g++ -std=c++26 -freflection reference/reflection_probe.cpp -o /tmp/probe \
//       && /tmp/probe
//
// Copy this file to a scratch directory and edit it to explore questions not
// covered by the skill document. Every check below was verified on
// gcc 16.2.0 / libstdc++ 16.2.0.

#include <meta>

#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>

// ---------------------------------------------------------------------------
// 1. Invocability matrix (consteval-computed, printed at runtime).
// ---------------------------------------------------------------------------

namespace probe_invocable
{

struct plain_functor
{
  int operator()() const { return 1; }
};

struct void_functor
{
  void operator()() const {}
};

struct int_arg_functor
{
  int operator()(int) const { return 1; }
};

struct not_callable
{
};

struct lvalue_qualified
{
  int operator()() & { return 1; }
};

struct rvalue_qualified
{
  int operator()() && { return 1; }
};

struct const_lvalue_qualified
{
  int operator()() const & { return 1; }
};

struct mixed_overloads
{
  int operator()() const { return 1; }
  void operator()() && {}
};

using fp_int_void = int (*)();
using fp_void_void = void (*)();
using fp_int_int = int (*)(int);

using fn_int = int();
using fn_void = void();

// `outcome` is fully evaluated inside the consteval call and reduced to a
// string_view. Exception messages must be promoted with define_static_string
// because they point into heap storage owned by the exception object.
consteval std::string_view
outcome (std::meta::info type)
{
  if (!std::meta::is_invocable_type (type, {}))
    {
      try
        {
          (void)std::meta::invoke_result (type, {});
          return "no exception";
        }
      catch (const std::meta::exception &except)
        {
          std::string message{ except.what () };
          return std::define_static_string (message);
        }
    }
  return std::meta::display_string_of (std::meta::invoke_result (type, {}));
}

struct outcome_row
{
  std::string_view name;
  bool invocable_v;
  std::string_view outcome_v;
};

consteval outcome_row
probe (std::meta::info type, std::string_view name)
{
  return { name, std::meta::is_invocable_type (type, {}), outcome (type) };
}

constexpr outcome_row rows[] = {
  probe (^^plain_functor, "functor int operator()() const"),
  probe (^^void_functor, "functor void operator()() const"),
  probe (^^int_arg_functor, "functor int operator()(int) const"),
  probe (^^not_callable, "non-callable struct"),
  probe (^^lvalue_qualified, "functor int operator()() &"),
  probe (^^rvalue_qualified, "functor int operator()() &&"),
  probe (^^const_lvalue_qualified, "functor int operator()() const &"),
  probe (^^mixed_overloads, "mixed const + && overloads"),
  probe (^^fp_int_void, "int(*)()"),
  probe (^^fp_void_void, "void(*)()"),
  probe (^^fp_int_int, "int(*)(int)"),
  probe (^^fn_int, "function type int()"),
  probe (^^fn_void, "function type void()"),
};

// is_invocable_type uses prvalue (INVOKE) semantics: an lvalue-ref-qualified
// operator() is rejected for a prvalue receiver, but add_lvalue_reference
// recovers the lvalue case (verified true).
consteval bool lvalue_only_as_ref ()
{
  return std::meta::is_invocable_type (std::meta::add_lvalue_reference (^^lvalue_qualified), {});
}

} // namespace probe_invocable

// ---------------------------------------------------------------------------
// 2. members_of vs nonstatic_data_members_of vs subobjects_of.
// ---------------------------------------------------------------------------

namespace probe_members
{

struct two_fields
{
  int first;
  double second;
};

// members_of includes implicitly-declared special members: 8 for a
// 2-data-member struct (default ctor, copy ctor, move ctor, copy assign,
// move assign, dtor + 2 data members).
consteval std::size_t
count (auto query)
{
  return query (^^two_fields).size ();
}

consteval std::size_t members_count ()
{
  return count ([] (std::meta::info type) {
    return std::meta::members_of (
        type, std::meta::access_context::unprivileged ());
  });
}

consteval std::size_t nonstatic_count ()
{
  return count ([] (std::meta::info type) {
    return std::meta::nonstatic_data_members_of (
        type, std::meta::access_context::unprivileged ());
  });
}

consteval std::size_t subobjects_count ()
{
  return count ([] (std::meta::info type) {
    return std::meta::subobjects_of (
        type, std::meta::access_context::unprivileged ());
  });
}

} // namespace probe_members

// ---------------------------------------------------------------------------
// 3. define_static_array + template for.
// ---------------------------------------------------------------------------

namespace probe_template_for
{

struct two_fields
{
  int first;
  double second;
};

// template for over define_static_array(query) WORKS; over the raw
// vector-returning query it fails with "refers to a result of 'operator new'".
consteval std::size_t
sum_of_member_sizes ()
{
  std::size_t total = 0;
  template for (constexpr auto member
                : std::define_static_array (std::meta::nonstatic_data_members_of (
                      ^^two_fields,
                      std::meta::access_context::unprivileged ())))
    {
      total += std::meta::size_of (member);
    }
  return total;
}

} // namespace probe_template_for

// ---------------------------------------------------------------------------
// 4. Alias reflections.
// ---------------------------------------------------------------------------

namespace probe_alias
{

using int_alias = int;

// is_same_type sees through aliases (true); info == does NOT (false);
// dealias first for == comparisons.
consteval bool same_type_alias ()
{
  return std::meta::is_same_type (^^int_alias, ^^int);
}

consteval bool info_equality_alias ()
{
  return ^^int_alias == ^^int;
}

consteval bool info_equality_dealiased ()
{
  return std::meta::dealias (^^int_alias) == ^^int;
}

consteval bool is_alias_reflection ()
{
  return std::meta::is_type_alias (^^int_alias);
}

} // namespace probe_alias

// ---------------------------------------------------------------------------
// 5. Lambda closures and incomplete-type guard.
// ---------------------------------------------------------------------------

namespace probe_lambda
{

consteval bool lambda_checks ()
{
  auto capturing = [value = 1] { return value; };
  auto plain = [] { return 1; };
  bool capturing_invocable
      = std::meta::is_invocable_type (^^decltype (capturing), {});
  bool plain_invocable
      = std::meta::is_invocable_type (^^decltype (plain), {});
  // Lambda closure types are ordinary class types; there is no
  // is_closure_type / is_lambda query in <meta>.
  bool is_class = std::meta::is_class_type (^^decltype (capturing));
  bool result_is_int = std::meta::is_same_type (
      std::meta::invoke_result (^^decltype (capturing), {}), ^^int);
  return capturing_invocable && plain_invocable && is_class && result_is_int;
}

} // namespace probe_lambda

namespace probe_incomplete
{

class incomplete_class;

// is_complete_type is false for an incomplete type, and is safe to call.
consteval bool is_incomplete ()
{
  return !std::meta::is_complete_type (^^incomplete_class);
}

// Passing an incomplete type to is_invocable_type / invoke_result is a HARD,
// non-catchable gcc error ("type trait ... preconditions not satisfied") -
// always guard with is_complete_type first. Not reproducible in this
// compiled probe; see SKILL.md gotchas.

} // namespace probe_incomplete

// ---------------------------------------------------------------------------
// 6. Validator pattern: structural functor + auto NTTP + static_assert.
// ---------------------------------------------------------------------------

namespace probe_validator
{

struct int_validator
{
  consteval std::string_view
  operator() (std::meta::info type) const
  {
    if (!std::meta::is_type (type))
      {
        return "reflection is not a type";
      }
    if (!std::meta::is_same_type (std::meta::dealias (type), ^^int))
      {
        return "type is not int";
      }
    return {};
  }
};

inline constexpr int_validator validate_is_int{};

template <class T, auto validator = validate_is_int>
struct checked_type
{
  static_assert (validator (^^T).empty (), "T failed validation");
  using type = T;
};

// Test stub: always passes.
struct always_ok
{
  consteval std::string_view
  operator() (std::meta::info) const
  {
    return {};
  }
};

} // namespace probe_validator

// ---------------------------------------------------------------------------
// main: reduce consteval results to constexpr scalars, print at runtime.
// ---------------------------------------------------------------------------

int
main ()
{
  using namespace probe_invocable;

  std::cout << std::boolalpha;

  std::cout << "invocability matrix "
               "(is_invocable_type / invoke_result):\n";
  for (const auto &row : rows)
    {
      std::cout << "  " << row.name << ": invocable=" << row.invocable_v
                << " outcome=" << row.outcome_v << "\n";
    }
  std::cout << "  lvalue-only callable as T&: " << lvalue_only_as_ref ()
            << "\n";

  std::cout << "member queries (2-data-member struct):\n"
            << "  members_of: " << probe_members::members_count () << "\n"
            << "  nonstatic_data_members_of: "
            << probe_members::nonstatic_count () << "\n"
            << "  subobjects_of: " << probe_members::subobjects_count ()
            << "\n";

  std::cout << "template for over define_static_array sum of member sizes: "
            << probe_template_for::sum_of_member_sizes () << "\n";

  std::cout << "alias reflections:\n"
            << "  is_same_type(^^int_alias, ^^int): "
            << probe_alias::same_type_alias () << "\n"
            << "  info == comparison: "
            << probe_alias::info_equality_alias () << "\n"
            << "  dealias then == comparison: "
            << probe_alias::info_equality_dealiased () << "\n"
            << "  is_type_alias(^^int_alias): "
            << probe_alias::is_alias_reflection () << "\n";

  std::cout << "lambda checks: " << probe_lambda::lambda_checks () << "\n";
  std::cout << "incomplete class is_complete_type false: "
            << probe_incomplete::is_incomplete () << "\n";

  std::cout << "validator pattern (checked<int> ok): "
            << sizeof (probe_validator::checked_type< int >::type) << "\n";
  std::cout << "validator pattern (stubbed, always_ok, no assert fires): "
            << sizeof (probe_validator::checked_type<
                       double, probe_validator::always_ok{} >::type)
            << "\n";
}
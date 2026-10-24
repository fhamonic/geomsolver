#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace gs {

// A name the expression language defines.
struct Builtin {
    enum class Kind : std::uint8_t {
        Function,  // a call, f(a, b, ...)
        // at(sweep = value, body), max_over(sweep, body),
        // min_over(sweep, body): the first argument names a sweep, and the
        // body is evaluated over that sweep, not once.
        SpecialForm,
        Constant,  // a name, not a call: pi
        // parallel(S1, S2), coincident(P, Q): a whole constraint by itself,
        // which compiles to its own rows; never a value inside an
        // expression.
        Predicate,
    };
    std::string_view name;
    Kind kind = Kind::Function;
    int min_args = 0;
    int max_args = 0;  // < 0: no maximum
    double value = 0;  // a Constant's value
};

// Every builtin, sorted by name. The compiler takes the functions' arity and
// the constants from here: a function that is not listed is an "unknown
// function", even when the compiler has code for it.
std::span<const Builtin> builtin_functions();
// The entry named `name`, nullptr when there is none.
const Builtin * find_builtin(std::string_view name);

}  // namespace gs

#pragma once

#include <cstdint>

namespace gs::app {

// The meaning of a status text's colour; each front-end's theme picks the
// colour.
enum class Tone : std::uint8_t {
    Normal,  // the toolkit's text colour
    Dim,
    Ok,
    Active,
    Bad,
    Warn,
    Neutral,  // a value nothing judges, such as a criterion without a bound
};

}  // namespace gs::app

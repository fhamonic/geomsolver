#pragma once

#include <cstdint>
#include <string>

namespace gs::app {

// Keys an action can be bound to, whatever the toolkit. chord_text() and the
// front-ends' key tables map A..Z and F1..F12 by their offset from A and F1:
// a key inserted inside either range shifts every key after it. New keys go
// after Delete.
enum class Key : std::uint8_t {
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    Delete,
    Space,
    Escape,
    Enter,
    Tab,
    Backspace,
    Up,
    Down,
};

enum class Mods : std::uint8_t {
    None = 0,
    Ctrl = 1 << 0,
    Shift = 1 << 1,
    Alt = 1 << 2,
    Super = 1 << 3,
};

constexpr Mods operator|(Mods a, Mods b) {
    return static_cast<Mods>(static_cast<std::uint8_t>(a) |
                             static_cast<std::uint8_t>(b));
}
// Whether `set` holds every modifier of `m`.
constexpr bool has(Mods set, Mods m) {
    return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(m)) ==
           static_cast<std::uint8_t>(m);
}

// The pointer's buttons, as the canvas and the tools see them.
enum class Button : std::uint8_t { Left, Right, Middle };

struct KeyChord {
    Key key;
    Mods mods = Mods::None;

    bool operator==(const KeyChord &) const = default;
};

// How menus show the chord: "Ctrl+Shift+Z", "Del", "Esc", "F2", "G".
std::string chord_text(KeyChord chord);

}  // namespace gs::app

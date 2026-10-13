#include "gs/app/keys.hpp"

#include <format>
#include <utility>

namespace gs::app {
namespace {

const char * tail_name(Key key) {
    switch(key) {
        case Key::Delete:
            return "Del";
        case Key::Space:
            return "Space";
        case Key::Escape:
            return "Esc";
        case Key::Enter:
            return "Enter";
        case Key::Tab:
            return "Tab";
        case Key::Backspace:
            return "Backspace";
        case Key::Up:
            return "Up";
        case Key::Down:
            return "Down";
        default:
            return "";
    }
}

}  // namespace

std::string chord_text(KeyChord chord) {
    std::string out;
    for(const auto & [mod, name] :
        {std::pair{Mods::Ctrl, "Ctrl+"}, std::pair{Mods::Shift, "Shift+"},
         std::pair{Mods::Alt, "Alt+"}, std::pair{Mods::Super, "Super+"}})
        if(has(chord.mods, mod)) out += name;
    const auto k = static_cast<int>(chord.key);
    if(chord.key <= Key::Z)
        out += static_cast<char>('A' + k);
    else if(chord.key <= Key::F12)
        out += std::format("F{}", k - static_cast<int>(Key::F1) + 1);
    else
        out += tail_name(chord.key);
    return out;
}

}  // namespace gs::app

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <imgui.h>

#include "gs/engine/diagnostics.hpp"
#include "gs/engine/types.hpp"

namespace gs::ui {

inline ImU32 rgba(int r, int g, int b, int a = 255) {
    return static_cast<ImU32>(r) | static_cast<ImU32>(g) << 8 |
           static_cast<ImU32>(b) << 16 | static_cast<ImU32>(a) << 24;
}
// Engine colours are 0xRRGGBBAA; ImGui packs ABGR.
ImU32 engine_color(std::uint32_t rrggbbaa, float alpha_scale = 1.0f);
ImVec4 engine_color_vec(std::uint32_t rrggbbaa);
std::string color_hex(const ImVec4 & c);  // "#RRGGBB" or "#RRGGBBAA"

namespace palette {
inline const ImVec4 ok{0.40f, 0.80f, 0.45f, 1.0f};
inline const ImVec4 active{0.95f, 0.75f, 0.30f, 1.0f};
inline const ImVec4 bad{1.00f, 0.42f, 0.38f, 1.0f};
inline const ImVec4 dim{0.60f, 0.62f, 0.66f, 1.0f};
inline const ImVec4 warn{0.98f, 0.80f, 0.35f, 1.0f};
}  // namespace palette

// std::string-backed InputText (imgui_stdlib is not part of the package).
bool input_text(const char * id, std::string & s, ImGuiInputTextFlags flags = 0,
                const char * hint = nullptr);
void text_colored(const ImVec4 & c, std::string_view text);
void help_marker(std::string_view text);
void tooltip_text(std::string_view text);
// Diagnostic line under an editor: "col 4: message" plus the expression with
// the offending part highlighted when the column is known.
void diagnostic_line(const Diagnostic & d, std::string_view expression);

// Short human text of a value: "0.4650", "(0.31, 0.02)", "Polygon[4]".
std::string format_value(const GeoValue & v);
// Number with 4 significant digits after unit conversion, plus the unit.
std::string format_display(double si, std::string_view unit);
std::string format_number(double v, int digits = 4);

inline ImVec2 to_im(Vec2d p) {
    return {static_cast<float>(p.x), static_cast<float>(p.y)};
}

}  // namespace gs::ui

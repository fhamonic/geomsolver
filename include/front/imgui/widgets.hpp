#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include <imgui.h>

#include "gs/app/status.hpp"
#include "gs/app/tone.hpp"
#include "gs/app/view.hpp"
#include "gs/engine/types.hpp"

namespace gs::front::imgui {

using namespace gs::app;

inline ImU32 rgba(int r, int g, int b, int a = 255) {
    return static_cast<ImU32>(r) | static_cast<ImU32>(g) << 8 |
           static_cast<ImU32>(b) << 16 | static_cast<ImU32>(a) << 24;
}
// Engine colours are 0xRRGGBBAA; ImGui packs ABGR.
ImU32 engine_color(std::uint32_t rrggbbaa, float alpha_scale = 1.0f);

namespace palette {
inline const ImVec4 ok{0.40f, 0.80f, 0.45f, 1.0f};
inline const ImVec4 active{0.95f, 0.75f, 0.30f, 1.0f};
inline const ImVec4 bad{1.00f, 0.42f, 0.38f, 1.0f};
inline const ImVec4 dim{0.60f, 0.62f, 0.66f, 1.0f};
inline const ImVec4 warn{0.98f, 0.80f, 0.35f, 1.0f};
inline const ImVec4 neutral{0.92f, 0.93f, 0.95f, 1.0f};
}  // namespace palette

const ImVec4 & tone_color(Tone tone);

// std::string-backed InputText (imgui_stdlib is not part of the package).
bool input_text(const char * id, std::string & s, ImGuiInputTextFlags flags = 0,
                const char * hint = nullptr);
// The same, calling `callback` for the events of `flags`' Callback* flags
// (Always, Edit, History, ...): the callback reads the field's own buffer,
// which is not `s` while the field is active, and edits it with
// InsertChars() and DeleteChars().
bool input_text(
    const char * id, std::string & s, ImGuiInputTextFlags flags,
    const std::function<void(ImGuiInputTextCallbackData &)> & callback);
// The same, of several lines: Enter types a new line, and Ctrl+Enter is the
// Enter that ImGuiInputTextFlags_EnterReturnsTrue reports.
bool input_text_multiline(const char * id, std::string & s, ImVec2 size,
                          ImGuiInputTextFlags flags = 0);
void text_colored(const ImVec4 & c, std::string_view text);
void tooltip_text(std::string_view text);
// Diagnostic line under an editor: the message, plus the expression with
// the offending part underlined when the column is known.
void diagnostic_line(const DiagnosticText & d);

inline ImVec2 to_im(Vec2d p) {
    return {static_cast<float>(p.x), static_cast<float>(p.y)};
}
inline ImVec2 to_im(Vec2f p) { return {p.x, p.y}; }

}  // namespace gs::front::imgui

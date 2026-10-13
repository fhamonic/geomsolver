#include "front/imgui/widgets.hpp"

#include <algorithm>
#include <cmath>

namespace gs::front::imgui {
namespace {

int resize_callback(ImGuiInputTextCallbackData * data) {
    if(data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto * s = static_cast<std::string *>(data->UserData);
        s->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = s->data();
    }
    return 0;
}

struct Chained {
    std::string * text;
    const std::function<void(ImGuiInputTextCallbackData &)> * callback;
};

int chained_callback(ImGuiInputTextCallbackData * data) {
    auto * c = static_cast<Chained *>(data->UserData);
    if(data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        c->text->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = c->text->data();
    } else {
        (*c->callback)(*data);
    }
    return 0;
}

int channel(std::uint32_t c, int shift) {
    return static_cast<int>((c >> shift) & 0xffu);
}

}  // namespace

ImU32 engine_color(std::uint32_t c, float alpha_scale) {
    const int a = static_cast<int>(
        std::lround(static_cast<float>(channel(c, 0)) * alpha_scale));
    return rgba(channel(c, 24), channel(c, 16), channel(c, 8),
                std::clamp(a, 0, 255));
}

const ImVec4 & tone_color(Tone tone) {
    switch(tone) {
        case Tone::Normal:
            return ImGui::GetStyle().Colors[ImGuiCol_Text];
        case Tone::Ok:
            return palette::ok;
        case Tone::Active:
            return palette::active;
        case Tone::Bad:
            return palette::bad;
        case Tone::Warn:
            return palette::warn;
        case Tone::Neutral:
            return palette::neutral;
        case Tone::Dim:
            break;
    }
    return palette::dim;
}

bool input_text(const char * id, std::string & s, ImGuiInputTextFlags flags,
                const char * hint) {
    flags |= ImGuiInputTextFlags_CallbackResize;
    if(hint != nullptr)
        return ImGui::InputTextWithHint(id, hint, s.data(), s.capacity() + 1,
                                        flags, resize_callback, &s);
    return ImGui::InputText(id, s.data(), s.capacity() + 1, flags,
                            resize_callback, &s);
}

bool input_text(
    const char * id, std::string & s, ImGuiInputTextFlags flags,
    const std::function<void(ImGuiInputTextCallbackData &)> & callback) {
    Chained chained{&s, &callback};
    return ImGui::InputText(id, s.data(), s.capacity() + 1,
                            flags | ImGuiInputTextFlags_CallbackResize,
                            chained_callback, &chained);
}

bool input_text_multiline(const char * id, std::string & s, ImVec2 size,
                          ImGuiInputTextFlags flags) {
    return ImGui::InputTextMultiline(id, s.data(), s.capacity() + 1, size,
                                     flags | ImGuiInputTextFlags_CallbackResize,
                                     resize_callback, &s);
}

void text_colored(const ImVec4 & c, std::string_view text) {
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopStyleColor();
}

void tooltip_text(std::string_view text) {
    if(ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void diagnostic_line(const DiagnosticText & d) {
    const ImVec4 & col = tone_color(d.tone);
    ImGui::PushTextWrapPos(0.0f);
    text_colored(col, d.message);
    ImGui::PopTextWrapPos();
    if(!d.marked) return;
    // The expression with everything from the column on underlined in red:
    // column positions do not map to pixels in a proportional font.
    const std::string_view tail =
        d.tail.empty() ? std::string_view(" ") : std::string_view(d.tail);
    ImGui::TextDisabled("  ");
    ImGui::SameLine(0, 0);
    ImGui::TextUnformatted(d.head.data(), d.head.data() + d.head.size());
    ImGui::SameLine(0, 0);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    text_colored(col, tail);
    const ImVec2 sz =
        ImGui::CalcTextSize(tail.data(), tail.data() + tail.size());
    const float y = p.y + ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddLine({p.x, y}, {p.x + sz.x, y},
                                        ImGui::GetColorU32(col), 1.5f);
}

}  // namespace gs::front::imgui

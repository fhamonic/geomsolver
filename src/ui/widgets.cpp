#include "ui/widgets.hpp"

#include <cmath>
#include <format>

namespace gs::ui {
namespace {

int resize_callback(ImGuiInputTextCallbackData * data) {
    if(data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto * s = static_cast<std::string *>(data->UserData);
        s->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = s->data();
    }
    return 0;
}

int channel(std::uint32_t c, int shift) {
    return static_cast<int>((c >> shift) & 0xffu);
}

int to_byte(float v) {
    return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

}  // namespace

ImU32 engine_color(std::uint32_t c, float alpha_scale) {
    const int a = static_cast<int>(
        std::lround(static_cast<float>(channel(c, 0)) * alpha_scale));
    return rgba(channel(c, 24), channel(c, 16), channel(c, 8),
                std::clamp(a, 0, 255));
}

ImVec4 engine_color_vec(std::uint32_t c) {
    return {static_cast<float>(channel(c, 24)) / 255.0f,
            static_cast<float>(channel(c, 16)) / 255.0f,
            static_cast<float>(channel(c, 8)) / 255.0f,
            static_cast<float>(channel(c, 0)) / 255.0f};
}

std::string color_hex(const ImVec4 & c) {
    const int a = to_byte(c.w);
    if(a == 255)
        return std::format("#{:02x}{:02x}{:02x}", to_byte(c.x), to_byte(c.y),
                           to_byte(c.z));
    return std::format("#{:02x}{:02x}{:02x}{:02x}", to_byte(c.x), to_byte(c.y),
                       to_byte(c.z), a);
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

void help_marker(std::string_view text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    tooltip_text(text);
}

void diagnostic_line(const Diagnostic & d, std::string_view expression) {
    const bool error = d.severity == Diagnostic::Severity::Error;
    const ImVec4 col = error ? palette::bad : palette::warn;
    ImGui::PushTextWrapPos(0.0f);
    if(d.column >= 0)
        text_colored(
            col, std::format("{} at column {}: {}", error ? "error" : "warning",
                             d.column, d.message));
    else
        text_colored(
            col, std::format("{}: {}", error ? "error" : "warning", d.message));
    ImGui::PopTextWrapPos();
    const auto col_u = static_cast<std::size_t>(d.column);
    if(d.column >= 0 && col_u <= expression.size() && !expression.empty()) {
        // The expression with everything from the column on underlined in
        // red: column positions do not map to pixels in a proportional font.
        const std::string_view head = expression.substr(0, col_u);
        std::string_view tail = expression.substr(col_u);
        if(tail.empty()) tail = " ";
        ImGui::TextDisabled("  ");
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(head.data(), head.data() + head.size());
        ImGui::SameLine(0, 0);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        text_colored(col, tail);
        const ImVec2 sz =
            ImGui::CalcTextSize(tail.data(), tail.data() + tail.size());
        const float y = p.y + ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddLine({p.x, y}, {p.x + sz.x, y},
                                            ImGui::GetColorU32(col), 1.5f);
    }
}

std::string format_number(double v, int digits) {
    if(!std::isfinite(v))
        return std::isnan(v) ? "nan" : (v > 0 ? "inf" : "-inf");
    if(v == 0.0) return "0";
    const double a = std::fabs(v);
    if(a >= 1e5 || a < 1e-4) return std::format("{:.{}e}", v, digits - 1);
    const int mag = static_cast<int>(std::floor(std::log10(a)));
    const int decimals = std::clamp(digits - 1 - mag, 0, 10);
    return std::format("{:.{}f}", v, decimals);
}

std::string format_display(double si, std::string_view unit) {
    const std::string n = format_number(to_display(si, unit));
    if(unit.empty()) return n;
    return std::format("{} {}", n, unit);
}

std::string format_value(const GeoValue & v) {
    switch(v.type) {
        case ValueType::Scalar:
            return v.data.empty() ? "?" : format_number(v.scalar(), 5);
        case ValueType::Vec:
            return v.data.size() < 2
                       ? "?"
                       : std::format("({}, {})", format_number(v.data[0]),
                                     format_number(v.data[1]));
        case ValueType::Shape:
            if(v.kind == ShapeKind::Circle && v.data.size() >= 3)
                return std::format(
                    "Circle(({}, {}), r={})", format_number(v.data[0]),
                    format_number(v.data[1]), format_number(v.data[2]));
            return std::format("{}[{}]", type_name(v.type, v.kind),
                               v.vertex_count());
    }
    return "?";
}

}  // namespace gs::ui

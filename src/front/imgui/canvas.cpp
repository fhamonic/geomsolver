#include "front/imgui/canvas.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "front/imgui/canvas_painter.hpp"
#include "front/imgui/shortcuts.hpp"
#include "front/imgui/widgets.hpp"
#include "gs/app/canvas_display.hpp"

namespace gs::front::imgui {
namespace {

std::string utf8(ImWchar c) {
    const auto u = static_cast<unsigned>(c);
    std::string out;
    if(u < 0x80) {
        out += static_cast<char>(u);
    } else if(u < 0x800) {
        out += static_cast<char>(0xc0 | (u >> 6));
        out += static_cast<char>(0x80 | (u & 0x3f));
    } else {
        out += static_cast<char>(0xe0 | (u >> 12));
        out += static_cast<char>(0x80 | ((u >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (u & 0x3f));
    }
    return out;
}

}  // namespace

void Canvas::draw(App & app, const Scene & scene) {
    CanvasController & canvas = app.canvas();
    const ImGuiIO & io = ImGui::GetIO();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 50.0f);
    size.y = std::max(size.y, 50.0f);
    // The value fields are submitted before the canvas, which they lie on:
    // the first item under the pointer takes it, and a click on the canvas
    // takes the keyboard from a field in the same frame. They are drawn
    // over it, on the second channel.
    ImDrawList * draw = ImGui::GetWindowDrawList();
    draw->ChannelsSplit(2);
    draw->ChannelsSetCurrent(1);
    fields(app, p0, size);
    draw->ChannelsSetCurrent(0);
    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton("##canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    PointerInput in{.pos = {io.MousePos.x, io.MousePos.y},
                    .delta = {io.MouseDelta.x, io.MouseDelta.y},
                    .wheel = io.MouseWheel,
                    .hovered = hovered,
                    .held = ImGui::IsItemActive(),
                    .mods = held_mods(),
                    .space_held = ImGui::IsKeyDown(ImGuiKey_Space),
                    .typing = io.WantTextInput};
    if(ImGui::IsItemActivated())
        for(const auto [imgui_button, button] :
            {std::pair{ImGuiMouseButton_Left, Button::Left},
             std::pair{ImGuiMouseButton_Right, Button::Right},
             std::pair{ImGuiMouseButton_Middle, Button::Middle}})
            if(ImGui::IsMouseClicked(imgui_button)) {
                in.pressed = button;
                in.clicks = ImGui::IsMouseDoubleClicked(imgui_button) ? 2 : 1;
                break;
            }
    canvas.set_viewport(p0.x, p0.y, size.x, size.y);
    canvas.update(in, scene);
    if(hovered) run_shortcuts(app, KeyScope::Canvas);
    // The characters no text field takes: a tool seeds a value field with
    // them. Their keys ran the actions above, a letter's its tool.
    if(hovered && !io.WantTextInput && !ImGui::IsAnyItemActive())
        for(const ImWchar c : io.InputQueueCharacters) canvas.typed(utf8(c));

    // The input above may have committed: a release ends a sketch drag, whose
    // live scene goes, and a tool's click commits its shape. Drawn from
    // `scene`, read before them, this frame would show the drawing as it was
    // before the gesture, for one frame. The next frame's app.scene() finds
    // this one up to date, so nothing is evaluated twice.
    const Scene & now =
        app.scene(canvas.dragging() || ImGui::IsAnyItemActive());
    const Tool * tool = app.tools().active();
    const PreviewList preview = tool ? tool->preview() : PreviewList();
    build_canvas_display(list_, canvas, app.document(), app.selection(),
                         app.hot(), now, ImGuiTextMeasurer(),
                         app.sketch().scene(), tool ? &preview : nullptr);
    ImGuiPainter painter(draw);
    list_.replay(painter);
    draw->ChannelsMerge();

    const std::optional<Hover> hv = canvas.hover(now);
    if(!hv) return;
    if(ImGui::BeginTooltip()) {
        ImGui::TextUnformatted(hv->title.c_str());
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        for(const std::string & l : hv->lines)
            ImGui::TextUnformatted(l.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void Canvas::fields(App & app, ImVec2 p0, ImVec2 size) {
    ToolHost & host = app.tools();
    const Tool * tool = host.active();
    const std::optional<ToolHost::Edit> & edit = host.editing();
    if(open_ && (!edit || edit->id != *open_)) ++serial_;
    open_ = edit ? std::optional(edit->id) : std::nullopt;
    if(!tool) {
        active_.reset();
        update_.reset();
        focusing_.reset();
        return;
    }
    if(std::optional<ToolHost::TextUpdate> u = host.take_text_update())
        update_ = std::move(u);
    const std::optional<std::string> focus = host.take_field_focus();
    const View2d & view = app.canvas().view();
    const float fs = ImGui::GetFontSize();
    std::optional<std::string> active;
    // A copy: applying a field may change the tool's.
    const std::vector<ValueField> fields(tool->fields().begin(),
                                         tool->fields().end());
    const float width = fs * 5.5f;
    const float step = ImGui::GetFrameHeightWithSpacing();
    // Fields at one anchor stack downwards, beside it on the side the canvas
    // has room on, and inside the canvas: ImGui scrolls the canvas's child to
    // bring a field that sticks out into view as it takes the keyboard, and
    // the whole drawing jumps with it.
    std::vector<Vec2d> anchors;
    for(const ValueField & f : fields) {
        const auto same = std::ranges::count(anchors, f.anchor);
        anchors.push_back(f.anchor);
        const auto stack =
            std::ranges::count(fields, f.anchor, &ValueField::anchor);
        const float height =
            static_cast<float>(stack - 1) * step + ImGui::GetFrameHeight();
        const Vec2d at = view.to_screen(f.anchor);
        float x = static_cast<float>(at.x) + 14.0f;
        if(x + width > p0.x + size.x)
            x = static_cast<float>(at.x) - 14.0f - width;
        float y = static_cast<float>(at.y) + 10.0f;
        if(y + height > p0.y + size.y)
            y = static_cast<float>(at.y) - 10.0f - height;
        x = std::clamp(x, p0.x, std::max(p0.x, p0.x + size.x - width));
        y = std::clamp(y, p0.y, std::max(p0.y, p0.y + size.y - height));
        const ImVec2 pos(x, y + static_cast<float>(same) * step);
        const bool open = edit && edit->id == f.id;
        const Status status = field_status(f);
        const ImVec4 & tone = tone_color(status.tone);
        // Only the open field is an item. A field of an earlier state stays
        // at its anchor, where an InputText would take the pointer from the
        // canvas: the click that places the next point would focus it, and
        // the digits typed next would go to A's x.
        if(!open && focus != f.id) {
            if(f.text.empty()) continue;  // not measured yet
            const ImGuiStyle & style = ImGui::GetStyle();
            const ImVec2 end(pos.x + width, pos.y + ImGui::GetFrameHeight());
            ImDrawList * draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(pos, end, ImGui::GetColorU32(ImGuiCol_FrameBg),
                                style.FrameRounding);
            draw->PushClipRect(pos, end, true);
            draw->AddText(ImVec2(pos.x + style.FramePadding.x,
                                 pos.y + style.FramePadding.y),
                          ImGui::GetColorU32(tone), f.text.c_str());
            draw->PopClipRect();
            continue;
        }
        ImGui::SetCursorScreenPos(pos);
        ImGui::PushID(f.id.c_str());
        std::string text = open ? edit->text : f.text;
        const bool escape =
            active_ == f.id &&
            ImGui::Shortcut(
                ImGuiKey_Escape,
                ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverActive);
        if(escape) host.field_key(f.id, Key::Escape);
        if(focus == f.id) {
            if(!update_ || update_->id != f.id)
                update_ = ToolHost::TextUpdate{f.id, text, text.size()};
            ImGui::SetKeyboardFocusHere();
            focusing_ = f.id;
        }
        // The characters typed while the field waits for the keyboard are
        // read here: the canvas passes on none while the field that Tab
        // left still counts as having the keyboard (WantTextInput), and
        // InputText drops those of the frame the focus request activates it
        // and consumes them. Until then they go to ToolHost::typed(); on
        // that frame they are queued again for the next: given to the host,
        // they would reach the field's buffer by the Always callback only on
        // the first frame typed into after them, which InputText reports as
        // an Edit before that, and the field would show "30c", the host
        // "30m".
        std::string queued;
        if(focusing_ == f.id)
            for(const ImWchar c : ImGui::GetIO().InputQueueCharacters)
                queued += utf8(c);
        ImGui::SetNextItemWidth(width);
        ImGui::PushStyleColor(ImGuiCol_Text, tone);
        const std::string id = std::format("##{}{}", f.id, serial_);
        const bool enter = input_text(
            id.c_str(), text,
            ImGuiInputTextFlags_EnterReturnsTrue |
                ImGuiInputTextFlags_CallbackAlways |
                ImGuiInputTextFlags_CallbackEdit |
                ImGuiInputTextFlags_CallbackCompletion,
            [&](ImGuiInputTextCallbackData & data) {
                const std::string_view now(
                    data.Buf, static_cast<std::size_t>(data.BufTextLen));
                switch(data.EventFlag) {
                    case ImGuiInputTextFlags_CallbackAlways:
                        if(update_ && update_->id == f.id) {
                            if(now != update_->text) {
                                data.DeleteChars(0, data.BufTextLen);
                                data.InsertChars(0, update_->text.data(),
                                                 update_->text.data() +
                                                     update_->text.size());
                            }
                            data.CursorPos = data.SelectionEnd =
                                static_cast<int>(update_->caret);
                            data.SelectionStart =
                                update_->select ? 0 : data.CursorPos;
                            update_.reset();
                        }
                        break;
                    case ImGuiInputTextFlags_CallbackEdit:
                        host.set_field_text(f.id, std::string(now));
                        break;
                    case ImGuiInputTextFlags_CallbackCompletion:
                        // InputText runs one callback a frame: characters
                        // typed on Tab's frame get no Edit, and Tab would
                        // move on from the text before them.
                        if(!edit || now != edit->text)
                            host.set_field_text(f.id, std::string(now));
                        host.field_key(f.id, Key::Tab);
                        break;
                    default:
                        break;
                }
            });
        ImGui::PopStyleColor();
        const bool held = ImGui::IsItemActive();
        if(!held && !queued.empty()) {
            host.typed(queued);
            ImGui::GetIO().InputQueueCharacters.resize(0);
        }
        if(held && focusing_ == f.id) {
            focusing_.reset();
            // A click activates it with that frame's characters inserted.
            if(ImGui::IsItemActivated() && !queued.empty() &&
               !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                ImGui::GetIO().AddInputCharactersUTF8(queued.c_str());
        }
        // Reported once ImGui routes the text input to it, a frame after
        // the one that activates it: the Canvas shortcuts of that frame run
        // with WantTextInput still false, and reported then, the letters of
        // a burst typed on it would run their tools' chords.
        if(held && ImGui::GetIO().WantTextInput) host.field_focused(f.id);
        if(held) active = f.id;
        tooltip_text(status.tip);
        // On the field's first frame with the keyboard, InputText's routes
        // for Enter and Esc, and the Esc claimed above, are not registered
        // yet: without these, the keys would be lost.
        const auto pressed = [](ImGuiKey k) {
            return ImGui::IsKeyPressed(k, false);
        };
        if(enter ||
           (held && (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter))))
            host.field_key(f.id, Key::Enter);
        else if(held && !escape && pressed(ImGuiKey_Escape))
            host.field_key(f.id, Key::Escape);
        ImGui::PopID();
    }
    active_ = std::move(active);
}

}  // namespace gs::front::imgui

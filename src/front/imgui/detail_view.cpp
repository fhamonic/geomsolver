#include "front/imgui/detail_view.hpp"

#include <array>
#include <functional>
#include <optional>
#include <string_view>

#include "front/imgui/widgets.hpp"

namespace gs::front::imgui {
namespace {

const ImVec4 kErrorFrame(0.45f, 0.12f, 0.12f, 0.9f);
const ImVec4 kPendingFrame(0.38f, 0.32f, 0.10f, 0.9f);

void wrapped(const ImVec4 & color, std::string_view text) {
    ImGui::PushTextWrapPos(0.0f);
    text_colored(color, text);
    ImGui::PopTextWrapPos();
}

// The Name item: the name, or its editor once a double click or F2 opened
// it. True when Enter renamed the entry: the sheet's paths are stale.
bool name_item(App & app, const DetailSheet & sheet, const DetailItem & item) {
    RenameEditor & editor = app.panels().host.renamer();
    const std::string & entry = item.field.path;
    if(entry.empty()) {
        ImGui::TextUnformatted(item.text.c_str());
        tooltip_text(item.tip);
        return false;
    }
    if(!editor.editing(entry, PanelId::Properties)) {
        ImGui::Selectable(item.text.c_str(), false,
                          ImGuiSelectableFlags_AllowDoubleClick);
        if(ImGui::IsItemHovered() &&
           ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            app.panels().host.open_rename(*sheet.key, PanelId::Properties);
        tooltip_text(item.tip);
        return false;
    }
    const std::string why(editor.refusal(entry));
    const bool renamed =
        name_field(app, *sheet.key, entry, item.text, -FLT_MIN);
    if(!why.empty()) wrapped(palette::bad, why);
    return renamed;
}

}  // namespace

bool name_field(App & app, const SelKey & key, const std::string & entry,
                const std::string & name, float width) {
    static std::string text;
    PanelHost & host = app.panels().host;
    RenameEditor & editor = host.renamer();
    const bool refused = !editor.refusal(entry).empty();
    if(editor.take_focus()) {
        if(!refused) text = name;
        ImGui::SetKeyboardFocusHere();
    }
    if(refused) ImGui::PushStyleColor(ImGuiCol_FrameBg, kErrorFrame);
    ImGui::SetNextItemWidth(width);
    const bool enter =
        input_text("##rename", text, ImGuiInputTextFlags_EnterReturnsTrue);
    if(refused) ImGui::PopStyleColor();
    // Also true on the Enter frame: Enter is handled first.
    const bool left = ImGui::IsItemDeactivated();
    tooltip_text(RenameEditor::kTip);
    if(enter) return host.rename(key, text);
    if(left) host.cancel_rename();
    return false;
}

bool draft_field(App & app, const Field & f, float width, const char * hint) {
    Document & doc = app.document();
    FieldDrafts & fields = app.panels().host.fields();
    FieldDrafts::Draft & d = fields.begin(doc, f.path);
    ImGui::PushID(f.path.c_str());
    // A window that appears has no size yet: the field would lose the
    // keyboard it is given. A window that has no focus yet waits for its
    // panel's focus request, or the field would focus it before the panels
    // asked for ahead of it show their tabs. The request waits.
    if(!ImGui::IsWindowAppearing() &&
       ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
       app.take_field_focus(f.path))
        ImGui::SetKeyboardFocusHere();
    const bool framed = d.error || d.pending;
    if(d.error)
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kErrorFrame);
    else if(d.pending)
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kPendingFrame);
    bool enter = false;
    if(f.kind == FieldKind::MultilineText) {
        // ImGui's multi-line field types a new line on Enter and reports
        // Ctrl+Enter as its Enter: FieldDrafts::enter_applies().
        const float h = ImGui::GetTextLineHeight() * 4.0f +
                        2.0f * ImGui::GetStyle().FramePadding.y;
        enter = input_text_multiline("##f", d.text, ImVec2(width, h),
                                     ImGuiInputTextFlags_EnterReturnsTrue |
                                         ImGuiInputTextFlags_WordWrap);
    } else {
        ImGui::SetNextItemWidth(width);
        enter = input_text("##f", d.text, ImGuiInputTextFlags_EnterReturnsTrue,
                           hint);
    }
    if(framed) ImGui::PopStyleColor();
    const bool active = ImGui::IsItemActive();
    tooltip_text(FieldDrafts::tip(f.path, f.kind));
    const bool committed = fields.end(doc, d, f.path, f.kind, active, enter);
    ImGui::PopID();
    return committed;
}

void field_diagnostics(App & app, const std::string & path,
                       const FieldDrafts::Diagnostics & ds) {
    if(ds.items.empty()) return;
    FieldDrafts & fields = app.panels().host.fields();
    Document & doc = app.document();
    ImGui::PushID(path.c_str());
    bool edited = false;
    for(const FieldDrafts::Item & item : ds.items) {
        diagnostic_line(item.text);
        if(!item.raw_editor && !item.removable) continue;
        const float button = ImGui::CalcTextSize(item.remove_label.c_str()).x +
                             2.0f * ImGui::GetStyle().FramePadding.x +
                             ImGui::GetStyle().ItemSpacing.x;
        if(item.raw_editor) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", FieldDrafts::kRawLabel);
            ImGui::SameLine();
            edited = draft_field(app, {item.path, FieldKind::RawJson}, -button);
            if(item.removable) ImGui::SameLine();
        }
        if(item.removable && !edited) {
            if(ImGui::SmallButton(item.remove_label.c_str())) {
                fields.remove_key(doc, item.path);
                edited = true;
            }
            tooltip_text(FieldDrafts::kRemoveTip);
        }
        // The edit recompiled the document: the items left describe it as
        // it was before.
        if(edited) break;
    }
    if(!edited && fields.can_revert(doc, path)) {
        if(ImGui::SmallButton(FieldDrafts::kRevert)) fields.revert(doc, path);
        tooltip_text(FieldDrafts::kRevertTip);
    }
    ImGui::PopID();
}

namespace {

// What the items of a sheet call: the keyed operations of Properties, or
// those of the Document panel.
struct SheetOps {
    std::function<bool(std::string_view, const DetailValue &)> set;
    std::function<bool(std::string_view)> press;
    std::function<bool(std::string_view)> begin_drag;
    std::function<bool(std::string_view, const DetailValue &)> update_drag;
    std::function<bool(std::string_view)> end_drag;
};

// A printf format of `unit`: a "%" unit must be doubled, or the format
// ends in a lone '%' (undefined behaviour in vsnprintf).
std::string number_format(std::string_view unit) {
    std::string fmt = "%.5g";
    if(!unit.empty()) fmt += ' ';
    for(const char ch : unit) {
        fmt += ch;
        if(ch == '%') fmt += '%';
    }
    return fmt;
}

// The drag of the widget just submitted: one undo step from its activation
// to its release, the value it shows read back from the sheet.
void drag(const SheetOps & ops, const DetailItem & item, bool changed,
          const DetailValue & value) {
    if(ImGui::IsItemActivated()) ops.begin_drag(item.id);
    if(changed) ops.update_drag(item.id, value);
    if(ImGui::IsItemDeactivated()) ops.end_drag(item.id);
}

// The editor of `item`. False when it changed the document: the items after
// it were read before, and may name what is gone.
bool editor(App & app, const DetailSheet & sheet, const DetailItem & item,
            const SheetOps & ops) {
    using Kind = DetailItem::Kind;
    switch(item.kind) {
        case Kind::Name:
            return !name_item(app, sheet, item);
        case Kind::Field: {
            const bool applied = draft_field(app, item.field, -FLT_MIN);
            if(!item.refusal.empty()) wrapped(palette::bad, item.refusal);
            return !applied;
        }
        case Kind::Value:
        case Kind::Note:
        case Kind::Section:
            ImGui::AlignTextToFramePadding();
            wrapped(tone_color(item.tone), item.text);
            if(!item.tip.empty()) tooltip_text(item.tip);
            return true;
        case Kind::Checkbox: {
            bool on = std::get<bool>(item.value);
            const bool clicked = ImGui::Checkbox("##v", &on);
            if(!item.tip.empty()) tooltip_text(item.tip);
            return !(clicked && ops.set(item.id, on));
        }
        case Kind::Choice: {
            const int k = std::get<int>(item.value);
            bool chosen = false;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if(ImGui::BeginCombo("##v", item.text.c_str())) {
                for(std::size_t c = 0; c < item.choices.size() && !chosen; ++c)
                    if(ImGui::Selectable(item.choices[c].c_str(),
                                         static_cast<int>(c) == k))
                        chosen = ops.set(item.id, static_cast<int>(c));
                ImGui::EndCombo();
            }
            if(!item.tip.empty()) tooltip_text(item.tip);
            return !chosen;
        }
        case Kind::Drag:
        case Kind::Slider: {
            const std::string fmt = number_format(item.unit);
            const bool bounded = item.min < item.max;
            double lo = item.min, hi = item.max;
            ImGui::SetNextItemWidth(-FLT_MIN);
            bool changed = false;
            DetailValue value = item.value;
            if(auto * pair = std::get_if<std::array<double, 2>>(&value)) {
                changed = ImGui::DragScalarN(
                    "##v", ImGuiDataType_Double, pair->data(), 2,
                    static_cast<float>(item.speed), bounded ? &lo : nullptr,
                    bounded ? &hi : nullptr, fmt.c_str());
            } else if(auto * whole = std::get_if<int>(&value)) {
                int ilo = static_cast<int>(lo), ihi = static_cast<int>(hi);
                changed = ImGui::DragScalar("##v", ImGuiDataType_S32, whole,
                                            static_cast<float>(item.speed),
                                            bounded ? &ilo : nullptr,
                                            bounded ? &ihi : nullptr, "%d");
            } else if(auto * one = std::get_if<double>(&value)) {
                changed = item.kind == Kind::Slider
                              ? ImGui::SliderScalar("##v", ImGuiDataType_Double,
                                                    one, &lo, &hi, fmt.c_str())
                              : ImGui::DragScalar(
                                    "##v", ImGuiDataType_Double, one,
                                    static_cast<float>(item.speed),
                                    bounded ? &lo : nullptr,
                                    bounded ? &hi : nullptr, fmt.c_str());
            }
            if(!item.tip.empty()) tooltip_text(item.tip);
            drag(ops, item, changed, value);
            return true;
        }
        case Kind::Colour: {
            DetailValue value = item.value;
            const bool changed = ImGui::ColorEdit4(
                "##v", std::get<Rgba>(value).data(),
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            if(!item.tip.empty()) tooltip_text(item.tip);
            drag(ops, item, changed, value);
            return true;
        }
        case Kind::Button: {
            const bool pressed = ImGui::SmallButton(item.label.c_str());
            if(!item.tip.empty()) tooltip_text(item.tip);
            return !(pressed && ops.press(item.id));
        }
    }
    return true;
}

// The items of `sheet` as a grid of labels and editors, a heading for each
// Section, its notes under it, then the diagnostics of its entry.
void sheet_view(App & app, const DetailSheet & sheet, const SheetOps & ops) {
    using Kind = DetailItem::Kind;
    PanelHost & host = app.panels().host;
    if(!sheet.title.empty()) {
        ImGui::TextUnformatted(sheet.title.c_str());
        ImGui::Separator();
    }
    const float fs = ImGui::GetFontSize();
    bool table = false;
    for(const DetailItem & item : sheet.items) {
        if(item.kind == Kind::Note) continue;
        if(item.kind == Kind::Section) {
            if(table) ImGui::EndTable();
            table = false;
            ImGui::SeparatorText(item.label.c_str());
            continue;
        }
        if(!table) {
            table = ImGui::BeginTable("##sheet", 2,
                                      ImGuiTableFlags_SizingStretchProp);
            if(!table) break;
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed,
                                    fs * 6.0f);
            ImGui::TableSetupColumn("value");
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if(item.kind != Kind::Button) {
            ImGui::TextDisabled("%s", item.label.c_str());
            if(!item.tip.empty()) tooltip_text(item.tip);
        }
        ImGui::TableNextColumn();
        ImGui::PushID(item.id.c_str());
        ImGui::BeginDisabled(item.read_only);
        const bool go_on = editor(app, sheet, item, ops);
        ImGui::EndDisabled();
        ImGui::PopID();
        if(!go_on) {
            ImGui::EndTable();
            return;
        }
    }
    if(table) ImGui::EndTable();
    for(const DetailItem & item : sheet.items)
        if(item.kind == Kind::Note) wrapped(palette::dim, item.text);
    if(sheet.path.empty()) return;
    const std::vector<std::string> owned = sheet.owned();
    field_diagnostics(
        app, sheet.path,
        host.fields().diagnostics(app.document(), sheet.path, owned));
    if(const std::string note = host.excluded_note(sheet.path); !note.empty())
        wrapped(palette::bad, note);
}

}  // namespace

void detail_view(App & app) {
    if(!ImGui::IsWindowAppearing())
        app.panels().host.take_rename_request(PanelId::Properties);
    PropertiesPresenter & p = app.panels().properties;
    const DetailSheet sheet = p.sheet();
    const std::optional<SelKey> key = sheet.key;
    if(!key) {
        sheet_view(app, sheet, {});
        return;
    }
    sheet_view(
        app, sheet,
        {.set = [&](std::string_view id,
                    const DetailValue & v) { return p.set(*key, id, v); },
         .press = [&](std::string_view id) { return p.press(*key, id); },
         .begin_drag =
             [&](std::string_view id) { return p.begin_drag(*key, id); },
         .update_drag =
             [&](std::string_view id, const DetailValue & v) {
                 return p.update_drag(*key, id, v);
             },
         .end_drag =
             [&](std::string_view id) { return p.end_drag(*key, id); }});
}

void document_view(App & app) {
    DocumentPresenter & d = app.panels().document;
    auto none = [](std::string_view) { return false; };
    sheet_view(app, d.sheet(),
               {.set = [&](std::string_view id,
                           const DetailValue & v) { return d.set(id, v); },
                .press = [&](std::string_view id) { return d.press(id); },
                .begin_drag = none,
                .update_drag = [](std::string_view,
                                  const DetailValue &) { return false; },
                .end_drag = none});
}

}  // namespace gs::front::imgui

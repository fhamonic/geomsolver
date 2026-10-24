#include "front/imgui/list_view.hpp"

#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "front/imgui/detail_view.hpp"
#include "front/imgui/shortcuts.hpp"
#include "front/imgui/widgets.hpp"

namespace gs::front::imgui {
namespace {

const ImU32 kHotRow = rgba(90, 110, 140, 70);

// An eye as large as a checkbox, open (an outline and its pupil) or shut
// (faded and struck through): a tick would read as "enabled", which is what
// the Check of the next tab means. True when clicked.
bool eye_button(bool open, bool dim) {
    const float fs = ImGui::GetFontSize();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##toggle", ImVec2(fs, fs));
    ImDrawList * draw = ImGui::GetWindowDrawList();
    if(ImGui::IsItemHovered())
        draw->AddRectFilled(p, ImVec2(p.x + fs, p.y + fs),
                            ImGui::GetColorU32(ImGuiCol_FrameBgHovered),
                            ImGui::GetStyle().FrameRounding);
    const ImU32 col = ImGui::GetColorU32(open && !dim ? ImGuiCol_Text
                                                      : ImGuiCol_TextDisabled);
    const ImVec2 c(p.x + 0.5f * fs, p.y + 0.5f * fs);
    draw->AddEllipse(c, ImVec2(0.45f * fs, 0.27f * fs), col, 0.0f, 0, 1.5f);
    if(open)
        draw->AddCircleFilled(c, 0.14f * fs, col);
    else
        draw->AddLine(ImVec2(p.x + 0.12f * fs, p.y + 0.85f * fs),
                      ImVec2(p.x + 0.88f * fs, p.y + 0.15f * fs), col, 1.5f);
    return pressed;
}

void toolbar(App & app, const std::vector<std::string> & ids) {
    for(std::size_t i = 0; i < ids.size(); ++i) {
        const Action & a = *app.actions().find(ids[i]);
        if(i > 0) ImGui::SameLine();
        ImGui::BeginDisabled(!a.is_enabled());
        const std::string text = a.caption.empty() ? a.label() : a.caption;
        const std::string label = std::format("{}###{}", text, a.id);
        if(ImGui::SmallButton(label.c_str())) a.run();
        ImGui::EndDisabled();
        // A caption stands for a label that names the selection, which
        // costs a Picker of the scene (formula.add_constraint): read only
        // under the pointer, never every frame.
        if(!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) continue;
        const std::string tip = !a.chords.empty() ? chord_text(a.chords.front())
                                : a.caption.empty() ? std::string()
                                                    : a.label();
        if(!tip.empty()) ImGui::SetTooltip("%s", tip.c_str());
    }
}

void filters(ListPresenter & list) {
    const std::vector<Filter> fs = list.filters();
    const float fs_px = ImGui::GetFontSize();
    for(std::size_t i = 0; i < fs.size(); ++i) {
        const Filter & f = fs[i];
        ImGui::PushID(static_cast<int>(i));
        if(i > 0) ImGui::SameLine();
        if(f.kind == Filter::Kind::Check) {
            bool on = std::get<bool>(f.value);
            if(ImGui::Checkbox(f.label.c_str(), &on)) list.set_filter(i, on);
        } else if(f.kind == Filter::Kind::Choice) {
            const int k = std::get<int>(f.value);
            ImGui::SetNextItemWidth(fs_px * 6.0f);
            if(ImGui::BeginCombo(
                   f.label.c_str(),
                   k >= 0 ? f.choices[static_cast<std::size_t>(k)].c_str()
                          : "")) {
                for(std::size_t c = 0; c < f.choices.size(); ++c)
                    if(ImGui::Selectable(f.choices[c].c_str(),
                                         static_cast<int>(c) == k))
                        list.set_filter(i, static_cast<int>(c));
                ImGui::EndCombo();
            }
        } else {
            std::string text = std::get<std::string>(f.value);
            ImGui::SetNextItemWidth(fs_px * 8.0f);
            if(input_text("##text", text, 0, f.label.c_str()))
                list.set_filter(i, text);
        }
        if(!f.tip.empty()) tooltip_text(f.tip);
        ImGui::PopID();
    }
}

// A cell's text, clipped to its column, with its tooltip when it does not
// fit or says more than the text.
void text_cell(const ListCell & cell) {
    if(cell.kind == ListCell::Kind::Empty) return;
    const bool clipped = ImGui::CalcTextSize(cell.text.c_str()).x >
                         ImGui::GetContentRegionAvail().x;
    text_colored(tone_color(cell.tone), cell.text);
    if(!cell.tip.empty() && (clipped || cell.tip != cell.text))
        tooltip_text(cell.tip);
}

class ListView {
public:
    ListView(App & app, PanelId panel, ListPresenter & list)
        : app_(app), panel_(panel), list_(list), host_(app.panels().host) {}

    std::optional<SelKey> draw() {
        toolbar(app_, list_.toolbar());
        filters(list_);
        // The window that appears has no size yet: its field could not keep
        // the keyboard.
        if(!ImGui::IsWindowAppearing()) host_.take_rename_request(panel_);
        const std::span<const Column> columns = list_.columns();
        const std::vector<ListCell> footer = list_.footer();
        const float fs = ImGui::GetFontSize();
        const float footer_h = static_cast<float>(footer.size()) *
                               ImGui::GetTextLineHeightWithSpacing();
        int shown = 0;
        for(std::size_t c = 0; c < columns.size(); ++c)
            shown += list_.column_shown(c) ? 1 : 0;
        if(ImGui::BeginTable("##rows", shown,
                             ImGuiTableFlags_RowBg |
                                 ImGuiTableFlags_BordersInnerH |
                                 ImGuiTableFlags_ScrollY,
                             ImVec2(0.0f, -footer_h))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            for(std::size_t c = 0; c < columns.size(); ++c)
                if(list_.column_shown(c))
                    ImGui::TableSetupColumn(
                        columns[c].label.c_str(),
                        columns[c].ems > 0.0f
                            ? ImGuiTableColumnFlags_WidthFixed
                            : ImGuiTableColumnFlags_WidthStretch,
                        columns[c].ems * fs);
            ImGui::TableHeadersRow();
            rows(std::nullopt);
            ImGui::EndTable();
        }
        for(const ListCell & line : footer) text_cell(line);
        return hovered_;
    }

private:
    // False once the rows' paths moved (a rename): the rest of this frame's
    // ids may name other entries.
    bool rows(const std::optional<RowId> & parent) {
        for(const RowId & id : list_.children(parent)) {
            const std::optional<ListRow> r = list_.row(id);
            if(!r) continue;
            // Not the path: a header and an entry of an included file have
            // none, and rows sharing an ID share their state.
            ImGui::PushID(id.header()
                              ? ("#" + id.group).c_str()
                              : std::format("{}:{}#{}",
                                            static_cast<int>(id.key.kind),
                                            key_name(id.key), id.ordinal)
                                    .c_str());
            const bool go_on = id.header() ? header(*r) : row(*r);
            ImGui::PopID();
            if(!go_on) return false;
        }
        return true;
    }

    // A group header: never selected, it opens and closes its rows.
    bool header(const ListRow & r) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemOpen(list_.expanded(r.id));
        const bool open =
            ImGui::TreeNodeEx("##group",
                              ImGuiTreeNodeFlags_SpanAllColumns |
                                  ImGuiTreeNodeFlags_LabelSpanAllColumns |
                                  ImGuiTreeNodeFlags_NoTreePushOnOpen,
                              "%s", r.label.c_str());
        list_.set_expanded(r.id, open);
        return !open || rows(r.id);
    }

    bool row(const ListRow & r) {
        const SelKey & key = r.id.key;
        ImGui::TableNextRow();
        if(app_.hot() == key)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, kHotRow);
        ImGui::TableNextColumn();
        if(host_.take_reveal(panel_, key)) ImGui::SetScrollHereY(0.5f);
        // A first column of toggles is as narrow as a toggle: the indent
        // would push it out of its column.
        const ListCell::Kind first = list_.columns().front().kind;
        if(first != ListCell::Kind::Check && first != ListCell::Kind::Eye)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                 static_cast<float>(r.depth) *
                                     ImGui::GetFontSize());
        if(r.dim)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                                ImGui::GetStyle().Alpha * 0.55f);
        // One selectable under the whole row: a click on any cell selects
        // it. AllowOverlap: the toggles and the name editor drawn over it
        // take their own clicks.
        const ImVec2 at = ImGui::GetCursorPos();
        const bool pressed =
            ImGui::Selectable("##row", app_.selection().contains(key),
                              ImGuiSelectableFlags_SpanAllColumns |
                                  ImGuiSelectableFlags_AllowOverlap |
                                  ImGuiSelectableFlags_AllowDoubleClick);
        interact(r, pressed);
        if(!r.tooltip.empty()) tooltip_text(r.tooltip);
        ImGui::SetCursorPos(at);
        const RenameEditor & editor = host_.renamer();
        const bool renaming = editor.editing(r.path, panel_);
        bool renamed = false, refusal_shown = false, after_name = false;
        for(std::size_t c = 0; c < r.cells.size() && !renamed; ++c) {
            if(!list_.column_shown(c)) continue;
            if(c > 0) ImGui::TableNextColumn();
            const ListCell & cell = r.cells[c];
            ImGui::PushID(static_cast<int>(c));
            const bool name_before =
                std::exchange(after_name, cell.kind == ListCell::Kind::Name);
            if(cell.kind == ListCell::Kind::Name && renaming) {
                renamed = name_field(app_, key, r.path, key.name, -FLT_MIN);
                if(ImGui::IsItemHovered()) hovered_ = key;
            } else if(renaming && !refusal_shown && name_before &&
                      !editor.refusal(r.path).empty()) {
                const std::string why(editor.refusal(r.path));
                text_cell({ListCell::Kind::Text, why, Tone::Bad, why});
                refusal_shown = true;
            } else if(cell.kind == ListCell::Kind::Check ||
                      cell.kind == ListCell::Kind::Eye) {
                toggle(r, c);
            } else {
                if(cell.kind == ListCell::Kind::Name) expander(r);
                text_cell(cell);
            }
            ImGui::PopID();
        }
        if(r.dim) ImGui::PopStyleVar();
        if(renamed) return false;
        return !r.expandable || !list_.expanded(r.id) || rows(r.id);
    }

    // Before the name of a row with rows under it, its arrow; the name of
    // a row under another, indented. In the Name column: a first column of
    // toggles has no room for either.
    void expander(const ListRow & r) {
        const ListCell::Kind first = list_.columns().front().kind;
        if(first != ListCell::Kind::Check && first != ListCell::Kind::Eye)
            return;
        const float fs = ImGui::GetFontSize();
        if(r.depth > 1)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                 static_cast<float>(r.depth - 1) * fs);
        if(!r.expandable) return;
        ImGui::SetNextItemOpen(list_.expanded(r.id));
        const bool open =
            ImGui::TreeNodeEx("##rows", ImGuiTreeNodeFlags_NoTreePushOnOpen);
        list_.set_expanded(r.id, open);
        ImGui::SameLine(0.0f, 0.0f);
    }

    // A toggle as large as the text: the rows keep the height of a line.
    void toggle(const ListRow & r, std::size_t c) {
        const ListCell & cell = r.cells[c];
        if(cell.kind == ListCell::Kind::Eye) {
            if(eye_button(cell.on, cell.tone == Tone::Dim))
                list_.toggle(r.id, c);
        } else {
            bool on = cell.on;
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
            if(ImGui::Checkbox("##toggle", &on)) list_.toggle(r.id, c);
            ImGui::PopStyleVar();
        }
        if(ImGui::IsItemHovered()) hovered_ = r.id.key;
        if(!cell.tip.empty()) tooltip_text(cell.tip);
    }

    // The row's selectable was just submitted, `pressed` by a click or a
    // double click: what the pointer did to it. Its press, not a click
    // while it is hovered: IsItemHovered() is true too over a toggle drawn
    // over the row, whose click must not select it.
    void interact(const ListRow & r, bool pressed) {
        const SelKey & key = r.id.key;
        if(pressed && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            app_.panels().activate(panel_, r.id,
                                   column(ImGui::TableGetHoveredColumn()));
        } else if(pressed) {
            host_.click(key, held_mods());
        }
        if(!ImGui::IsItemHovered()) return;
        hovered_ = key;
        if(ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            const ImVec2 at = ImGui::GetMousePos();
            app_.context_click(key, {at.x, at.y});
        }
    }

    // The list's column drawn as the table's column `drawn`: the hidden
    // ones take no table column.
    std::optional<std::size_t> column(int drawn) const {
        for(std::size_t c = 0; drawn >= 0 && c < list_.columns().size(); ++c)
            if(list_.column_shown(c) && drawn-- == 0) return c;
        return std::nullopt;
    }

    App & app_;
    PanelId panel_;
    ListPresenter & list_;
    PanelHost & host_;
    std::optional<SelKey> hovered_;
};

}  // namespace

std::optional<SelKey> list_view(App & app, PanelId panel,
                                ListPresenter & list) {
    if(!app.document().has_instance()) {
        ImGui::TextDisabled("%s", PanelHost::kNoInstance);
        return std::nullopt;
    }
    return ListView(app, panel, list).draw();
}

}  // namespace gs::front::imgui

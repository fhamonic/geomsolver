#include "front/imgui/table_view.hpp"

#include <format>
#include <string>
#include <vector>

#include "front/imgui/widgets.hpp"

namespace gs::front::imgui {
namespace {

void choices_cell(TablePresenter & table, std::size_t i, std::size_t column,
                  const Cell & cell, std::vector<std::string> & choices) {
    ImGui::PushID(static_cast<int>(i));
    ImGui::PushID(static_cast<int>(column));
    if(ImGui::SmallButton(cell.text.c_str())) ImGui::OpenPopup("choices");
    if(!cell.tip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        tooltip_text(cell.tip);
    if(ImGui::BeginPopup("choices")) {
        table.choices(i, column, choices);
        for(std::size_t k = 0; k < choices.size(); ++k)
            if(ImGui::Selectable(choices[k].c_str()))
                table.choose(i, column, k);
        ImGui::EndPopup();
    }
    ImGui::PopID();
    ImGui::PopID();
}

}  // namespace

void table_view(const char * id, TablePresenter & table, ImGuiTableFlags flags,
                ImVec2 size) {
    const std::vector<std::string> & headers = table.headers();
    if(!ImGui::BeginTable(id, static_cast<int>(headers.size()), flags, size))
        return;
    ImGui::TableSetupScrollFreeze(1, 1);
    for(const std::string & h : headers) ImGui::TableSetupColumn(h.c_str());
    ImGui::TableHeadersRow();
    TableRow row;
    std::vector<std::string> choices;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(table.rows()));
    while(clipper.Step())
        for(int ri = clipper.DisplayStart; ri < clipper.DisplayEnd; ++ri) {
            const auto i = static_cast<std::size_t>(ri);
            table.row(i, row);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string label = std::format("{}##row{}", row.label, i);
            // AllowOverlap: without it the row swallows the clicks meant for
            // a button in a later cell.
            if(ImGui::Selectable(label.c_str(), row.selected,
                                 ImGuiSelectableFlags_SpanAllColumns |
                                     ImGuiSelectableFlags_AllowOverlap))
                table.activate(i);
            if(!row.tip.empty() &&
               ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                tooltip_text(row.tip);
            for(std::size_t c = 0; c < row.cells.size(); ++c) {
                ImGui::TableNextColumn();
                const Cell & cell = row.cells[c];
                switch(cell.kind) {
                    case Cell::Kind::Empty:
                        break;
                    case Cell::Kind::Text:
                        if(cell.tone == Tone::Normal)
                            ImGui::TextUnformatted(cell.text.c_str());
                        else
                            text_colored(tone_color(cell.tone), cell.text);
                        if(!cell.tip.empty()) tooltip_text(cell.tip);
                        break;
                    case Cell::Kind::Choices:
                        choices_cell(table, i, c, cell, choices);
                        break;
                }
            }
        }
    ImGui::EndTable();
}

}  // namespace gs::front::imgui

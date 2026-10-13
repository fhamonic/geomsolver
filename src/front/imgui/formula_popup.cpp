#include "front/imgui/formula_popup.hpp"

#include <algorithm>
#include <format>
#include <string_view>
#include <utility>
#include <vector>

#include "front/imgui/widgets.hpp"

namespace gs::front::imgui {
namespace {

using Outcome = FormulaDraft::Outcome;

constexpr const char * kId = "###formula";
constexpr std::size_t kRows = 8;  // candidates shown before the list scrolls

float label_width() { return ImGui::GetFontSize() * 6.0f; }
float field_width() { return ImGui::GetFontSize() * 30.0f; }

void label(const char * text, const std::string & tip) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", text);
    if(!tip.empty()) tooltip_text(tip);
    ImGui::SameLine(label_width());
}

}  // namespace

std::string FormulaPopup::draw() {
    FormulaDraft & draft = app_.formula();
    // ImGui closes a modal when another window takes the focus: the draft,
    // still open, opens it again.
    if(draft.take_open_request() ||
       (draft.is_open() && !ImGui::IsPopupOpen(kId))) {
        ImGui::OpenPopup(kId);
        update_.reset();
        focus_ = true;
    }
    const std::string title = std::format("{}{}", draft.title(), kId);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    // Its width from the first frame: a diagnostic wraps at the window's
    // edge, and in the narrow window that measures the content first its
    // lines would make the window tall, and centre it too high.
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(label_width() + field_width() +
                   2.0f * ImGui::GetStyle().WindowPadding.x,
               0.0f),
        ImVec2(FLT_MAX, FLT_MAX));
    if(!ImGui::BeginPopupModal(title.c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
        return {};
    const std::vector<DetailItem> items = draft.fields();
    if(!items.empty()) field(draft, items.front());
    formula_field(draft);
    candidates(draft);
    for(std::size_t i = 1; i < items.size(); ++i) field(draft, items[i]);

    const FormulaDraft::Check & check = draft.check();
    ImGui::PushTextWrapPos(label_width() + field_width());
    if(!check.status.text.empty()) {
        text_colored(tone_color(check.status.tone), check.status.text);
        if(!check.status.tip.empty()) tooltip_text(check.status.tip);
    }
    if(check.diagnostic) diagnostic_line(*check.diagnostic);
    if(!check.note.empty()) text_colored(palette::dim, check.note);
    if(!draft.refusal().empty()) text_colored(palette::bad, draft.refusal());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if(ImGui::Button(draft.commit_label().c_str()) && !draft.commit())
        focus_ = true;
    ImGui::SameLine();
    if(ImGui::Button(FormulaDraft::kCancel)) draft.cancel();
    if(!draft.is_open()) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return draft.title();
}

void FormulaPopup::formula_field(FormulaDraft & draft) {
    if(std::optional<FormulaDraft::TextUpdate> u = draft.take_text_update()) {
        text_ = u->text;
        update_ = std::move(u);
    }
    // Esc is the draft's: the field would revert its text first, then lose
    // the keyboard, and the list would stay shown.
    if(ImGui::Shortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteGlobal |
                                            ImGuiInputFlags_RouteOverActive))
        draft.key(Key::Escape);
    if(active_ && ImGui::IsKeyPressed(ImGuiKey_Tab, false)) draft.key(Key::Tab);
    // Not while a button is held: the item it pressed, such as the row
    // whose double click opened the pop-up, is active until the release,
    // and keeps the focus asked for meanwhile.
    if(focus_ && !ImGui::IsAnyMouseDown()) {
        focus_ = false;
        // Activated from the focus API, ImGui selects the whole text, which
        // the next key typed would replace: the callback puts the caret
        // back.
        if(!update_) update_ = FormulaDraft::TextUpdate{text_, draft.caret()};
        ImGui::SetKeyboardFocusHere();
    }
    label(FormulaDraft::kFormula, FormulaDraft::kKeysTip);
    ImGui::SetNextItemWidth(field_width());
    const bool enter = input_text(
        "##formula", text_,
        ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_CallbackAlways |
            ImGuiInputTextFlags_CallbackEdit |
            ImGuiInputTextFlags_CallbackHistory,
        [&](ImGuiInputTextCallbackData & data) {
            const std::string_view now(
                data.Buf, static_cast<std::size_t>(data.BufTextLen));
            switch(data.EventFlag) {
                case ImGuiInputTextFlags_CallbackAlways:
                    if(update_) {
                        if(now != update_->text) {
                            data.DeleteChars(0, data.BufTextLen);
                            data.InsertChars(
                                0, update_->text.data(),
                                update_->text.data() + update_->text.size());
                        }
                        data.CursorPos = data.SelectionStart =
                            data.SelectionEnd =
                                static_cast<int>(update_->caret);
                        update_.reset();
                    } else if(static_cast<std::size_t>(data.CursorPos) !=
                              draft.caret()) {
                        draft.set_caret(
                            static_cast<std::size_t>(data.CursorPos));
                    }
                    break;
                case ImGuiInputTextFlags_CallbackEdit:
                    update_.reset();
                    draft.set_text(std::string(now),
                                   static_cast<std::size_t>(data.CursorPos));
                    break;
                case ImGuiInputTextFlags_CallbackHistory:
                    draft.key(data.EventKey == ImGuiKey_UpArrow ? Key::Up
                                                                : Key::Down);
                    break;
                default:
                    break;
            }
        });
    active_ = ImGui::IsItemActive();
    tooltip_text(FormulaDraft::kKeysTip);
    // A single-line field gives the keyboard up on Enter.
    if(enter) {
        const Outcome o = draft.key(Key::Enter);
        if(o == Outcome::Accept || (o == Outcome::Commit && draft.is_open()))
            focus_ = true;
    }
}

void FormulaPopup::candidates(FormulaDraft & draft) {
    if(!draft.list_shown()) return;
    const std::vector<Completion> & all = draft.candidates();
    const float fs = ImGui::GetFontSize();
    const float rows = static_cast<float>(std::min(all.size(), kRows));
    const ImVec2 size(label_width() + field_width(),
                      rows * ImGui::GetTextLineHeightWithSpacing() +
                          2.0f * ImGui::GetStyle().CellPadding.y);
    if(!ImGui::BeginTable("##candidates", 3,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg,
                          size))
        return;
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("kind", ImGuiTableColumnFlags_WidthFixed,
                            fs * 6.0f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed,
                            fs * 8.0f);
    for(std::size_t i = 0; i < all.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool highlighted = i == draft.highlighted();
        if(ImGui::Selectable(all[i].name.c_str(), highlighted,
                             ImGuiSelectableFlags_SpanAllColumns)) {
            draft.accept(i);
            focus_ = true;
        }
        if(highlighted && !ImGui::IsItemVisible()) ImGui::SetScrollHereY();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", all[i].kind.c_str());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", all[i].value.c_str());
        ImGui::PopID();
        if(focus_) break;
    }
    ImGui::EndTable();
}

void FormulaPopup::field(FormulaDraft & draft, const DetailItem & item) {
    using Kind = DetailItem::Kind;
    ImGui::PushID(item.id.c_str());
    label(item.label.c_str(), item.tip);
    ImGui::SetNextItemWidth(field_width());
    if(item.kind == Kind::Field) {
        std::string text = item.text;
        const bool enter =
            input_text("##v", text, ImGuiInputTextFlags_EnterReturnsTrue);
        if(text != item.text) draft.set(item.id, text);
        if(enter) draft.commit();
    } else if(item.kind == Kind::Choice) {
        if(ImGui::BeginCombo("##v", item.text.c_str())) {
            for(std::size_t c = 0; c < item.choices.size(); ++c)
                if(ImGui::Selectable(
                       item.choices[c].c_str(),
                       DetailValue(static_cast<int>(c)) == item.value))
                    draft.set(item.id, static_cast<int>(c));
            ImGui::EndCombo();
        }
    } else if(item.kind == Kind::Checkbox) {
        bool on = std::get<bool>(item.value);
        if(ImGui::Checkbox("##v", &on)) draft.set(item.id, on);
    }
    ImGui::PopID();
}

}  // namespace gs::front::imgui

#include "ui/inspector.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <vector>

#include "ui/widgets.hpp"

namespace gs::ui {
namespace {

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_BordersInnerH |
                                        ImGuiTableFlags_SizingStretchProp;

const char * const kUnits[] = {"", "m", "cm", "mm", "deg", "rad", "%"};

// Keys of an object member of the document, copied: edits made while drawing
// replace the document and would invalidate iterators into it.
std::vector<std::string> keys_of(const Document & doc, const char * member) {
    std::vector<std::string> out;
    const Json * j = doc.instance()->get(member);
    if(j && j->is_object())
        for(const auto & [k, v] : j->items()) out.push_back(k);
    return out;
}

std::size_t size_of(const Document & doc, const char * member) {
    const Json * j = doc.instance()->get(member);
    return j && j->is_array() ? j->size() : 0;
}

std::string string_at(const Document & doc, const std::string & path,
                      std::string fallback = {}) {
    const Json * j = doc.instance()->get(path);
    return j && j->is_string() ? j->get<std::string>() : fallback;
}

bool bool_at(const Document & doc, const std::string & path, bool fallback) {
    const Json * j = doc.instance()->get(path);
    return j && j->is_boolean() ? j->get<bool>() : fallback;
}

bool unit_combo(const char * id, std::string & unit) {
    bool changed = false;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.0f);
    if(ImGui::BeginCombo(id, unit.empty() ? "-" : unit.c_str())) {
        for(const char * u : kUnits)
            if(ImGui::Selectable(*u ? u : "-", unit == u)) {
                unit = u;
                changed = true;
            }
        ImGui::EndCombo();
    }
    return changed;
}

struct Status {
    ImVec4 color = palette::dim;
    std::string text;
    std::string tip;
};

// Worst of a set of row groups on the verification grid.
Status group_status(const Model & m, const Scene & s,
                    const std::vector<int> & groups, double tol) {
    Status st;
    if(!s.verification || groups.empty()) {
        st.text = groups.empty() ? "disabled" : "-";
        return st;
    }
    double worst = -std::numeric_limits<double>::infinity();
    int wg = -1;
    for(const int g : groups) {
        const double v = s.verification->groups[uz(g)].violation;
        if(v > worst || std::isnan(v)) {
            worst = v;
            wg = g;
        }
    }
    const GroupCheck & c = s.verification->groups[uz(wg)];
    const RowGroup & rg = m.groups()[uz(wg)];
    const std::string where =
        rg.sweep >= 0 && std::isfinite(c.sweep_value)
            ? std::format(" at {}={}", m.sweeps()[uz(rg.sweep)].name,
                          format_number(c.sweep_value, 4))
            : "";
    if(std::isnan(worst) || worst > tol) {
        st.color = palette::bad;
        st.text = std::format("violated {}{}", format_number(worst, 3), where);
    } else if(worst > -1e-6) {
        st.color = palette::active;
        st.text = std::format("active {}{}", format_number(-worst, 2), where);
    } else {
        st.color = palette::ok;
        st.text = std::format("margin {}{}", format_number(-worst, 3), where);
    }
    st.tip = std::format(
        "Worst row over the {}-sample grid, in the row's natural unit ({}). "
        "Positive margin = satisfied.",
        s.verify_samples, rg.natural);
    return st;
}

void status_text(const Status & st) {
    text_colored(st.color, st.text);
    if(!st.tip.empty()) tooltip_text(st.tip);
}

// Model entry of document entry i: the same position when the names agree
// (compile keeps file order), else the only entry with that name. A stale
// model may differ from the document, and names may repeat while the
// document does not compile.
template <class Info>
int entry_index(const std::vector<Info> & list, std::size_t i,
                const std::string & name) {
    if(i < list.size() && list[i].name == name) return static_cast<int>(i);
    int found = -1;
    for(std::size_t k = 0; k < list.size(); ++k)
        if(list[k].name == name) {
            if(found >= 0) return -1;
            found = static_cast<int>(k);
        }
    return found;
}

// First "<prefix><k>" (k from `from`) not used as a name in `member`.
std::string unused_name(const Document & doc, const char * member,
                        const char * prefix, std::size_t from) {
    const Json * list = doc.instance()->get(member);
    for(std::size_t k = from;; ++k) {
        const std::string name = std::format("{}{}", prefix, k);
        bool used = false;
        if(list && list->is_array())
            for(const Json & e : *list)
                used |=
                    e.is_object() && e.contains("name") && e["name"] == name;
        if(!used) return name;
    }
}

// "criteria[2].weight": a key of a constraint or criterion entry, where
// the compiler warns about keys it does not use.
bool entry_key(const std::string & path) {
    if(!path.starts_with("constraints[") && !path.starts_with("criteria["))
        return false;
    const std::size_t close = path.find("].");
    return close != std::string::npos &&
           path.find_first_of(".[", close + 2) == std::string::npos;
}

// "weight" of "criteria[2].weight".
std::string last_key(const std::string & path) {
    const std::size_t dot = path.find_last_of('.');
    return dot == std::string::npos ? path : path.substr(dot + 1);
}

bool small_delete_button(const char * id) {
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.15f, 0.15f, 0.6f));
    const bool pressed = ImGui::SmallButton("x");
    ImGui::PopStyleColor();
    tooltip_text("Delete this entry");
    ImGui::PopID();
    return pressed;
}

}  // namespace

bool Inspector::field(Document & doc, const std::string & path, Kind kind,
                      float width, const char * hint) {
    drawn_.insert(path);
    const Json * j = doc.instance()->get(path);
    const std::string current = j ? json_text(*j) : std::string();
    Edit & e = edits_[path];
    if(!e.active && !e.pending) e.text = current;
    bool has_error = false;
    for(const Diagnostic * d : doc.diagnostics_at(path))
        if(d->severity == Diagnostic::Severity::Error) has_error = true;
    ImGui::PushID(path.c_str());
    if(has_error)
        ImGui::PushStyleColor(ImGuiCol_FrameBg,
                              ImVec4(0.45f, 0.12f, 0.12f, 0.9f));
    else if(e.pending)
        ImGui::PushStyleColor(ImGuiCol_FrameBg,
                              ImVec4(0.38f, 0.32f, 0.10f, 0.9f));
    ImGui::SetNextItemWidth(width);
    const bool enter =
        input_text("##f", e.text, ImGuiInputTextFlags_EnterReturnsTrue, hint);
    if(has_error || e.pending) ImGui::PopStyleColor();
    e.active = ImGui::IsItemActive();
    if(!e.active) e.pending = e.text != current;
    tooltip_text(
        std::format("{}\nEnter applies and recompiles, Esc cancels.", path));
    bool committed = false;
    if(enter) {
        e.pending = false;
        if(e.text != current) {
            Json value;
            if(kind == Kind::NumberOrExpression) {
                value = number_or_string(e.text);
            } else if(kind == Kind::RawJson) {
                try {
                    value = Json::parse(e.text);
                } catch(const nlohmann::json::exception &) {
                    value = Json(e.text);
                }
            } else {
                value = Json(e.text);
            }
            doc.edit(path, std::move(value));
            committed = true;
        }
    }
    ImGui::PopID();
    return committed;
}

void Inspector::field_diagnostics(Document & doc, const std::string & path) {
    const std::vector<const Diagnostic *> ds = doc.diagnostics_at(path);
    if(ds.empty()) return;
    ImGui::PushID(path.c_str());
    bool any_error = false;
    bool edited = false;
    std::unordered_set<std::string> keys;
    for(const Diagnostic * d : ds) {
        const Json * j = doc.instance()->get(d->path);
        const std::string text =
            j && j->is_string() ? j->get<std::string>() : std::string();
        diagnostic_line(*d, text);
        const bool error = d->severity == Diagnostic::Severity::Error;
        any_error |= error;
        if(!j || drawn_.contains(d->path) || !keys.insert(d->path).second)
            continue;
        // A key without an editor of its own. An error there gets a raw JSON
        // field, which cannot remove the key (empty text is stored as ""):
        // a key the compiler rejects or ignores ("weight", a misspelt key, a
        // "bound" its role does not use) needs the remove button instead.
        const bool removable = error || entry_key(d->path);
        const std::string remove =
            std::format("remove \"{}\"", last_key(d->path));
        const float button = ImGui::CalcTextSize(remove.c_str()).x +
                             2.0f * ImGui::GetStyle().FramePadding.x +
                             ImGui::GetStyle().ItemSpacing.x;
        if(error) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("JSON");
            ImGui::SameLine();
            edited = field(doc, d->path, Kind::RawJson, -button);
            if(removable) ImGui::SameLine();
        }
        if(removable && !edited) {
            if(ImGui::SmallButton(remove.c_str())) {
                edits_.erase(d->path);
                doc.erase(d->path);
                edited = true;
            }
            tooltip_text("Delete this key from the instance");
        }
        // The edit recompiled the document: `ds` points at diagnostics that
        // no longer exist.
        if(edited) break;
    }
    if(!edited && any_error && doc.model()) {
        if(ImGui::SmallButton("revert")) {
            doc.revert(path);
            edits_.erase(path);
        }
        tooltip_text("Restore the text of the last document that compiled");
    }
    ImGui::PopID();
}

void Inspector::draw(Document & doc, const Scene & scene) {
    if(!doc.has_instance()) {
        ImGui::TextDisabled("No instance loaded. Use File > Open.");
        return;
    }
    if(seen_instance_ != doc.instance()) {
        forget_edits();
        seen_instance_ = doc.instance();
    }
    drawn_.clear();
    header(doc, scene);
    // A new compile result opens every section holding an error once; the
    // user can collapse it again afterwards.
    const bool fresh = doc.compile_generation() != seen_compile_;
    seen_compile_ = doc.compile_generation();
    auto section = [&](const char * title, const char * root, bool open) {
        int errors = 0;
        for(const Diagnostic * d : doc.diagnostics_at(root))
            if(d->severity == Diagnostic::Severity::Error) ++errors;
        if((fresh && errors > 0) || expand_all_) ImGui::SetNextItemOpen(true);
        const std::string label =
            errors > 0 ? std::format("{}  ({} error{})###{}", title, errors,
                                     errors > 1 ? "s" : "", root)
                       : std::format("{}###{}", title, root);
        if(errors > 0)
            ImGui::PushStyleColor(ImGuiCol_Header,
                                  ImVec4(0.50f, 0.16f, 0.16f, 0.8f));
        const bool shown = ImGui::CollapsingHeader(
            label.c_str(), open ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if(errors > 0) ImGui::PopStyleColor();
        return shown;
    };
    if(section("Design variables", "design", true)) design(doc);
    if(section("Constraints", "constraints", true)) constraints(doc, scene);
    if(section("Criteria", "criteria", true)) criteria(doc, scene);
    if(section("Params", "params", false)) params(doc);
    if(section("Let bindings", "let", false)) lets(doc, scene);
    if(section("Display items", "display", false)) display(doc);
    if(section("Sweeps", "sweeps", false)) sweeps(doc);
    if(section("Constant geometry", "geometry", false)) geometry(doc);
    expand_all_ = false;
}

void Inspector::header(Document & doc, const Scene & scene) {
    const std::string file = doc.path().filename().string();
    ImGui::TextUnformatted(file.c_str());
    tooltip_text(doc.path().string());
    if(doc.dirty()) {
        ImGui::SameLine();
        text_colored(palette::warn, "(modified)");
    }
    int errors = 0, warnings = 0;
    for(const Diagnostic & d : doc.diagnostics())
        (d.severity == Diagnostic::Severity::Error ? errors : warnings)++;
    if(!doc.model())
        text_colored(palette::bad,
                     std::format("does not compile: {} error(s)", errors));
    else if(doc.stale())
        text_colored(
            palette::bad,
            std::format("{} error(s): the last good model stays active",
                        errors));
    else
        text_colored(
            palette::ok,
            std::format("compiled: {} coordinates, {} row groups",
                        doc.model()->n(), doc.model()->groups().size()));
    if(warnings > 0) {
        ImGui::SameLine();
        text_colored(palette::warn, std::format("{} warning(s)", warnings));
    }
    if(errors > 0) {
        ImGui::PushTextWrapPos(0.0f);
        int shown = 0;
        for(const Diagnostic & d : doc.diagnostics()) {
            if(d.severity != Diagnostic::Severity::Error) continue;
            if(shown++ == 3) {
                ImGui::TextDisabled("... (all of them under Messages)");
                break;
            }
            text_colored(palette::bad, d.to_string());
        }
        ImGui::PopTextWrapPos();
    }
    const Json * desc = doc.instance()->get("description");
    if(desc && desc->is_string()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", desc->get_ref<const std::string &>().c_str());
        ImGui::PopTextWrapPos();
    }
    if(doc.model() && scene.verification) {
        if(ImGui::SmallButton("Reset design values")) doc.reset_values();
        tooltip_text("Discard design moves made since the last save/compile");
    }
    // Diagnostics not shown next to a field (included files, schema).
    for(const Diagnostic & d : doc.diagnostics())
        if(d.path.empty() || d.path.find(':') != std::string::npos ||
           d.path.starts_with("include"))
            diagnostic_line(d, {});
    ImGui::Spacing();
}

void Inspector::params(Document & doc) {
    const std::vector<std::string> names = keys_of(doc, "params");
    const auto model = doc.model();
    if(ImGui::BeginTable("params", 3, kTableFlags)) {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("value or expression");
        ImGui::TableSetupColumn("SI", ImGuiTableColumnFlags_WidthFixed);
        for(const std::string & name : names) {
            const std::string path = "params." + name;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableNextColumn();
            field(doc, path, Kind::NumberOrExpression, -FLT_MIN);
            field_diagnostics(doc, path);
            ImGui::TableNextColumn();
            const int pi = model ? model->find_param(name) : -1;
            if(pi >= 0)
                ImGui::TextDisabled(
                    "%s", format_value(model->params()[uz(pi)].value).c_str());
        }
        ImGui::EndTable();
    }
}

void Inspector::design(Document & doc) {
    const std::vector<std::string> names = keys_of(doc, "design");
    if(names.empty()) {
        ImGui::TextDisabled("no design variables");
        return;
    }
    const float fs = ImGui::GetFontSize();
    if(!ImGui::BeginTable("design", 3, kTableFlags)) return;
    ImGui::TableSetupColumn("var", ImGuiTableColumnFlags_WidthFixed, fs * 3.5f);
    ImGui::TableSetupColumn("value");
    ImGui::TableSetupColumn("fixed", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    for(const std::string & name : names) {
        const std::string base = "design." + name;
        const std::shared_ptr<const Model> model = doc.model();
        const int vi = model ? model->find_var(name) : -1;
        const bool is_point = string_at(doc, base + ".type") == "point";
        const bool fixed = bool_at(doc, base + ".fixed", false);
        std::string unit = string_at(doc, base + ".unit");
        const std::string shown_unit = unit.empty() && is_point ? "m" : unit;
        ImGui::PushID(base.c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(name.c_str());
        const std::string note = string_at(doc, base + ".note");
        if(!note.empty()) tooltip_text(note);

        ImGui::TableNextColumn();
        if(vi >= 0) {
            const DesignVar & dv = model->design()[uz(vi)];
            std::vector<double> val = doc.var_value(vi);
            double shown[2] = {0.0, 0.0};
            for(std::size_t k = 0; k < val.size() && k < 2; ++k)
                shown[k] = to_display(val[k], shown_unit);
            const auto held = held_.find(base);
            if(dv.fixed && held != held_.end()) {
                shown[0] = held->second[0];
                shown[1] = held->second[1];
            }
            // A printf format: the "%" unit must be doubled, or the format
            // ends in a lone '%' (undefined behaviour in vsnprintf).
            std::string fmt = "%.5g ";
            for(const char ch : shown_unit) {
                fmt += ch;
                if(ch == '%') fmt += '%';
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            bool changed = false;
            bool commit = false;
            if(dv.type == VarType::Point) {
                const double speed = to_display(0.002, shown_unit) == 0.0
                                         ? 0.002
                                         : to_display(0.002, shown_unit);
                changed = ImGui::DragScalarN("##v", ImGuiDataType_Double, shown,
                                             2, static_cast<float>(speed),
                                             nullptr, nullptr, fmt.c_str());
            } else {
                double lo = to_display(dv.min, shown_unit);
                double hi = to_display(dv.max, shown_unit);
                changed = ImGui::SliderScalar("##v", ImGuiDataType_Double,
                                              shown, &lo, &hi, fmt.c_str());
            }
            commit = ImGui::IsItemDeactivatedAfterEdit();
            if(dv.fixed && ImGui::IsItemActive())
                held_[base] = {shown[0], shown[1]};
            else
                held_.erase(base);
            tooltip_text(dv.fixed
                             ? "Fixed: the value is a compile-time constant "
                               "(applied when the edit ends)"
                             : "Drag or Ctrl+click to type; the value is "
                               "projected onto the domain");
            const bool apply = dv.fixed ? commit : changed;
            if(apply) {
                std::vector<double> si(val.size());
                for(std::size_t k = 0; k < si.size(); ++k)
                    si[k] = from_display(shown[k], shown_unit);
                doc.set_var_value(vi, si);
            }
        } else {
            const Json * v = doc.instance()->get(base + ".value");
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", v ? v->dump().c_str() : "(no value)");
        }

        ImGui::TableNextColumn();
        bool f = fixed;
        drawn_.insert(base + ".fixed");
        if(ImGui::Checkbox("##fixed", &f)) doc.edit(base + ".fixed", f);
        tooltip_text("Fixed variables keep their value and take no coordinate");

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        {
            const std::string type = string_at(doc, base + ".type", "?");
            drawn_.insert(base + ".type");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if(ImGui::BeginCombo("##type", type.c_str(),
                                 ImGuiComboFlags_NoArrowButton)) {
                for(const char * t : {"scalar", "point"})
                    if(ImGui::Selectable(t, type == t) && type != t)
                        doc.edit(base + ".type", t);
                ImGui::EndCombo();
            }
            tooltip_text(
                "Variable type: a scalar needs min and max, a point a "
                "domain");
        }
        ImGui::TableNextColumn();
        if(is_point) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("domain");
            ImGui::SameLine();
            field(doc, base + ".domain", Kind::Expression, -FLT_MIN);
        } else {
            const float w = (ImGui::GetContentRegionAvail().x - fs * 9.0f) / 2;
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("min");
            ImGui::SameLine();
            field(doc, base + ".min", Kind::NumberOrExpression,
                  std::max(w, fs * 3));
            ImGui::SameLine();
            ImGui::TextDisabled("max");
            ImGui::SameLine();
            field(doc, base + ".max", Kind::NumberOrExpression,
                  std::max(w, fs * 3));
            ImGui::SameLine();
            drawn_.insert(base + ".unit");
            if(unit_combo("##unit", unit)) {
                if(unit.empty())
                    doc.erase(base + ".unit");
                else
                    doc.edit(base + ".unit", unit);
            }
        }
        field_diagnostics(doc, base);
        ImGui::TableNextColumn();
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void Inspector::sweeps(Document & doc) {
    for(const std::string & name : keys_of(doc, "sweeps")) {
        const std::string base = "sweeps." + name;
        ImGui::PushID(base.c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(name.c_str());
        const float w = ImGui::GetFontSize() * 6.0f;
        ImGui::SameLine();
        ImGui::TextDisabled("min");
        ImGui::SameLine();
        field(doc, base + ".min", Kind::NumberOrExpression, w);
        ImGui::SameLine();
        ImGui::TextDisabled("max");
        ImGui::SameLine();
        field(doc, base + ".max", Kind::NumberOrExpression, w);
        field_diagnostics(doc, base);
        ImGui::PopID();
    }
}

void Inspector::lets(Document & doc, const Scene & scene) {
    const std::vector<std::string> names = keys_of(doc, "let");
    const Model * sm = scene.model.get();
    if(ImGui::BeginTable("lets", 4, kTableFlags)) {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("expression",
                                ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("value now", ImGuiTableColumnFlags_WidthStretch,
                                1.4f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        for(const std::string & name : names) {
            const std::string path = "let." + name;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableNextColumn();
            field(doc, path, Kind::Expression, -FLT_MIN);
            field_diagnostics(doc, path);
            ImGui::TableNextColumn();
            const int li = sm ? sm->find_let(name) : -1;
            if(li >= 0 && uz(li) < scene.lets.size()) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s",
                                    format_value(scene.lets[uz(li)]).c_str());
                tooltip_text(type_name(sm->lets()[uz(li)].type,
                                       sm->lets()[uz(li)].kind));
            }
            ImGui::TableNextColumn();
            if(small_delete_button(path.c_str())) {
                doc.erase(path);
                forget_edits();
                break;
            }
        }
        ImGui::EndTable();
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
    input_text("##newlet", new_name_, 0, "new name");
    ImGui::SameLine();
    if(ImGui::Button("Add let") && !new_name_.empty() &&
       !doc.instance()->get("let." + new_name_)) {
        doc.edit("let." + new_name_, "0");
        new_name_.clear();
    }
}

void Inspector::constraints(Document & doc, const Scene & scene) {
    const std::size_t count = size_of(doc, "constraints");
    const Model * sm = scene.model.get();
    const double tol = doc.feas_tol();
    for(std::size_t i = 0; i < count; ++i) {
        const std::string base = std::format("constraints[{}]", i);
        const std::string name = string_at(doc, base + ".name", "?");
        ImGui::PushID(base.c_str());
        bool enabled = bool_at(doc, base + ".enabled", true);
        drawn_.insert(base + ".enabled");
        if(ImGui::Checkbox("##en", &enabled))
            doc.edit(base + ".enabled", enabled);
        tooltip_text("Enabled");
        ImGui::SameLine();
        field(doc, base + ".name", Kind::Text, ImGui::GetFontSize() * 7.0f);
        const std::string forall = string_at(doc, base + ".forall");
        drawn_.insert(base + ".forall");
        ImGui::SameLine();
        ImGui::TextDisabled("for all");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.0f);
        if(ImGui::BeginCombo("##forall", forall.empty() ? "-" : forall.c_str(),
                             ImGuiComboFlags_NoArrowButton)) {
            if(ImGui::Selectable("-", forall.empty()) && !forall.empty())
                doc.erase(base + ".forall");
            for(const std::string & sw : keys_of(doc, "sweeps"))
                if(ImGui::Selectable(sw.c_str(), forall == sw) && forall != sw)
                    doc.edit(base + ".forall", sw);
            ImGui::EndCombo();
        }
        tooltip_text("Sweep the constraint must hold for (\"forall\")");
        const int ci = sm ? entry_index(sm->constraints(), i, name) : -1;
        if(ci >= 0 && scene.verification) {
            const ConstraintInfo & info = sm->constraints()[uz(ci)];
            ImGui::SameLine();
            status_text(group_status(*sm, scene, info.groups, tol));
            bool now = false;
            for(const int g : info.groups)
                if(uz(g) < scene.violation_now.size() &&
                   scene.violation_now[uz(g)] > tol)
                    now = true;
            if(now) {
                ImGui::SameLine();
                text_colored(palette::bad, "[here]");
                tooltip_text("Violated at the current sweep position");
            }
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x +
                        ImGui::GetCursorPosX() - ImGui::GetFontSize() * 1.2f);
        if(small_delete_button("del")) {
            ImGui::PopID();
            doc.erase(base);
            forget_edits();
            break;
        }
        ImGui::Indent();
        field(doc, base + ".expr", Kind::Expression, -FLT_MIN);
        field_diagnostics(doc, base);
        const std::string note = string_at(doc, base + ".note");
        if(!note.empty()) ImGui::TextDisabled("%s", note.c_str());
        ImGui::Unindent();
        ImGui::Separator();
        ImGui::PopID();
    }
    if(ImGui::Button("Add constraint")) {
        Json c = Json::object();
        c["name"] = unused_name(doc, "constraints", "c", count + 1);
        c["expr"] = "0 <= 1";
        doc.edit(std::format("constraints[{}]", count), std::move(c));
    }
}

void Inspector::criteria(Document & doc, const Scene & scene) {
    const std::size_t count = size_of(doc, "criteria");
    const Model * sm = scene.model.get();
    const float fs = ImGui::GetFontSize();
    for(std::size_t i = 0; i < count; ++i) {
        const std::string base = std::format("criteria[{}]", i);
        const std::string name = string_at(doc, base + ".name", "?");
        const std::string role = string_at(doc, base + ".role", "report");
        std::string unit = string_at(doc, base + ".unit");
        ImGui::PushID(base.c_str());
        field(doc, base + ".name", Kind::Text, fs * 7.0f);
        ImGui::SameLine();
        const int ci = sm ? entry_index(sm->criteria(), i, name) : -1;
        const CriterionCheck * cc = ci >= 0 && scene.verification
                                        ? &scene.verification->criteria[uz(ci)]
                                        : nullptr;
        if(cc) {
            const CriterionInfo & info = sm->criteria()[uz(ci)];
            ImVec4 col = ImVec4(0.92f, 0.93f, 0.95f, 1.0f);
            if(std::isfinite(cc->violation))
                col = cc->violation > doc.feas_tol() ? palette::bad
                      : cc->violation > -1e-9        ? palette::active
                                                     : palette::ok;
            std::string text = format_display(cc->value, info.unit);
            if(info.aggregate != Aggregate::None && info.sweep >= 0 &&
               std::isfinite(cc->sweep_value))
                text +=
                    std::format("  at {}={}", sm->sweeps()[uz(info.sweep)].name,
                                format_number(cc->sweep_value, 4));
            text_colored(col, text);
            if(std::isfinite(cc->violation))
                tooltip_text(std::format("bound {} ({} the value)",
                                         format_display(info.bound, info.unit),
                                         info.role == CriterionRole::Max
                                             ? "upper bound of"
                                             : "lower bound of"));
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x +
                        ImGui::GetCursorPosX() - fs * 1.2f);
        if(small_delete_button("del")) {
            ImGui::PopID();
            doc.erase(base);
            forget_edits();
            break;
        }
        ImGui::Indent();
        ImGui::SetNextItemWidth(fs * 6.0f);
        drawn_.insert(base + ".role");
        if(ImGui::BeginCombo("##role", role.c_str())) {
            for(const char * r :
                {"minimize", "maximize", "max", "min", "report"}) {
                if(!ImGui::Selectable(r, role == r) || role == r) continue;
                const std::string bpath = base + ".bound";
                const Json * jb = doc.instance()->get(bpath);
                const std::optional<Json> bound =
                    jb ? std::optional<Json>(*jb) : std::nullopt;
                if(std::string_view(r) == "max" ||
                   std::string_view(r) == "min") {
                    // A max/min role needs a bound: the one parked when the
                    // criterion lost it, else the current value, so the
                    // change compiles and is satisfied.
                    const auto parked = parked_bounds_.find(bpath);
                    if(!bound && parked != parked_bounds_.end())
                        doc.edit(bpath, parked->second);
                    else if(!bound && cc)
                        doc.edit(bpath, cc->value);
                    doc.edit(base + ".role", r);
                } else {
                    // The other roles ignore a bound, which would then warn;
                    // it is parked so that switching back restores it.
                    doc.edit(base + ".role", r);
                    if(bound) {
                        parked_bounds_[bpath] = *bound;
                        doc.erase(bpath);
                    }
                }
            }
            ImGui::EndCombo();
        }
        tooltip_text(
            "minimize / maximize: the objective (one criterion at most); "
            "max: value <= bound; min: value >= bound; report: shown only");
        ImGui::SameLine();
        if(role == "max" || role == "min") {
            ImGui::TextDisabled("bound");
            ImGui::SameLine();
            field(doc, base + ".bound", Kind::NumberOrExpression, fs * 6.0f);
            ImGui::SameLine();
        }
        ImGui::TextDisabled("unit");
        ImGui::SameLine();
        drawn_.insert(base + ".unit");
        if(unit_combo("##unit", unit)) {
            if(unit.empty())
                doc.erase(base + ".unit");
            else
                doc.edit(base + ".unit", unit);
        }
        field(doc, base + ".expr", Kind::Expression, -FLT_MIN);
        field_diagnostics(doc, base);
        ImGui::Unindent();
        ImGui::Separator();
        ImGui::PopID();
    }
    if(ImGui::Button("Add criterion")) {
        Json c = Json::object();
        c["name"] = unused_name(doc, "criteria", "k", count + 1);
        c["expr"] = "0";
        c["role"] = "report";
        doc.edit(std::format("criteria[{}]", count), std::move(c));
    }
}

void Inspector::display(Document & doc) {
    const std::size_t count = size_of(doc, "display");
    const float fs = ImGui::GetFontSize();
    for(std::size_t i = 0; i < count; ++i) {
        const std::string base = std::format("display[{}]", i);
        ImGui::PushID(base.c_str());
        std::uint32_t rgba32 = 0x3b6fb6ffu;
        const std::string hex = string_at(doc, base + ".color");
        if(hex.size() == 7 || hex.size() == 9) {
            try {
                rgba32 = static_cast<std::uint32_t>(
                    std::stoul(hex.substr(1), nullptr, 16));
                if(hex.size() == 7) rgba32 = rgba32 << 8 | 0xffu;
            } catch(const std::exception &) {
            }
        }
        ImVec4 col = engine_color_vec(rgba32);
        for(const char * key :
            {".color", ".fill", ".trace", ".width", ".ghosts"})
            drawn_.insert(base + key);
        ImGui::ColorEdit4(
            "##col", &col.x,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        if(ImGui::IsItemDeactivatedAfterEdit())
            doc.edit(base + ".color", color_hex(col));
        ImGui::SameLine();
        field(doc, base + ".expr", Kind::Expression,
              ImGui::GetContentRegionAvail().x - fs * 1.6f);
        ImGui::SameLine();
        if(small_delete_button("del")) {
            ImGui::PopID();
            doc.erase(base);
            forget_edits();
            break;
        }
        ImGui::Indent();
        ImGui::TextDisabled("label");
        ImGui::SameLine();
        field(doc, base + ".label", Kind::Text, fs * 5.0f);
        ImGui::SameLine();
        bool fill = bool_at(doc, base + ".fill", false);
        if(ImGui::Checkbox("fill", &fill)) doc.edit(base + ".fill", fill);
        ImGui::SameLine();
        bool trace = bool_at(doc, base + ".trace", false);
        if(ImGui::Checkbox("trace", &trace)) doc.edit(base + ".trace", trace);
        ImGui::SameLine();
        const Json * jw = doc.instance()->get(base + ".width");
        double width = jw && jw->is_number() ? jw->get<double>() : 1.0;
        ImGui::SetNextItemWidth(fs * 3.0f);
        const double wmin = 0.5, wmax = 10.0;
        ImGui::DragScalar("width", ImGuiDataType_Double, &width, 0.1f, &wmin,
                          &wmax, "%.1f");
        if(ImGui::IsItemDeactivatedAfterEdit())
            doc.edit(base + ".width", width);
        ImGui::SameLine();
        const Json * jg = doc.instance()->get(base + ".ghosts");
        int ghosts = jg && jg->is_number_integer() ? jg->get<int>() : 0;
        ImGui::SetNextItemWidth(fs * 3.0f);
        ImGui::DragInt("ghosts", &ghosts, 0.2f, 0, 64);
        if(ImGui::IsItemDeactivatedAfterEdit())
            doc.edit(base + ".ghosts", ghosts);
        field_diagnostics(doc, base);
        ImGui::Unindent();
        ImGui::PopID();
    }
    if(ImGui::Button("Add display item")) {
        Json d = Json::object();
        d["expr"] = "vec(0, 0)";
        d["color"] = "#ffffff";
        doc.edit(std::format("display[{}]", count), std::move(d));
    }
}

void Inspector::geometry(Document & doc) {
    const Model * m = doc.model() ? doc.model().get() : doc.background().get();
    if(!m) {
        ImGui::TextDisabled("unavailable until the instance compiles");
        return;
    }
    if(ImGui::BeginTable("geometry", 3, kTableFlags)) {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("value");
        ImGui::TableSetupColumn("file", ImGuiTableColumnFlags_WidthFixed);
        for(const GeometryInfo & g : m->geometry()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(g.name.c_str());
            if(!g.note.empty()) tooltip_text(g.note);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", format_value(g.value).c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s",
                                g.file.empty() ? "(instance)" : g.file.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Included files are read-only here.");
}

}  // namespace gs::ui

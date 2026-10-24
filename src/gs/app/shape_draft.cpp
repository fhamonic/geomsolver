#include "gs/app/shape_draft.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <format>
#include <utility>

#include "gs/app/entry_edits.hpp"
#include "gs/engine/model.hpp"

namespace gs::app {
namespace {

// "d3", "d3 and a1", "d3, a1 and h2".
std::string listed(const std::vector<std::string> & names) {
    std::string out;
    for(std::size_t i = 0; i < names.size(); ++i) {
        if(i > 0) out += i + 1 == names.size() ? " and " : ", ";
        out += names[i];
    }
    return out;
}

// The paths of the errors `doc` gets once its format is /2, which only /2
// may hold a sketch in: the keys /1 warns about and ignores
// ("constraints[0].release"). /2 refuses them, and the compile then leaves
// the item that holds one out of the model.
std::vector<std::string> strict_errors(const Document & doc) {
    Instance copy = *doc.instance();
    if(!copy.set("format", "geomsolver-instance/2")) return {};
    CompileOptions options;
    options.isolate_items = true;
    CompileResult r;
    try {
        r = compile(copy, options);
    } catch(const std::exception &) {
        return {};
    }
    auto error = [](const Diagnostic & d) {
        return d.severity == Diagnostic::Severity::Error;
    };
    std::vector<std::string> out;
    for(const Diagnostic & d : r.diagnostics)
        if(error(d) && !std::ranges::contains(out, d.path) &&
           std::ranges::none_of(doc.diagnostics(), [&](const Diagnostic & e) {
               return error(e) && e.path == d.path;
           }))
            out.push_back(d.path);
    return out;
}

}  // namespace

ShapeDraft::ShapeDraft(const ToolContext & ctx) : doc_(ctx.document()) {}

std::string ShapeDraft::element_path(std::string_view name) const {
    return std::format("sketch.elements.{}", name);
}

std::string ShapeDraft::next_point_name(
    std::span<const std::string> taken) const {
    return next_element_name("P", taken);
}

std::string ShapeDraft::next_element_name(
    std::string_view prefix, std::span<const std::string> taken) const {
    if(!doc_.has_instance()) return std::format("{}1", prefix);
    std::vector<std::string> all(taken_.begin(), taken_.end());
    all.insert(all.end(), taken.begin(), taken.end());
    return unused_point_name(*doc_.instance(), "", all, prefix);
}

std::string ShapeDraft::point(const SnapResult & snap, Vec2d at, bool suggest,
                              bool helper) {
    if(!snap.ref.empty()) return snap.ref;
    const std::string name = next_point_name();
    taken_.push_back(name);
    Json value{{"type", "point"}, {"value", {at.x, at.y}}};
    if(helper) value["helper"] = true;
    batch_.set(element_path(name), std::move(value));
    if(suggest)
        for(SnapConstraint & c : snap_constraints(snap, name))
            this->suggest("on", std::move(c.formula));
    return name;
}

std::string ShapeDraft::element(std::string_view prefix, Json value) {
    const std::string name = next_element_name(prefix);
    taken_.push_back(name);
    batch_.set(element_path(name), std::move(value));
    created_.emplace_back(SelKey::Kind::Element, name);
    return name;
}

std::string unused_constraint_name(const Instance & instance,
                                   std::string_view prefix,
                                   std::span<const std::string> taken) {
    const Json * list = instance.get("constraints");
    for(std::size_t k = 1;; ++k) {
        const std::string name = std::format("{}{}", prefix, k);
        const bool listed_already =
            list && list->is_array() &&
            std::ranges::any_of(*list, [&](const Json & c) {
                return c.is_object() && c.contains("name") && c["name"] == name;
            });
        if(!listed_already && !name_used(instance, name) &&
           !std::ranges::contains(taken, name))
            return name;
    }
}

std::string ShapeDraft::constraint_name(std::string_view prefix) const {
    return unused_constraint_name(*doc_.instance(), prefix, taken_);
}

std::string ShapeDraft::constraint(std::string_view prefix, std::string formula,
                                   Json keys) {
    std::string name = constraint_name(prefix);
    taken_.push_back(name);
    constraints_.push_back({name, std::move(formula), std::move(keys)});
    return name;
}

void ShapeDraft::merge_into(SelKey point, std::string derived) {
    merge_.emplace(std::move(point), std::move(derived));
}

std::string ShapeDraft::dimension(std::string_view prefix, std::string formula,
                                  Json keys, std::string tried) {
    std::string name = constraint_name(prefix);
    taken_.push_back(name);
    dimensions_.push_back(
        {name, std::move(formula), std::move(keys), std::move(tried)});
    return name;
}

bool ShapeDraft::set_formula(SketchEdit & edit, std::string_view name,
                             std::string formula) {
    for(EditBatch::Op & op : edit.batch.ops)
        if(op.kind == EditBatch::Op::Kind::Set &&
           op.path.starts_with("constraints[") && op.value.is_object() &&
           op.value.value("name", "") == name) {
            op.value["expr"] = std::move(formula);
            return true;
        }
    return false;
}

void ShapeDraft::suggest(std::string_view prefix, std::string formula) {
    std::string name = constraint_name(prefix);
    taken_.push_back(name);
    suggestions_.push_back({std::move(name), std::move(formula)});
}

SketchEdit ShapeDraft::edit(ToolContext & ctx, std::string_view label,
                            Drafted * drafted) const {
    SketchEdit out;
    const Instance & inst = *doc_.instance();
    EditBatch batch(created_.empty()
                        ? std::string(label)
                        : std::format("{} {}", label, created_.front().name));
    const Json * format = inst.get("format");
    if(format && *format == "geomsolver-instance/1") {
        if(const std::vector<std::string> keys = strict_errors(doc_);
           !keys.empty()) {
            out.refusal = std::format(
                "this file holds keys a /2 file may not: {}; remove them, "
                "then draw",
                listed(keys));
            return out;
        }
        batch.set("format", "geomsolver-instance/2");
    }
    for(const EditBatch::Op & op : batch_.ops) batch.ops.push_back(op);
    const Json * list = inst.get("constraints");
    const std::size_t first = list && list->is_array() ? list->size() : 0;
    auto entry = [](const NewConstraint & c) {
        Json out{{"name", c.name}, {"expr", c.formula}};
        for(const auto & [key, value] : c.keys.items()) out[key] = value;
        return out;
    };

    Tried tried{.dependent = std::vector<bool>(dimensions_.size()),
                .drawn = std::vector<bool>(dimensions_.size()),
                .unranked = std::vector<bool>(dimensions_.size()),
                .fixed = std::vector<bool>(dimensions_.size()),
                .stuck = std::vector<bool>(dimensions_.size()),
                .kept = std::vector<bool>(suggestions_.size()),
                .untested = std::vector<bool>(suggestions_.size())};
    std::size_t at = first;
    for(const NewConstraint & c : constraints_)
        batch.set(std::format("constraints[{}]", at++), entry(c));
    const std::size_t tested = at;
    if(!dimensions_.empty() || !suggestions_.empty()) {
        EditBatch trial = batch;
        std::vector<std::string> dims, suggested;
        for(const NewConstraint & c : dimensions_) {
            dims.push_back(std::format("constraints[{}]", at++));
            Json tried = entry(c);
            if(!c.tried.empty()) tried["expr"] = c.tried;
            trial.set(dims.back(), std::move(tried));
        }
        for(const NewConstraint & c : suggestions_) {
            suggested.push_back(std::format("constraints[{}]", at++));
            trial.set(suggested.back(), entry(c));
        }
        tried = ctx.try_constrain(trial, dims, suggested);
    }

    at = tested;
    std::vector<std::string> references, untested, drawn, unranked, fixed;
    const bool reference =
        ctx.options().dependent == DependentDimension::Reference;
    for(std::size_t i = 0; i < dimensions_.size(); ++i) {
        Json c = entry(dimensions_[i]);
        // Whatever the answer to Q3: driving, it would be Malformed at the
        // first value typed into it.
        if(tried.fixed[i]) {
            c["driving"] = false;
            fixed.push_back(dimensions_[i].name);
        } else if(tried.dependent[i] && reference) {
            c["driving"] = false;
            references.push_back(dimensions_[i].name);
        }
        if(tried.drawn[i]) drawn.push_back(dimensions_[i].name);
        if(tried.unranked[i]) unranked.push_back(dimensions_[i].name);
        batch.set(std::format("constraints[{}]", at++), std::move(c));
    }
    for(std::size_t j = 0; j < suggestions_.size(); ++j) {
        if(tried.kept[j])
            batch.set(std::format("constraints[{}]", at++),
                      entry(suggestions_[j]));
        else if(tried.untested[j])
            untested.push_back(suggestions_[j].name);
    }

    std::vector<std::string> report;
    if(!fixed.empty())
        report.push_back(
            std::format("{} {} nothing that can move: added as {}",
                        listed(fixed), fixed.size() == 1 ? "reads" : "read",
                        fixed.size() == 1 ? "a reference dimension"
                                          : "reference dimensions"));
    if(!references.empty())
        report.push_back(
            std::format("{} would over-constrain the sketch: added as {}",
                        listed(references),
                        references.size() == 1 ? "a reference dimension"
                                               : "reference dimensions"));
    if(!untested.empty())
        report.push_back(std::format(
            "{} not added: too many parameters around {} to test {}",
            listed(untested), untested.size() == 1 ? "it" : "them",
            untested.size() == 1 ? "it" : "them"));
    for(const std::string & r : report)
        out.report += (out.report.empty() ? "" : "; ") + r;
    out.batch = std::move(batch);
    out.created = created_;
    references.insert(references.begin(), fixed.begin(), fixed.end());
    if(drafted)
        *drafted = {std::move(references), std::move(drawn),
                    std::move(unranked)};
    if(merge_) {
        // The merge reads the document the shape is added to.
        Instance after = inst;
        SketchEdit merged;
        if(!apply_ops(after, out.batch))
            merged.refusal = "the shape does not apply";
        else
            merged = gs::app::merge_into(after, merge_->first, merge_->second);
        if(!merged.refusal.empty()) {
            if(drafted) drafted->merge_refused = true;
            SketchEdit no;
            no.refusal = std::move(merged.refusal);
            return no;
        }
        for(EditBatch::Op & op : merged.batch.ops)
            out.batch.ops.push_back(std::move(op));
        out.merged = std::move(merged.merged);
        out.removed = std::move(merged.removed);
        if(!merged.report.empty())
            out.report += (out.report.empty() ? "" : "; ") + merged.report;
    }
    return out;
}

}  // namespace gs::app

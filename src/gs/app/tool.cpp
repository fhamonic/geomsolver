#include "gs/app/tool.hpp"

#include <cmath>
#include <exception>
#include <format>
#include <utility>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/picker.hpp"
#include "gs/engine/evaluator.hpp"
#include "gs/engine/model.hpp"

namespace gs::app {

std::vector<SnapConstraint> snap_constraints(const SnapResult & snap,
                                             std::string_view point) {
    switch(snap.kind) {
        case SnapResult::Kind::Edge:
            return {{std::format("{}({}, {})", snap.round ? "on" : "on_line",
                                 point, snap.on),
                     "On"}};
        case SnapResult::Kind::Axis:
            return {
                snap.on == "x"
                    ? SnapConstraint{std::format("{}.y == 0", point), "y=0"}
                    : SnapConstraint{std::format("{}.x == 0", point), "x=0"}};
        case SnapResult::Kind::Origin:
            return {{std::format("{}.x == 0", point), "x=0"},
                    {std::format("{}.y == 0", point), "y=0"}};
        default:
            return {};
    }
}

PickFilter pick_filter_of(std::initializer_list<ToolPick::Kind> kinds) {
    PickFilter f;
    for(const ToolPick::Kind k : kinds)
        f.kinds = static_cast<std::uint8_t>(f.kinds |
                                            (1u << static_cast<unsigned>(k)));
    return f;
}

Status field_status(const ValueField & f) {
    return {.tone = f.refusal  ? Tone::Bad
                    : f.set    ? Tone::Ok
                    : f.placed ? Tone::Dim
                               : Tone::Neutral,
            .text = f.text,
            .tip = f.refusal ? std::format("{}: {}", f.label, *f.refusal)
                             : f.label};
}

const Document & ToolContext::document() const { return app_.document(); }

const SketchSession & ToolContext::sketch() const { return app_.sketch(); }

const Selection & ToolContext::selection() const { return app_.selection(); }

const ToolOptions & ToolContext::options() const {
    return app_.tools().options();
}

std::string_view ToolContext::frame() const { return {}; }

void ToolContext::select(const SelKey & key) {
    app_.selection().set(key);
    app_.selection().reveal(key);
}

std::optional<ToolPick> ToolContext::pick_of(const SelKey & key) {
    const Scene & scene = app_.scene(false);
    return Picker(scene, app_.canvas().shown(scene)).of(key);
}

std::vector<std::optional<ToolPick>> ToolContext::picks_of(
    std::span<const SelKey> keys) {
    const Scene & scene = app_.scene(false);
    const Picker picker(scene, app_.canvas().shown(scene));
    std::vector<std::optional<ToolPick>> out;
    for(const SelKey & k : keys) out.push_back(picker.of(k));
    return out;
}

bool ToolContext::selection_from_tool() const {
    return app_.selection_from_tool();
}

void ToolContext::open_field(std::string_view id, std::string text,
                             bool select) {
    app_.tools().open(std::string(id), std::move(text), select);
}

bool ToolContext::commit(SketchEdit edit) {
    return app_.apply_sketch_edit(std::move(edit), true);
}

ToolContext::Value ToolContext::evaluate(std::string_view text,
                                         ValueField::Unit unit) const {
    Value out;
    // number_literal() takes "30 " as 30, whose "deg" would then follow a
    // blank: "30 deg" does not parse.
    const auto first = text.find_first_not_of(" \t");
    text = first == std::string_view::npos
               ? std::string_view()
               : text.substr(first, text.find_last_not_of(" \t") - first + 1);
    out.written = std::string(text);
    if(number_literal(text) && unit == ValueField::Unit::Angle)
        out.written += "deg";
    const Document & doc = app_.document();
    if(!doc.has_instance()) {
        out.refusal = "no document";
        return out;
    }
    const CompileResult r = gs::compile(
        *doc.instance(), {.probes = {out.written}, .isolate_items = true});
    if(!r.model) {
        out.refusal = "not a value";
        for(const Diagnostic & d : r.diagnostics)
            if(d.path.starts_with("probes")) {
                out.refusal = d.message;
                break;
            }
        return out;
    }
    const Model & m = *r.model;
    const ExprInfo & probe = m.probes().front();
    if(probe.type != ValueType::Scalar) {
        out.refusal = "a number is needed";
        return out;
    }
    if(!probe.constant) {
        out.refusal =
            "a sketch value may not depend on a design variable or a sweep";
        return out;
    }
    try {
        Evaluator ev(m);
        const ExprRef ref[] = {probe.ref};
        const std::vector<GeoValue> v = ev.values(
            m.initial_x(), std::vector<double>(m.sweeps().size()), ref);
        const double value = v.front().scalar();
        if(!std::isfinite(value)) {
            out.refusal = "not a finite value";
            return out;
        }
        out.value = value;
    } catch(const std::exception & e) {
        out.refusal = e.what();
    }
    return out;
}

Tried ToolContext::try_constrain(
    const EditBatch & batch, std::span<const std::string> dimensions,
    std::span<const std::string> suggestions) const {
    return gs::app::try_constrain(app_.document(), batch, dimensions,
                                  suggestions);
}

}  // namespace gs::app

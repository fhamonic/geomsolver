#include "gs/app/formula_draft.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <exception>
#include <format>
#include <tuple>

#include "gs/app/app.hpp"
#include "gs/app/criteria.hpp"
#include "gs/app/elements.hpp"
#include "gs/app/entry_edits.hpp"
#include "gs/app/format.hpp"
#include "gs/engine/builtins.hpp"
#include "gs/engine/evaluator.hpp"
#include "gs/engine/expression.hpp"

namespace gs::app {
namespace {

using Kind = SelKey::Kind;
using DK = DetailItem::Kind;

constexpr std::size_t kMaxCandidates = 12;

constexpr const char * kNoModel =
    "The document does not compile: only the syntax of the formula is "
    "checked";

bool ident(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::string lower(std::string_view s) {
    std::string out(s);
    for(char & c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view trimmed(std::string_view s) {
    const std::size_t b = s.find_first_not_of(" \t\n");
    if(b == std::string_view::npos) return {};
    return s.substr(b, s.find_last_not_of(" \t\n") - b + 1);
}

std::string text_at(const Json & entry, const char * key) {
    const auto it = entry.find(key);
    return it != entry.end() ? json_text(*it) : std::string();
}

// A criterion's bound as the fields Bound, From and To show it.
std::array<std::string, 3> bound_texts(const Json & entry) {
    const auto b = entry.find("bound");
    if(b == entry.end()) return {};
    if(b->is_array() && b->size() == 2)
        return {std::string(), json_text((*b)[0]), json_text((*b)[1])};
    return {json_text(*b), std::string(), std::string()};
}

// What a selected entry is, for a pre-fill.
enum class Shape : std::uint8_t { Point, Shape, Scalar };
struct Named {
    std::string name;  // or the expression of an anonymous display item
    Shape shape = Shape::Scalar;
    int sweep = -1;  // in the model's sweeps
};

Shape shape_of(ValueType type) {
    return type == ValueType::Vec     ? Shape::Point
           : type == ValueType::Shape ? Shape::Shape
                                      : Shape::Scalar;
}

std::optional<Named> named(const Document & doc, const SelKey & key) {
    const Model * m = doc.model() ? doc.model().get() : doc.background().get();
    if(key.kind == Kind::Param || key.kind == Kind::Design)
        if(entry_paths(*doc.instance(), key).empty()) return std::nullopt;
    switch(key.kind) {
        case Kind::Param:
            return Named{key.name, Shape::Scalar};
        case Kind::Design:
            return Named{key.name,
                         entry_kind(doc, key) == EntryKind::DesignPoint
                             ? Shape::Point
                             : Shape::Scalar};
        case Kind::Geometry: {
            const std::vector<GeometryEntry> all =
                geometry_entries(*doc.instance());
            const auto g =
                std::ranges::find(all, key.name, &GeometryEntry::name);
            if(g == all.end()) return std::nullopt;
            return Named{key.name, geometry_type(*g->json) == "point"
                                       ? Shape::Point
                                       : Shape::Shape};
        }
        case Kind::Let: {
            const int i = m ? m->find_let(key.name) : -1;
            if(i < 0) return std::nullopt;
            const LetInfo & let = m->lets()[static_cast<std::size_t>(i)];
            return Named{key.name, shape_of(let.type), let.sweep};
        }
        case Kind::Display: {
            if(!doc.model() || doc.stale()) return std::nullopt;
            for(const DisplayInfo & d : doc.model()->display())
                if(display_index(d) == key.index)
                    return Named{d.text, shape_of(d.type), d.sweep};
            return std::nullopt;
        }
        case Kind::Sweep:
        case Kind::Constraint:
        case Kind::Criterion:
        case Kind::Element:
        case Kind::Body:
            break;
    }
    return std::nullopt;
}

// The name of `n` before a ".x": a display item's expression in brackets.
std::string member_of(const Named & n) {
    return is_identifier(n.name) ? n.name : std::format("({})", n.name);
}

// The innermost call open at `at` in `text` is at(), max_over() or
// min_over(), and `at` is in its first argument, which names a sweep.
bool in_sweep_argument(std::string_view text, std::size_t at) {
    int depth = 0;
    for(std::size_t i = at; i-- > 0;) {
        const char c = text[i];
        if(c == ')') ++depth;
        if(c == '(' && depth-- == 0) {
            std::size_t e = i;
            while(e > 0 && text[e - 1] == ' ') --e;
            std::size_t b = e;
            while(b > 0 && ident(text[b - 1])) --b;
            const Builtin * f = find_builtin(text.substr(b, e - b));
            return f && f->kind == Builtin::Kind::SpecialForm;
        }
        if(c == ',' && depth == 0) return false;
    }
    return false;
}

std::string arity(const Builtin & b) {
    if(b.kind == Builtin::Kind::Constant) return format_number(b.value, 6);
    if(b.max_args < 0) return std::format("{}+ args", b.min_args);
    if(b.min_args == b.max_args)
        return std::format("{} arg{}", b.min_args, b.min_args == 1 ? "" : "s");
    return std::format("{}-{} args", b.min_args, b.max_args);
}

std::string builtin_kind(Builtin::Kind k) {
    switch(k) {
        case Builtin::Kind::Function:
            return "function";
        case Builtin::Kind::SpecialForm:
            return "special form";
        case Builtin::Kind::Constant:
            return "constant";
        case Builtin::Kind::Predicate:
            return "predicate";
    }
    return "";
}

// The entries a formula may name, with their kind and a short value.
std::vector<Completion> entries(const Document & doc) {
    std::vector<Completion> out;
    const Instance & inst = *doc.instance();
    const Model * m = doc.model().get();
    auto section = [&](const char * name) {
        const Json * j = inst.get(name);
        return j && j->is_object() ? j : nullptr;
    };
    if(const Json * params = section("params"))
        for(const auto & [name, value] : params->items())
            out.push_back({name, "param", json_text(value)});
    if(const Json * design = section("design"))
        for(const auto & [name, value] : design->items()) {
            const int i = m && !doc.stale() ? m->find_var(name) : -1;
            std::string v = text_at(value, "type");
            if(i >= 0) {
                const std::vector<double> d = doc.var_value(i);
                v = d.size() == 2 ? std::format("({}, {})", format_number(d[0]),
                                                format_number(d[1]))
                                  : format_number(d.front());
            }
            out.push_back({name, "design", v});
        }
    if(const Json * sweeps = section("sweeps"))
        for(const auto & [name, value] : sweeps->items())
            out.push_back({name, "sweep",
                           std::format("[{}, {}]", text_at(value, "min"),
                                       text_at(value, "max"))});
    if(const Json * lets = section("let"))
        for(const auto & [name, value] : lets->items()) {
            const int i = m ? m->find_let(name) : -1;
            const LetInfo * let =
                i >= 0 ? &m->lets()[static_cast<std::size_t>(i)] : nullptr;
            out.push_back(
                {name, "let", let ? kind_text(let->type, let->kind) : ""});
        }
    for(const GeometryEntry & g : geometry_entries(inst))
        out.push_back({g.name, "geometry", geometry_type(*g.json)});
    if(const Json * sketch = section("sketch")) {
        auto object = [&](const char * key) {
            return sketch->contains(key) && (*sketch)[key].is_object()
                       ? &(*sketch)[key]
                       : nullptr;
        };
        if(const Json * elements = object("elements"))
            for(const auto & [name, value] : elements->items())
                out.push_back({name, "element", text_at(value, "type")});
        if(const Json * bodies = object("bodies"))
            for(const auto & [name, value] : bodies->items())
                out.push_back({name, "body", ""});
    }
    return out;
}

// The members of body `body` of `doc` with their types, by their names
// within it: what completes after "tv.".
std::vector<Completion> members(const Document & doc, std::string_view body) {
    std::vector<Completion> out;
    const Json * elements =
        doc.instance()->get(std::format("sketch.bodies.{}.elements", body));
    if(elements && elements->is_object())
        for(const auto & [name, value] : elements->items())
            out.push_back({name, "element", text_at(value, "type")});
    return out;
}

// The text before the last comparison of `text` outside brackets: what a
// formula typed up to "dist(A, B) >= " is worth so far. The whole text
// when it has none.
std::string left_side(std::string_view text) {
    int depth = 0;
    std::size_t cut = text.size();
    for(std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if(c == '(') ++depth;
        if(c == ')') --depth;
        if(depth == 0 && (c == '<' || c == '>' || c == '=')) {
            cut = i;
            break;
        }
    }
    return std::string(trimmed(text.substr(0, cut)));
}

}  // namespace

Prefill constraint_prefill(const Document & doc,
                           std::span<const SelKey> selection) {
    if(!doc.has_instance() || selection.empty() || selection.size() > 2)
        return {};
    std::vector<Named> n;
    for(const SelKey & k : selection)
        if(std::optional<Named> one = named(doc, k)) n.push_back(*one);
    if(n.size() != selection.size()) return {};
    const Model * m = doc.model().get();
    auto sweep = [&](int k) {
        return m && k >= 0 && static_cast<std::size_t>(k) < m->sweeps().size()
                   ? m->sweeps()[static_cast<std::size_t>(k)].name
                   : std::string();
    };
    Prefill p;
    if(n.size() == 2) {
        const bool points =
            n[0].shape == Shape::Point && n[1].shape == Shape::Point;
        const bool shapes =
            n[0].shape != Shape::Scalar && n[1].shape != Shape::Scalar;
        if(points) {
            p.text = std::format("dist({}, {}) >= ", n[0].name, n[1].name);
        } else if(shapes) {
            p.text = std::format("clearance({}, {}) >= ", n[0].name, n[1].name);
            p.forall = sweep(n[0].sweep >= 0 ? n[0].sweep : n[1].sweep);
        }
        p.caret = p.text.size();
        return p;
    }
    if(n[0].shape == Shape::Point) {
        p.text = std::format("{}.x >= ", member_of(n[0]));
        p.caret = p.text.size();
    } else if(n[0].shape == Shape::Shape) {
        p.text = std::format("clearance({}, )", n[0].name);
        p.caret = p.text.size() - 1;
    } else {
        p.text = std::format("{} >= ", n[0].name);
        p.caret = p.text.size();
    }
    return p;
}

SketchPrefills sketch_prefills(std::span<const SketchOperand> operands) {
    using K = SketchOperand::Kind;
    SketchPrefills out;
    auto at_end = [](std::string text) {
        const std::size_t n = text.size();
        return Prefill{std::move(text), n, {}};
    };
    if(operands.size() == 1 && operands[0].kind == K::Line) {
        const std::string & l = operands[0].ref;
        out.bound = at_end(std::format("len({}) <= ", l));
        out.range = {std::format(" <= len({}) <= ", l), 0, {}};
        return out;
    }
    if(operands.size() != 2) return out;
    const SketchOperand & a = operands[0];
    const SketchOperand & b = operands[1];
    if(a.kind == K::Point && b.kind == K::Point) {
        out.bound = at_end(std::format("dist({}, {}) >= ", a.ref, b.ref));
        out.range = {std::format(" <= dist({}, {}) <= ", a.ref, b.ref), 0, {}};
        // As drawn: the one further along first. Level or plumb at the
        // editor's 4 decimals, the first first and `>=`, which holds there:
        // `>` would be violated as drawn (check gap 7).
        auto order = [&](double pa, double pb, char axis) {
            const bool tie = std::fabs(pb - pa) < 0.5e-4;
            const SketchOperand & hi = pb > pa && !tie ? b : a;
            const SketchOperand & lo = pb > pa && !tie ? a : b;
            return at_end(std::format("{}.{} {} {}.{}", hi.ref, axis,
                                      tie ? ">=" : ">", lo.ref, axis));
        };
        out.order_x = order(a.at.x, b.at.x, 'x');
        out.order_y = order(a.at.y, b.at.y, 'y');
        return out;
    }
    out.bound = at_end(std::format("clearance({}, {}) >= ", a.ref, b.ref));
    return out;
}

std::string prefill_label(const Prefill & p) {
    const std::string_view text(p.text);
    const std::string_view after = text.substr(p.caret);
    return std::format("Add {}...{}{}", text.substr(0, p.caret), after,
                       !after.empty() && after.ends_with(' ') ? "..." : "");
}

FormulaDraft::FormulaDraft(App & app)
    : app_(app), link_(app.changed().connect([this](Changes c) {
        if(!open_) return;
        // An entry edited is kept by its path, which may now name another;
        // a new one goes at the end of its list, wherever that is now.
        if(c.has(Change::Instance) || (c.has(Change::Structure) && !adding())) {
            cancel();
            return;
        }
        if(c.has_any(Change::Content | Change::Model | Change::Structure))
            compile_due_ = true;
        if(c.has_any(Change::Values | Change::Sweep)) evaluate_due_ = true;
    })) {}

void FormulaDraft::open(Mode mode, std::string name, std::string text,
                        std::size_t caret) {
    mode_ = mode;
    open_ = open_request_ = true;
    paths_ = app_.document().path_generation();
    name_ = std::move(name);
    text_ = std::move(text);
    caret_ = std::min(caret, text_.size());
    update_ = TextUpdate{text_, caret_};
    forall_.clear();
    enabled_ = true;
    role_ = "report";
    bound_.clear();
    from_.clear();
    to_.clear();
    unit_ = 0;
    refusal_.clear();
    // An edit opens with the caret after its formula's last name, often: a
    // list shown then would take the first Esc, which must cancel.
    hidden_ = true;
    compile_due_ = evaluate_due_ = true;
    complete();
}

void FormulaDraft::add_constraint() {
    const Document & doc = app_.document();
    if(!doc.has_instance()) return;
    add_constraint(constraint_prefill(doc, app_.selection().keys()));
}

void FormulaDraft::add_constraint(const Prefill & p) {
    const Document & doc = app_.document();
    if(!doc.has_instance()) return;
    target_.clear();
    old_name_.clear();
    open(Mode::AddConstraint,
         unused_item_name(*doc.instance(), "constraints", "c"), p.text,
         p.caret);
    forall_ = p.forall;
}

void FormulaDraft::add_criterion() {
    const Document & doc = app_.document();
    if(!doc.has_instance()) return;
    target_.clear();
    old_name_.clear();
    open(Mode::AddCriterion, unused_item_name(*doc.instance(), "criteria", "k"),
         {}, 0);
}

void FormulaDraft::add_quantity() {
    const Document & doc = app_.document();
    if(!doc.has_instance()) return;
    target_.clear();
    old_name_.clear();
    open(Mode::AddQuantity, unused_name(*doc.instance(), "q"), {}, 0);
}

bool FormulaDraft::edit(const SelKey & key) {
    const Document & doc = app_.document();
    if(!doc.has_instance()) return false;
    const Instance & inst = *doc.instance();
    if(key.kind == Kind::Let) {
        const std::string path = "let." + key.name;
        const Json * text = inst.get(path);
        if(!text) return false;
        target_ = path;
        old_name_ = key.name;
        open(Mode::EditQuantity, key.name, json_text(*text),
             json_text(*text).size());
        return true;
    }
    if(key.kind != Kind::Constraint && key.kind != Kind::Criterion)
        return false;
    const std::vector<std::string> paths = entry_paths(inst, key);
    if(paths.empty()) return false;
    const Json & entry = *inst.get(paths.front());
    target_ = paths.front();
    old_name_ = key.name;
    const std::string expr = text_at(entry, "expr");
    open(key.kind == Kind::Constraint ? Mode::EditConstraint
                                      : Mode::EditCriterion,
         key.name, expr, expr.size());
    forall_ = text_at(entry, "forall");
    if(criterion()) {
        role_ = text_at(entry, "role");
        std::array<std::string, 3> bound = bound_texts(entry);
        bound_ = std::move(bound[0]);
        from_ = std::move(bound[1]);
        to_ = std::move(bound[2]);
        unit_ = std::get<int>(unit_choice(doc, target_ + ".unit", {}).value);
    }
    return true;
}

bool FormulaDraft::constraint() const {
    return mode_ == Mode::AddConstraint || mode_ == Mode::EditConstraint;
}

bool FormulaDraft::criterion() const {
    return mode_ == Mode::AddCriterion || mode_ == Mode::EditCriterion;
}

bool FormulaDraft::adding() const {
    return mode_ == Mode::AddConstraint || mode_ == Mode::AddCriterion ||
           mode_ == Mode::AddQuantity;
}

std::string FormulaDraft::title() const {
    const char * what = constraint()  ? "constraint"
                        : criterion() ? "criterion"
                                      : "let";
    if(adding()) return std::format("Add {}", what);
    return std::format("Edit {} {}", what,
                       old_name_.empty() ? target_ : old_name_);
}

std::string FormulaDraft::commit_label() const {
    return adding() ? "Add" : "Save";
}

std::string FormulaDraft::path() const {
    if(!adding()) return target_;
    if(mode_ == Mode::AddQuantity) return "let." + name_;
    const char * section = constraint() ? "constraints" : "criteria";
    const Json * list = app_.document().instance()->get(section);
    return std::format("{}[{}]", section,
                       list && list->is_array() ? list->size() : 0);
}

Json FormulaDraft::entry() const {
    if(mode_ == Mode::AddQuantity || mode_ == Mode::EditQuantity) return text_;
    const Json * was =
        adding() ? nullptr : app_.document().instance()->get(target_);
    Json e = was && was->is_object() ? *was : Json::object();
    if(!name_.empty() || adding()) e["name"] = name_;
    e["expr"] = text_;
    if(constraint()) {
        if(forall_.empty())
            e.erase("forall");
        else
            e["forall"] = forall_;
    } else {
        e["role"] = role_;
        // Rebuilt from its text, a bound the user did not touch would change
        // all the same: an integer 1 saved as 1.0, and the bound of a role
        // that takes none, which the compiler warns about, erased.
        const bool untouched =
            was && text_at(*was, "role") == role_ &&
            bound_texts(*was) == std::array{bound_, from_, to_};
        if(!untouched) {
            if(role_ == "max" || role_ == "min")
                e["bound"] = number_or_string(bound_);
            else if(role_ == "range")
                e["bound"] = Json::array(
                    {number_or_string(from_), number_or_string(to_)});
            else
                e.erase("bound");
        }
        const DetailItem unit =
            unit_choice(app_.document(), path() + ".unit", {});
        if(unit_ == 0)
            e.erase("unit");
        else if(unit_ > 0)
            e["unit"] = unit.choices[static_cast<std::size_t>(unit_)];
    }
    if(adding() && !enabled_) e["enabled"] = false;
    return e;
}

std::vector<DetailItem> FormulaDraft::fields() const {
    std::vector<DetailItem> out;
    if(!open_) return out;
    const Document & doc = app_.document();
    const bool let = !constraint() && !criterion();
    out.push_back(
        {.kind = DK::Field,
         .id = "name",
         .label = "Name",
         .field = {{}, FieldKind::Text},
         .text = name_,
         .tip = let ? "Letters, digits and _, not starting with a digit, "
                      "unused by any other entry; a new name renames every "
                      "use"
                : constraint() ? "Unique among the constraints"
                               : "Unique among the criteria"});
    if(constraint()) {
        DetailItem forall{.kind = DK::Choice,
                          .id = "forall",
                          .label = "For all",
                          .tip =
                              "The sweep at every position of which it must "
                              "hold; none: at the sweep positions now only",
                          .value = forall_.empty() ? 0 : -1,
                          .choices = {"none"}};
        if(const Json * sweeps = doc.instance()->get("sweeps");
           sweeps && sweeps->is_object())
            for(const auto & [name, value] : sweeps->items()) {
                if(name == forall_)
                    forall.value = static_cast<int>(forall.choices.size());
                forall.choices.push_back(name);
            }
        forall.text = forall_.empty() ? std::string("none") : forall_;
        out.push_back(std::move(forall));
    }
    if(adding() && !let)
        out.push_back(
            {.kind = DK::Checkbox,
             .id = "enabled",
             .label = "Enabled",
             .tip = constraint()
                        ? "Unchecked: compiled and shown, but not required"
                        : "Unchecked: its value is shown, but it is neither "
                          "bounded nor optimised; this raises the format to "
                          "/2",
             .value = enabled_});
    if(criterion()) {
        const auto r = std::ranges::find(kCriterionRoles, role_);
        out.push_back(
            {.kind = DK::Choice,
             .id = "role",
             .label = "Role",
             .text = role_,
             .tip = "minimize or maximize: the objective (one enabled at "
                    "most); max: value <= bound; min: value >= bound; range: "
                    "lo <= value <= hi; report: shown only",
             .value = r == std::end(kCriterionRoles)
                          ? -1
                          : static_cast<int>(r - std::begin(kCriterionRoles)),
             .choices = {std::begin(kCriterionRoles),
                         std::end(kCriterionRoles)}});
        const char * tip = "A number in SI units, or an expression: \"2deg\"";
        auto bound = [&](const char * id, const char * label,
                         const std::string & text) {
            out.push_back({.kind = DK::Field,
                           .id = id,
                           .label = label,
                           .field = {{}, FieldKind::NumberOrExpression},
                           .text = text,
                           .tip = tip});
        };
        if(role_ == "max" || role_ == "min") bound("bound", "Bound", bound_);
        if(role_ == "range") {
            bound("from", "From", from_);
            bound("to", "To", to_);
        }
        DetailItem unit = unit_choice(doc, path() + ".unit",
                                      "The unit its value is shown in");
        unit.value = unit_;
        unit.field = {};
        unit.text = unit_ >= 0 ? unit.choices[static_cast<std::size_t>(unit_)]
                               : std::string();
        out.push_back(std::move(unit));
    }
    return out;
}

bool FormulaDraft::set(std::string_view id, const DetailValue & value) {
    const std::vector<DetailItem> items = fields();
    const auto it = std::ranges::find(items, id, &DetailItem::id);
    if(it == items.end()) return false;
    const DetailItem & item = *it;
    if(item.kind == DK::Field) {
        const std::string * text = std::get_if<std::string>(&value);
        if(!text) return false;
        std::string & to = id == "name"    ? name_
                           : id == "bound" ? bound_
                           : id == "from"  ? from_
                                           : to_;
        to = *text;
    } else if(item.kind == DK::Checkbox) {
        const bool * on = std::get_if<bool>(&value);
        if(!on) return false;
        enabled_ = *on;
    } else {
        const int * k = std::get_if<int>(&value);
        if(!k || *k < 0 || static_cast<std::size_t>(*k) >= item.choices.size())
            return false;
        const std::string & choice = item.choices[static_cast<std::size_t>(*k)];
        if(id == "forall") forall_ = *k == 0 ? std::string() : choice;
        if(id == "role") role_ = choice;
        if(id == "unit") unit_ = *k;
    }
    compile_due_ = true;
    refusal_.clear();
    return true;
}

void FormulaDraft::set_text(std::string text, std::size_t caret) {
    if(text != text_) {
        hidden_ = false;
        compile_due_ = true;
        refusal_.clear();
    }
    text_ = std::move(text);
    caret_ = std::min(caret, text_.size());
    complete();
}

void FormulaDraft::set_caret(std::size_t caret) {
    caret_ = std::min(caret, text_.size());
    complete();
}

std::optional<FormulaDraft::TextUpdate> FormulaDraft::take_text_update() {
    return std::exchange(update_, std::nullopt);
}

void FormulaDraft::complete() {
    candidates_.clear();
    highlighted_ = 0;
    std::size_t from = caret_, to = caret_;
    while(from > 0 && ident(text_[from - 1])) --from;
    while(to < text_.size() && ident(text_[to])) ++to;
    replaced_ = {from, to};
    const std::string typed = text_.substr(from, caret_ - from);
    if(!typed.empty() &&
       std::isdigit(static_cast<unsigned char>(typed.front())) != 0)
        return;
    // After "A." or "(...).", but not after the "0." of a number; after
    // "tv.", tv a body, its members, but after "tv.c." the coordinates.
    if(from > 0 && text_[from - 1] == '.') {
        std::size_t b = from - 1;
        while(b > 0 && ident(text_[b - 1])) --b;
        const bool named =
            b < from - 1 &&
            std::isdigit(static_cast<unsigned char>(text_[b])) == 0;
        if(named && (b == 0 || text_[b - 1] != '.') &&
           app_.document().has_instance())
            if(std::vector<Completion> found =
                   members(app_.document(),
                           std::string_view(text_).substr(b, from - 1 - b));
               !found.empty()) {
                for(Completion & c : found)
                    if(c.name.starts_with(typed))
                        candidates_.push_back(std::move(c));
                return;
            }
        if(named || (from >= 2 && text_[from - 2] == ')'))
            for(const char * c : {"x", "y"})
                if(std::string_view(c).starts_with(typed))
                    candidates_.push_back({c, "coordinate", ""});
        return;
    }
    if(typed.empty() || !app_.document().has_instance()) return;

    struct Ranked {
        Completion c;
        std::tuple<bool, bool, bool, bool, std::string> rank;
    };
    const std::string want = lower(typed);
    const bool sweeps_first = in_sweep_argument(text_, from);
    std::vector<Ranked> found;
    auto offer = [&](Completion c, bool builtin) {
        const std::string name = lower(c.name);
        const bool prefix = name.starts_with(want);
        if(!prefix && name.find(want) == std::string::npos) return;
        const bool sweep = c.kind == "sweep";
        std::string key = c.name;
        found.push_back({std::move(c),
                         {!prefix, key != typed, sweeps_first && !sweep,
                          builtin, std::move(key)}});
    };
    const bool let = mode_ == Mode::AddQuantity || mode_ == Mode::EditQuantity;
    for(Completion & c : entries(app_.document()))
        if(!(let && c.kind == "let" && c.name == old_name_))
            offer(std::move(c), false);
    for(const Builtin & b : builtin_functions())
        offer({std::string(b.name), builtin_kind(b.kind), arity(b)}, true);
    std::ranges::stable_sort(found, {}, &Ranked::rank);
    for(std::size_t i = 0; i < found.size() && i < kMaxCandidates; ++i)
        candidates_.push_back(std::move(found[i].c));
}

FormulaDraft::Outcome FormulaDraft::key(Key k) {
    switch(k) {
        case Key::Up:
        case Key::Down:
            if(!list_shown()) return Outcome::None;
            if(k == Key::Up && highlighted_ > 0) --highlighted_;
            if(k == Key::Down && highlighted_ + 1 < candidates_.size())
                ++highlighted_;
            return Outcome::Moved;
        case Key::Enter:
            if(list_shown() &&
               candidates_[highlighted_].name !=
                   std::string_view(text_).substr(
                       replaced_.first, replaced_.second - replaced_.first)) {
                accept(highlighted_);
                return Outcome::Accept;
            }
            commit();
            return Outcome::Commit;
        case Key::Escape:
            if(list_shown()) {
                hidden_ = true;
                return Outcome::HideList;
            }
            cancel();
            return Outcome::Cancel;
        case Key::Tab:
            hidden_ = true;
            return Outcome::NextField;
        default:
            return Outcome::None;
    }
}

void FormulaDraft::accept(std::size_t i) {
    if(i >= candidates_.size()) return;
    const Completion c = candidates_[i];
    const auto [from, to] = replaced_;
    text_.replace(from, to - from, c.name);
    caret_ = from + c.name.size();
    if(c.kind == "function" || c.kind == "special form" ||
       c.kind == "predicate") {
        if(caret_ >= text_.size() || text_[caret_] != '(')
            text_.insert(caret_, "(");
        ++caret_;
    }
    update_ = TextUpdate{text_, caret_};
    hidden_ = false;
    compile_due_ = true;
    refusal_.clear();
    complete();
}

bool FormulaDraft::insert(const SelKey & key) {
    if(!open_ || key.kind == Kind::Constraint || key.kind == Kind::Criterion)
        return false;
    std::string name = key.name;
    if(key.kind == Kind::Display) {
        const Json * expr = app_.document().instance()->get(
            std::format("display[{}].expr", key.index));
        if(!expr || !expr->is_string()) return false;
        name = expr->get<std::string>();
    }
    text_.insert(caret_, name);
    caret_ += name.size();
    update_ = TextUpdate{text_, caret_};
    compile_due_ = true;
    refusal_.clear();
    complete();
    return true;
}

const FormulaDraft::Check & FormulaDraft::check() {
    if(!open_) return check_;
    if(compile_due_)
        compile();
    else if(evaluate_due_)
        evaluate();
    return check_;
}

void FormulaDraft::compile() {
    compile_due_ = evaluate_due_ = false;
    check_ = {};
    model_.reset();
    probe_.clear();
    const Document & doc = app_.document();
    if(!doc.has_instance() || trimmed(text_).empty()) return;
    const bool let = !constraint() && !criterion();
    if(!doc.model() || doc.stale()) {
        const ParseResult parsed = parse_expression(text_);
        if(!parsed.ok())
            check_.diagnostic = diagnostic_text({Diagnostic::Severity::Error,
                                                 {},
                                                 parsed.error_column,
                                                 parsed.error},
                                                text_);
        check_.note = kNoModel;
        return;
    }
    // A let is compiled under the name it has, or under an unused one while
    // its new name is no name: its uses keep their meaning.
    std::string at = path();
    if(let && (!old_name_.empty() || !name_problem().empty()))
        at = "let." + (old_name_.empty() ? unused_name(*doc.instance(), "q")
                                         : old_name_);
    Instance copy = *doc.instance();
    std::string error;
    if(!copy.set(at, entry(), &error)) {
        check_.status = {Tone::Bad, error, {}};
        return;
    }
    const CompileResult r = gs::compile(copy, {.isolate_items = true});
    const std::string expr = let ? at : at + ".expr";
    const Diagnostic * first = nullptr;
    for(const Diagnostic & d : r.diagnostics) {
        if(!path_is_under(d.path, at)) continue;
        const bool better =
            !first || (d.severity == Diagnostic::Severity::Error &&
                       first->severity != Diagnostic::Severity::Error);
        if(better) first = &d;
    }
    if(first)
        check_.diagnostic = diagnostic_text(
            *first, first->path == expr ? std::string_view(text_) : "");
    const Model & now = *doc.model();
    auto fits = [&](const Model & m) {
        return m.coordinate_names() == now.coordinate_names() &&
               m.sweeps().size() == now.sweeps().size();
    };
    const bool excluded =
        std::ranges::contains(r.excluded, at, &ExcludedItem::path);
    if(r.model && !excluded && fits(*r.model)) {
        model_ = r.model;
        compiled_path_ = at;
        evaluate();
        return;
    }
    const ParseResult parsed = parse_expression(text_);
    const bool relation =
        parsed.ok() && parsed.ast->kind == Ast::Kind::Relation;
    if(parsed.ok() && !(constraint() && !relation)) return;
    const std::string side = left_side(text_);
    if(side.empty() || !parse_expression(side).ok()) return;
    const CompileResult p =
        gs::compile(*doc.instance(), {.probes = {side}, .isolate_items = true});
    if(!p.model || !fits(*p.model)) return;
    model_ = p.model;
    probe_ = side;
    evaluate();
}

void FormulaDraft::evaluate() {
    evaluate_due_ = false;
    if(!model_) return;
    const Document & doc = app_.document();
    const Model & m = *model_;
    const std::span<const double> x = doc.x();
    const std::span<const double> st = doc.sweep_t();
    try {
        Evaluator ev(m);
        if(!probe_.empty()) {
            const ExprRef ref[] = {m.probes().front().ref};
            check_.status = {
                Tone::Dim,
                std::format("{} = {} now", probe_,
                            shape_text(ev.values(x, st, ref).front())),
                "The left side of the comparison, at the design and the sweep "
                "positions now"};
            return;
        }
        const double tol = doc.feas_tol();
        if(constraint()) {
            const auto c = std::ranges::find(m.constraints(), compiled_path_,
                                             &ConstraintInfo::path);
            if(c == m.constraints().end()) return;
            std::string value = "-";
            if(c->value.ref.node >= 0) {
                const ExprRef ref[] = {c->value.ref};
                value = format_value(ev.values(x, st, ref).front());
            }
            Scene s;
            s.verification = ev.verify(x, SceneCache::kCoarseSamples);
            s.verify_samples = SceneCache::kCoarseSamples;
            const Status g = group_status(m, s, c->groups, tol);
            check_.status = {
                g.tone, std::format("value now {}, {}", value, g.text), g.tip};
        } else if(criterion()) {
            const auto c = std::ranges::find(m.criteria(), compiled_path_,
                                             &CriterionInfo::path);
            if(c == m.criteria().end()) return;
            const Verification v = ev.verify(x, SceneCache::kCoarseSamples);
            const CriterionCheck & cc =
                v.criteria[static_cast<std::size_t>(c - m.criteria().begin())];
            Status & out = check_.status;
            out.text =
                std::format("value {}", format_display(cc.value, c->unit));
            out.tone = Tone::Neutral;
            if(std::isfinite(cc.violation)) {
                const bool bad = cc.violation > tol;
                out.tone = bad                    ? Tone::Bad
                           : cc.violation > -1e-9 ? Tone::Active
                                                  : Tone::Ok;
                out.text += bad ? ", violated" : ", within its bound";
            }
            out.tip = std::format("On the {}-sample grid",
                                  SceneCache::kCoarseSamples);
        } else {
            const int i = m.find_let(compiled_path_.substr(4));
            if(i < 0) return;
            const ExprRef ref[] = {m.lets()[static_cast<std::size_t>(i)].ref};
            check_.status = {
                Tone::Normal,
                std::format("value now {}",
                            shape_text(ev.values(x, st, ref).front())),
                "At the design and the sweep positions now"};
        }
    } catch(const std::exception & e) {
        check_.status = {Tone::Bad, e.what(), {}};
    }
}

std::string FormulaDraft::name_problem() const {
    const Instance & inst = *app_.document().instance();
    if(!constraint() && !criterion()) {
        if(!is_identifier(name_))
            return "A let's name is letters, digits and _, not starting with "
                   "a digit";
        if(name_ == old_name_) return {};
        if(find_builtin(name_))
            return std::format("{} is a builtin name", name_);
        if(name_used(inst, name_))
            return std::format("{} is already the name of an entry", name_);
        return {};
    }
    const char * section = constraint() ? "constraints" : "criteria";
    if(name_.empty())
        return std::format("A {} needs a name",
                           constraint() ? "constraint" : "criterion");
    const Json * list = inst.get(section);
    for(std::size_t i = 0; list && list->is_array() && i < list->size(); ++i)
        if(std::format("{}[{}]", section, i) != target_ &&
           (*list)[i].is_object() && (*list)[i].value("name", "") == name_)
            return std::format("{} is already the name of a {}", name_,
                               constraint() ? "constraint" : "criterion");
    return {};
}

bool FormulaDraft::commit() {
    refusal_.clear();
    if(!open_) return false;
    Document & doc = app_.document();
    if(trimmed(text_).empty()) {
        refusal_ = "Type a formula first";
        return false;
    }
    if(std::string why = name_problem(); !why.empty()) {
        refusal_ = std::move(why);
        return false;
    }
    const char * what = constraint()  ? "constraint"
                        : criterion() ? "criterion"
                                      : "let";
    const bool let = !constraint() && !criterion();
    const Kind kind = constraint()  ? Kind::Constraint
                      : criterion() ? Kind::Criterion
                                    : Kind::Let;
    const bool renamed = !adding() && name_ != old_name_;
    // The key it has now, before a rename: an unnamed one is keyed by place.
    const std::optional<SelKey> was =
        adding() ? std::nullopt : entry_key(*doc.instance(), target_);
    if(!adding() && !renamed) {
        const Json * now = doc.instance()->get(target_);
        if(now && *now == entry()) {
            open_ = false;
            return true;
        }
    }
    EditBatch batch(std::format("{} {} {}", adding() ? "add" : "edit", what,
                                adding() ? name_ : old_name_));
    if(adding()) {
        batch.set(path(), entry());
    } else {
        Json e = entry();
        if(renamed && !let) {
            if(old_name_.empty())
                e.erase("name");
            else
                e["name"] = old_name_;
        }
        batch.set(target_, std::move(e));
        if(renamed) batch.rename(target_, name_);
    }
    // Closed first: the edit's own Structure must not cancel it.
    open_ = false;
    std::string error;
    doc.apply(std::move(batch), &error);
    if(!error.empty()) {
        open_ = true;
        refusal_ = std::move(error);
        return false;
    }
    if(renamed && was) app_.entry_renamed(*was, name_);
    if(adding()) app_.show_added({kind, name_}, false);
    return true;
}

void FormulaDraft::cancel() {
    open_ = false;
    refusal_.clear();
}

}  // namespace gs::app

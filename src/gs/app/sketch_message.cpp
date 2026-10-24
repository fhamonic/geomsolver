#include "gs/app/sketch_message.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <set>
#include <utility>

namespace gs::app {
namespace {

using Kind = SketchMessage::Kind;
using Part = SketchMessage::Part;

constexpr std::size_t kNamed = 5;  // names of a list before "and N more"

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

int constraint_of(const Model & m, RowRef r) {
    return m.groups()[uz(r.group)].source;
}

SelKey constraint_key(const Model & m, int constraint) {
    return {SelKey::Kind::Constraint, m.constraints()[uz(constraint)].name};
}

class Builder {
public:
    explicit Builder(const Model & m) : m_(m) {}

    void text(std::string t) {
        if(!out_.empty() && out_.back().select.empty())
            out_.back().text += t;
        else
            out_.push_back({std::move(t), {}, {}});
    }
    void link(std::string t, std::vector<SelKey> select, std::string tip) {
        out_.push_back({std::move(t), std::move(select), std::move(tip)});
    }
    void constraint(int c, std::string tip = {}) {
        link(m_.constraints()[uz(c)].name, {constraint_key(m_, c)},
             tip.empty() ? "Click to select it" : std::move(tip));
    }
    // "dAB, dAC, ... and 3 more": the rest one link that selects them all.
    void constraints(const std::vector<int> & cs) {
        for(std::size_t i = 0; i < cs.size() && i < kNamed; ++i) {
            if(i > 0) text(", ");
            constraint(cs[i]);
        }
        more(cs);
    }
    // " and 3 more", selecting cs past the kNamed-th; nothing when none is.
    void more(const std::vector<int> & cs) {
        if(cs.size() <= kNamed) return;
        std::vector<SelKey> rest;
        for(std::size_t i = kNamed; i < cs.size(); ++i)
            rest.push_back(constraint_key(m_, cs[i]));
        text(" and ");
        std::string count = std::format("{} more", rest.size());
        link(std::move(count), std::move(rest), "Click to select them");
    }
    std::vector<Part> take() { return std::move(out_); }

private:
    const Model & m_;
    std::vector<Part> out_;
};

std::string_view reason(sketch::MalformedConstraint::Reason r) {
    using R = sketch::MalformedConstraint::Reason;
    switch(r) {
        case R::Segment:
            return "a zero-length segment";
        case R::Radius:
            return "a radius not positive";
        case R::Span:
            return "a span outside (0, 2 pi)";
        case R::Inner:
            return "an inner circle not smaller";
        case R::NotFinite:
            return "not finite";
        case R::NoParameter:
            return "nothing it reads can move";
        case R::Degenerate:
            return "degenerate";
    }
    return "malformed";
}

SketchMessage conflicting(const Model & m, const sketch::Settled & s) {
    Builder b(m);
    b.text("Conflicting: ");
    for(std::size_t g = 0; g < s.conflict_groups.size(); ++g) {
        if(g > 0) b.text("; ");
        std::vector<int> group = s.conflict_groups[g];
        if(!s.converged) {
            b.text("{");
            b.constraints(group);
            b.text("}");
            continue;
        }
        // settle() leaves out the latest of each group.
        const int out = group.back();
        group.pop_back();
        b.constraint(out, "Left out of the solve: click to select it");
        b.text(" left out");
        if(group.empty()) continue;
        b.text(" (with ");
        b.constraints(group);
        b.text(")");
    }
    if(!s.converged) b.text(": nothing moved");
    return {Kind::Conflicting, Tone::Bad, false, false, b.take()};
}

SketchMessage malformed(const Model & m, const sketch::Settled & s) {
    Builder b(m);
    b.text("Malformed: ");
    for(std::size_t i = 0; i < s.malformed.size() && i < kNamed; ++i) {
        const sketch::MalformedConstraint & mc = s.malformed[i];
        if(i > 0) b.text(", ");
        b.constraint(mc.constraint, mc.why);
        b.text(std::format(" ({})", reason(mc.reason)));
    }
    std::vector<int> all;
    for(const sketch::MalformedConstraint & mc : s.malformed)
        all.push_back(mc.constraint);
    b.more(all);
    return {Kind::Malformed, Tone::Bad, false, false, b.take()};
}

// The redundant constraints of a diagnosis, as the CLI's report counts
// them: one whose rows are only partly dependent (one of coincident's two)
// is partially redundant.
std::optional<SketchMessage> redundant(const Model & m,
                                       const sketch::Diagnosis & d) {
    std::map<int, std::set<int>> with;
    std::map<int, int> dependent, rows;
    for(const sketch::Dependency & dep : d.rank.dependent) {
        const int c = constraint_of(m, dep.row);
        ++dependent[c];
        for(const RowRef & g : dep.group)
            if(constraint_of(m, g) != c) with[c].insert(constraint_of(m, g));
    }
    if(dependent.empty()) return std::nullopt;
    for(const sketch::Component & comp : d.components)
        for(const RowRef & r : comp.rows) ++rows[constraint_of(m, r)];
    Builder b(m);
    bool any = false;
    for(const bool partial : {false, true}) {
        std::vector<int> list;
        for(const auto & [c, n] : dependent)
            if((n < rows[c]) == partial) list.push_back(c);
        if(list.empty()) continue;
        b.text(partial
                   ? (any ? "; partially redundant: " : "Partially redundant: ")
                   : "Redundant: ");
        any = true;
        for(std::size_t i = 0; i < list.size() && i < kNamed; ++i) {
            const int c = list[i];
            if(i > 0) b.text(", ");
            b.constraint(c);
            if(with[c].empty()) continue;
            b.text(" (with ");
            b.constraints({with[c].begin(), with[c].end()});
            b.text(")");
        }
        b.more(list);
    }
    return SketchMessage{Kind::Redundant, Tone::Warn, false, false, b.take()};
}

SketchMessage free_dofs(const Model & m, const sketch::Diagnosis & d, int dof) {
    Builder b(m);
    std::vector<SelKey> movers;
    for(const sketch::FreeBlock & f : d.free)
        if(f.count > 0)
            movers.push_back(
                {SelKey::Kind::Element, m.sketch()[uz(f.element)].name});
    const std::string count =
        std::format("{} free DOF{}", dof, dof == 1 ? "" : "s");
    if(movers.empty())
        b.text(count);
    else
        b.link(count, std::move(movers),
               "Click to select the elements that can still move");
    // Each held gauge part is one of the DOFs counted, and marks its body.
    std::set<int> loose;
    for(const int c : d.held) loose.insert(m.sketch_coords()[uz(c)].body);
    for(const int body : loose) {
        const std::string & name = m.sketch_bodies()[uz(body)].name;
        b.text("; ");
        b.link(std::format("{}: frame not anchored", name),
               {{SelKey::Kind::Body, name}},
               "No fixed member or constraint ties the body's frame to its "
               "elements: click to select the body");
    }
    return {Kind::FreeDofs, Tone::Normal, false, false, b.take()};
}

}  // namespace

std::string SketchMessage::text() const {
    std::string out;
    for(const Part & p : parts) out += p.text;
    return out;
}

std::string SketchMessage::dimmed() const {
    if(stale)
        return "The sketch does not compile: this is the message of its last "
               "good state (see Messages)";
    if(greyed)
        return "The diagnosis of the last change runs: this is the previous "
               "message";
    return {};
}

std::vector<SelKey> SketchMessage::select() const {
    for(const Part & p : parts)
        if(!p.select.empty()) return p.select;
    return {};
}

std::optional<SketchMessage> sketch_message(const Document & doc) {
    const std::shared_ptr<const Model> & model = doc.sketch_model();
    if(!model) return std::nullopt;
    const Model & m = *model;
    const sketch::Settled & s = doc.settled();
    auto plain = [](Kind kind, Tone tone, std::string text,
                    std::string tip = {}) {
        return SketchMessage{
            kind, tone, false, false, {{std::move(text), {}, std::move(tip)}}};
    };
    if(s.status == sketch::Status::Empty)
        return plain(Kind::Empty, Tone::Dim, "Empty sketch",
                     "No free value and no geometry constraint");
    if(!s.conflict_groups.empty()) return conflicting(m, s);
    if(!s.malformed.empty()) return malformed(m, s);
    if(!s.converged)
        return plain(
            Kind::NotConverged, Tone::Bad,
            std::format("Did not converge: nothing moved (largest geometry "
                        "residual {:.3g} m)",
                        s.residual),
            "The solve stopped short of the geometry constraints: undo the "
            "last edit, or change a dimension a little");
    // Converged, so a diagnosis comes: a redundancy outranks the residual
    // of a drawing off its constraints.
    const std::optional<SketchDiagnosis> & d = doc.diagnosis();
    if(!d || d->generation != doc.sketch_generation())
        return plain(Kind::Diagnosing, Tone::Dim, "Diagnosing the sketch...");
    const sketch::Diagnosis & r = d->result;
    if(std::optional<SketchMessage> red = redundant(m, r)) return red;
    if(s.residual > s.options.tol)
        return plain(
            Kind::NotSolved, Tone::Warn,
            std::format("Not solved: largest geometry residual {:.3g} m",
                        s.residual),
            "The drawing as written is off its geometry constraints: Solve "
            "sketch, in the context menu, or any edit of the sketch but a "
            "note or a helper flag moves it onto them");
    const int dof = r.rank.dof.value_or(0);
    if(dof > 0) return free_dofs(m, r, dof);
    return plain(Kind::Full, Tone::Ok, "Fully constrained",
                 "No element can move: every value is determined by the "
                 "constraints");
}

std::vector<SketchWarning> sketch_warnings(const Document & doc) {
    std::vector<SketchWarning> out;
    const std::optional<SketchDiagnosis> & d = doc.diagnosis();
    if(!d) return out;
    const Model & m = *d->model;
    std::set<int> seen;
    auto add = [&](const RowRef & r, std::string_view what) {
        const int c = constraint_of(m, r);
        if(!seen.insert(c).second) return;
        const std::string & name = m.constraints()[uz(c)].name;
        out.push_back(
            {constraint_key(m, c), std::format("{}: {}", name, what)});
    };
    for(const RowRef & r : d->result.rank.near_dependent)
        add(r, "near a singular configuration (a linkage at dead centre?)");
    for(const RowRef & r : d->result.singular)
        add(r,
            "singular configuration: dependent as drawn only (circles that "
            "happen to touch?), not redundant");
    return out;
}

}  // namespace gs::app

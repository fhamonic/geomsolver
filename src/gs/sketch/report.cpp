#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>

#include "gs/sketch/sketch.hpp"

namespace gs::sketch {

namespace {

using Json = nlohmann::ordered_json;
using uz = std::size_t;

std::string plural(std::size_t n, std::string_view one) {
    return std::format("{} {}{}", n, one, n == 1 ? "" : "s");
}

std::string join(const std::vector<std::string> & items) {
    std::string s;
    for(const std::string & i : items) s += (s.empty() ? "" : ", ") + i;
    return s;
}

int constraint_of(const Model & m, RowRef r) {
    return m.groups()[uz(r.group)].source;
}

const std::string & name_of(const Model & m, int constraint) {
    return m.constraints()[uz(constraint)].name;
}

// What the report reads: the diagnosis when there is one, else what
// settle() ranked; no rank for a solve that failed.
struct View {
    const Rank * rank = nullptr;
    const std::vector<Component> * components = nullptr;
    const std::vector<int> * held = nullptr;
};

View view(const Settled & s, const Diagnosis * d) {
    View v;
    v.components = d ? &d->components : &s.components;
    v.held = d ? &d->held : &s.held;
    if(s.converged) v.rank = d ? &d->rank : s.rank ? &*s.rank : nullptr;
    return v;
}

std::vector<std::string> names_of(const Model & m,
                                  const std::vector<int> & constraints) {
    std::vector<std::string> out;
    for(const int c : constraints) out.push_back(name_of(m, c));
    return out;
}

// "d3 left out (with dAB, dAC)" per group when the rest settled, else
// "{dAB, dAC, dBC}: nothing moved".
std::string conflicts(const Model & m, const Settled & s) {
    std::vector<std::string> items;
    for(const std::vector<int> & g : s.conflict_groups) {
        std::vector<std::string> n = names_of(m, g);
        if(!s.converged) {
            items.push_back("{" + join(n) + "}");
            continue;
        }
        const std::string last = n.back();
        n.pop_back();
        items.push_back(last + " left out" +
                        (n.empty() ? "" : " (with " + join(n) + ")"));
    }
    return join(items) + (s.converged ? "" : ": nothing moved");
}

struct Redundancy {
    int constraint = -1;
    bool partial = false;
    std::vector<int> with;  // constraints, in document order
};

// The dependent rows per constraint: a constraint some of whose rows stay
// independent (one of coincident's two) is partially redundant.
std::vector<Redundancy> redundancies(const Model & m, const View & v) {
    std::map<int, std::set<int>> with;
    std::map<int, int> dependent;
    for(const Dependency & d : v.rank->dependent) {
        const int c = constraint_of(m, d.row);
        ++dependent[c];
        std::set<int> & w = with[c];
        for(const RowRef & g : d.group)
            if(constraint_of(m, g) != c) w.insert(constraint_of(m, g));
    }
    std::map<int, int> rows;
    for(const Component & comp : *v.components)
        for(const RowRef & r : comp.rows) ++rows[constraint_of(m, r)];
    std::vector<Redundancy> out;
    for(const auto & [c, n] : dependent)
        out.push_back({c, n < rows[c], {with[c].begin(), with[c].end()}});
    return out;
}

// The near-dependent rows, and with a diagnosis the rows dependent at the
// drawing only.
std::vector<std::string> singular(const Model & m, const View & v,
                                  const Diagnosis * d) {
    std::set<int> cs;
    for(const RowRef & r : v.rank->near_dependent)
        cs.insert(constraint_of(m, r));
    if(d)
        for(const RowRef & r : d->singular) cs.insert(constraint_of(m, r));
    std::vector<std::string> out;
    for(const int c : cs) out.push_back(name_of(m, c));
    return out;
}

// Per body whose frame is not anchored: the gauge directions held.
std::vector<std::pair<std::string, int>> unanchored(const Model & m,
                                                    const View & v) {
    std::map<int, int> count;
    for(const int c : *v.held) ++count[m.sketch_coords()[uz(c)].body];
    std::vector<std::pair<std::string, int>> out;
    for(const auto & [b, n] : count)
        out.emplace_back(m.sketch_bodies()[uz(b)].name, n);
    return out;
}

// " (P7, door)": the elements that can still move, from the diagnosis's
// free blocks, at most 5 named; empty without a diagnosis or a free block.
std::string movers(const Model & m, const Diagnosis * d) {
    if(!d) return {};
    std::vector<std::string> names;
    std::size_t more = 0;
    for(const FreeBlock & b : d->free) {
        if(b.count == 0) continue;
        if(names.size() < 5)
            names.push_back(m.sketch()[uz(b.element)].name);
        else
            ++more;
    }
    if(names.empty()) return {};
    return " (" + join(names) +
           (more ? std::format(" and {} more", more) : std::string()) + ")";
}

std::string_view status_name(Status s) {
    switch(s) {
        case Status::Empty:
            return "empty";
        case Status::Conflicting:
            return "conflicting";
        case Status::Malformed:
            return "malformed";
        case Status::Redundant:
            return "redundant";
        case Status::NotConverged:
            return "not converged";
        case Status::Solved:
            return "solved";
        case Status::UnderConstrained:
            return "under-constrained";
        case Status::Full:
            return "fully constrained";
    }
    return "?";
}

}  // namespace

std::string format_report(const Settled & s, const Diagnosis * d,
                          bool verbose) {
    if(!s.model) return {};
    const Model & m = *s.model;
    const View v = view(s, d);
    std::string line = std::format(
        "sketch: {} in {}", plural(m.sketch_coords().size(), "parameter"),
        plural(v.components->size(), "component"));
    if(v.rank) {
        if(v.rank->dof)
            line += std::format("; {} free DOF{}{}", *v.rank->dof,
                                *v.rank->dof == 1 ? "" : "s", movers(m, d));
        std::vector<std::string> items;
        for(const Redundancy & r : redundancies(m, v)) {
            std::vector<std::string> with;
            for(const int c : r.with) with.push_back(name_of(m, c));
            items.push_back(
                std::format("{} ({}with {})", name_of(m, r.constraint),
                            r.partial ? "partially, " : "", join(with)));
        }
        // "none" would be a claim about the components left unranked.
        if(!items.empty() || v.rank->dof)
            line += "; redundant: " + (items.empty() ? "none" : join(items));
    }
    if(s.status == Status::NotConverged)
        line += "; did not converge";
    else
        line += "; conflicting: " +
                (s.conflict_groups.empty() ? "none" : conflicts(m, s));
    std::vector<std::string> bad;
    for(const MalformedConstraint & mc : s.malformed)
        bad.push_back(name_of(m, mc.constraint));
    line += "; malformed: " + (bad.empty() ? "none" : join(bad));
    line += std::format(
        "; {}largest geometry residual {:.3g} m",
        s.residual <= s.options.tol ? "" : "not solved: ", s.residual);
    std::string out = line + "\n";
    for(const MalformedConstraint & mc : s.malformed)
        out += std::format("  {} is malformed: {}\n", name_of(m, mc.constraint),
                           mc.why);
    if(v.rank) {
        const std::vector<std::string> near = singular(m, v, d);
        if(!near.empty())
            out += "  singular configuration (a linkage at dead centre?): " +
                   join(near) + "\n";
    }
    if(d && d->sparse)
        out += std::format(
            "  above {} parameters a component is ranked by a sparse QR: the "
            "constraint named redundant is one of its group, not always the "
            "latest\n",
            s.options.dense_columns);
    for(const auto & [body, n] : unanchored(m, v))
        out += std::format(
            "  {}: the frame is not anchored to its members "
            "({} DOF{})\n",
            body, n, n == 1 ? "" : "s");
    if(!verbose) return out;
    for(std::size_t i = 0; i < v.components->size(); ++i) {
        const Component & c = (*v.components)[i];
        out += std::format(
            "  component {}: {}, {}{}, L_c {:.3g} m\n", i + 1,
            plural(c.columns.size(), "parameter"), plural(c.rows.size(), "row"),
            v.rank && v.rank->ranked[i]
                ? std::format(", rank {}", v.rank->independent[i].size())
                : std::string(),
            c.length);
    }
    std::vector<char> used(m.sketch_coords().size(), 0);
    for(const Component & c : *v.components)
        for(const int col : c.columns) used[uz(col)] = 1;
    std::vector<std::string> unread, held;
    for(const int col : *v.held) {
        used[uz(col)] = 1;
        held.push_back(m.sketch_coords()[uz(col)].name);
    }
    for(std::size_t col = 0; col < used.size(); ++col)
        if(!used[col]) unread.push_back(m.sketch_coords()[col].name);
    if(!unread.empty()) out += "  in no row: " + join(unread) + "\n";
    if(!held.empty()) out += "  held: " + join(held) + "\n";
    if(d) {
        std::vector<std::string> free;
        for(const FreeBlock & b : d->free)
            if(b.count > 0)
                free.push_back(std::format(
                    "{} ({})", m.sketch()[uz(b.element)].name, b.count));
        if(!free.empty()) out += "  free: " + join(free) + "\n";
    }
    return out;
}

Json report_json(const Settled & s, const Diagnosis * d) {
    Json j = Json::object();
    if(!s.model) return j;
    const Model & m = *s.model;
    const View v = view(s, d);
    // The diagnosis's rank replaces settle()'s: unconfirmed, and absent
    // above Options::sync_columns.
    const bool ranked =
        s.status == Status::Solved || s.status == Status::Redundant ||
        s.status == Status::UnderConstrained || s.status == Status::Full;
    j["status"] = std::string(status_name(d && ranked ? d->status : s.status));
    j["parameters"] = m.sketch_coords().size();
    j["components"] = v.components->size();
    j["dof"] = v.rank && v.rank->dof ? Json(*v.rank->dof) : Json(nullptr);
    Json red = Json::array(), sing = Json::array();
    if(v.rank) {
        for(const Redundancy & r : redundancies(m, v)) {
            Json with = Json::array();
            for(const int c : r.with) with.push_back(name_of(m, c));
            red.push_back({{"constraint", name_of(m, r.constraint)},
                           {"partial", r.partial},
                           {"with", with}});
        }
        for(const std::string & n : singular(m, v, d)) sing.push_back(n);
    }
    j["redundant"] = red;
    j["conflicting"] = s.status == Status::Conflicting;
    j["left_out"] = names_of(m, s.conflicting);
    Json groups = Json::array();
    for(const std::vector<int> & g : s.conflict_groups)
        groups.push_back(names_of(m, g));
    j["conflict_groups"] = groups;
    Json bad = Json::array();
    for(const MalformedConstraint & mc : s.malformed)
        bad.push_back(
            {{"constraint", name_of(m, mc.constraint)}, {"reason", mc.why}});
    j["malformed"] = bad;
    j["singular"] = sing;
    Json loose = Json::array();
    for(const auto & [body, n] : unanchored(m, v))
        loose.push_back({{"body", body}, {"dofs", n}});
    j["unanchored"] = loose;
    j["residual"] =
        std::isfinite(s.residual) ? Json(s.residual) : Json(nullptr);
    j["report"] = format_report(s, d, false);
    return j;
}

}  // namespace gs::sketch

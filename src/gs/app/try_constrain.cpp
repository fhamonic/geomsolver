#include "gs/app/try_constrain.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <optional>
#include <string>

#include "gs/sketch/sketch.hpp"

namespace gs::app {
namespace {

bool same(const RowRef & a, const RowRef & b) {
    return a.group == b.group && a.split == b.split;
}

// The rows the sketch solver solves for the constraint at `path`; nullopt
// when the model left it out (an error excluded it).
std::optional<std::vector<RowRef>> solved_rows(const Model & m,
                                               const std::string & path,
                                               int & index) {
    const auto c =
        std::ranges::find(m.constraints(), path, &ConstraintInfo::path);
    if(c == m.constraints().end()) return std::nullopt;
    index = static_cast<int>(c - m.constraints().begin());
    std::vector<RowRef> rows;
    for(const int g : c->groups) {
        const RowGroup & group = m.groups()[static_cast<std::size_t>(g)];
        if(group.role != GroupRole::Solve) continue;
        for(int s = 0; s < group.split; ++s) rows.push_back({g, s});
    }
    return rows;
}

// Whether every component that holds one of `rows` was ranked.
bool ranked(const sketch::Settled & s, const std::vector<RowRef> & rows) {
    for(const RowRef & r : rows)
        for(std::size_t c = 0; c < s.components.size(); ++c)
            if(std::ranges::any_of(
                   s.components[c].rows,
                   [&](const RowRef & x) { return same(x, r); }) &&
               !(c < s.rank->ranked.size() && s.rank->ranked[c]))
                return false;
    return true;
}

// Whether every component that holds one of `rows` has at most `columns`
// columns: one settle() did not rank is then one whose polish fell short.
bool small(const sketch::Settled & s, const std::vector<RowRef> & rows,
           int columns) {
    for(const RowRef & r : rows)
        for(const sketch::Component & c : s.components)
            if(std::ranges::any_of(
                   c.rows, [&](const RowRef & x) { return same(x, r); }) &&
               static_cast<int>(c.columns.size()) > columns)
                return false;
    return true;
}

// Whether a component holds one of `rows`.
bool in_component(const sketch::Settled & s, const std::vector<RowRef> & rows) {
    return std::ranges::any_of(rows, [&](const RowRef & r) {
        return std::ranges::any_of(
            s.components, [&](const sketch::Component & c) {
                return std::ranges::any_of(
                    c.rows, [&](const RowRef & x) { return same(x, r); });
            });
    });
}

bool dependent(std::span<const sketch::Dependency> deps,
               const std::vector<RowRef> & rows, bool confirmed) {
    return std::ranges::any_of(rows, [&](const RowRef & r) {
        return std::ranges::any_of(deps, [&](const sketch::Dependency & d) {
            return same(d.row, r) && (!confirmed || d.confirmed);
        });
    });
}

}  // namespace

Tried try_constrain(const Document & doc, const EditBatch & batch,
                    std::span<const std::string> dimensions,
                    std::span<const std::string> suggestions) {
    Tried out;
    out.dependent.assign(dimensions.size(), false);
    out.drawn.assign(dimensions.size(), false);
    out.unranked.assign(dimensions.size(), false);
    out.fixed.assign(dimensions.size(), false);
    out.stuck.assign(dimensions.size(), false);
    out.kept.assign(suggestions.size(), false);
    out.untested.assign(suggestions.size(), false);
    if(!doc.has_instance()) return out;
    Instance copy = *doc.instance();
    if(!apply_ops(copy, batch)) return out;
    sketch::SettleOptions options{.dry_run = true, .isolate_items = true};
    options.only.assign(dimensions.begin(), dimensions.end());
    options.only.insert(options.only.end(), suggestions.begin(),
                        suggestions.end());
    sketch::Settled s;
    try {
        s = sketch::settle(copy, options);
    } catch(const std::exception &) {
        return out;
    }
    if(!s.model) return out;
    const Model & m = *s.model;
    const bool solved = s.converged && s.rank.has_value();
    auto left_out = [&](int index) {
        return std::ranges::contains(s.conflicting, index) ||
               std::ranges::contains(s.malformed, index,
                                     &sketch::MalformedConstraint::constraint);
    };

    // A component settle() did not rank though it is small enough: its
    // polish fell short, as at a drawing a commit's settle left within its
    // tolerance (a measured dimension written rounded), where a dependent
    // row is off its set by that much. The diagnosis ranks it there.
    const int sync = s.options.sync_columns;
    auto retry = [&](const std::vector<RowRef> & rows) {
        return s.converged && small(s, rows, sync) &&
               !(s.rank && ranked(s, rows));
    };
    std::vector<std::size_t> retried;
    std::vector<std::vector<RowRef>> retried_rows;
    for(std::size_t j = 0; j < suggestions.size(); ++j) {
        int index = -1;
        const std::optional<std::vector<RowRef>> rows =
            solved_rows(m, suggestions[j], index);
        if(!rows || rows->empty() || left_out(index)) continue;
        if(retry(*rows)) {
            retried.push_back(j);
            retried_rows.push_back(*rows);
            continue;
        }
        if(!solved || !ranked(s, *rows)) {
            out.untested[j] = true;
            continue;
        }
        out.kept[j] = !dependent(s.rank->dependent, *rows, false);
    }

    // The second point only for a dimension dependent at the drawing, or
    // in a component its polish left unranked.
    std::vector<std::size_t> suspects;
    std::vector<std::vector<RowRef>> suspect_rows;
    for(std::size_t i = 0; i < dimensions.size(); ++i) {
        int index = -1;
        const std::optional<std::vector<RowRef>> rows =
            solved_rows(m, dimensions[i], index);
        if(!rows || rows->empty()) continue;
        // Left out as conflicting: dependent on the rows before it off
        // their set, which no second point turns independent.
        if(std::ranges::contains(s.conflicting, index)) {
            out.dependent[i] = out.drawn[i] = true;
            continue;
        }
        if(left_out(index)) {
            const auto bad = std::ranges::find(
                s.malformed, index, &sketch::MalformedConstraint::constraint);
            using Reason = sketch::MalformedConstraint::Reason;
            out.stuck[i] = bad != s.malformed.end() &&
                           (bad->reason == Reason::NoParameter ||
                            bad->reason == Reason::Degenerate) &&
                           !in_component(s, *rows);
            continue;
        }
        if(s.converged && !in_component(s, *rows)) {
            out.fixed[i] = true;
            continue;
        }
        if(!retry(*rows)) {
            if(!solved || !ranked(s, *rows)) {
                out.unranked[i] = true;
                continue;
            }
            if(!dependent(s.rank->dependent, *rows, false)) continue;
            out.drawn[i] = true;
        }
        suspects.push_back(i);
        suspect_rows.push_back(*rows);
    }
    if(suspects.empty() && retried.empty()) return out;
    try {
        const sketch::System system(s.model, s.options);
        std::vector<std::string> asked;
        for(const std::size_t i : suspects) asked.emplace_back(dimensions[i]);
        for(const std::size_t j : retried) asked.emplace_back(suggestions[j]);
        const sketch::Diagnosis d =
            system.diagnose(m.initial_x(), s.q, sketch::left_out(s), {}, asked);
        auto singular = [&](const std::vector<RowRef> & rows) {
            return std::ranges::any_of(rows, [&](const RowRef & r) {
                return std::ranges::any_of(
                    d.singular, [&](const RowRef & x) { return same(x, r); });
            });
        };
        for(std::size_t k = 0; k < suspects.size(); ++k) {
            const std::size_t i = suspects[k];
            out.drawn[i] =
                out.drawn[i] ||
                dependent(d.rank.dependent, suspect_rows[k], false) ||
                singular(suspect_rows[k]);
            out.dependent[i] =
                dependent(d.rank.dependent, suspect_rows[k], true);
        }
        for(std::size_t k = 0; k < retried.size(); ++k)
            out.kept[retried[k]] =
                !dependent(d.rank.dependent, retried_rows[k], false) &&
                !singular(retried_rows[k]);
    } catch(const std::exception &) {
        for(const std::size_t j : retried) out.untested[j] = true;
    }
    return out;
}

}  // namespace gs::app

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <random>
#include <stdexcept>

#include "core.hpp"
#include "gs/engine/kind.hpp"

namespace gs::sketch {

namespace {

using namespace detail;
using Json = nlohmann::ordered_json;

void add(std::vector<MalformedConstraint> & list, MalformedConstraint m) {
    const auto it = std::ranges::lower_bound(list, m.constraint, {},
                                             &MalformedConstraint::constraint);
    if(it == list.end() || it->constraint != m.constraint)
        list.insert(it, std::move(m));
}

std::vector<int> constraints_of(const std::vector<MalformedConstraint> & l) {
    std::vector<int> out;
    for(const MalformedConstraint & m : l) out.push_back(m.constraint);
    return out;
}

// Why a row of `ci` whose gradient vanishes is Malformed, with the form
// that has a gradient there: dist(A, B) == 0 is sqrt at 0, whose
// derivative is 0; a distance tangency at a shared point is flat in every
// direction (plan 1.5).
std::string degenerate_why(const Instance & inst, const ConstraintInfo & ci) {
    std::string why =
        "its gradient vanishes, so no move of the drawing tells "
        "how to satisfy it";
    const KindMatch k = match_kind(ci.text, KindNames(inst.doc()));
    auto text = [&](TextSpan s) {
        return ci.text.substr(static_cast<std::size_t>(s.begin),
                              static_cast<std::size_t>(s.end - s.begin));
    };
    if(k.kind == ConstraintKind::Distance &&
       k.relation == KindRelation::Equal && k.operands.size() == 2 &&
       k.constants.size() == 1 &&
       Instance::Json::parse(text(k.constants[0]), nullptr, false) == 0)
        why += std::format(": write coincident({}, {})",
                           text(k.operands[0].span), text(k.operands[1].span));
    else if(k.kind == ConstraintKind::Tangent)
        why += ": at a shared point, write tangent_at";
    return why;
}

// Unmoved numbers keep their written form ("320cm", 0.34 rather than
// 0.34000000000000002): only a number that moved is rewritten.
bool moved(double now, double drawn) {
    return std::fabs(now - drawn) > 1e-12 * std::max(1.0, std::fabs(now));
}

std::vector<std::string> write_back(Instance & inst, const Model & m,
                                    std::span<const double> q0,
                                    std::span<const double> q) {
    std::vector<std::string> written;
    for(WrittenValue & v : written_values(inst, m, q0, q)) {
        std::string err;
        if(!inst.set(v.path, std::move(v.value), &err))
            throw std::logic_error("writing " + v.path + ": " + err);
        written.push_back(std::move(v.path));
    }
    return written;
}

// Per row: how far its value at q0 moved since the previous commit's
// model, a typed dimension's new value less its old one; 0 for a row that
// has no match there.
std::vector<double> dimension_moves(Work & w, const Model * previous,
                                    std::span<const double> q0,
                                    std::span<const int> kept) {
    std::vector<double> delta(uz(w.count()), 0.0);
    if(!previous) return delta;
    const std::vector<SketchCoord> & a = previous->sketch_coords();
    const std::vector<SketchCoord> & b = w.model.sketch_coords();
    if(a.size() != b.size() ||
       !std::ranges::equal(a, b, {}, &SketchCoord::name, &SketchCoord::name))
        return delta;
    RowEvaluator rows(*previous);
    std::vector<double> rp(rows.rows().size());
    rows.values(previous->initial_x(), q0, rp);
    std::map<std::pair<std::string, int>, double> old;
    for(std::size_t i = 0; i < rp.size(); ++i) {
        const RowRef ref = rows.rows()[i];
        const RowGroup & g = previous->groups()[uz(ref.group)];
        if(g.role == GroupRole::Solve) old[{g.name, ref.split}] = rp[i];
    }
    std::vector<double> r;
    w.values(q0, r);
    for(const int row : kept) {
        const auto it =
            old.find({w.group(row).name, w.rows.rows()[uz(row)].split});
        // A row the drawing already holds is not walked from its old value:
        // the walk would take the drawing off it and back. A tangent_at
        // flipped with its segment mirrored onto the new side, in one batch,
        // reads pi in the old model.
        if(it != old.end() && std::isfinite(it->second) &&
           !(std::fabs(r[uz(row)]) <= w.options.tol))
            delta[uz(row)] = r[uz(row)] - it->second;
    }
    return delta;
}

// The continuation steps of a component: a row moved by more than 0.05 L_c
// (10 degrees for an angle row) per step is split, up to 10 steps (plan
// 3.4). So is a length dimension moved by more than 5 % of what it measures
// at q0: L_c is the size of the whole component, and a linkage tied to a
// 5 m wall, stepped by 0.2 m, folds over.
int steps(Work & w, const Comp & c, const std::vector<double> & delta,
          std::span<const double> q0) {
    double k = 1.0;
    std::vector<int> moved;
    for(std::size_t i = 0; i < c.rows.size(); ++i) {
        const int row = c.rows[i];
        const RowUnit unit =
            w.group(row).units[uz(w.rows.rows()[uz(row)].split)];
        const double d = delta[uz(row)];
        if(unit == RowUnit::Angle) {
            k = std::max(k, std::fabs(d) / (std::numbers::pi / 18.0));
            continue;
        }
        k = std::max(k, std::fabs(c.weight[i] * d) / (0.05 * c.length));
        if(d != 0.0) moved.push_back(row);
    }
    if(!moved.empty()) {
        const std::vector<double> size =
            dimensions(w, moved, q0, c.length, false);
        for(std::size_t i = 0; i < moved.size(); ++i)
            if(size[i] > 0.0)
                k = std::max(k,
                             std::fabs(delta[uz(moved[i])]) / (0.05 * size[i]));
    }
    return static_cast<int>(std::min(10.0, std::ceil(k)));
}

// One more solve of a component that stopped off its set, from its start
// nudged by a seeded 1e-3 L_c per column (plan 3.4). A consistent set
// drawn flat or symmetric, a dyad on the line through its ends, stops at a
// saddle whose first-order data are a least-squares point's: A^T f = 0,
// the rows dependent, f along their left null vector. Only a second stop
// is a conflict. q takes the nudged stop when it converges, or when its
// merit is lower: a conflict's group is read at the lower stop, the nearer
// the least-squares point, where the residual follows the conflict.
double merit(Work & w, const Comp & c, std::span<const double> q) {
    std::vector<double> r;
    w.values(q, r);
    double phi = 0.0;
    for(std::size_t k = 0; k < c.rows.size(); ++k) {
        const double v = c.weight[k] * r[uz(c.rows[k])];
        phi += v * v;
    }
    return phi;
}

Outcome nudged(Work & w, const Comp & c, std::span<const double> start,
               std::vector<double> & q) {
    std::mt19937 rng(0x5eedu + static_cast<unsigned>(c.columns.front()));
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    Eigen::VectorXd d(static_cast<Eigen::Index>(c.columns.size()));
    for(Eigen::Index j = 0; j < d.size(); ++j) d[j] = 1e-3 * c.length * u(rng);
    for(const double sign : {1.0, -1.0}) {
        std::vector<double> q2;
        move(c, start, sign * d, q2);
        if(!admissible(w, c, q2)) continue;
        const Outcome out = solve(w, c, q2, {});
        if(out == Outcome::Converged || merit(w, c, q2) < merit(w, c, q))
            for(const int col : c.columns) q[uz(col)] = q2[uz(col)];
        return out == Outcome::Converged ? out : Outcome::Inconsistent;
    }
    return Outcome::Inconsistent;
}

// Whether `row` of c, flat at q where c converged, is as flat at a seeded
// point 1e-3 L_c from q in its columns: then no drawing changes it, as a
// world length between two constants of one body, which its pose only
// moves rigidly, and it is satisfied, not Malformed. Each column takes its
// own amount: one shift for all keeps A = B, and dist(A, B) == 0 would
// pass for constant.
bool constant_row(Work & w, const Comp & c, std::span<const double> q,
                  int row) {
    auto at = [&](int col) {
        const auto it = std::ranges::lower_bound(c.columns, col);
        return it != c.columns.end() && *it == col
                   ? static_cast<Eigen::Index>(it - c.columns.begin())
                   : Eigen::Index{-1};
    };
    std::mt19937 rng(0x5eedu + static_cast<unsigned>(row));
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    Eigen::VectorXd d = Eigen::VectorXd::Zero(std::ssize(c.columns));
    for(const int col : w.rows.columns(row))
        if(const Eigen::Index j = at(col); j >= 0)
            d[j] = 1e-3 * c.length * u(rng);
    std::vector<double> q2;
    move(c, q, d, q2);
    const int list[] = {row};
    std::vector<double> r;
    std::vector<JacobianEntry> J;
    w.jacobian(q2, list, r, J);
    const double weight = c.weight[uz(static_cast<int>(
        std::ranges::lower_bound(c.rows, row) - c.rows.begin()))];
    // In scaled columns, as degenerate_rows() measures it: measured in
    // those of q, a pose row could be flat here and not there.
    Eigen::VectorXd g = Eigen::VectorXd::Zero(d.size());
    const std::vector<Eigen::Matrix3d> T = c.scaling.transforms(q2);
    for(const JacobianEntry & e : J)
        if(const Eigen::Index j = at(e.column); j >= 0)
            c.scaling.scatter(T, static_cast<int>(j), weight * e.value,
                              [&](int p, double v) { g[p] += v; });
    return std::isfinite(r[0]) && g.norm() < w.options.degenerate;
}

// The first candidate group of a conflict (Conflict::candidates) whose
// latest row's constraint, left out, lets the rest of c converge from the
// drawn values; empty when none does. Without the test, a near-dependent
// row that only stood out where the solve stopped would be left out, and
// the drawing written would break it for no reason.
std::vector<int> tested(Work & w, const Comp & c, std::span<const double> q0,
                        const std::vector<std::vector<int>> & candidates) {
    for(const std::vector<int> & group : candidates) {
        Comp rest = c;
        const int constraint = w.constraint[uz(group.back())];
        std::vector<int> rows;
        for(const int row : c.rows)
            if(w.constraint[uz(row)] == constraint) rows.push_back(row);
        erase_rows(rest, rows);
        std::vector<double> q(q0.begin(), q0.end());
        const std::vector<double> start = q;
        Outcome out = solve(w, rest, q, {});
        if(out == Outcome::Inconsistent) out = nudged(w, rest, start, q);
        if(out == Outcome::Converged) return group;
    }
    return {};
}

// The window form of the arcs' angle relations (CompileOptions::
// arc_windows), compiled at most once per settle, at the first component
// that asks for it. A component whose start_angle(k) == theta or
// end_angle(k) == theta stopped short is solved once more with it, from the
// drawn values: the wrapped row's cut, 180 degrees from theta, can lie
// between the drawing and the target, and its step then runs into the span
// guard. It runs without the continuation: dimension_moves() reads the
// wrapped row's difference, whose sign points the wrong way there.
class Windows {
public:
    Windows(const Instance & instance, const SettleOptions & so)
        : instance_(instance), so_(so) {}

    static bool wanted(Work & w, const Comp & c) {
        return std::ranges::any_of(
            c.rows, [&](int row) { return w.group(row).arc_window; });
    }
    // A Work over the window model; null when it does not compile, or when
    // its rows do not line up with w's, which a Comp of w indexes.
    Work * work(const Work & w) {
        if(tried_) return work_ ? &*work_ : nullptr;
        tried_ = true;
        CompileOptions co;
        co.sketch = SketchBinding::Free;
        co.row_plans = true;
        co.isolate_items = so_.isolate_items;
        co.arc_windows = true;
        CompileResult cr = compile(instance_, co);
        if(!cr.ok()) return nullptr;
        model_ = std::move(cr.model);
        work_.emplace(*model_, so_.solver);
        const auto same = [](const RowRef & a, const RowRef & b) {
            return a.group == b.group && a.split == b.split;
        };
        if(!std::ranges::equal(work_->rows.rows(), w.rows.rows(), same) ||
           work_->solve != w.solve)
            work_.reset();
        return work_ ? &*work_ : nullptr;
    }

private:
    const Instance & instance_;
    const SettleOptions & so_;
    bool tried_ = false;
    std::shared_ptr<const Model> model_;
    std::optional<Work> work_;
};

// One solve of every component from the drawn values, the constraints of
// `left` left out (plan 3.4).
struct Pass {
    std::vector<MalformedConstraint> malformed;
    std::vector<int> held;
    std::vector<Comp> comps;
    std::vector<Outcome> outcomes;  // per component
    std::vector<double> q;
};

Pass solve_pass(Work & w, const Instance & instance, const SettleOptions & so,
                Windows & windows, const Frozen & frozen,
                std::span<const double> q0, std::span<const double> r0,
                const std::vector<MalformedConstraint> & start,
                std::span<const int> left, std::span<const char> wanted) {
    using R = MalformedConstraint::Reason;
    const Model & m = w.model;
    Pass p;
    p.malformed = start;
    std::vector<int> excluded = constraints_of(p.malformed);
    excluded.insert(excluded.end(), left.begin(), left.end());
    std::vector<int> kept = without(w, w.solve, excluded);
    {
        std::vector<double> r;
        std::vector<JacobianEntry> J;
        w.jacobian(q0, kept, r, J);
        p.held = gauge_holds(w, q0, kept, J, {});
    }
    auto degenerate = [&](int row) {
        const int c = w.constraint[uz(row)];
        add(p.malformed, {c, R::Degenerate,
                          degenerate_why(instance, m.constraints()[uz(c)])});
    };
    // A row whose columns are all held never changes: Malformed when it is
    // off its value, else satisfied, as a row with no column is.
    for(const int row : held_only(w, kept, p.held))
        if(!(std::fabs(r0[uz(row)]) <= so.solver.tol)) degenerate(row);
    kept = without(w, kept, constraints_of(p.malformed));
    const std::vector<double> delta = dimension_moves(w, so.previous, q0, kept);
    // A row off its value with no gradient, at the drawn values or where
    // its component's solve stopped, cannot be moved onto it: Malformed,
    // and every component is solved again from the drawn values without
    // it. Tested only once a solve has failed, the nudged one too: the
    // solve gives a flat row a gradient when it moves its points apart, as
    // dist(A, B) == 0.5 with B drawn at A.
    for(;;) {
        kept = without(w, kept, constraints_of(p.malformed));
        p.comps = components(w, kept, p.held, frozen, wanted);
        p.q.assign(q0.begin(), q0.end());
        p.outcomes.clear();
        for(const Comp & c : p.comps) {
            const int K = steps(w, c, delta, q0);
            std::vector<double> shift(c.rows.size());
            for(int i = 1; i < K; ++i) {
                for(std::size_t k = 0; k < c.rows.size(); ++k)
                    shift[k] = (1.0 - static_cast<double>(i) / K) *
                               delta[uz(c.rows[k])];
                std::vector<double> qi = p.q;
                if(solve(w, c, qi, shift) != Outcome::Converged) break;
                p.q = std::move(qi);
            }
            const std::vector<double> before = p.q;
            Outcome out = solve(w, c, p.q, {});
            if(out != Outcome::Converged && Windows::wanted(w, c))
                if(Work * ww = windows.work(w)) {
                    std::vector<double> qw = p.q;
                    for(const int col : c.columns) qw[uz(col)] = q0[uz(col)];
                    if(solve(*ww, c, qw, {}) == Outcome::Converged) {
                        p.q = std::move(qw);
                        out = Outcome::Converged;
                    }
                }
            if(out == Outcome::Inconsistent) out = nudged(w, c, before, p.q);
            p.outcomes.push_back(out);
        }
        std::vector<double> r;
        w.values(p.q, r);
        bool flat = false;
        auto test = [&](const Comp & c, std::span<const double> q,
                        std::span<const double> rq) {
            for(const int row : degenerate_rows(w, c, q)) {
                const auto k = std::ranges::find(c.rows, row) - c.rows.begin();
                if(!(std::fabs(c.weight[uz(static_cast<int>(k))] *
                               rq[uz(row)]) <= so.solver.tol)) {
                    degenerate(row);
                    flat = true;
                }
            }
        };
        for(std::size_t i = 0; i < p.comps.size(); ++i) {
            if(p.outcomes[i] == Outcome::Converged) continue;
            test(p.comps[i], q0, r0);
            test(p.comps[i], p.q, r);
        }
        if(!flat) return p;
    }
}

}  // namespace

Settled settle(Instance & instance, const SettleOptions & so) {
    Settled out;
    out.options = so.solver;
    CompileOptions co;
    co.sketch = SketchBinding::Free;
    co.row_plans = true;
    co.isolate_items = so.isolate_items;
    CompileResult cr = compile(instance, co);
    out.diagnostics = std::move(cr.diagnostics);
    out.excluded = std::move(cr.excluded);
    if(!cr.ok()) return out;
    out.model = cr.model;
    const Model & m = *out.model;
    Work w(m, so.solver);
    const std::vector<double> q0 = m.initial_q();
    out.q = q0;

    std::vector<double> r0;
    w.values(q0, r0);
    const Frozen frozen = freeze(w, q0);
    const std::vector<MalformedConstraint> start =
        start_checks(w, w.solve, frozen, r0);
    // The conflict protocol (plan 3.4): each inconsistent component leaves
    // out the latest constraint of its conflict group, and every component
    // is solved again from the drawn values, so that what is written stays
    // a function of the document alone; 3 solves at most.
    // SettleOptions::only, per row index.
    std::vector<char> wanted;
    if(!so.only.empty()) {
        wanted.assign(w.rows.rows().size(), 0);
        for(std::size_t row = 0; row < wanted.size(); ++row)
            wanted[row] =
                w.constraint[row] >= 0 &&
                std::ranges::contains(
                    so.only, m.constraints()[uz(w.constraint[row])].path);
    }
    std::vector<int> left;
    Pass p;
    Windows windows(instance, so);
    for(int pass = 0;; ++pass) {
        p = solve_pass(w, instance, so, windows, frozen, q0, r0, start, left,
                       wanted);
        const bool failed =
            std::ranges::contains(p.outcomes, Outcome::NotConverged);
        const bool inconsistent =
            std::ranges::contains(p.outcomes, Outcome::Inconsistent);
        if(failed || !inconsistent) {
            if(failed) out.status = Status::NotConverged;
            break;
        }
        bool shown = true;
        for(std::size_t i = 0; i < p.comps.size(); ++i) {
            if(p.outcomes[i] != Outcome::Inconsistent) continue;
            Conflict cf = conflict(w, p.comps[i], p.q);
            if(cf.group.empty())
                cf.group = tested(w, p.comps[i], q0, cf.candidates);
            if(cf.group.empty()) {
                shown = false;
                continue;
            }
            std::vector<int> group;
            for(const int row : cf.group)
                if(!std::ranges::contains(group, w.constraint[uz(row)]))
                    group.push_back(w.constraint[uz(row)]);
            std::ranges::sort(group);
            out.conflict_groups.push_back(group);
            left.push_back(w.constraint[uz(cf.group.back())]);
        }
        if(!shown) {
            out.status = Status::NotConverged;
            break;
        }
        if(so.solver.conflicts == Options::Conflicts::Refuse || pass == 2) {
            out.status = Status::Conflicting;
            break;
        }
    }
    out.malformed = std::move(p.malformed);
    out.held = std::move(p.held);
    std::vector<Comp> & comps = p.comps;
    if(out.status == Status::Conflicting ||
       out.status == Status::NotConverged) {
        for(const Comp & c : comps)
            out.components.push_back(public_component(w, c));
        out.residual = residual(w, comps, q0);
        return out;
    }
    std::ranges::sort(left);
    out.conflicting = left;
    out.converged = true;
    std::vector<double> & q = p.q;

    // Malformed too: a row whose gradient vanishes at the solution (plan
    // 3.5), which a rank would otherwise call dependent. A constant row
    // leaves the rank too: a diagnosis drops it as degenerate.
    for(Comp & c : comps) {
        const std::vector<int> rows = degenerate_rows(w, c, q);
        for(const int row : rows) {
            if(constant_row(w, c, q, row)) continue;
            const int ci = w.constraint[uz(row)];
            add(out.malformed,
                {ci, MalformedConstraint::Reason::Degenerate,
                 degenerate_why(instance, m.constraints()[uz(ci)])});
        }
        erase_rows(c, rows);
    }
    // Per component: ranking only when every component is small would let
    // one large component hide the rank of a new small one, and
    // try_constrain (plan 4.4) would then drop every suggestion.
    std::vector<CompRank> ranks(comps.size());
    std::vector<bool> ranked(comps.size(), false);
    for(std::size_t i = 0; i < comps.size(); ++i) {
        if(!comps[i].dense(so.solver)) continue;
        std::vector<double> qp = q;
        if(!polish(w, comps[i], qp)) continue;
        ranks[i] = rank(w, comps[i], qp);
        ranked[i] = true;
    }
    // No component at all is a complete rank: its DOFs are the free columns.
    if(comps.empty() || std::ranges::contains(ranked, true))
        out.rank = collect(w, comps, ranks, ranked, out.held, out.held.size());
    // The components left alone have DOFs too.
    if(out.rank && !so.only.empty()) out.rank->dof.reset();
    if(!so.dry_run) out.written = write_back(instance, m, q0, q);
    out.q = q;
    out.residual =
        residual(w, comps, so.dry_run ? std::span(q0) : std::span(q));
    for(const Comp & c : comps)
        out.components.push_back(public_component(w, c));
    out.status = !out.conflicting.empty() ? Status::Conflicting
                 : !out.malformed.empty() ? Status::Malformed
                 : out.rank               ? rank_status(w, *out.rank)
                                          : Status::Solved;
    return out;
}

std::vector<WrittenValue> written_values(const Instance & inst, const Model & m,
                                         std::span<const double> q0,
                                         std::span<const double> q) {
    const std::vector<SketchCoord> & coords = m.sketch_coords();
    if(q0.size() != coords.size() || q.size() != coords.size())
        throw std::invalid_argument(
            "written_values: q0 and q need one value per column");
    std::vector<WrittenValue> out;
    // Angles are written as solved, in the turn they were drawn in. Wrapped
    // into (-pi, pi] here, they would flip what reads them unwrapped:
    // `end_angle(door) >= 250deg`, held at 255 degrees, would read -105.
    for(std::size_t c = 0; c < coords.size(); c += uz(coords[c].dims)) {
        const SketchCoord & sc = coords[c];
        const std::size_t dims = uz(sc.dims);
        if(c + dims > coords.size() ||
           (dims == 2 && coords[c + 1].path != sc.path))
            throw std::logic_error("the columns of " + sc.path +
                                   " are not consecutive");
        bool any = false;
        for(std::size_t k = 0; k < dims; ++k) any |= moved(q[c + k], q0[c + k]);
        if(!any) continue;
        Json v;
        if(dims == 2) {
            const Json * old = inst.get(sc.path);
            v = Json::array();
            for(std::size_t k = 0; k < 2; ++k)
                v.push_back(!moved(q[c + k], q0[c + k]) && old &&
                                    old->is_array() && old->size() == 2
                                ? (*old)[k]
                                : Json(q[c + k]));
        } else {
            v = q[c];
        }
        out.push_back({sc.path, std::move(v)});
    }
    return out;
}

std::vector<int> left_out(const Settled & settled) {
    std::vector<int> out = constraints_of(settled.malformed);
    out.insert(out.end(), settled.conflicting.begin(),
               settled.conflicting.end());
    std::ranges::sort(out);
    return out;
}

}  // namespace gs::sketch

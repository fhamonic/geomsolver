#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <map>
#include <numbers>
#include <stdexcept>

#include "core.hpp"
#include "gs/engine/kind.hpp"

namespace gs::sketch {

namespace {

using namespace detail;
using Eigen::Index;

// Point `vertex` of a value plan: numbers 2 vertex and 2 vertex + 1.
struct PointRef {
    int plan = -1;
    int vertex = 0;
};

// A sign a frame must keep (plan 3.7): the orientation of the triangle
// P, Q, R of two distance rows that share Q (a dyad), or the sign of the
// pair of an unsigned parallel (the dot of the directions) or
// perpendicular (their cross product).
struct Orientation {
    enum class Kind : std::uint8_t { Triangle, Parallel, Perpendicular };
    Kind kind = Kind::Triangle;
    // A triangle's P, Q, R; a pair's first segment's ends, then the
    // second's.
    std::array<PointRef, 4> p{};
    double sign = 0.0;
};

int element_index(const Model & m, const std::string & name) {
    const auto it = std::ranges::find(m.sketch(), name, &SketchInfo::name);
    return it == m.sketch().end() ? -1
                                  : static_cast<int>(it - m.sketch().begin());
}

}  // namespace

struct Drag::Impl {
    explicit Impl(std::shared_ptr<const SystemData> d)
        : data(std::move(d)), w(*data->model, data->options) {}

    std::shared_ptr<const SystemData> data;  // keeps the model alive
    Work w;
    std::vector<Handle> handles;
    // The dragged components' rows and columns as one, with the handles'
    // own columns in no component.
    Comp comp;
    std::vector<int> held;
    std::vector<Vec2d> start;
    // The largest L_c of the components dragged; 0 without a row.
    double length = 0.0;
    Damped damped;
    std::vector<Orientation> guards;
    std::vector<int> plans;  // every plan the guards read, ascending

    int coordinates() const {
        int k = 0;
        for(const Handle & h : handles)
            k += h.kind == Handle::Kind::Columns
                     ? static_cast<int>(h.columns.size())
                     : 2;
        return k;
    }
    // The handles' values at q, flat, and their Jacobian in the scaled
    // columns of comp (one row per coordinate).
    void handle_values(std::span<const double> q, Eigen::VectorXd & h,
                       Eigen::MatrixXd * J);
    // World positions of the guards' points at q, per plan.
    std::map<int, std::vector<double>> positions(std::span<const double> q);
    double orientation(const Orientation & o,
                       const std::map<int, std::vector<double>> & at) const;
    bool keeps_orientations(std::span<const double> q);
    // One step of the priority solve towards the handle values `target`
    // from q into `next`; false when its corrections did not converge.
    bool step(std::span<const double> q, const Eigen::VectorXd & target,
              std::vector<double> & next);
    void working_set(std::span<const int> kept, std::span<const double> q);
    void record_orientations(std::span<const double> q);
};

namespace {

int column_index(const Comp & c, int column) {
    const auto it = std::ranges::lower_bound(c.columns, column);
    return it != c.columns.end() && *it == column
               ? static_cast<int>(it - c.columns.begin())
               : -1;
}

bool is_angle(const Model & m, int column) {
    return m.sketch_coords()[uz(column)].part == SketchPart::Angle;
}

}  // namespace

void Drag::Impl::handle_values(std::span<const double> q, Eigen::VectorXd & h,
                               Eigen::MatrixXd * J) {
    const Index n = static_cast<Index>(comp.columns.size());
    h.resize(coordinates());
    if(J) J->setZero(coordinates(), n);
    std::vector<double> numbers;
    std::vector<JacobianEntry> entries;
    std::vector<Eigen::Matrix3d> T;
    if(J) T = comp.scaling.transforms(q);
    // Adds a, a derivative in column `col` of q, into row `row` of J, in
    // scaled columns.
    auto put = [&](Index row, int col, double a) {
        const int j = column_index(comp, col);
        if(j >= 0)
            comp.scaling.scatter(T, j, a,
                                 [&](int p, double v) { (*J)(row, p) += v; });
    };
    // Adds d (number `k` of a plan) / d q into row `row` of J, times a.
    auto add = [&](Index row, int k, double a) {
        for(const JacobianEntry & e : entries)
            if(e.row == k) put(row, e.column, a * e.value);
    };
    Index r = 0;
    for(const Handle & hd : handles) {
        switch(hd.kind) {
            case Handle::Kind::Columns:
                for(const int col : hd.columns) {
                    h[r] = q[uz(col)];
                    if(J) put(r, col, 1.0);
                    ++r;
                }
                break;
            case Handle::Kind::WorldPoint: {
                entries.clear();
                const int plan = w.rows.value_plan(hd.value);
                if(J)
                    w.rows.value_jacobian(w.x, q, plan, numbers, entries);
                else
                    w.rows.value(w.x, q, plan, numbers);
                const int k = 2 * hd.vertex;
                h[r] = numbers[uz(k)];
                h[r + 1] = numbers[uz(k + 1)];
                if(J) {
                    add(r, k, 1.0);
                    add(r + 1, k + 1, 1.0);
                }
                r += 2;
                break;
            }
            case Handle::Kind::BodyPoint: {
                const SketchBody & b = w.model.sketch_bodies()[uz(hd.body)];
                std::vector<double> o, phi;
                std::vector<JacobianEntry> eo, ephi;
                const int po = w.rows.value_plan(b.origin);
                const int pa = w.rows.value_plan(b.angle);
                w.rows.value_jacobian(w.x, q, po, o, eo);
                w.rows.value_jacobian(w.x, q, pa, phi, ephi);
                const double c = std::cos(phi[0]), s = std::sin(phi[0]);
                const Vec2d l = hd.local;
                h[r] = o[0] + c * l.x - s * l.y;
                h[r + 1] = o[1] + s * l.x + c * l.y;
                if(J) {
                    entries = eo;
                    add(r, 0, 1.0);
                    add(r + 1, 1, 1.0);
                    // d (R(phi) l) / d phi = (-s lx - c ly, c lx - s ly).
                    entries = ephi;
                    add(r, 0, -s * l.x - c * l.y);
                    add(r + 1, 0, c * l.x - s * l.y);
                }
                r += 2;
                break;
            }
        }
    }
}

std::map<int, std::vector<double>> Drag::Impl::positions(
    std::span<const double> q) {
    std::map<int, std::vector<double>> at;
    for(const int plan : plans) w.rows.value(w.x, q, plan, at[plan]);
    return at;
}

double Drag::Impl::orientation(
    const Orientation & o,
    const std::map<int, std::vector<double>> & at) const {
    auto pt = [&](const PointRef & p) {
        const std::vector<double> & v = at.at(p.plan);
        return Vec2d{v[uz(2 * p.vertex)], v[uz(2 * p.vertex + 1)]};
    };
    const Vec2d a = pt(o.p[0]), b = pt(o.p[1]), c = pt(o.p[2]);
    if(o.kind == Orientation::Kind::Triangle) {
        const double ux = b.x - a.x, uy = b.y - a.y;
        const double vx = c.x - b.x, vy = c.y - b.y;
        const double cross = ux * vy - uy * vx;
        return std::fabs(cross) > 1e-9 * std::hypot(ux, uy) * std::hypot(vx, vy)
                   ? (cross > 0.0 ? 1.0 : -1.0)
                   : 0.0;
    }
    const Vec2d d = pt(o.p[3]);
    const double ux = b.x - a.x, uy = b.y - a.y;
    const double vx = d.x - c.x, vy = d.y - c.y;
    const double v = o.kind == Orientation::Kind::Parallel ? ux * vx + uy * vy
                                                           : ux * vy - uy * vx;
    return std::fabs(v) > 1e-9 * std::hypot(ux, uy) * std::hypot(vx, vy)
               ? (v > 0.0 ? 1.0 : -1.0)
               : 0.0;
}

bool Drag::Impl::keeps_orientations(std::span<const double> q) {
    if(guards.empty()) return true;
    const std::map<int, std::vector<double>> at = positions(q);
    return std::ranges::none_of(guards, [&](const Orientation & o) {
        return orientation(o, at) * o.sign < 0.0;
    });
}

bool Drag::Impl::step(std::span<const double> q, const Eigen::VectorXd & target,
                      std::vector<double> & next) {
    const Options & opt = w.options;
    const Index n = static_cast<Index>(comp.columns.size());
    Linear L;
    Eigen::VectorXd d0 = Eigen::VectorXd::Zero(n);
    const bool rows = !comp.rows.empty();
    if(rows) {
        linearise_sparse(w, comp, q, L);
        d0 = damped.step(L.sparse, L.f);
    }
    Eigen::VectorXd h;
    Eigen::MatrixXd Jh;
    handle_values(q, h, &Jh);
    // G = P J_h^T, P the projector onto the rows' null space, with the
    // frame's factorisation.
    Eigen::MatrixXd G = Jh.transpose();
    if(rows)
        for(Index i = 0; i < G.cols(); ++i)
            G.col(i) -= L.sparse.transpose() *
                        damped.solve(L.sparse * Eigen::VectorXd(G.col(i)));
    Eigen::MatrixXd GtG = G.transpose() * G;
    const double mu = 1e-6 * GtG.trace();
    Eigen::VectorXd e = target - h;
    for(Index r = 0, i = 0; i < static_cast<Index>(handles.size()); ++i) {
        const Handle & hd = handles[uz(static_cast<int>(i))];
        if(hd.kind != Handle::Kind::Columns) {
            r += 2;
            continue;
        }
        for(const int col : hd.columns) {
            // A turn's target on the current turn: the rotate handle
            // crosses -x without a jump of 2 pi.
            if(is_angle(w.model, col))
                e[r] = std::remainder(e[r], 2.0 * std::numbers::pi);
            ++r;
        }
    }
    Eigen::VectorXd y = Eigen::VectorXd::Zero(G.cols());
    // A handle direction the rows determine projects to rounding noise, and
    // mu, a share of tr(G^T G), shrinks with it: solved for, it would push
    // every free column by the step's cap on each frame. Such a direction
    // is told by its projection against the handle's own, unprojected one.
    const Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eig(GtG);
    const double floor = 1e-16 * Jh.squaredNorm();
    const Eigen::VectorXd & lambda = eig.eigenvalues();
    if((lambda.array() > floor).all()) {
        if(mu > 0.0) {
            GtG.diagonal().array() += mu;
            y = GtG.ldlt().solve(e - Jh * d0);
        }
    } else {
        const Eigen::MatrixXd & V = eig.eigenvectors();
        const Eigen::VectorXd b = V.transpose() * (e - Jh * d0);
        const double damp =
            1e-6 * (lambda.array() > floor).select(lambda, 0.0).sum();
        for(Index i = 0; i < lambda.size(); ++i)
            if(lambda[i] > floor) y += V.col(i) * (b[i] / (lambda[i] + damp));
    }
    Eigen::VectorXd d = d0 + G * y;
    // Capped in the largest scaled column: a near-singular handle direction
    // would otherwise throw a column far, while a rigid move of many
    // columns, each by the step, is no larger here than one column's.
    if(length > 0.0) {
        const double big = d.lpNorm<Eigen::Infinity>();
        if(big > 0.1 * length) d *= 0.1 * length / big;
    }
    detail::move(comp, q, d, next);
    if(!admissible(w, comp, next)) return false;
    if(!rows) return true;
    std::vector<double> r;
    for(int it = 0; it < 10; ++it) {
        w.values(next, r);
        if(merit_inf(comp, r, {}) <= opt.tol) return true;
        linearise_sparse(w, comp, next, L);
        std::vector<double> trial;
        detail::move(comp, next, damped.step(L.sparse, L.f), trial);
        if(!admissible(w, comp, trial)) return false;
        next = std::move(trial);
    }
    w.values(next, r);
    return merit_inf(comp, r, {}) <= opt.tol;
}

Drag::Drag(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Drag::~Drag() = default;
Drag::Drag(Drag &&) noexcept = default;
Drag & Drag::operator=(Drag &&) noexcept = default;

std::span<const int> Drag::free_columns() const { return impl_->comp.columns; }
std::span<const Vec2d> Drag::start() const { return impl_->start; }
std::span<const int> Drag::held() const { return impl_->held; }

bool Drag::move(std::span<const Vec2d> targets, std::span<double> q) {
    Impl & D = *impl_;
    if(targets.size() != D.handles.size())
        throw std::invalid_argument(std::format(
            "{} targets for {} handles", targets.size(), D.handles.size()));
    const std::size_t columns = D.w.model.sketch_coords().size();
    if(q.size() != columns)
        throw std::invalid_argument(std::format(
            "q has {} columns, the model has {}", q.size(), columns));
    // A handle with nothing to move, the gizmo of a fixed body.
    if(D.coordinates() == 0) return true;
    Eigen::VectorXd goal(D.coordinates());
    for(Index r = 0, i = 0; i < static_cast<Index>(targets.size()); ++i) {
        const Handle & h = D.handles[uz(static_cast<int>(i))];
        const Vec2d t = targets[uz(static_cast<int>(i))];
        if(h.kind != Handle::Kind::Columns) {
            goal[r++] = t.x;
            goal[r++] = t.y;
        } else if(!h.columns.empty()) {
            goal[r++] = t.x;
            if(h.columns.size() == 2) goal[r++] = t.y;
        }
    }
    Eigen::VectorXd now;
    D.handle_values(q, now, nullptr);
    Eigen::VectorXd jump = goal - now;
    // The jump in scaled units: a column handle's by how far its column
    // alone moves the drawing, a turn by its body's points.
    Eigen::VectorXd scaled = jump;
    for(Index r = 0, i = 0; i < static_cast<Index>(D.handles.size()); ++i) {
        const Handle & h = D.handles[uz(static_cast<int>(i))];
        if(h.kind != Handle::Kind::Columns) {
            r += 2;
            continue;
        }
        for(const int col : h.columns) {
            if(is_angle(D.w.model, col))
                jump[r] = std::remainder(jump[r], 2.0 * std::numbers::pi);
            const int j = column_index(D.comp, col);
            scaled[r] = j >= 0 ? jump[r] * D.comp.scaling.unit(j) : jump[r];
            ++r;
        }
    }
    // Continuation: a jump of more than 0.05 L_c is split, up to 10 steps.
    int K = 1;
    if(D.length > 0.0)
        K = static_cast<int>(std::clamp(
            std::ceil(scaled.lpNorm<Eigen::Infinity>() / (0.05 * D.length)),
            1.0, 10.0));
    std::vector<double> current(q.begin(), q.end()), next;
    for(int s = 1; s <= K; ++s) {
        const Eigen::VectorXd target =
            now + (static_cast<double>(s) / K) * jump;
        if(!D.step(current, target, next) || !D.keeps_orientations(next)) break;
        current = next;
        if(s == K) {
            std::ranges::copy(current, q.begin());
            return true;
        }
    }
    std::ranges::copy(current, q.begin());
    return false;
}

namespace {

// The columns a handle reads.
std::vector<int> handle_columns(Work & w, const Handle & h) {
    std::vector<int> out;
    auto plan_columns = [&](ExprRef ref) {
        const int plan = w.rows.value_plan(ref);
        const std::span<const int> c = w.rows.value_columns(plan);
        out.insert(out.end(), c.begin(), c.end());
    };
    switch(h.kind) {
        case Handle::Kind::Columns:
            out = h.columns;
            break;
        case Handle::Kind::WorldPoint:
            plan_columns(h.value);
            break;
        case Handle::Kind::BodyPoint: {
            const SketchBody & b = w.model.sketch_bodies()[uz(h.body)];
            plan_columns(b.origin);
            plan_columns(b.angle);
            break;
        }
    }
    std::ranges::sort(out);
    const auto [last, end] = std::ranges::unique(out);
    out.erase(last, end);
    return out;
}

void check_handle(Work & w, const Handle & h) {
    const Model & m = w.model;
    switch(h.kind) {
        case Handle::Kind::Columns:
            if(h.columns.size() > 2)
                throw std::invalid_argument(
                    "a Columns handle moves one or two columns");
            for(const int c : h.columns)
                if(c < 0 || uz(c) >= m.sketch_coords().size())
                    throw std::invalid_argument(
                        std::format("column {} out of range (the model has "
                                    "{})",
                                    c, m.sketch_coords().size()));
            break;
        case Handle::Kind::WorldPoint: {
            const int plan = w.rows.value_plan(h.value);
            if(plan < 0)
                throw std::invalid_argument(
                    "a WorldPoint handle names no world value of the sketch");
            std::vector<double> v;
            w.rows.value(w.x, m.initial_q(), plan, v);
            if(h.vertex < 0 || uz(2 * h.vertex + 1) >= v.size())
                throw std::invalid_argument(
                    std::format("point {} out of range of a value of {} "
                                "numbers",
                                h.vertex, v.size()));
            break;
        }
        case Handle::Kind::BodyPoint:
            if(h.body < 0 || uz(h.body) >= m.sketch_bodies().size())
                throw std::invalid_argument(
                    std::format("body {} out of range (the model has {})",
                                h.body, m.sketch_bodies().size()));
            break;
    }
}

// The points of an operand a guard reads: one for a point, its two ends
// for a segment or an edge; none for what is neither.
std::vector<PointRef> operand_points(Work & w, const KindOperand & op,
                                     bool segment) {
    const Model & m = w.model;
    const int e = element_index(m, op.element);
    if(e < 0) return {};
    const SketchInfo & si = m.sketch()[uz(e)];
    const int plan = w.rows.value_plan(si.ref);
    if(plan < 0) return {};
    std::vector<double> v;
    w.rows.value(w.x, m.initial_q(), plan, v);
    const int n = static_cast<int>(v.size()) / 2;
    const bool closed =
        si.type == SketchType::Polygon || si.type == SketchType::Box;
    const bool chain = closed || si.type == SketchType::Polyline;
    if(segment) {
        if(op.part == ElementPart::Whole && si.type == SketchType::Segment)
            return {{plan, 0}, {plan, 1}};
        if(op.part == ElementPart::Edge && chain && op.index >= 0 &&
           (op.index + 1 < n || (closed && op.index < n)))
            return {{plan, op.index}, {plan, (op.index + 1) % n}};
        return {};
    }
    switch(op.part) {
        case ElementPart::Whole:
            if(si.type == SketchType::Point) return {{plan, 0}};
            return {};
        case ElementPart::Start:
            if(si.type == SketchType::Segment ||
               si.type == SketchType::Polyline)
                return {{plan, 0}};
            return {};
        case ElementPart::End:
            if(si.type == SketchType::Segment ||
               si.type == SketchType::Polyline)
                return {{plan, n - 1}};
            return {};
        case ElementPart::Center:
            if(si.type == SketchType::Circle) return {{plan, 0}};
            return {};
        case ElementPart::Vertex:
            if(chain && op.index >= 0 && op.index < n)
                return {{plan, op.index}};
            return {};
        default:
            return {};
    }
}

}  // namespace

// The orientations a drag of these rows records (plan 3.7): the unsigned
// two-argument parallel and perpendicular, and the triangles of distance
// rows that share a point. Only operands that are elements or their named
// parts count; a shared point is one at the same coordinates, to the bit.
// A triangle flat at the start, or a pair at a right or flat angle, has
// no sign to keep.
void Drag::Impl::record_orientations(std::span<const double> q) {
    using K = ConstraintKind;
    const Model & m = w.model;
    std::vector<int> constraints;
    for(const int row : comp.rows) constraints.push_back(w.constraint[uz(row)]);
    std::ranges::sort(constraints);
    const auto [cl, ce] = std::ranges::unique(constraints);
    constraints.erase(cl, ce);
    // Per distance row: its constraint and its ends.
    std::vector<std::pair<int, std::array<PointRef, 2>>> distances;
    std::vector<Orientation> pairs;
    for(const int c : constraints) {
        const KindMatch & km = w.kind(c);
        if((km.kind == K::Parallel || km.kind == K::Perpendicular) &&
           !km.side && km.operands.size() == 2) {
            const std::vector<PointRef> a =
                operand_points(w, km.operands[0], true);
            const std::vector<PointRef> b =
                operand_points(w, km.operands[1], true);
            if(a.size() == 2 && b.size() == 2)
                pairs.push_back({km.kind == K::Parallel
                                     ? Orientation::Kind::Parallel
                                     : Orientation::Kind::Perpendicular,
                                 {a[0], a[1], b[0], b[1]}});
        } else if(km.kind == K::Distance &&
                  km.relation == KindRelation::Equal &&
                  km.operands.size() == 2) {
            const std::vector<PointRef> a =
                operand_points(w, km.operands[0], false);
            const std::vector<PointRef> b =
                operand_points(w, km.operands[1], false);
            if(a.size() == 1 && b.size() == 1)
                distances.push_back({c, {a[0], b[0]}});
        } else if(km.kind == K::Length && km.relation == KindRelation::Equal &&
                  km.operands.size() == 1) {
            const std::vector<PointRef> s =
                operand_points(w, km.operands[0], true);
            if(s.size() == 2) distances.push_back({c, {s[0], s[1]}});
        }
    }
    for(const Orientation & o : pairs)
        for(const PointRef & p : o.p) plans.push_back(p.plan);
    for(const auto & d : distances)
        for(const PointRef & p : d.second) plans.push_back(p.plan);
    std::ranges::sort(plans);
    const auto [last, end] = std::ranges::unique(plans);
    plans.erase(last, end);
    const std::map<int, std::vector<double>> at = positions(q);
    auto same = [&](const PointRef & a, const PointRef & b) {
        const std::vector<double> & u = at.at(a.plan);
        const std::vector<double> & v = at.at(b.plan);
        return std::memcmp(&u[uz(2 * a.vertex)], &v[uz(2 * b.vertex)],
                           2 * sizeof(double)) == 0;
    };
    for(Orientation o : pairs) {
        o.sign = orientation(o, at);
        if(o.sign != 0.0) guards.push_back(o);
    }
    // A dyad's joint is a point only its two distance rows hold, as a
    // four-bar's: where other rows hold it too (a triangulated strip), the
    // triangle is no assembly mode, and a nearly flat one would stop a
    // drag that bends it through flat.
    std::vector<std::vector<int>> holders(m.sketch_coords().size());
    for(const int row : comp.rows)
        for(const int col : w.rows.columns(row))
            holders[uz(col)].push_back(w.constraint[uz(row)]);
    auto joint = [&](const PointRef & p, int c1, int c2) {
        return std::ranges::all_of(w.rows.value_columns(p.plan), [&](int col) {
            return std::ranges::all_of(
                holders[uz(col)], [&](int c) { return c == c1 || c == c2; });
        });
    };
    // The ends of the distance rows by their coordinates.
    std::map<std::pair<double, double>, std::vector<std::pair<int, int>>> ends;
    for(std::size_t i = 0; i < distances.size(); ++i)
        for(int a = 0; a < 2; ++a) {
            const PointRef & e = distances[i].second[uz(a)];
            const std::vector<double> & v = at.at(e.plan);
            ends[{v[uz(2 * e.vertex)], v[uz(2 * e.vertex + 1)]}].emplace_back(
                static_cast<int>(i), a);
        }
    for(const auto & [where, list] : ends)
        for(std::size_t i = 0; i < list.size(); ++i)
            for(std::size_t j = i + 1; j < list.size(); ++j) {
                const auto & [di, a] = list[i];
                const auto & [dj, b] = list[j];
                const auto & [ci, ei] = distances[uz(di)];
                const auto & [cj, ej] = distances[uz(dj)];
                const PointRef & shared = ei[uz(a)];
                const PointRef & p = ei[uz(1 - a)];
                const PointRef & r = ej[uz(1 - b)];
                if(same(p, r) || !joint(shared, ci, cj)) continue;
                Orientation o{Orientation::Kind::Triangle,
                              {p, shared, r, PointRef{}}};
                o.sign = orientation(o, at);
                if(o.sign != 0.0) guards.push_back(o);
            }
}

// The dragged rows and columns: the components of the kept rows over the
// free columns that hold a handle's column, merged, with the handles'
// columns in no component.
void Drag::Impl::working_set(std::span<const int> kept,
                             std::span<const double> q) {
    std::vector<int> rows(kept.begin(), kept.end());
    const std::vector<int> orphans = held_only(w, rows, held);
    std::erase_if(rows,
                  [&](int r) { return std::ranges::contains(orphans, r); });
    const Frozen frozen = freeze(w, q);
    const std::vector<Comp> comps = components(w, rows, held, frozen);
    std::vector<int> wanted;
    for(const Handle & h : handles)
        for(const int c : handle_columns(w, h))
            if(!std::ranges::binary_search(held, c)) wanted.push_back(c);
    Comp merged;
    std::vector<std::pair<int, double>> weighted;
    length = 0.0;
    for(const Comp & c : comps) {
        if(std::ranges::none_of(wanted, [&](int col) {
               return std::ranges::binary_search(c.columns, col);
           }))
            continue;
        merged.columns.insert(merged.columns.end(), c.columns.begin(),
                              c.columns.end());
        for(std::size_t k = 0; k < c.rows.size(); ++k)
            weighted.emplace_back(c.rows[k], c.weight[k]);
        length = std::max(length, c.length);
        merged.magnitude = std::max(merged.magnitude, c.magnitude);
    }
    merged.columns.insert(merged.columns.end(), wanted.begin(), wanted.end());
    std::ranges::sort(merged.columns);
    const auto [last, end] = std::ranges::unique(merged.columns);
    merged.columns.erase(last, end);
    std::ranges::sort(weighted);
    for(const auto & [row, weight] : weighted) {
        merged.rows.push_back(row);
        merged.weight.push_back(weight);
    }
    merged.length = std::max(1e-3, length);
    merged.scaling = scaling(w.model, merged.columns, frozen);
    comp = std::move(merged);
}

Drag System::begin_drag(std::span<const Handle> handles,
                        std::span<const int> held,
                        std::span<const int> fallback,
                        std::span<const double> x, std::span<const double> q,
                        std::span<const int> left_out) const {
    auto D = std::make_unique<Drag::Impl>(data_);
    Work & w = D->w;
    if(x.size() != w.x.size())
        throw std::invalid_argument(std::format(
            "x has {} coordinates, the model has {}", x.size(), w.x.size()));
    w.x.assign(x.begin(), x.end());
    D->handles.assign(handles.begin(), handles.end());
    for(const Handle & h : D->handles) check_handle(w, h);
    if(D->coordinates() > 5)
        throw std::invalid_argument(std::format(
            "{} handle coordinates: a drag holds at most 5", D->coordinates()));
    const std::vector<int> kept = kept_rows(w, q, left_out);
    std::vector<double> rk;
    std::vector<JacobianEntry> J;
    w.jacobian(q, kept, rk, J);
    // The held columns: the caller's, the free gauge parts of the bodies
    // they leave free, and the fallback unless the handles cannot follow
    // with it held.
    auto hold = [&](bool with_fallback) {
        std::vector<int> h(held.begin(), held.end());
        if(with_fallback) h.insert(h.end(), fallback.begin(), fallback.end());
        std::ranges::sort(h);
        const std::vector<int> gauge = gauge_holds(w, q, kept, J, h);
        h.insert(h.end(), gauge.begin(), gauge.end());
        std::ranges::sort(h);
        const auto [last, end] = std::ranges::unique(h);
        h.erase(last, end);
        D->held = std::move(h);
        D->working_set(kept, q);
    };
    hold(true);
    if(!fallback.empty() && D->coordinates() > 0) {
        Eigen::VectorXd h;
        Eigen::MatrixXd Jh;
        D->handle_values(q, h, &Jh);
        Eigen::MatrixXd G = Jh.transpose();
        if(!D->comp.rows.empty()) {
            Linear L;
            linearise_sparse(w, D->comp, q, L);
            D->damped.factor(L.sparse);
            for(Index i = 0; i < G.cols(); ++i)
                G.col(i) -=
                    L.sparse.transpose() *
                    D->damped.solve(L.sparse * Eigen::VectorXd(G.col(i)));
        }
        const Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eig(
            G.transpose() * G, Eigen::EigenvaluesOnly);
        if(!(eig.eigenvalues().minCoeff() > 1e-9)) {
            // The rows change with the fallback freed, and so does the
            // pattern the factorisation analysed.
            D->damped.reset();
            hold(false);
        }
    }
    Eigen::VectorXd h0;
    D->handle_values(q, h0, nullptr);
    for(Index r = 0, i = 0; i < static_cast<Index>(D->handles.size()); ++i) {
        const Handle & hd = D->handles[uz(static_cast<int>(i))];
        Vec2d s;
        if(hd.kind != Handle::Kind::Columns) {
            s = {h0[r], h0[r + 1]};
            r += 2;
        } else if(!hd.columns.empty()) {
            s.x = h0[r++];
            if(hd.columns.size() == 2) s.y = h0[r++];
        }
        D->start.push_back(s);
    }
    D->record_orientations(q);
    return Drag(std::move(D));
}

namespace {

// World mode holds every body's element columns, so that bodies are rigid;
// Edit body every pose and the element columns of the other bodies.
std::vector<int> scope_held(const Model & m, int edit) {
    std::vector<int> held;
    for(std::size_t b = 0; b < m.sketch_bodies().size(); ++b) {
        const SketchBody & sb = m.sketch_bodies()[b];
        if(edit < 0 || static_cast<int>(b) != edit)
            held.insert(held.end(), sb.local_columns.begin(),
                        sb.local_columns.end());
        if(edit >= 0)
            held.insert(held.end(), sb.columns.begin(), sb.columns.end());
    }
    std::ranges::sort(held);
    return held;
}

const SketchInfo & element_at(const Model & m, int element) {
    if(element < 0 || uz(element) >= m.sketch().size())
        throw std::invalid_argument(
            std::format("element {} out of range (the model has {})", element,
                        m.sketch().size()));
    return m.sketch()[uz(element)];
}

const SketchBody & body_at(const Model & m, int body) {
    if(body < 0 || uz(body) >= m.sketch_bodies().size())
        throw std::invalid_argument(
            std::format("body {} out of range (the model has {})", body,
                        m.sketch_bodies().size()));
    return m.sketch_bodies()[uz(body)];
}

// The columns of the point element a circle's or an arc's centre slot
// names; none for a derived centre ("end(door)"), which no gesture holds.
std::vector<int> centre_columns(const Model & model, const SketchInfo & si) {
    const int centre = element_index(model, si.slots.at(0).text);
    return centre >= 0 ? model.sketch()[uz(centre)].columns
                       : std::vector<int>{};
}

}  // namespace

Gesture drag_point(const Model & model, int element, int edit) {
    const SketchInfo & si = element_at(model, element);
    if(si.type != SketchType::Point)
        throw std::invalid_argument(si.name + " is not a point");
    Gesture g;
    g.handles.push_back({Handle::Kind::WorldPoint, {}, si.ref, 0});
    g.held = scope_held(model, edit);
    return g;
}

Gesture drag_edge(const Model & model, int element, int edge, int edit) {
    const SketchInfo & si = element_at(model, element);
    const int n = si.type == SketchType::Segment ? 2
                  : si.type == SketchType::Box
                      ? 4
                      : static_cast<int>(si.slots.size());
    const bool closed =
        si.type == SketchType::Polygon || si.type == SketchType::Box;
    if(si.type == SketchType::Point || si.type == SketchType::Circle ||
       si.type == SketchType::Arc || edge < 0 || edge >= (closed ? n : n - 1))
        throw std::invalid_argument(
            std::format("{} has no edge {}", si.name, edge));
    Gesture g;
    g.handles.push_back({Handle::Kind::WorldPoint, {}, si.ref, edge});
    g.handles.push_back({Handle::Kind::WorldPoint, {}, si.ref, (edge + 1) % n});
    g.held = scope_held(model, edit);
    return g;
}

Gesture drag_circle_edge(const Model & model, int element, int edit) {
    const SketchInfo & si = element_at(model, element);
    if(si.type != SketchType::Circle)
        throw std::invalid_argument(si.name + " is not a circle");
    Gesture g;
    g.handles.push_back({Handle::Kind::Columns, si.columns, {}, 0});
    g.held = scope_held(model, edit);
    g.fallback = centre_columns(model, si);
    return g;
}

Gesture drag_arc_end(const Model & model, int element, bool end, int edit) {
    const SketchInfo & si = element_at(model, element);
    if(si.type != SketchType::Arc)
        throw std::invalid_argument(si.name + " is not an arc");
    Gesture g;
    g.handles.push_back(
        {Handle::Kind::WorldPoint, {}, end ? si.arc.end : si.arc.start, 0});
    g.held = scope_held(model, edit);
    g.fallback = centre_columns(model, si);
    return g;
}

Gesture drag_arc_edge(const Model & model, int element, int edit) {
    const SketchInfo & si = element_at(model, element);
    if(si.type != SketchType::Arc)
        throw std::invalid_argument(si.name + " is not an arc");
    Gesture g;
    g.held = scope_held(model, edit);
    if(si.columns.empty()) {
        g.handles.push_back({Handle::Kind::Columns, {}, {}, 0});
        return g;
    }
    g.handles.push_back({Handle::Kind::Columns, {si.columns[0]}, {}, 0});
    g.held.insert(g.held.end(), {si.columns[1], si.columns[2]});
    std::ranges::sort(g.held);
    const auto [last, stop] = std::ranges::unique(g.held);
    g.held.erase(last, stop);
    g.fallback = centre_columns(model, si);
    return g;
}

Gesture drag_body(const Model & model, int body, Vec2d local) {
    const SketchBody & b = body_at(model, body);
    Gesture g;
    Handle h;
    h.kind = Handle::Kind::BodyPoint;
    h.body = body;
    h.local = local;
    g.handles.push_back(h);
    g.held = scope_held(model, -1);
    if(b.columns.size() == 3) g.fallback = {b.columns[2]};
    return g;
}

Gesture move_body(const Model & model, int body) {
    const SketchBody & b = body_at(model, body);
    Gesture g;
    g.held = scope_held(model, -1);
    if(b.columns.size() == 3) {
        g.handles.push_back(
            {Handle::Kind::Columns, {b.columns[0], b.columns[1]}, {}, 0});
        g.held.push_back(b.columns[2]);
        std::ranges::sort(g.held);
    } else {
        g.handles.push_back({Handle::Kind::Columns, {}, {}, 0});
    }
    return g;
}

Gesture turn_body(const Model & model, int body) {
    const SketchBody & b = body_at(model, body);
    Gesture g;
    g.held = scope_held(model, -1);
    if(b.columns.size() == 3) {
        g.handles.push_back({Handle::Kind::Columns, {b.columns[2]}, {}, 0});
        g.held.insert(g.held.end(), {b.columns[0], b.columns[1]});
        std::ranges::sort(g.held);
    } else {
        g.handles.push_back({Handle::Kind::Columns, {}, {}, 0});
    }
    return g;
}

}  // namespace gs::sketch

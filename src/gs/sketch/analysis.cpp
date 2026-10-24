#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>

#include "core.hpp"

namespace gs::sketch::detail {

Work::Work(const Model & m, const Options & o)
    : model(m)
    , options(o)
    , rows(m)
    , evaluator(m)
    , x(m.initial_x())
    , t0_(m.sweeps().size(), 0.0)
    , names_(m)
    , kinds_(m.constraints().size())
    , elements_(m.constraints().size()) {
    for(std::size_t e = 0; e < m.sketch().size(); ++e)
        by_name_.emplace_back(m.sketch()[e].name, static_cast<int>(e));
    std::ranges::sort(by_name_);
    constraint.assign(rows.rows().size(), -1);
    for(int i = 0; i < count(); ++i) {
        const RowGroup & g = group(i);
        if(g.role != GroupRole::Solve) continue;
        if(g.kind != GroupKind::Constraint ||
           static_cast<int>(g.weights.size()) != g.split)
            throw std::logic_error("a Solve group '" + g.name +
                                   "' is not a constraint with its weights");
        solve.push_back(i);
        constraint[uz(i)] = g.source;
    }
}

const RowGroup & Work::group(int row) const {
    return model.groups()[uz(rows.rows()[uz(row)].group)];
}

void Work::values(std::span<const double> q, std::vector<double> & r) {
    r.resize(uz(count()));
    rows.values(x, q, r);
}

void Work::jacobian(std::span<const double> q, std::span<const int> list,
                    std::vector<double> & r, std::vector<JacobianEntry> & J) {
    r.resize(list.size());
    J.clear();
    rows.jacobian(x, q, list, r, J);
}

std::vector<GeoValue> Work::evaluate(std::span<const double> q,
                                     std::span<const ExprRef> refs) {
    evaluator.set_q(q);
    return evaluator.values(x, t0_, refs);
}

const KindMatch & Work::kind(int c) {
    std::optional<KindMatch> & k = kinds_[uz(c)];
    if(!k) {
        k = match_kind(model.constraints()[uz(c)].text, names_);
        for(const std::string & name : k->elements) {
            const auto it = std::ranges::lower_bound(
                by_name_, std::string_view(name), {},
                &std::pair<std::string_view, int>::first);
            if(it != by_name_.end() && it->first == name)
                elements_[uz(c)].push_back(it->second);
        }
    }
    return *k;
}

std::span<const int> Work::elements(int c) {
    kind(c);
    return elements_[uz(c)];
}

Frozen freeze(Work & w, std::span<const double> q) {
    const Model & m = w.model;
    std::vector<ExprRef> refs;
    for(const SketchInfo & e : m.sketch()) refs.push_back(e.ref);
    for(const RowGroup & g : m.groups())
        if(g.role == GroupRole::Solve)
            for(const RowGuard & guard : g.guards) refs.push_back(guard.value);
    // An arc's points are its centre and its ends: its polyline's vertices
    // would weigh a body's metric by how finely the arc is tessellated.
    for(const SketchInfo & e : m.sketch())
        if(e.type == SketchType::Arc)
            refs.insert(refs.end(), {e.arc.center, e.arc.start, e.arc.end});
    const std::vector<GeoValue> v = w.evaluate(q, refs);
    Frozen f;
    f.element.assign(v.begin(), v.begin() + std::ssize(m.sketch()));
    f.guard.resize(m.groups().size());
    std::size_t k = m.sketch().size();
    for(std::size_t g = 0; g < m.groups().size(); ++g)
        if(m.groups()[g].role == GroupRole::Solve)
            for(std::size_t i = 0; i < m.groups()[g].guards.size(); ++i)
                f.guard[g].push_back(v[k++].scalar());
    std::vector<std::vector<Vec2d>> arc_points(m.sketch().size());
    for(std::size_t e = 0; e < m.sketch().size(); ++e)
        if(m.sketch()[e].type == SketchType::Arc)
            for(int i = 0; i < 3; ++i) arc_points[e].push_back(v[k++].vec());
    for(const SketchBody & b : m.sketch_bodies()) {
        PosePoints & p = f.pose.emplace_back();
        if(b.columns.size() != 3) continue;
        const Vec2d o{q[uz(b.columns[0])], q[uz(b.columns[1])]};
        const double phi = q[uz(b.columns[2])];
        const double c = std::cos(phi), s = std::sin(phi);
        std::vector<Vec2d> at;
        auto add = [&](double x, double y) {
            x -= o.x;
            y -= o.y;
            if(std::isfinite(x) && std::isfinite(y))
                at.push_back({c * x + s * y, -s * x + c * y});
        };
        for(const int e : b.elements) {
            const GeoValue & g = f.element[uz(e)];
            if(!arc_points[uz(e)].empty()) {
                for(const Vec2d & p : arc_points[uz(e)]) add(p.x, p.y);
            } else if(g.type == ValueType::Vec ||
                      (g.type == ValueType::Shape &&
                       g.kind == ShapeKind::Circle)) {
                if(g.data.size() >= 2) add(g.data[0], g.data[1]);
            } else if(g.type == ValueType::Shape) {
                for(std::size_t i = 0; i + 1 < g.data.size(); i += 2)
                    add(g.data[i], g.data[i + 1]);
            }
        }
        // A body with no point is its origin.
        if(at.empty()) at.push_back({0.0, 0.0});
        const double n = static_cast<double>(at.size());
        for(const Vec2d & a : at) {
            p.centroid.x += a.x / n;
            p.centroid.y += a.y / n;
        }
        // From the centroid, not as the mean of |offset|^2 less |centroid|^2:
        // a body far from its origin would lose its spread to cancellation.
        for(const Vec2d & a : at)
            p.spread += (std::pow(a.x - p.centroid.x, 2) +
                         std::pow(a.y - p.centroid.y, 2)) /
                        n;
    }
    return f;
}

Scaling scaling(const Model & m, std::span<const int> columns,
                const Frozen & frozen) {
    const std::vector<SketchCoord> & coords = m.sketch_coords();
    Scaling s;
    s.diag.reserve(columns.size());
    for(const int c : columns) s.diag.push_back(coords[uz(c)].scale);
    s.block.assign(columns.size(), -1);
    for(std::size_t b = 0; b < m.sketch_bodies().size(); ++b) {
        const std::vector<int> & pose = m.sketch_bodies()[b].columns;
        if(pose.size() != 3) continue;
        Scaling::Block k;
        for(int i = 0; i < 3; ++i) {
            k.pose[uz(i)] = pose[uz(i)];
            const auto it = std::ranges::lower_bound(columns, pose[uz(i)]);
            if(it == columns.end() || *it != pose[uz(i)]) continue;
            k.part[uz(k.size)] = i;
            k.at[uz(k.size)] = static_cast<int>(it - columns.begin());
            ++k.size;
        }
        if(k.size == 0) continue;
        k.points = frozen.pose[b];
        for(int i = 0; i < k.size; ++i)
            s.block[uz(k.at[uz(i)])] = static_cast<int>(s.blocks.size());
        s.blocks.push_back(k);
    }
    return s;
}

std::vector<Eigen::Matrix3d> Scaling::transforms(
    std::span<const double> q) const {
    std::vector<Eigen::Matrix3d> out;
    out.reserve(blocks.size());
    for(const Block & k : blocks) {
        Eigen::Matrix3d & T = out.emplace_back(Eigen::Matrix3d::Identity());
        if(k.part[uz(k.size - 1)] != 2) continue;
        // L of M over the columns present, in their order: I on the
        // origin, and on the angle's row the centroid and, under the
        // spread, the part of the centroid whose origin column is held.
        const double phi = q[uz(k.pose[2])];
        const double c = std::cos(phi), s = std::sin(phi);
        const Vec2d & g = k.points.centroid;
        const double gx = c * g.x - s * g.y, gy = s * g.x + c * g.y;
        const int a = k.size - 1;
        Eigen::Matrix3d L = Eigen::Matrix3d::Identity();
        bool x = false, y = false;
        for(int i = 0; i < a; ++i) {
            (k.part[uz(i)] == 0 ? x : y) = true;
            L(a, i) = k.part[uz(i)] == 0 ? -gy : gx;
        }
        L(a, a) = std::sqrt(k.points.spread + (x ? 0.0 : gy * gy) +
                            (y ? 0.0 : gx * gx));
        const Eigen::MatrixXd l = L.topLeftCorner(k.size, k.size);
        T.topLeftCorner(k.size, k.size) =
            l.triangularView<Eigen::Lower>()
                .solve(Eigen::MatrixXd::Identity(k.size, k.size))
                .transpose();
    }
    return out;
}

void Scaling::unscale(std::span<const double> q, Eigen::MatrixXd & Z) const {
    for(std::size_t j = 0; j < diag.size(); ++j)
        if(block[j] < 0) Z.row(static_cast<Eigen::Index>(j)) *= diag[j];
    const std::vector<Eigen::Matrix3d> T = transforms(q);
    for(std::size_t b = 0; b < blocks.size(); ++b) {
        const Block & k = blocks[b];
        Eigen::MatrixXd part(k.size, Z.cols());
        for(int i = 0; i < k.size; ++i) part.row(i) = Z.row(k.at[uz(i)]);
        part = T[b].topLeftCorner(k.size, k.size) * part;
        for(int i = 0; i < k.size; ++i) Z.row(k.at[uz(i)]) = part.row(i);
    }
}

double Scaling::unit(int pos) const {
    const int b = block[uz(pos)];
    if(b < 0) return 1.0 / diag[uz(pos)];
    const Block & k = blocks[uz(b)];
    int i = 0;
    while(k.at[uz(i)] != pos) ++i;
    if(k.part[uz(i)] != 2) return 1.0;
    // M's diagonal on the angle, |g|^2 + spread, whatever the angle.
    const Vec2d & g = k.points.centroid;
    return std::sqrt(k.points.spread + g.x * g.x + g.y * g.y);
}

void Scaling::move(std::span<const int> columns, std::span<const double> q,
                   const Eigen::VectorXd & d, std::vector<double> & out) const {
    out.assign(q.begin(), q.end());
    for(std::size_t j = 0; j < columns.size(); ++j)
        if(block[j] < 0)
            out[uz(columns[j])] += diag[j] * d[static_cast<Eigen::Index>(j)];
    const std::vector<Eigen::Matrix3d> T = transforms(q);
    for(std::size_t b = 0; b < blocks.size(); ++b) {
        const Block & k = blocks[b];
        if(k.size == 3) {
            const double phi = q[uz(k.pose[2])];
            const double turn = d[k.at[2]] / std::sqrt(k.points.spread);
            const Vec2d & g = k.points.centroid;
            const double c0 = std::cos(phi), s0 = std::sin(phi);
            const double c1 = std::cos(phi + turn), s1 = std::sin(phi + turn);
            out[uz(k.pose[0])] = q[uz(k.pose[0])] + (c0 - c1) * g.x -
                                 (s0 - s1) * g.y + d[k.at[0]];
            out[uz(k.pose[1])] = q[uz(k.pose[1])] + (s0 - s1) * g.x +
                                 (c0 - c1) * g.y + d[k.at[1]];
            out[uz(k.pose[2])] = phi + turn;
            continue;
        }
        for(int i = 0; i < k.size; ++i)
            for(int j = 0; j < k.size; ++j)
                out[uz(columns[uz(k.at[uz(i)])])] +=
                    T[b](i, j) * d[k.at[uz(j)]];
    }
}

std::vector<MalformedConstraint> start_checks(const Work & w,
                                              std::span<const int> kept,
                                              const Frozen & frozen,
                                              std::span<const double> r) {
    using R = MalformedConstraint::Reason;
    std::vector<MalformedConstraint> out;
    auto add = [&](int row, R reason, std::string why) {
        const int c = w.constraint[uz(row)];
        if(!out.empty() && out.back().constraint == c) return;
        out.push_back({c, reason, std::move(why)});
    };
    for(const int row : kept) {
        const int g = w.rows.rows()[uz(row)].group;
        const RowGroup & rg = w.model.groups()[uz(g)];
        for(std::size_t k = 0; k < rg.guards.size(); ++k) {
            // An overflowed length passes > 0, and the unit directions it
            // divides are then 0: their rows are finite, often satisfied.
            if(!std::isfinite(frozen.guard[uz(g)][k])) {
                add(row, R::NotFinite, "it is not finite at the drawn values");
                continue;
            }
            if(frozen.guard[uz(g)][k] > 0.0) continue;
            switch(rg.guards[k].kind) {
                case RowGuard::Kind::Segment:
                    add(row, R::Segment,
                        "a segment whose direction it reads has zero length");
                    break;
                case RowGuard::Kind::Radius:
                    add(row, R::Radius,
                        "a radius it reads or sets is not positive");
                    break;
                case RowGuard::Kind::Span:
                    add(row, R::Span,
                        "an arc's span it reads or sets is outside (0, 2 pi)");
                    break;
                case RowGuard::Kind::Inner:
                    add(row, R::Inner,
                        "an internal tangency needs its first circle larger "
                        "than the second");
                    break;
            }
        }
        if(!std::isfinite(r[uz(row)]))
            add(row, R::NotFinite, "it is not finite at the drawn values");
        // Satisfied, it is a check no drawing breaks, as the Drawn binding
        // makes it: Malformed would fail a correct drawing.
        if(w.rows.columns(row).empty() &&
           !(std::fabs(r[uz(row)]) <= w.options.tol))
            add(row, R::NoParameter,
                "it reads no free parameter of the sketch, so no drawing "
                "changes it");
    }
    return out;
}

namespace {

// The gauge directions of one body as data: g[k] over the columns of
// q, k = 0, 1, 2 the frame's x, its y and a turn (model.hpp, SketchBody).
struct Gauges {
    std::vector<int> columns;  // the body's pose and element columns
    std::vector<std::array<double, 3>> g;  // per entry of `columns`
};

Gauges gauges(const Model & m, const SketchBody & b,
              std::span<const double> q) {
    Gauges G;
    const double phi = q[uz(b.columns[2])];
    const double c = std::cos(phi), s = std::sin(phi);
    G.columns = {b.columns[0], b.columns[1], b.columns[2]};
    G.g = {{-c, s, 0.0}, {-s, -c, 0.0}, {0.0, 0.0, -1.0}};
    for(const int e : b.elements) {
        const SketchInfo & si = m.sketch()[uz(e)];
        if(si.columns.empty()) continue;
        if(si.type == SketchType::Arc) {
            // Its angles turn with its points; its radius stays.
            for(const int a : {si.columns[1], si.columns[2]}) {
                G.columns.push_back(a);
                G.g.push_back({0.0, 0.0, 1.0});
            }
            continue;
        }
        if(si.type != SketchType::Point) continue;
        const int x = si.columns[0], y = si.columns[1];
        G.columns.push_back(x);
        G.g.push_back({1.0, 0.0, -q[uz(y)]});
        G.columns.push_back(y);
        G.g.push_back({0.0, 1.0, q[uz(x)]});
    }
    return G;
}

}  // namespace

std::vector<int> gauge_holds(Work & w, std::span<const double> q,
                             std::span<const int> kept,
                             const std::vector<JacobianEntry> & J,
                             std::span<const int> held_already) {
    const Model & m = w.model;
    const std::vector<SketchCoord> & coords = m.sketch_coords();
    // Each row of J in scaled columns: its norm makes the test a cosine,
    // whatever the row's units.
    std::vector<double> norm(kept.size(), 0.0);
    for(const JacobianEntry & e : J) {
        const double v = e.value * coords[uz(e.column)].scale;
        norm[uz(e.row)] += v * v;
    }
    for(double & v : norm) v = std::sqrt(v);
    std::vector<int> held;
    for(const SketchBody & b : m.sketch_bodies()) {
        if(b.columns.size() != 3) continue;
        const Gauges G = gauges(m, b, q);
        if(std::ranges::any_of(G.columns, [&](int col) {
               return std::ranges::contains(held_already, col);
           }))
            continue;
        std::vector<int> where(coords.size(), -1);
        for(std::size_t i = 0; i < G.columns.size(); ++i)
            where[uz(G.columns[i])] = static_cast<int>(i);
        // The length of each direction in scaled columns.
        std::array<double, 3> len{};
        for(std::size_t i = 0; i < G.columns.size(); ++i)
            for(int k = 0; k < 3; ++k) {
                const double v = G.g[i][uz(k)] / coords[uz(G.columns[i])].scale;
                len[uz(k)] += v * v;
            }
        for(double & v : len) v = std::sqrt(v);

        std::vector<std::array<double, 3>> M;
        // The rows: J g for each unit direction, over the row's length.
        std::vector<std::array<double, 3>> jg(kept.size(), {0.0, 0.0, 0.0});
        std::vector<char> reads(kept.size(), 0);
        for(const JacobianEntry & e : J) {
            const int i = where[uz(e.column)];
            if(i < 0) continue;
            reads[uz(e.row)] = 1;
            for(int k = 0; k < 3; ++k)
                jg[uz(e.row)][uz(k)] += e.value * G.g[uz(i)][uz(k)];
        }
        for(std::size_t r = 0; r < kept.size(); ++r)
            if(reads[r] && norm[r] > 0.0)
                M.push_back({jg[r][0] / (norm[r] * len[0]),
                             jg[r][1] / (norm[r] * len[1]),
                             jg[r][2] / (norm[r] * len[2])});
        // The body's elements in the world: a direction that moves one (a
        // constant element, a fixed point) is no gauge, even when no row
        // reads that element. Central differences; a translation moves
        // nothing exactly, a turn to second order.
        std::vector<ExprRef> refs;
        for(const int e : b.elements) refs.push_back(m.sketch()[uz(e)].ref);
        std::vector<std::vector<double>> motion(3);
        for(int k = 0; k < 3; ++k) {
            const double eps = k < 2 ? 1e-3 : 1e-5;
            std::vector<double> plus(q.begin(), q.end()), minus = plus;
            for(std::size_t i = 0; i < G.columns.size(); ++i) {
                plus[uz(G.columns[i])] += eps * G.g[i][uz(k)];
                minus[uz(G.columns[i])] -= eps * G.g[i][uz(k)];
            }
            const std::vector<GeoValue> a = w.evaluate(plus, refs),
                                        z = w.evaluate(minus, refs);
            for(std::size_t e = 0; e < a.size(); ++e)
                for(std::size_t d = 0; d < a[e].data.size(); ++d)
                    motion[uz(k)].push_back((a[e].data[d] - z[e].data[d]) /
                                            (2.0 * eps * len[uz(k)]));
        }
        for(std::size_t d = 0; d < motion[0].size(); ++d)
            M.push_back({motion[0][d], motion[1][d], motion[2][d]});

        // The free directions: the null space of M.
        Eigen::MatrixXd A(
            static_cast<Eigen::Index>(std::max<std::size_t>(M.size(), 3)), 3);
        A.setZero();
        for(std::size_t r = 0; r < M.size(); ++r)
            for(int k = 0; k < 3; ++k)
                A(static_cast<Eigen::Index>(r), k) = M[r][uz(k)];
        const Eigen::JacobiSVD<Eigen::MatrixXd> svd(A, Eigen::ComputeFullV);
        std::vector<Eigen::Vector3d> loose;
        for(int k = 0; k < 3; ++k)
            if(svd.singularValues()[k] <= w.options.degenerate)
                loose.push_back(svd.matrixV().col(k));
        // One pose column per free direction, such that the free directions
        // restricted to the held columns stay independent: holding them
        // then removes those directions and no other.
        Eigen::MatrixXd P(static_cast<Eigen::Index>(loose.size()), 3);
        for(std::size_t i = 0; i < loose.size(); ++i)
            for(int c = 0; c < 3; ++c) {
                double v = 0.0;
                for(int k = 0; k < 3; ++k)
                    v += loose[i][k] * G.g[uz(c)][uz(k)] / len[uz(k)];
                P(static_cast<Eigen::Index>(i), c) =
                    v / coords[uz(b.columns[uz(c)])].scale;
            }
        std::vector<char> row_done(loose.size(), 0), col_done(3, 0);
        for(std::size_t step = 0; step < loose.size(); ++step) {
            Eigen::Index br = -1, bc = -1;
            double best = -1.0;
            for(Eigen::Index r = 0; r < P.rows(); ++r)
                for(Eigen::Index c = 0; c < 3; ++c)
                    if(!row_done[uz(static_cast<int>(r))] &&
                       !col_done[uz(static_cast<int>(c))] &&
                       std::fabs(P(r, c)) > best) {
                        best = std::fabs(P(r, c));
                        br = r;
                        bc = c;
                    }
            row_done[uz(static_cast<int>(br))] = 1;
            col_done[uz(static_cast<int>(bc))] = 1;
            for(Eigen::Index r = 0; r < P.rows(); ++r)
                if(!row_done[uz(static_cast<int>(r))])
                    P.row(r) -= P(r, bc) / P(br, bc) * P.row(br);
            held.push_back(b.columns[uz(static_cast<int>(bc))]);
        }
    }
    std::ranges::sort(held);
    return held;
}

std::vector<Comp> components(Work & w, std::span<const int> kept,
                             std::span<const int> held, const Frozen & frozen,
                             std::span<const char> wanted) {
    const Model & m = w.model;
    const std::vector<SketchCoord> & coords = m.sketch_coords();
    const int n = static_cast<int>(coords.size());
    std::vector<int> parent(uz(n));
    std::iota(parent.begin(), parent.end(), 0);
    auto find = [&](int c) {
        while(parent[uz(c)] != c) {
            parent[uz(c)] = parent[uz(parent[uz(c)])];
            c = parent[uz(c)];
        }
        return c;
    };
    std::vector<char> is_held(uz(n), 0);
    for(const int c : held) is_held[uz(c)] = 1;
    std::vector<int> first(kept.size(), -1);
    std::vector<char> read(uz(n), 0);
    for(std::size_t k = 0; k < kept.size(); ++k)
        for(const int c : w.rows.columns(kept[k])) {
            if(is_held[uz(c)]) continue;
            read[uz(c)] = 1;
            if(first[k] < 0)
                first[k] = c;
            else
                parent[uz(find(c))] = find(first[k]);
        }
    // A body's free pose columns join the component of any one of them,
    // read or not. Left out, an origin no row reads is treated as held by
    // the metric (Scaling), and angle(tv) == a turns the body about its
    // drawn origin instead of its centroid.
    for(const SketchBody & b : m.sketch_bodies()) {
        const auto at = std::ranges::find_if(
            b.columns, [&](int c) { return read[uz(c)] != 0; });
        if(at == b.columns.end()) continue;
        for(const int c : b.columns)
            if(!is_held[uz(c)]) parent[uz(find(c))] = find(*at);
    }
    // An arc's two angles join one component when rows read both:
    // admissible() reads the span from both, and solved apart, a step of one
    // is refused against the other's value from before it moved
    // (start_angle(k) == T, end_angle(k) == T + 90deg, T typed past the
    // drawn end).
    for(const SketchInfo & e : m.sketch()) {
        if(e.type != SketchType::Arc || e.columns.size() != 3) continue;
        const int a0 = e.columns[1], a1 = e.columns[2];
        if(read[uz(a0)] && read[uz(a1)]) parent[uz(find(a1))] = find(a0);
    }
    std::vector<int> comp_of(uz(n), -1);
    std::vector<Comp> comps;
    // A root none of whose rows is wanted: its component is not built.
    constexpr int kSkipped = -2;
    if(!wanted.empty())
        for(std::size_t k = 0; k < kept.size(); ++k)
            if(first[k] >= 0) comp_of[uz(find(first[k]))] = kSkipped;
    for(std::size_t k = 0; k < kept.size(); ++k)
        if(first[k] >= 0 && !wanted.empty() && wanted[uz(kept[k])])
            comp_of[uz(find(first[k]))] = -1;
    for(std::size_t k = 0; k < kept.size(); ++k) {
        if(first[k] < 0) continue;
        const int root = find(first[k]);
        if(comp_of[uz(root)] == kSkipped) continue;
        if(comp_of[uz(root)] < 0) {
            comp_of[uz(root)] = static_cast<int>(comps.size());
            comps.emplace_back();
        }
        comps[uz(comp_of[uz(root)])].rows.push_back(kept[k]);
    }
    for(int c = 0; c < n; ++c)
        if(!is_held[uz(c)] && comp_of[uz(find(c))] >= 0)
            comps[uz(comp_of[uz(find(c))])].columns.push_back(c);

    // The elements each component's columns move: those whose value's plan
    // reads one of them. Moving each component's columns and evaluating the
    // whole sketch for each instead costs the number of components times
    // the sketch, 38 % of a rank test on 125 rectangles.
    std::vector<std::vector<int>> moves(comps.size());
    for(std::size_t e = 0; e < m.sketch().size(); ++e) {
        const int plan = w.rows.value_plan(m.sketch()[e].ref);
        if(plan < 0) continue;
        for(const int c : w.rows.value_columns(plan)) {
            if(is_held[uz(c)]) continue;
            const int k = comp_of[uz(find(c))];
            if(k < 0) continue;
            std::vector<int> & list = moves[uz(k)];
            if(list.empty() || list.back() != static_cast<int>(e))
                list.push_back(static_cast<int>(e));
        }
    }
    for(std::size_t i = 0; i < comps.size(); ++i) {
        Comp & comp = comps[i];
        comp.scaling = scaling(m, comp.columns, frozen);
        // Its box covers every element its columns move, a wall drawn
        // through its point as well, and every element its rows name, fixed
        // ones included: the box of a lone point alone would give L_c = 1e-3
        // m, which weighs an angle(b) row by a millimetre, splits any typed
        // change above 50 um into 10 solves, and sets a polish target far
        // below the rounding of its coordinates.
        std::vector<char> seen(m.sketch().size(), 0);
        for(const int e : moves[i]) seen[uz(e)] = 1;
        for(const int row : comp.rows)
            for(const int e : w.elements(w.constraint[uz(row)]))
                seen[uz(e)] = 1;
        double lo[2] = {INFINITY, INFINITY}, hi[2] = {-INFINITY, -INFINITY};
        auto cover = [&](double x, double y, double r) {
            lo[0] = std::min(lo[0], x - r);
            lo[1] = std::min(lo[1], y - r);
            hi[0] = std::max(hi[0], x + r);
            hi[1] = std::max(hi[1], y + r);
        };
        for(std::size_t e = 0; e < seen.size(); ++e) {
            if(!seen[e]) continue;
            const GeoValue & v = frozen.element[e];
            if(v.type == ValueType::Vec)
                cover(v.data[0], v.data[1], 0.0);
            else if(v.type == ValueType::Shape && v.kind == ShapeKind::Circle)
                cover(v.data[0], v.data[1], std::fabs(v.data[2]));
            else
                for(std::size_t i = 0; i + 1 < v.data.size(); i += 2)
                    cover(v.data[i], v.data[i + 1], 0.0);
        }
        const double diag =
            lo[0] <= hi[0] ? std::hypot(hi[0] - lo[0], hi[1] - lo[1]) : 0.0;
        comp.length = std::isfinite(diag) ? std::max(1e-3, diag) : 1e-3;
        comp.magnitude =
            std::max({comp.length, std::fabs(lo[0]), std::fabs(lo[1]),
                      std::fabs(hi[0]), std::fabs(hi[1])});
        if(!std::isfinite(comp.magnitude)) comp.magnitude = comp.length;
        for(const int row : comp.rows) {
            const RowRef ref = w.rows.rows()[uz(row)];
            const RowGroup & g = m.groups()[uz(ref.group)];
            switch(g.weights[uz(ref.split)]) {
                case RowWeight::One:
                    comp.weight.push_back(1.0);
                    break;
                case RowWeight::Scale:
                    comp.weight.push_back(comp.length);
                    break;
                case RowWeight::Shortest: {
                    double shortest = INFINITY;
                    for(std::size_t k = 0; k < g.guards.size(); ++k)
                        if(g.guards[k].kind == RowGuard::Kind::Segment ||
                           g.guards[k].kind == RowGuard::Kind::Radius)
                            shortest = std::min(shortest,
                                                frozen.guard[uz(ref.group)][k]);
                    if(!std::isfinite(shortest))
                        throw std::logic_error("the direction rows of '" +
                                               g.name +
                                               "' read no length to weigh "
                                               "them by");
                    comp.weight.push_back(
                        std::max(1e-3 * comp.length, shortest));
                    break;
                }
            }
        }
    }
    return comps;
}

std::vector<int> kept_rows(Work & w, std::span<const double> q,
                           std::span<const int> left_out) {
    std::vector<double> r;
    w.values(q, r);
    std::vector<int> kept;
    for(const int row : without(w, w.solve, left_out))
        if(std::isfinite(r[uz(row)]) && !w.rows.columns(row).empty())
            kept.push_back(row);
    return kept;
}

std::vector<int> without(const Work & w, std::span<const int> rows,
                         std::span<const int> constraints) {
    std::vector<int> out;
    for(const int row : rows)
        if(!std::ranges::contains(constraints, w.constraint[uz(row)]))
            out.push_back(row);
    return out;
}

std::vector<int> held_only(const Work & w, std::span<const int> rows,
                           std::span<const int> held) {
    std::vector<int> out;
    for(const int row : rows) {
        const std::span<const int> cols = w.rows.columns(row);
        if(!cols.empty() && std::ranges::all_of(cols, [&](int c) {
               return std::ranges::binary_search(held, c);
           }))
            out.push_back(row);
    }
    return out;
}

void erase_rows(Comp & c, std::span<const int> rows) {
    for(std::size_t k = c.rows.size(); k-- > 0;)
        if(std::ranges::contains(rows, c.rows[k])) {
            c.rows.erase(c.rows.begin() + static_cast<std::ptrdiff_t>(k));
            c.weight.erase(c.weight.begin() + static_cast<std::ptrdiff_t>(k));
        }
}

std::vector<int> unread_columns(const Work & w, const std::vector<Comp> & comps,
                                std::span<const int> held) {
    std::vector<char> used(w.model.sketch_coords().size(), 0);
    for(const int c : held) used[uz(c)] = 1;
    for(const Comp & comp : comps)
        for(const int c : comp.columns) used[uz(c)] = 1;
    std::vector<int> out;
    for(std::size_t c = 0; c < used.size(); ++c)
        if(!used[c]) out.push_back(static_cast<int>(c));
    return out;
}

std::vector<double> dimensions(Work & w, std::span<const int> rows,
                               std::span<const double> q, double length,
                               bool angles) {
    using K = ConstraintKind;
    const Model & m = w.model;
    std::vector<double> out(rows.size(), 0.0);
    std::vector<ExprRef> refs;
    std::vector<std::size_t> at;
    for(std::size_t k = 0; k < rows.size(); ++k) {
        const int con = w.constraint[uz(rows[k])];
        const ConstraintInfo & ci = m.constraints()[uz(con)];
        const KindMatch & km = w.kind(con);
        const bool angle = km.kind == K::Angle || km.kind == K::StartAngle ||
                           km.kind == K::EndAngle || km.kind == K::Span;
        const bool dimension =
            km.relation == KindRelation::Equal &&
            (km.kind == K::Distance || km.kind == K::Length ||
             km.kind == K::LineDistance || km.kind == K::DistanceX ||
             km.kind == K::DistanceY || km.kind == K::LockX ||
             km.kind == K::LockY || (angles && angle) || km.kind == K::Radius);
        if(!dimension || ci.value.ref.node < 0) continue;
        if(km.kind == K::LockX || km.kind == K::LockY) {
            out[k] = length;
        } else if(!km.magnitude) {
            out[k] = 1.0;
        } else {
            refs.push_back(ci.value.ref);
            at.push_back(k);
        }
    }
    if(refs.empty()) return out;
    const std::vector<GeoValue> v = w.evaluate(q, refs);
    for(std::size_t i = 0; i < at.size(); ++i)
        out[at[i]] = std::fabs(v[i].scalar());
    return out;
}

Component public_component(const Work & w, const Comp & c) {
    Component p;
    p.columns = c.columns;
    for(const int row : c.rows) p.rows.push_back(w.rows.rows()[uz(row)]);
    p.length = c.length;
    return p;
}

Rank collect(const Work & w, const std::vector<Comp> & comps,
             const std::vector<CompRank> & ranks,
             const std::vector<bool> & ranked, std::span<const int> held,
             std::size_t gauges) {
    Rank R;
    R.ranked = ranked;
    auto ref = [&](int row) { return w.rows.rows()[uz(row)]; };
    int dof = 0;
    for(std::size_t i = 0; i < comps.size(); ++i) {
        std::vector<RowRef> indep;
        if(!ranked[i]) {
            R.independent.push_back(std::move(indep));
            continue;
        }
        for(const int row : ranks[i].independent) indep.push_back(ref(row));
        dof += static_cast<int>(comps[i].columns.size() - indep.size());
        R.independent.push_back(std::move(indep));
        for(const CompRank::Dependent & dep : ranks[i].dependent) {
            Dependency d{ref(dep.row), {}, dep.consistent, dep.confirmed};
            for(const int g : dep.group) d.group.push_back(ref(g));
            R.dependent.push_back(std::move(d));
        }
        for(const int row : ranks[i].near) R.near_dependent.push_back(ref(row));
    }
    if(std::ranges::contains(ranked, false)) return R;
    R.dof =
        dof + static_cast<int>(unread_columns(w, comps, held).size() + gauges);
    return R;
}

Status rank_status(const Work & w, const Rank & r) {
    if(w.model.sketch_coords().empty() && w.solve.empty()) return Status::Empty;
    if(!r.dependent.empty()) return Status::Redundant;
    if(!r.dof) return Status::Solved;
    return *r.dof > 0 ? Status::UnderConstrained : Status::Full;
}

}  // namespace gs::sketch::detail

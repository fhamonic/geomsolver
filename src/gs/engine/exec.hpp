#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

#include "gs/engine/dual.hpp"
#include "gs/engine/geometry.hpp"
#include "program.hpp"

// Templated implementation of every program op for T = double and Dual<N>.
//
// Every selection (min/max, clearance features, extremes, dyad branch) is made
// on values computed in double, then only the winning feature is recomputed in
// T. That keeps the T result a smooth function of the active feature and makes
// the double and Dual paths take identical decisions, hence identical values.
namespace gs::detail {

using ad::val;

template <class T>
struct V2 {
    T x, y;
};
template <class T>
inline V2<T> ld2(const T * p) {
    return {p[0], p[1]};
}
template <class T>
inline void st2(T * o, const V2<T> & v) {
    o[0] = v.x;
    o[1] = v.y;
}
template <class T>
inline V2<T> operator+(const V2<T> & a, const V2<T> & b) {
    return {a.x + b.x, a.y + b.y};
}
template <class T>
inline V2<T> operator-(const V2<T> & a, const V2<T> & b) {
    return {a.x - b.x, a.y - b.y};
}
template <class T>
inline V2<T> scale(const V2<T> & a, const T & s) {
    return {a.x * s, a.y * s};
}
template <class T>
inline T dot(const V2<T> & a, const V2<T> & b) {
    return a.x * b.x + a.y * b.y;
}
template <class T>
inline T cross(const V2<T> & a, const V2<T> & b) {
    return a.x * b.y - a.y * b.x;
}
template <class T>
inline T norm(const V2<T> & a) {
    return ad::sqrt(a.x * a.x + a.y * a.y);
}
// cs = (cos a, sin a)
template <class T>
inline V2<T> rot(const V2<T> & v, const V2<T> & cs) {
    return {cs.x * v.x - cs.y * v.y, cs.y * v.x + cs.x * v.y};
}
template <class T>
inline Vec2d vval(const T * p) {
    return {val(p[0]), val(p[1])};
}

template <class T>
T dist_point_segment(const V2<T> & p, const V2<T> & a, const V2<T> & b) {
    const V2<T> ab = b - a, ap = p - a;
    const T L2 = dot(ab, ab);
    if(val(L2) <= 0.0) return norm(ap);
    const T t = dot(ap, ab) / L2;
    if(val(t) <= 0.0) return norm(ap);
    if(val(t) >= 1.0) return norm(p - b);
    return norm(ap - scale(ab, t));
}

// Closest / deepest feature of a clearance computation, selected on values.
// Points are (shape 0/1, vertex index):
//   PP: distance between point (ps, pi) and point (es, e0);
//   PS: distance from point (ps, pi) to segment (es: e0 -> e1), negated when
//       sign < 0 (the point is inside);
//   EV: signed distance of vertex (ps, pi) to the outward line of edge
//       (es: e0 -> e1); sign is the edge owner's orientation, except for a
//       point against a polygon, where it follows the inside test.
struct Feature {
    enum class Kind : std::uint8_t { PP, PS, EV };
    Kind kind = Kind::PP;
    int ps = 0, pi = 0;
    int es = 0, e0 = 0, e1 = 0;
    double sign = 1.0;
    double value = std::numeric_limits<double>::infinity();
};

struct Piece {
    int shape = 0;
    const int * idx = nullptr;
    int n = 0;
    double orient = 1.0;
};

struct Scratch {
    std::vector<Vec2d> v[2];
    std::vector<std::vector<int>> pieces[2];
    std::vector<Piece> piece_list[2];
    // Pieces point into iota: grow it before collecting pieces of either
    // shape, never in between, or the first shape's pointers dangle.
    std::vector<int> iota;
    void ensure_iota(int n) {
        while(static_cast<int>(iota.size()) < n + 1)
            iota.push_back(static_cast<int>(iota.size()));
    }
};

template <class T>
T eval_feature(const Feature & f, const T * d0, const T * d1) {
    auto pt = [&](int s, int i) {
        const T * d = s == 0 ? d0 : d1;
        return V2<T>{d[2 * i], d[2 * i + 1]};
    };
    switch(f.kind) {
        case Feature::Kind::PP:
            return norm(pt(f.ps, f.pi) - pt(f.es, f.e0));
        case Feature::Kind::PS: {
            const T r = dist_point_segment(pt(f.ps, f.pi), pt(f.es, f.e0),
                                           pt(f.es, f.e1));
            return f.sign < 0.0 ? -r : r;
        }
        case Feature::Kind::EV: {
            const V2<T> a = pt(f.es, f.e0);
            const V2<T> e = pt(f.es, f.e1) - a;
            const V2<T> v = pt(f.ps, f.pi) - a;
            const T L = norm(e);
            const T s = (e.y * v.x - e.x * v.y) / L;
            return f.sign < 0.0 ? -s : s;
        }
    }
    return T(0.0);
}

// Vertex k of the closed ring V[0, n) lies on the line through its two
// neighbours, the boundary going on in the same direction (a redundant
// vertex, such as (0.5, 0) in (0, 0), (0.5, 0), (1, 0), ...).
inline bool straight_vertex(const std::vector<Vec2d> & V, int n, int k) {
    const Vec2d u = V[uz((k + n - 1) % n)], v = V[uz(k)],
                w = V[uz((k + 1) % n)];
    const double ax = v.x - u.x, ay = v.y - u.y, bx = w.x - v.x, by = w.y - v.y;
    const double la = std::hypot(ax, ay), lb = std::hypot(bx, by);
    return la > 0.0 && lb > 0.0 && ax * bx + ay * by > 0.0 &&
           std::fabs(ax * by - ay * bx) <= 1e-10 * la * lb;
}

inline Feature point_vs_shape(int ps, int pi, int s, ShapeKind kind, int n,
                              const Scratch & sc) {
    const Vec2d p = sc.v[ps][static_cast<std::size_t>(pi)];
    const std::vector<Vec2d> & V = sc.v[s];
    Feature f;
    f.kind = Feature::Kind::PS;
    f.ps = ps;
    f.pi = pi;
    f.es = s;
    const bool closed = kind == ShapeKind::Polygon;
    const int edges = closed ? n : n - 1;
    for(int k = 0; k < edges; ++k) {
        const int k1 = (k + 1) % n;
        const double d = dist_point_segment<double>(
            {p.x, p.y}, {V[uz(k)].x, V[uz(k)].y}, {V[uz(k1)].x, V[uz(k1)].y});
        if(d < f.value) {
            f.value = d;
            f.e0 = k;
            f.e1 = k1;
        }
    }
    if(!closed) return f;
    const std::span<const Vec2d> poly(V.data(), uz(n));
    const bool inside = point_in_polygon(p, poly);
    if(inside) {
        f.sign = -1.0;
        f.value = -f.value;
    }
    // Facing an edge's interior, or a vertex where the boundary runs straight
    // on, the signed distance is the distance to the edge's line, which stays
    // smooth as the point crosses the boundary. The segment distance's
    // derivative vanishes (sqrt at 0) or is rounding noise there: a pivot
    // resting on its domain's edge would get no usable gradient.
    const Vec2d a = V[uz(f.e0)], b = V[uz(f.e1)];
    const double ex = b.x - a.x, ey = b.y - a.y, L2 = ex * ex + ey * ey;
    if(!(L2 > 0.0)) return f;
    const double t = ((p.x - a.x) * ex + (p.y - a.y) * ey) / L2;
    if(t <= 0.0 && !straight_vertex(V, n, f.e0)) return f;
    if(t >= 1.0 && !straight_vertex(V, n, f.e1)) return f;
    f.kind = Feature::Kind::EV;
    // The sign must make the line distance agree with the inside test, not
    // follow the polygon's orientation: a polygon with no interior
    // (box(0, 0, 1, 0)) or a self-intersecting one has outside points on the
    // inner side of their nearest edge, which the orientation reports as
    // penetrating by their whole distance. Within rounding of the line the
    // point's own inside test is noise, so it is asked just off the line on
    // both sides; the orientation decides only where both answers agree (a
    // polygon with no interior, or a point at a vertex), else a point on a
    // lobe's edge of a self-intersecting polygon gets the reversed gradient.
    const double L = std::sqrt(L2);
    const double line = (ey * (p.x - a.x) - ex * (p.y - a.y)) / L;
    const double scale =
        std::max({std::fabs(p.x), std::fabs(p.y), std::fabs(a.x),
                  std::fabs(a.y), std::fabs(b.x), std::fabs(b.y)});
    if(std::fabs(line) > 1e-12 * scale) {
        f.sign = (line > 0.0) != inside ? 1.0 : -1.0;
        return f;
    }
    // Beyond the 1e-12 * scale band, so each probe is decided off the line,
    // yet short against the edge, so neither crosses another edge away from
    // a vertex.
    const double off = (1e-11 * scale + 1e-9 * L) / L;
    const bool plus = point_in_polygon({p.x + off * ey, p.y - off * ex}, poly);
    const bool minus = point_in_polygon({p.x - off * ey, p.y + off * ex}, poly);
    if(plus != minus)
        f.sign = plus ? -1.0 : 1.0;
    else
        f.sign = signed_area(poly) >= 0.0 ? 1.0 : -1.0;
    return f;
}

// A piece with no interior (a segment, or a polygon whose vertices are all on
// one line) has no edge normal along its own line, so the edge-normal axes
// miss the gap between two collinear, disjoint pieces.
inline bool flat_piece(const Piece & P, const Scratch & sc) {
    if(P.n <= 2) return true;
    const std::vector<Vec2d> & V = sc.v[P.shape];
    double a2 = 0.0, len2 = 0.0;
    const Vec2d o = V[uz(P.idx[0])];
    for(int k = 0; k < P.n; ++k) {
        const Vec2d a = V[uz(P.idx[k])], b = V[uz(P.idx[(k + 1) % P.n])];
        a2 += (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        len2 = std::max(len2,
                        (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
    }
    return std::fabs(a2) <= 1e-12 * len2;
}

// Gap between the projections of P and Q on the line of the longest edge of
// `flat`; positive only when the pieces are disjoint.
inline double along_flat_gap(const Piece & flat, const Piece & P,
                             const Piece & Q, const Scratch & sc) {
    const std::vector<Vec2d> & VF = sc.v[flat.shape];
    double ux = 0.0, uy = 0.0, best = 0.0;
    for(int k = 0; k < flat.n; ++k) {
        const Vec2d a = VF[uz(flat.idx[k])],
                    b = VF[uz(flat.idx[(k + 1) % flat.n])];
        const double ex = b.x - a.x, ey = b.y - a.y, l2 = ex * ex + ey * ey;
        if(l2 > best) {
            best = l2;
            ux = ex;
            uy = ey;
        }
    }
    if(!(best > 0.0)) return -std::numeric_limits<double>::infinity();
    const double L = std::sqrt(best);
    ux /= L;
    uy /= L;
    auto range = [&](const Piece & R, double & lo, double & hi) {
        lo = std::numeric_limits<double>::infinity();
        hi = -lo;
        for(int j = 0; j < R.n; ++j) {
            const Vec2d q = sc.v[R.shape][uz(R.idx[j])];
            const double pr = q.x * ux + q.y * uy;
            lo = std::min(lo, pr);
            hi = std::max(hi, pr);
        }
    };
    double plo = 0, phi = 0, qlo = 0, qhi = 0;
    range(P, plo, phi);
    range(Q, qlo, qhi);
    return std::max(qlo - phi, plo - qhi);
}

inline Feature convex_pair(const Piece & P, const Piece & Q,
                           const Scratch & sc) {
    Feature best;
    best.kind = Feature::Kind::EV;
    best.value = -std::numeric_limits<double>::infinity();
    for(int owner = 0; owner < 2; ++owner) {
        const Piece & E = owner == 0 ? P : Q;
        const Piece & O = owner == 0 ? Q : P;
        const std::vector<Vec2d> & VE = sc.v[E.shape];
        const std::vector<Vec2d> & VO = sc.v[O.shape];
        for(int k = 0; k < E.n; ++k) {
            const int i0 = E.idx[k], i1 = E.idx[(k + 1) % E.n];
            const Vec2d a = VE[uz(i0)], b = VE[uz(i1)];
            const double ex = b.x - a.x, ey = b.y - a.y;
            const double L = std::sqrt(ex * ex + ey * ey);
            if(!(L > 0.0)) continue;
            const double nx = E.orient * ey / L, ny = -E.orient * ex / L;
            const double ref = nx * a.x + ny * a.y;
            double pmin = std::numeric_limits<double>::infinity();
            int jmin = 0;
            for(int j = 0; j < O.n; ++j) {
                const Vec2d q = VO[uz(O.idx[j])];
                const double pr = nx * q.x + ny * q.y;
                if(pr < pmin) {
                    pmin = pr;
                    jmin = j;
                }
            }
            if(pmin - ref > best.value) {
                best.value = pmin - ref;
                best.ps = O.shape;
                best.pi = O.idx[jmin];
                best.es = E.shape;
                best.e0 = i0;
                best.e1 = i1;
                best.sign = E.orient;
            }
        }
    }
    if(best.value == -std::numeric_limits<double>::infinity()) {
        Feature f;
        f.kind = Feature::Kind::PP;
        f.ps = P.shape;
        f.pi = P.idx[0];
        f.es = Q.shape;
        f.e0 = Q.idx[0];
        const Vec2d a = sc.v[P.shape][uz(f.pi)], b = sc.v[Q.shape][uz(f.e0)];
        f.value = std::hypot(a.x - b.x, a.y - b.y);
        return f;
    }
    if(best.value <= 0.0) {
        const bool fp = flat_piece(P, sc), fq = flat_piece(Q, sc);
        if(!fp && !fq) return best;
        double gap = -std::numeric_limits<double>::infinity();
        if(fp) gap = std::max(gap, along_flat_gap(P, P, Q, sc));
        if(fq) gap = std::max(gap, along_flat_gap(Q, P, Q, sc));
        if(!(gap > 0.0)) return best;
    }
    Feature sep;
    sep.kind = Feature::Kind::PS;
    for(int owner = 0; owner < 2; ++owner) {
        const Piece & E = owner == 0 ? P : Q;
        const Piece & O = owner == 0 ? Q : P;
        const std::vector<Vec2d> & VE = sc.v[E.shape];
        const std::vector<Vec2d> & VO = sc.v[O.shape];
        const int edges = E.n == 2 ? 1 : E.n;
        for(int k = 0; k < edges; ++k) {
            const int i0 = E.idx[k], i1 = E.idx[(k + 1) % E.n];
            const V2<double> a{VE[uz(i0)].x, VE[uz(i0)].y},
                b{VE[uz(i1)].x, VE[uz(i1)].y};
            for(int j = 0; j < O.n; ++j) {
                const Vec2d q = VO[uz(O.idx[j])];
                const double d = dist_point_segment<double>({q.x, q.y}, a, b);
                if(d < sep.value) {
                    sep.value = d;
                    sep.ps = O.shape;
                    sep.pi = O.idx[j];
                    sep.es = E.shape;
                    sep.e0 = i0;
                    sep.e1 = i1;
                }
            }
        }
    }
    return sep;
}

struct ShapeArg {
    bool point = false;   // Vec or circle
    bool circle = false;  // radius at data[2]
    ShapeKind kind = ShapeKind::Polygon;
    int n = 1;
    const ShapeMeta * meta = nullptr;
};

inline ShapeArg shape_arg(const ArgRef & a,
                          const std::vector<ShapeMeta> & metas) {
    ShapeArg s;
    if(a.type == ValueType::Vec) {
        s.point = true;
        return s;
    }
    s.meta = &metas[uz(a.meta)];
    s.kind = s.meta->kind;
    s.n = s.meta->n;
    if(s.kind == ShapeKind::Circle) {
        s.point = true;
        s.circle = true;
        s.n = 1;
    }
    return s;
}

inline void collect_pieces(int s, const ShapeArg & a, Scratch & sc,
                           std::vector<Piece> & out) {
    out.clear();
    const int n = a.n;
    if(a.kind == ShapeKind::Polyline) {
        for(int k = 0; k + 1 < n; ++k)
            out.push_back({s, sc.iota.data() + k, 2, 1.0});
        return;
    }
    const std::span<const Vec2d> V(sc.v[s].data(), uz(n));
    const bool convex =
        a.meta->convex == 1 || (a.meta->convex == -1 && is_convex(V));
    if(convex) {
        const double orient = a.meta->orient != 0
                                  ? a.meta->orient
                                  : (signed_area(V) >= 0.0 ? 1.0 : -1.0);
        out.push_back({s, sc.iota.data(), n, orient});
        return;
    }
    const std::vector<std::vector<int>> * pcs = &a.meta->pieces;
    if(a.meta->convex == -1) {
        sc.pieces[s] = convex_decomposition(V);
        pcs = &sc.pieces[s];
    }
    for(const std::vector<int> & p : *pcs)
        out.push_back({s, p.data(), static_cast<int>(p.size()), 1.0});
}

template <class T>
void load_values(int s, const ShapeArg & a, const T * d, Scratch & sc) {
    std::vector<Vec2d> & V = sc.v[s];
    V.resize(uz(a.n));
    for(int i = 0; i < a.n; ++i) V[uz(i)] = {val(d[2 * i]), val(d[2 * i + 1])};
}

template <class T>
T clearance(const ArgRef & ra, const ArgRef & rb, const T * da, const T * db,
            const std::vector<ShapeMeta> & metas, Scratch & sc) {
    const ShapeArg A = shape_arg(ra, metas), B = shape_arg(rb, metas);
    load_values(0, A, da, sc);
    load_values(1, B, db, sc);
    Feature f;
    if(A.point && B.point) {
        f.kind = Feature::Kind::PP;
        f.ps = 0;
        f.es = 1;
    } else if(A.point) {
        f = point_vs_shape(0, 0, 1, B.kind, B.n, sc);
    } else if(B.point) {
        f = point_vs_shape(1, 0, 0, A.kind, A.n, sc);
    } else {
        sc.ensure_iota(std::max(A.n, B.n));
        collect_pieces(0, A, sc, sc.piece_list[0]);
        collect_pieces(1, B, sc, sc.piece_list[1]);
        for(const Piece & p : sc.piece_list[0])
            for(const Piece & q : sc.piece_list[1]) {
                const Feature g = convex_pair(p, q, sc);
                if(g.value < f.value) f = g;
            }
    }
    T r = eval_feature(f, da, db);
    if(A.circle) r = r - da[2];
    if(B.circle) r = r - db[2];
    return r;
}

// min_x / max_x / min_y / max_y over points, vertices and circle extremes.
template <class T>
T extreme(Op op, const ArgRef * args, int nargs, const T * b,
          const std::vector<ShapeMeta> & metas) {
    const bool is_max = op == Op::MaxX || op == Op::MaxY;
    const int c = (op == Op::MinX || op == Op::MaxX) ? 0 : 1;
    double best = is_max ? -std::numeric_limits<double>::infinity()
                         : std::numeric_limits<double>::infinity();
    int barg = 0, bidx = 0;
    for(int k = 0; k < nargs; ++k) {
        const ArgRef & a = args[k];
        const T * d = b + a.slot;
        auto consider = [&](double v, int idx) {
            if(is_max ? v > best : v < best) {
                best = v;
                barg = k;
                bidx = idx;
            }
        };
        if(a.type == ValueType::Vec) {
            consider(val(d[c]), 0);
        } else {
            const ShapeMeta & m = metas[uz(a.meta)];
            if(m.kind == ShapeKind::Circle)
                consider(is_max ? val(d[c]) + val(d[2]) : val(d[c]) - val(d[2]),
                         -1);
            else
                for(int i = 0; i < m.n; ++i) consider(val(d[2 * i + c]), i);
        }
    }
    const T * d = b + args[barg].slot;
    if(bidx < 0) return is_max ? d[c] + d[2] : d[c] - d[2];
    return d[2 * bidx + c];
}

// max / min over g of dot(point, v).
template <class T>
T projection(bool is_max, const ArgRef & g, const T * d, const V2<T> & v,
             const std::vector<ShapeMeta> & metas) {
    const double vx = val(v.x), vy = val(v.y);
    if(g.type == ValueType::Vec) return dot(ld2(d), v);
    const ShapeMeta & m = metas[uz(g.meta)];
    if(m.kind == ShapeKind::Circle) {
        const T c = dot(ld2(d), v);
        const T r = d[2] * norm(v);
        return is_max ? c + r : c - r;
    }
    double best = is_max ? -std::numeric_limits<double>::infinity()
                         : std::numeric_limits<double>::infinity();
    int bi = 0;
    for(int i = 0; i < m.n; ++i) {
        const double p = val(d[2 * i]) * vx + val(d[2 * i + 1]) * vy;
        if(is_max ? p > best : p < best) {
            best = p;
            bi = i;
        }
    }
    return dot(ld2(d + 2 * bi), v);
}

// visible_fraction(eye, target, occluder...): the share of the target's length
// whose sight segment from the eye crosses no occluder's interior.
//
// For a target segment (a, b) and a convex occluder piece K, K clipped to the
// triangle (eye, a, b) is the part of K that sight segments to (a, b) can
// cross, and the rays from the eye through its vertices bound the hidden
// interval of the segment's parameter. The intervals of all pieces are merged,
// so overlapping shadows count once. Clipping and projection run in T; the
// extreme vertices and the merge are chosen on values.

// A circle occluder becomes this regular polygon, circumscribed: an inscribed
// one would report sight lines that cross the disk's rim as visible. Its
// corners stick out by 1 / cos(pi / 64) - 1 = 0.12 % of the radius.
inline constexpr int kCircleSides = 64;

struct CircleTable {
    double c[kCircleSides], s[kCircleSides];
    double scale;
};

inline const CircleTable & circle_table() {
    static const CircleTable t = [] {
        CircleTable r{};
        for(int i = 0; i < kCircleSides; ++i)
            ad::sin_cos(
                2.0 * std::numbers::pi * static_cast<double>(i) / kCircleSides,
                r.s[i], r.c[i]);
        r.scale = 1.0 / std::cos(std::numbers::pi / kCircleSides);
        return r;
    }();
    return t;
}

template <class T>
using Poly = std::vector<V2<T>>;

// Convex pieces of one occluder in T. A polyline gives its segments, which
// hide what lies behind them like thin walls.
template <class T>
void occluder_pieces(const ArgRef & r, const T * d,
                     const std::vector<ShapeMeta> & metas, Scratch & sc,
                     std::vector<Poly<T>> & out) {
    const ShapeArg s = shape_arg(r, metas);
    if(s.circle) {
        const CircleTable & ct = circle_table();
        const T R = d[2] * ct.scale;
        Poly<T> p(uz(kCircleSides));
        for(int i = 0; i < kCircleSides; ++i)
            p[uz(i)] = {d[0] + R * ct.c[i], d[1] + R * ct.s[i]};
        out.push_back(std::move(p));
        return;
    }
    if(s.point) return;
    load_values(0, s, d, sc);
    sc.ensure_iota(s.n);
    collect_pieces(0, s, sc, sc.piece_list[0]);
    for(const Piece & pc : sc.piece_list[0]) {
        Poly<T> p(uz(pc.n));
        for(int j = 0; j < pc.n; ++j) p[uz(j)] = ld2(d + 2 * pc.idx[j]);
        out.push_back(std::move(p));
    }
}

// The part of the convex polygon `in` left of (or on) the line p0 -> p1.
template <class T>
void clip_left(const Poly<T> & in, const V2<T> & p0, const V2<T> & p1,
               Poly<T> & out) {
    out.clear();
    const std::size_t n = in.size();
    const V2<T> e = p1 - p0;
    for(std::size_t i = 0; i < n; ++i) {
        const V2<T> & c = in[i];
        const V2<T> & nx = in[(i + 1) % n];
        const T dc = cross(e, c - p0), dn = cross(e, nx - p0);
        const double vc = val(dc), vn = val(dn);
        if(vc >= 0.0) out.push_back(c);
        if((vc > 0.0 && vn < 0.0) || (vc < 0.0 && vn > 0.0))
            out.push_back(c + scale(nx - c, dc / (dc - dn)));
    }
}

// Parameter u on a -> b where the ray from the eye along d meets the line ab,
// with A = a - eye and Bv = b - eye.
template <class T>
T ray_param(const V2<T> & d, const V2<T> & A, const V2<T> & Bv) {
    const T ca = cross(d, A), cb = cross(d, Bv);
    return ca / (ca - cb);
}

template <class T>
struct Shadow {
    double lo = 0.0, hi = 0.0;
    T tlo{}, thi{};
};

// Hidden interval of one clipped piece, clamped to [0, 1]; false when it
// hides nothing. A vertex within sqrt(near2) of the eye has no reliable
// direction: it exists only when the eye is on or inside the piece, and the
// piece's other vertices then bound the same rays.
template <class T>
bool shadow(const Poly<T> & poly, const V2<T> & eye, const V2<T> & A,
            const V2<T> & Bv, double near2, Shadow<T> & out) {
    const V2<double> Av{val(A.x), val(A.y)}, Bd{val(Bv.x), val(Bv.y)};
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    std::size_t ilo = 0, ihi = 0;
    for(std::size_t i = 0; i < poly.size(); ++i) {
        const V2<double> d{val(poly[i].x) - val(eye.x),
                           val(poly[i].y) - val(eye.y)};
        if(dot(d, d) <= near2) continue;
        const double u = ray_param(d, Av, Bd);
        if(u < lo) {
            lo = u;
            ilo = i;
        }
        if(u > hi) {
            hi = u;
            ihi = i;
        }
    }
    if(!(hi > 0.0 && lo < 1.0 && hi > lo)) return false;
    out.lo = std::max(lo, 0.0);
    out.hi = std::min(hi, 1.0);
    out.tlo = lo > 0.0 ? ray_param(poly[ilo] - eye, A, Bv) : T(0.0);
    out.thi = hi < 1.0 ? ray_param(poly[ihi] - eye, A, Bv) : T(1.0);
    return true;
}

// True when every vertex of `poly` is within `tol` of the line through p and
// q. A clipped piece that collapses onto the target's own line only touches
// the target from behind (a face on its own body, a wall along the target):
// sight segments end on it without crossing its interior.
template <class T>
bool on_line(const Poly<T> & poly, const V2<T> & p, const V2<T> & q,
             double tol) {
    const V2<double> pv{val(p.x), val(p.y)};
    const V2<double> e{val(q.x) - pv.x, val(q.y) - pv.y};
    const double lim = tol * norm(e);
    for(const V2<T> & v : poly)
        if(std::fabs(cross(e, V2<double>{val(v.x) - pv.x, val(v.y) - pv.y})) >
           lim)
            return false;
    return true;
}

// Hidden share of the parameter range [0, 1] of a -> b, for an eye off the
// line ab; `area` is cross(a - eye, b - eye).
template <class T>
T hidden_share(const V2<T> & eye, const V2<T> & a, const V2<T> & b,
               const std::vector<Poly<T>> & pieces, double area, double near) {
    const V2<T> A = a - eye, Bv = b - eye;
    const V2<T> & p = area > 0.0 ? a : b;
    const V2<T> & q = area > 0.0 ? b : a;
    // Clipping leaves vertices on the target line with a rounding error far
    // below this; a real occluder in front of the target is far thicker.
    const double line_tol =
        1e-10 * std::max(norm(V2<double>{val(A.x), val(A.y)}),
                         norm(V2<double>{val(Bv.x), val(Bv.y)}));
    Poly<T> c1, c2;
    std::vector<Shadow<T>> sh;
    for(const Poly<T> & k : pieces) {
        clip_left(k, eye, p, c1);
        clip_left(c1, p, q, c2);
        clip_left(c2, q, eye, c1);
        if(on_line(c1, p, q, line_tol)) continue;
        Shadow<T> s;
        if(shadow(c1, eye, A, Bv, near * near, s)) sh.push_back(s);
    }
    std::sort(sh.begin(), sh.end(),
              [](const Shadow<T> & x, const Shadow<T> & y) {
                  return x.lo < y.lo || (x.lo == y.lo && x.hi < y.hi);
              });
    T hidden(0.0);
    for(std::size_t k = 0; k < sh.size();) {
        const T lo = sh[k].tlo;
        T hi = sh[k].thi;
        double hv = sh[k].hi;
        for(++k; k < sh.size() && sh[k].lo <= hv; ++k)
            if(sh[k].hi > hv) {
                hv = sh[k].hi;
                hi = sh[k].thi;
            }
        hidden = hidden + (hi - lo);
    }
    return hidden;
}

// The eye on the target's line, which is the line through `eye` along the
// unit vector w; the target covers [sa, sb] in the coordinate s = dot(w, x -
// eye). Every sight segment runs along the line, so a piece the line enters
// hides everything beyond the entry point. Returns the hidden share of [sa,
// sb] or, when sa == sb, 1 if that point is hidden and 0 if not. Values only:
// the configuration is a single point of the design space.
template <class T>
double hidden_on_line(const V2<double> & eye, const V2<double> & w, double sa,
                      double sb, const std::vector<Poly<T>> & pieces) {
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<std::pair<double, double>> iv;
    std::vector<double> h, s;
    for(const Poly<T> & k : pieces) {
        const std::size_t n = k.size();
        h.assign(n, 0.0);
        s.assign(n, 0.0);
        double extent = std::max(std::fabs(sa), std::fabs(sb));
        for(std::size_t i = 0; i < n; ++i) {
            const V2<double> d{val(k[i].x) - eye.x, val(k[i].y) - eye.y};
            h[i] = cross(w, d);
            s[i] = dot(w, d);
            extent = std::max(extent, std::hypot(d.x, d.y));
        }
        const double tol = 1e-12 * extent;
        const auto [hmin, hmax] = std::minmax_element(h.begin(), h.end());
        if(!(*hmax > tol && *hmin < -tol)) continue;
        double s_in = inf, s_out = -inf;
        for(std::size_t i = 0; i < n; ++i) {
            const std::size_t j = (i + 1) % n;
            double x = inf;
            if(std::fabs(h[i]) <= tol)
                x = s[i];
            else if((h[i] > tol && h[j] < -tol) || (h[i] < -tol && h[j] > tol))
                x = s[i] + (s[j] - s[i]) * (h[i] / (h[i] - h[j]));
            else
                continue;
            s_in = std::min(s_in, x);
            s_out = std::max(s_out, x);
        }
        if(s_out > 0.0) iv.emplace_back(std::max(s_in, 0.0), inf);
        if(s_in < 0.0) iv.emplace_back(-inf, std::min(s_out, 0.0));
    }
    if(sa == sb) {
        for(const auto & [lo, hi] : iv)
            if(lo < sa && sa < hi) return 1.0;
        return 0.0;
    }
    for(auto & [lo, hi] : iv) {
        lo = std::max(lo, sa);
        hi = std::min(hi, sb);
    }
    std::sort(iv.begin(), iv.end());
    double hidden = 0.0, cur_lo = 0.0, cur_hi = -inf;
    for(const auto & [lo, hi] : iv) {
        if(!(hi > lo)) continue;
        if(lo > cur_hi) {
            if(cur_hi > cur_lo) hidden += cur_hi - cur_lo;
            cur_lo = lo;
            cur_hi = hi;
        } else {
            cur_hi = std::max(cur_hi, hi);
        }
    }
    if(cur_hi > cur_lo) hidden += cur_hi - cur_lo;
    return hidden / (sb - sa);
}

template <class T>
T visible_fraction(const ArgRef * A, int nargs, const T * b,
                   const std::vector<ShapeMeta> & metas, Scratch & sc) {
    const V2<T> eye = ld2(b + A[0].slot);
    const T * td = b + A[1].slot;
    const int nt = metas[uz(A[1].meta)].n;
    std::vector<Poly<T>> pieces;
    for(int k = 2; k < nargs; ++k)
        occluder_pieces(A[k], b + A[k].slot, metas, sc, pieces);
    // The selections below would turn a NaN input into a finite answer.
    bool finite = std::isfinite(val(eye.x)) && std::isfinite(val(eye.y));
    for(int i = 0; i < 2 * nt; ++i)
        finite = finite && std::isfinite(val(td[i]));
    for(const Poly<T> & k : pieces)
        for(const V2<T> & v : k)
            finite =
                finite && std::isfinite(val(v.x)) && std::isfinite(val(v.y));
    if(!finite) return T(std::numeric_limits<double>::quiet_NaN());

    const V2<double> ev{val(eye.x), val(eye.y)};
    T seen(0.0), total(0.0);
    for(int i = 0; i + 1 < nt; ++i) {
        const V2<T> p = ld2(td + 2 * i), q = ld2(td + 2 * i + 2);
        const T L = norm(q - p);
        const double Lv = val(L);
        if(!(Lv > 0.0)) continue;
        const V2<double> pa{val(p.x) - ev.x, val(p.y) - ev.y},
            pb{val(q.x) - ev.x, val(q.y) - ev.y};
        const double area = cross(pa, pb), na = norm(pa), nb = norm(pb);
        T hidden(0.0);
        if(std::fabs(area) > 1e-12 * na * nb) {
            hidden =
                hidden_share(eye, p, q, pieces, area, 1e-12 * std::max(na, nb));
        } else {
            const V2<double> w = scale(pb - pa, 1.0 / Lv);
            const double sa = dot(w, pa), sb = dot(w, pb);
            hidden = T(hidden_on_line(ev, w, std::min(sa, sb), std::max(sa, sb),
                                      pieces));
        }
        seen = seen + L * (1.0 - hidden);
        total = total + L;
    }
    if(val(total) > 0.0) return seen / total;
    // A target of zero length is one point, seen or hidden.
    const V2<double> d{val(td[0]) - ev.x, val(td[1]) - ev.y};
    const double r = norm(d);
    if(!(r > 0.0)) return T(1.0);
    return T(1.0 - hidden_on_line(ev, scale(d, 1.0 / r), r, r, pieces));
}

template <class T>
void dyad(const T * c1p, const T & r1, const T * c2p, const T & r2,
          const T & branch, T * o) {
    const V2<T> c1 = ld2(c1p), c2 = ld2(c2p);
    const V2<T> dv = c2 - c1;
    const T dk2 = dot(dv, dv);
    const T dk = ad::sqrt(dk2);
    const T r1s = r1 * r1, r2s = r2 * r2;
    const T q = dk2 + r1s - r2s;
    // Q = h^2 dk^2 = 4 area^2 of the (r1, r2, dk) triangle: negative exactly
    // when the circles miss, and smooth, so -Q / (r1^2 r2^2) gives the
    // assembly row a gradient even where the dyad is clamped.
    const T Q = r1s * dk2 - 0.25 * (q * q);
    const T den = r1s * r2s;
    o[2] = val(den) > 0.0 ? Q / den : Q;
    const double dkv = val(dk), r1v = val(r1), r2v = val(r2);
    o[3] = T(std::max(dkv - (r1v + r2v), std::fabs(r1v - r2v) - dkv));
    if(dkv > 1e-12 * std::max(1.0, std::fabs(r1v) + std::fabs(r2v))) {
        const V2<T> e{dv.x / dk, dv.y / dk};
        const T a = q / (2.0 * dk);
        const T h2 = Q / dk2;
        T h = val(h2) > 0.0 ? ad::sqrt(h2) : T(0.0);
        if(val(branch) < 0.0) h = -h;
        o[0] = c1.x + e.x * a - e.y * h;
        o[1] = c1.y + e.y * a + e.x * h;
    } else {
        o[0] = c1.x + r1;
        o[1] = c1.y;
    }
}

template <class T>
V2<T> centroid(const ShapeMeta & m, const T * d) {
    if(m.kind == ShapeKind::Circle) return ld2(d);
    const int n = m.n;
    if(m.kind == ShapeKind::Polygon && n >= 3) {
        T a2(0.0), cx(0.0), cy(0.0);
        for(int i = 0; i < n; ++i) {
            const int j = (i + 1) % n;
            const T cr = d[2 * i] * d[2 * j + 1] - d[2 * j] * d[2 * i + 1];
            a2 = a2 + cr;
            cx = cx + (d[2 * i] + d[2 * j]) * cr;
            cy = cy + (d[2 * i + 1] + d[2 * j + 1]) * cr;
        }
        if(val(a2) != 0.0) {
            const T k = 3.0 * a2;
            return {cx / k, cy / k};
        }
    }
    T sx(0.0), sy(0.0);
    for(int i = 0; i < n; ++i) {
        sx = sx + d[2 * i];
        sy = sy + d[2 * i + 1];
    }
    return {sx / static_cast<double>(n), sy / static_cast<double>(n)};
}

// Must stay the single implementation of chart evaluation: Chart::map uses it
// with T = double, so x -> value is bit-identical in the API and the evaluator.
template <class T>
void chart_map(const Chart & c, const T * u, T * out) {
    switch(c.kind) {
        case ChartKind::Fixed:
            break;
        case ChartKind::Interval:
            out[0] = c.lo + u[0] * (c.hi - c.lo);
            break;
        case ChartKind::Segment:
            out[0] = c.origin.x + u[0] * c.e1.x;
            out[1] = c.origin.y + u[0] * c.e1.y;
            break;
        case ChartKind::Parallelogram:
        case ChartKind::BoundingBox:
            out[0] = c.origin.x + u[0] * c.e1.x + u[1] * c.e2.x;
            out[1] = c.origin.y + u[0] * c.e1.y + u[1] * c.e2.y;
            break;
    }
}

template <class T>
void exec_instr(const Instr & I, const ArgRef * A,
                const std::vector<ShapeMeta> & metas, T * b, Scratch & sc) {
    T * o = b + I.out;
    auto s = [&](int k) -> const T & { return b[A[k].slot]; };
    auto p = [&](int k) -> const T * { return b + A[k].slot; };
    auto v = [&](int k) { return ld2(b + A[k].slot); };
    switch(I.op) {
        case Op::Const:
        case Op::Var:
        case Op::Sweep:
            break;
        case Op::Add:
            o[0] = s(0) + s(1);
            break;
        case Op::Sub:
            o[0] = s(0) - s(1);
            break;
        case Op::Mul:
            o[0] = s(0) * s(1);
            break;
        case Op::Div:
            o[0] = s(0) / s(1);
            break;
        case Op::Neg:
            o[0] = -s(0);
            break;
        case Op::Pow:
            o[0] = ad::pow(s(0), s(1));
            break;
        case Op::Sqrt:
            o[0] = ad::sqrt(s(0));
            break;
        case Op::Sin:
            o[0] = ad::sin(s(0));
            break;
        case Op::Cos:
            o[0] = ad::cos(s(0));
            break;
        case Op::Tan:
            o[0] = ad::tan(s(0));
            break;
        case Op::Asin:
            o[0] = ad::asin(s(0));
            break;
        case Op::Acos:
            o[0] = ad::acos(s(0));
            break;
        case Op::Atan:
            o[0] = ad::atan(s(0));
            break;
        case Op::Atan2:
            o[0] = ad::atan2(s(0), s(1));
            break;
        case Op::Abs:
            o[0] = ad::abs(s(0));
            break;
        case Op::Exp:
            o[0] = ad::exp(s(0));
            break;
        case Op::Log:
            o[0] = ad::log(s(0));
            break;
        case Op::Sq:
            o[0] = s(0) * s(0);
            break;
        case Op::Min:
        case Op::Max: {
            int best = 0;
            for(int k = 1; k < I.arg_count; ++k) {
                const double vk = val(s(k)), vb = val(s(best));
                if(I.op == Op::Min ? vk < vb : vk > vb) best = k;
            }
            o[0] = s(best);
            break;
        }
        case Op::Clamp: {
            const double x = val(s(0));
            o[0] = x < val(s(1)) ? s(1) : (x > val(s(2)) ? s(2) : s(0));
            break;
        }
        case Op::VAdd:
            st2(o, v(0) + v(1));
            break;
        case Op::VSub:
            st2(o, v(0) - v(1));
            break;
        case Op::VNeg:
            o[0] = -p(0)[0];
            o[1] = -p(0)[1];
            break;
        case Op::VScale:
            st2(o, scale(v(0), s(1)));
            break;
        case Op::VDiv:
            o[0] = p(0)[0] / s(1);
            o[1] = p(0)[1] / s(1);
            break;
        case Op::MakeVec:
            o[0] = s(0);
            o[1] = s(1);
            break;
        case Op::GetX:
            o[0] = p(0)[0];
            break;
        case Op::GetY:
            o[0] = p(0)[1];
            break;
        case Op::Dir:
            ad::cis(s(0), o[0], o[1]);
            break;
        case Op::Rotate:
            st2(o, rot(v(0), v(1)));
            break;
        case Op::Perp:
            o[0] = -p(0)[1];
            o[1] = p(0)[0];
            break;
        case Op::Dot:
            o[0] = dot(v(0), v(1));
            break;
        case Op::Cross:
            o[0] = cross(v(0), v(1));
            break;
        case Op::Norm:
            o[0] = norm(v(0));
            break;
        case Op::Normalize: {
            const V2<T> a = v(0);
            const T L = norm(a);
            o[0] = a.x / L;
            o[1] = a.y / L;
            break;
        }
        case Op::Dist:
            o[0] = norm(v(0) - v(1));
            break;
        case Op::Angle:
            o[0] = ad::atan2(p(0)[1], p(0)[0]);
            break;
        case Op::AngleBetween: {
            const V2<T> a = v(0), c = v(1);
            o[0] = ad::atan2(cross(a, c), dot(a, c));
            break;
        }
        case Op::SinBetween: {
            const V2<T> a = v(0), c = v(1);
            o[0] = cross(a, c) / (norm(a) * norm(c));
            break;
        }
        case Op::Mean: {
            T sx = p(0)[0], sy = p(0)[1];
            for(int k = 1; k < I.arg_count; ++k) {
                sx = sx + p(k)[0];
                sy = sy + p(k)[1];
            }
            o[0] = sx / static_cast<double>(I.arg_count);
            o[1] = sy / static_cast<double>(I.arg_count);
            break;
        }
        case Op::MeanS: {
            T sx = s(0);
            for(int k = 1; k < I.arg_count; ++k) sx = sx + s(k);
            o[0] = sx / static_cast<double>(I.arg_count);
            break;
        }
        case Op::Box:
            o[0] = s(0);
            o[1] = s(1);
            o[2] = s(2);
            o[3] = s(1);
            o[4] = s(2);
            o[5] = s(3);
            o[6] = s(0);
            o[7] = s(3);
            break;
        case Op::Rect: {
            const V2<T> c = v(0), cs = v(3);
            const T hw = s(1) * 0.5, hh = s(2) * 0.5;
            const T mhw = -hw, mhh = -hh;
            st2(o, c + rot(V2<T>{mhw, mhh}, cs));
            st2(o + 2, c + rot(V2<T>{hw, mhh}, cs));
            st2(o + 4, c + rot(V2<T>{hw, hh}, cs));
            st2(o + 6, c + rot(V2<T>{mhw, hh}, cs));
            break;
        }
        case Op::MakePolygon:
        case Op::MakePolyline:
            for(int k = 0; k < I.arg_count; ++k) {
                o[2 * k] = p(k)[0];
                o[2 * k + 1] = p(k)[1];
            }
            break;
        case Op::MakeCircle:
            o[0] = p(0)[0];
            o[1] = p(0)[1];
            o[2] = s(1);
            break;
        case Op::Place: {
            const V2<T> t = v(1), cs = v(2);
            if(A[0].type == ValueType::Vec) {
                st2(o, rot(v(0), cs) + t);
                break;
            }
            const ShapeMeta & m = metas[uz(A[0].meta)];
            const T * g = p(0);
            if(m.kind == ShapeKind::Circle) {
                st2(o, rot(ld2(g), cs) + t);
                o[2] = g[2];
            } else {
                for(int i = 0; i < m.n; ++i)
                    st2(o + 2 * i, rot(ld2(g + 2 * i), cs) + t);
            }
            break;
        }
        case Op::Translate: {
            const V2<T> t = v(1);
            if(A[0].type == ValueType::Vec) {
                st2(o, v(0) + t);
                break;
            }
            const ShapeMeta & m = metas[uz(A[0].meta)];
            const T * g = p(0);
            if(m.kind == ShapeKind::Circle) {
                st2(o, ld2(g) + t);
                o[2] = g[2];
            } else {
                for(int i = 0; i < m.n; ++i) st2(o + 2 * i, ld2(g + 2 * i) + t);
            }
            break;
        }
        case Op::Vertex:
            o[0] = p(0)[2 * I.aux];
            o[1] = p(0)[2 * I.aux + 1];
            break;
        case Op::Center: {
            if(A[0].type == ValueType::Vec) {
                st2(o, v(0));
                break;
            }
            st2(o, centroid(metas[uz(A[0].meta)], p(0)));
            break;
        }
        case Op::Clearance:
            o[0] = clearance(A[0], A[1], p(0), p(1), metas, sc);
            break;
        case Op::MinX:
        case Op::MaxX:
        case Op::MinY:
        case Op::MaxY:
            o[0] = extreme(I.op, A, I.arg_count, b, metas);
            break;
        case Op::MaxProj:
            o[0] = projection(true, A[0], p(0), v(1), metas);
            break;
        case Op::MinProj:
            o[0] = projection(false, A[0], p(0), v(1), metas);
            break;
        case Op::VisibleFraction:
            o[0] = visible_fraction(A, I.arg_count, b, metas, sc);
            break;
        case Op::Dyad:
            dyad(p(0), s(1), p(2), s(3), s(4), o);
            break;
        case Op::BranchOf: {
            const Vec2d c1 = vval(p(0)), c2 = vval(p(1)), q = vval(p(2));
            const double cr =
                (c2.x - c1.x) * (q.y - c1.y) - (c2.y - c1.y) * (q.x - c1.x);
            o[0] = T(cr >= 0.0 ? 1.0 : -1.0);
            break;
        }
    }
}

}  // namespace gs::detail

#include "gs/engine/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace gs {
namespace {

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

double cross3(Vec2d a, Vec2d b, Vec2d c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// Closed-triangle test for a CCW triangle; eps absorbs round-off on edges.
bool in_triangle(Vec2d p, Vec2d a, Vec2d b, Vec2d c, double eps) {
    return cross3(a, b, p) >= -eps && cross3(b, c, p) >= -eps &&
           cross3(c, a, p) >= -eps;
}

bool piece_convex(std::span<const Vec2d> poly, const std::vector<int> & idx,
                  double eps) {
    const std::size_t n = idx.size();
    for(std::size_t i = 0; i < n; ++i) {
        const Vec2d a = poly[uz(idx[i])], b = poly[uz(idx[(i + 1) % n])],
                    c = poly[uz(idx[(i + 2) % n])];
        if(cross3(a, b, c) < -eps) return false;
    }
    return true;
}

}  // namespace

double signed_area(std::span<const Vec2d> poly) {
    double a = 0.0;
    const std::size_t n = poly.size();
    for(std::size_t i = 0; i < n; ++i) {
        const Vec2d p = poly[i], q = poly[(i + 1) % n];
        a += p.x * q.y - p.y * q.x;
    }
    return 0.5 * a;
}

bool is_convex(std::span<const Vec2d> poly) {
    const std::size_t n = poly.size();
    if(n < 3) return true;
    int sign = 0;
    for(std::size_t i = 0; i < n; ++i) {
        const double c = cross3(poly[i], poly[(i + 1) % n], poly[(i + 2) % n]);
        const int s = c > 0.0 ? 1 : (c < 0.0 ? -1 : 0);
        if(s == 0) continue;
        if(sign == 0) sign = s;
        if(s != sign) return false;
    }
    return true;
}

bool point_in_polygon(Vec2d p, std::span<const Vec2d> poly) {
    bool inside = false;
    const std::size_t n = poly.size();
    for(std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const Vec2d a = poly[i], b = poly[j];
        if((a.y > p.y) != (b.y > p.y) &&
           p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

std::vector<std::vector<int>> convex_decomposition(
    std::span<const Vec2d> poly) {
    const int n = static_cast<int>(poly.size());
    std::vector<int> ring(uz(n));
    for(int i = 0; i < n; ++i) ring[uz(i)] = i;
    if(signed_area(poly) < 0.0) std::reverse(ring.begin(), ring.end());
    if(n <= 3 || is_convex(poly)) return {ring};

    // Relative to the polygon's own extent: a tolerance from the absolute
    // coordinates swamps every turn of a small polygon far from the origin
    // and clips away all of its pieces. The second term covers the rounding
    // of coordinate differences far from the origin.
    double x0 = poly[0].x, x1 = x0, y0 = poly[0].y, y1 = y0, far = 0.0;
    for(const Vec2d & p : poly) {
        x0 = std::min(x0, p.x);
        x1 = std::max(x1, p.x);
        y0 = std::min(y0, p.y);
        y1 = std::max(y1, p.y);
        far = std::max({far, std::fabs(p.x), std::fabs(p.y)});
    }
    const double extent = std::max(x1 - x0, y1 - y0);
    const double eps = std::max(
        {1e-12 * extent * extent,
         8.0 * std::numeric_limits<double>::epsilon() * far * extent, 1e-300});

    std::vector<std::vector<int>> tris;
    std::vector<int> rem = ring;
    while(rem.size() > 3) {
        const std::size_t m = rem.size();
        std::size_t ear = m;
        for(std::size_t i = 0; i < m && ear == m; ++i) {
            const int ia = rem[(i + m - 1) % m], ib = rem[i],
                      ic = rem[(i + 1) % m];
            const Vec2d a = poly[uz(ia)], b = poly[uz(ib)], c = poly[uz(ic)];
            if(cross3(a, b, c) <= eps) continue;
            bool blocked = false;
            for(std::size_t k = 0; k < m && !blocked; ++k) {
                const int iv = rem[k];
                if(iv == ia || iv == ib || iv == ic) continue;
                const Vec2d v = poly[uz(iv)];
                if((v.x == a.x && v.y == a.y) || (v.x == b.x && v.y == b.y) ||
                   (v.x == c.x && v.y == c.y))
                    continue;
                blocked = in_triangle(v, a, b, c, eps);
            }
            if(!blocked) ear = i;
        }
        if(ear == m) {
            // No clean ear (degenerate or self-intersecting input): drop the
            // vertex with the largest turn so the loop always terminates.
            double best = -1e300;
            for(std::size_t i = 0; i < m; ++i) {
                const double c =
                    cross3(poly[uz(rem[(i + m - 1) % m])], poly[uz(rem[i])],
                           poly[uz(rem[(i + 1) % m])]);
                if(c > best) {
                    best = c;
                    ear = i;
                }
            }
        }
        const int ia = rem[(ear + m - 1) % m], ib = rem[ear],
                  ic = rem[(ear + 1) % m];
        if(cross3(poly[uz(ia)], poly[uz(ib)], poly[uz(ic)]) > eps)
            tris.push_back({ia, ib, ic});
        rem.erase(rem.begin() + static_cast<std::ptrdiff_t>(ear));
    }
    if(cross3(poly[uz(rem[0])], poly[uz(rem[1])], poly[uz(rem[2])]) > eps)
        tris.push_back(rem);

    // Hertel-Mehlhorn: remove a diagonal whenever the union stays convex.
    bool merged = true;
    while(merged) {
        merged = false;
        for(std::size_t i = 0; i < tris.size() && !merged; ++i) {
            for(std::size_t j = i + 1; j < tris.size() && !merged; ++j) {
                const std::vector<int> & P = tris[i];
                const std::vector<int> & Q = tris[j];
                for(std::size_t a = 0; a < P.size() && !merged; ++a) {
                    const int u = P[a], v = P[(a + 1) % P.size()];
                    for(std::size_t b = 0; b < Q.size(); ++b) {
                        if(Q[b] != v || Q[(b + 1) % Q.size()] != u) continue;
                        std::vector<int> U;
                        for(std::size_t k = 0; k <= a; ++k) U.push_back(P[k]);
                        for(std::size_t k = 2; k < Q.size(); ++k)
                            U.push_back(Q[(b + k) % Q.size()]);
                        for(std::size_t k = a + 1; k < P.size(); ++k)
                            U.push_back(P[k]);
                        if(piece_convex(poly, U, eps)) {
                            tris[i] = std::move(U);
                            tris.erase(tris.begin() +
                                       static_cast<std::ptrdiff_t>(j));
                            merged = true;
                        }
                        break;
                    }
                }
            }
        }
    }
    return tris;
}

}  // namespace gs

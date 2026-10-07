#include "gs/engine/model.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>

#include "exec.hpp"
#include "gs/engine/evaluator.hpp"
#include "gs/engine/geometry.hpp"

namespace gs {
namespace {

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

double clamp01(double u) {
    return std::isfinite(u) ? std::clamp(u, 0.0, 1.0) : 0.5;
}

Vec2d nearest_on_segment(Vec2d p, Vec2d a, Vec2d b) {
    const double ex = b.x - a.x, ey = b.y - a.y;
    const double L2 = ex * ex + ey * ey;
    double t = L2 > 0.0 ? ((p.x - a.x) * ex + (p.y - a.y) * ey) / L2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return {a.x + t * ex, a.y + t * ey};
}

// Nearest point of a polygon (solid) or circle (disc) domain.
Vec2d project_into(const GeoValue & dom, Vec2d p) {
    if(dom.kind == ShapeKind::Circle) {
        const Vec2d c = dom.circle_center();
        const double r = dom.circle_radius();
        const double dx = p.x - c.x, dy = p.y - c.y, d = std::hypot(dx, dy);
        if(d <= r || d == 0.0) return p;
        return {c.x + dx * r / d, c.y + dy * r / d};
    }
    const int n = dom.vertex_count();
    std::vector<Vec2d> v(uz(n));
    for(int i = 0; i < n; ++i) v[uz(i)] = dom.vertex(i);
    if(dom.kind == ShapeKind::Polygon && point_in_polygon(p, v)) return p;
    Vec2d best = p;
    double bd = std::numeric_limits<double>::infinity();
    const int edges = dom.kind == ShapeKind::Polygon ? n : n - 1;
    for(int k = 0; k < edges; ++k) {
        const Vec2d q = nearest_on_segment(p, v[uz(k)], v[uz((k + 1) % n)]);
        const double d = std::hypot(q.x - p.x, q.y - p.y);
        if(d < bd) {
            bd = d;
            best = q;
        }
    }
    return best;
}

}  // namespace

int Chart::dims() const {
    switch(kind) {
        case ChartKind::Fixed:
            return 0;
        case ChartKind::Interval:
        case ChartKind::Segment:
            return 1;
        case ChartKind::Parallelogram:
        case ChartKind::BoundingBox:
            return 2;
    }
    return 0;
}

void Chart::map(const double * u, double * value) const {
    detail::chart_map<double>(*this, u, value);
}

void Chart::unmap(const double * value, double * u) const {
    switch(kind) {
        case ChartKind::Fixed:
            break;
        case ChartKind::Interval:
            u[0] = hi > lo ? clamp01((value[0] - lo) / (hi - lo)) : 0.0;
            break;
        case ChartKind::Segment: {
            const double L2 = e1.x * e1.x + e1.y * e1.y;
            u[0] = L2 > 0.0 ? clamp01(((value[0] - origin.x) * e1.x +
                                       (value[1] - origin.y) * e1.y) /
                                      L2)
                            : 0.0;
            break;
        }
        case ChartKind::Parallelogram:
        case ChartKind::BoundingBox: {
            const double dx = value[0] - origin.x, dy = value[1] - origin.y;
            const double det = e1.x * e2.y - e1.y * e2.x;
            if(det != 0.0) {
                u[0] = clamp01((dx * e2.y - dy * e2.x) / det);
                u[1] = clamp01((e1.x * dy - e1.y * dx) / det);
            } else {
                const double L1 = e1.x * e1.x + e1.y * e1.y,
                             L2 = e2.x * e2.x + e2.y * e2.y;
                u[0] = L1 > 0.0 ? clamp01((dx * e1.x + dy * e1.y) / L1) : 0.0;
                u[1] = L2 > 0.0 ? clamp01((dx * e2.x + dy * e2.y) / L2) : 0.0;
            }
            break;
        }
    }
}

Model::Model() = default;
Model::~Model() = default;

int Model::n() const { return n_; }

namespace {
template <class V>
int find_named(const std::vector<V> & v, std::string_view name) {
    for(std::size_t i = 0; i < v.size(); ++i)
        if(v[i].name == name) return static_cast<int>(i);
    return -1;
}
}  // namespace

int Model::find_var(std::string_view name) const {
    return find_named(design_, name);
}
int Model::find_sweep(std::string_view name) const {
    return find_named(sweeps_, name);
}
int Model::find_let(std::string_view name) const {
    return find_named(lets_, name);
}
int Model::find_criterion(std::string_view name) const {
    return find_named(criteria_, name);
}
int Model::find_constraint(std::string_view name) const {
    return find_named(constraints_, name);
}
int Model::find_param(std::string_view name) const {
    return find_named(params_, name);
}

std::vector<std::string> Model::coordinate_names() const {
    std::vector<std::string> out;
    for(const DesignVar & v : design_) {
        if(v.fixed) continue;
        if(v.chart.dims() == 1)
            out.push_back(v.name);
        else if(v.chart.dims() == 2) {
            out.push_back(v.name + ".u");
            out.push_back(v.name + ".v");
        }
    }
    return out;
}

int Model::values_size() const { return values_size_; }

std::vector<double> Model::initial_x() const {
    std::vector<double> values(uz(values_size_), 0.0);
    for(const DesignVar & v : design_)
        for(std::size_t k = 0; k < v.value.size(); ++k)
            values[uz(v.value_offset) + k] = v.value[k];
    return x_from_values(values);
}

std::vector<double> Model::values_from_x(std::span<const double> x) const {
    if(static_cast<int>(x.size()) != n_)
        throw std::invalid_argument(std::format(
            "x has {} coordinates, the model has {}", x.size(), n_));
    std::vector<double> out(uz(values_size_), 0.0);
    for(const DesignVar & v : design_) {
        double * dst = out.data() + v.value_offset;
        if(v.fixed) {
            std::copy(v.value.begin(), v.value.end(), dst);
            continue;
        }
        v.chart.map(x.data() + v.coord, dst);
    }
    return out;
}

std::vector<double> Model::x_from_values(std::span<const double> values) const {
    if(static_cast<int>(values.size()) != values_size_)
        throw std::invalid_argument(std::format(
            "{} values given, the model has {}", values.size(), values_size_));
    std::vector<double> x(uz(n_), 0.5);
    for(std::size_t i = 0; i < design_.size(); ++i)
        if(!design_[i].fixed)
            set_var_value(static_cast<int>(i),
                          values.subspan(uz(design_[i].value_offset),
                                         uz(design_[i].size())),
                          x);
    return x;
}

std::vector<double> Model::var_value(int var, std::span<const double> x) const {
    const DesignVar & v = design_.at(uz(var));
    if(static_cast<int>(x.size()) != n_)
        throw std::invalid_argument(std::format(
            "x has {} coordinates, the model has {}", x.size(), n_));
    if(v.fixed) return v.value;
    std::vector<double> out(uz(v.size()));
    v.chart.map(x.data() + v.coord, out.data());
    return out;
}

void Model::set_var_value(int var, std::span<const double> value,
                          std::span<double> x) const {
    const DesignVar & v = design_.at(uz(var));
    if(static_cast<int>(x.size()) != n_)
        throw std::invalid_argument(std::format(
            "x has {} coordinates, the model has {}", x.size(), n_));
    if(v.fixed) return;
    if(value.size() != uz(v.size()))
        throw std::invalid_argument(
            std::format("'{}' takes {} numbers", v.name, v.size()));
    double val[2] = {value[0], value.size() > 1 ? value[1] : 0.0};
    // Clamping oblique (u, v) coordinates is a projection only when e1 and
    // e2 are orthogonal; a skewed parallelogram needs the true nearest point.
    const Chart & ch = v.chart;
    const bool skewed =
        ch.kind == ChartKind::Parallelogram &&
        std::fabs(ch.e1.x * ch.e2.x + ch.e1.y * ch.e2.y) >
            1e-12 * std::hypot(ch.e1.x, ch.e1.y) * std::hypot(ch.e2.x, ch.e2.y);
    if(v.chart.kind == ChartKind::BoundingBox || skewed) {
        const Vec2d q = project_into(v.domain, {val[0], val[1]});
        val[0] = q.x;
        val[1] = q.y;
    }
    v.chart.unmap(val, x.data() + v.coord);
}

NlpLayout Model::nlp_layout(const SampleSets & samples) const {
    NlpLayout L;
    L.n = n_;
    auto ns = [&](int k) {
        return k < 0 ? 1 : static_cast<int>(samples.t.at(uz(k)).size());
    };
    for(std::size_t g = 0; g < groups_.size(); ++g) {
        L.group_offset.push_back(static_cast<int>(L.rows.size()));
        const RowGroup & G = groups_[g];
        if(G.sweep < 0) {
            for(int s = 0; s < G.split; ++s)
                L.rows.push_back({static_cast<int>(g), s, -1});
        } else {
            for(int i = 0; i < ns(G.sweep); ++i)
                for(int s = 0; s < G.split; ++s)
                    L.rows.push_back({static_cast<int>(g), s, i});
        }
    }
    L.group_offset.push_back(static_cast<int>(L.rows.size()));
    for(const detail::ObjTerm & o : program_->objective) {
        if(o.epigraph) {
            for(int i = 0; i < ns(o.term.sweep); ++i)
                L.objective.push_back({o.criterion, o.weight, i, true});
        } else {
            L.objective.push_back({o.criterion, o.weight, -1, false});
        }
    }
    return L;
}

std::string Model::row_name(const NlpRow & row,
                            const SampleSets & samples) const {
    const RowGroup & G = groups_.at(uz(row.group));
    std::string s = G.name;
    if(G.split > 1) s += row.split == 0 ? "(+)" : "(-)";
    if(G.sweep >= 0 && row.sample >= 0) {
        const SweepInfo & sw = sweeps_[uz(G.sweep)];
        s +=
            std::format("[{}={:.6g}]", sw.name,
                        sw.value(samples.t.at(uz(G.sweep)).at(uz(row.sample))));
    }
    return s;
}

}  // namespace gs

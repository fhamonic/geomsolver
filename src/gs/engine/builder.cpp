#include "builder.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gs::detail {
namespace {

void put_bytes(std::string & key, const void * p, std::size_t n) {
    key.append(static_cast<const char *>(p), n);
}
template <class V>
void put(std::string & key, const V & v) {
    put_bytes(key, &v, sizeof v);
}

}  // namespace

int Builder::push(Node n, std::span<const int> args, const std::string & key) {
    n.arg_begin = static_cast<int>(P.node_args.size());
    n.arg_count = static_cast<int>(args.size());
    P.node_args.insert(P.node_args.end(), args.begin(), args.end());
    const int id = static_cast<int>(P.nodes.size());
    P.nodes.push_back(n);
    cse_.emplace(key, id);
    return id;
}

int Builder::constant(ValueType type, ShapeKind kind,
                      std::span<const double> data) {
    std::string key = "C";
    put(key, type);
    put(key, kind);
    put_bytes(key, data.data(), data.size_bytes());
    if(auto it = cse_.find(key); it != cse_.end()) return it->second;
    Node n;
    n.op = Op::Const;
    n.type = type;
    n.dep = DepKind::Const;
    n.aux = static_cast<int>(P.pool.size());
    n.size = static_cast<int>(data.size());
    P.pool.insert(P.pool.end(), data.begin(), data.end());
    if(type == ValueType::Shape) {
        ShapeMeta m;
        m.kind = kind;
        if(kind != ShapeKind::Circle) {
            m.n = static_cast<int>(data.size() / 2);
            std::vector<Vec2d> pts(uz(m.n));
            for(std::size_t i = 0; i < pts.size(); ++i)
                pts[i] = {data[2 * i], data[2 * i + 1]};
            if(kind == ShapeKind::Polygon) {
                m.orient = signed_area(pts) >= 0.0 ? 1 : -1;
                m.convex = is_convex(pts) ? 1 : 0;
                if(m.convex == 0) m.pieces = convex_decomposition(pts);
            }
        }
        n.meta = static_cast<int>(P.metas.size());
        P.metas.push_back(std::move(m));
    }
    return push(n, {}, key);
}

int Builder::var(int index, ValueType type) {
    std::string key = "V";
    put(key, index);
    if(auto it = cse_.find(key); it != cse_.end()) return it->second;
    Node n;
    n.op = Op::Var;
    n.type = type;
    n.dep = DepKind::Design;
    n.aux = index;
    n.size = type == ValueType::Scalar ? 1 : 2;
    return push(n, {}, key);
}

int Builder::sweep(int index) {
    std::string key = "S";
    put(key, index);
    if(auto it = cse_.find(key); it != cse_.end()) return it->second;
    Node n;
    n.op = Op::Sweep;
    n.type = ValueType::Scalar;
    n.dep = DepKind::Sweep;
    n.sweep = index;
    n.aux = index;
    n.size = 1;
    return push(n, {}, key);
}

Builder::Inferred Builder::infer(Op op, std::span<const int> args) const {
    Inferred r;
    auto shape = [&](ShapeKind k, int nv) {
        r.type = ValueType::Shape;
        r.has_meta = true;
        r.meta.kind = k;
        r.meta.n = nv;
        r.size = k == ShapeKind::Circle ? 3 : 2 * nv;
    };
    switch(op) {
        case Op::VAdd:
        case Op::VSub:
        case Op::VNeg:
        case Op::VScale:
        case Op::VDiv:
        case Op::MakeVec:
        case Op::Dir:
        case Op::Rotate:
        case Op::Perp:
        case Op::Normalize:
        case Op::Mean:
        case Op::Vertex:
        case Op::Center:
            r.type = ValueType::Vec;
            r.size = 2;
            break;
        case Op::Dyad:
            r.type = ValueType::Vec;
            r.size = 4;
            break;
        case Op::Box:
        case Op::Rect:
            shape(ShapeKind::Polygon, 4);
            r.meta.convex = 1;
            break;
        case Op::MakePolygon:
            shape(ShapeKind::Polygon, static_cast<int>(args.size()));
            break;
        case Op::MakePolyline:
            shape(ShapeKind::Polyline, static_cast<int>(args.size()));
            break;
        case Op::MakeCircle:
            shape(ShapeKind::Circle, 0);
            break;
        case Op::Place:
        case Op::Translate: {
            const Node & g = node(args[0]);
            if(g.type == ValueType::Vec) {
                r.type = ValueType::Vec;
                r.size = 2;
            } else {
                r.type = ValueType::Shape;
                r.size = g.size;
                r.has_meta = true;
                // A rigid motion keeps orientation, convexity and the convex
                // pieces: a placed constant polygon reuses its decomposition.
                r.meta = P.metas[uz(g.meta)];
            }
            break;
        }
        default:
            r.type = ValueType::Scalar;
            r.size = 1;
            break;
    }
    return r;
}

int Builder::fold(Op op, std::span<const int> args, int aux,
                  const Inferred & inf) {
    std::vector<double> buf;
    std::vector<ArgRef> refs;
    for(const int a : args) {
        const Node & n = node(a);
        refs.push_back({static_cast<int>(buf.size()), n.type, n.meta});
        buf.insert(buf.end(), P.pool.begin() + n.aux,
                   P.pool.begin() + n.aux + n.size);
    }
    Instr I;
    I.op = op;
    I.out = static_cast<int>(buf.size());
    I.aux = aux;
    I.arg_count = static_cast<int>(args.size());
    buf.resize(buf.size() + uz(inf.size));
    exec_instr<double>(I, refs.data(), P.metas, buf.data(), scratch_);
    if(op == Op::Dyad) {
        const double gap = buf[uz(I.out) + 3];
        const double r1 = buf[uz(refs[1].slot)], r2 = buf[uz(refs[3].slot)];
        if(!(gap <= 1e-9 * std::max(1.0, std::fabs(r1) + std::fabs(r2))))
            failed_dyads_.push_back(gap);
    }
    const int keep = op == Op::Dyad ? 2 : inf.size;
    return constant(inf.type, inf.meta.kind,
                    std::span<const double>(buf.data() + I.out, uz(keep)));
}

int Builder::make(Op op, std::span<const int> args, int aux) {
    DepKind dep = DepKind::Const;
    int sw = -1;
    for(const int a : args) {
        const Node & n = node(a);
        if(n.dep == DepKind::Sweep) {
            if(sw >= 0 && sw != n.sweep) {
                conflict_ = {sw, n.sweep};
                return kTwoSweeps;
            }
            sw = n.sweep;
            dep = DepKind::Sweep;
        } else if(n.dep == DepKind::Design && dep == DepKind::Const) {
            dep = DepKind::Design;
        }
    }
    const Inferred inf = infer(op, args);
    if(dep == DepKind::Const) return fold(op, args, aux, inf);

    std::string key = "N";
    put(key, op);
    put(key, aux);
    for(const int a : args) put(key, a);
    if(auto it = cse_.find(key); it != cse_.end()) return it->second;

    Node n;
    n.op = op;
    n.type = inf.type;
    n.dep = dep;
    n.sweep = sw;
    n.aux = aux;
    n.size = inf.size;
    if(inf.has_meta) {
        n.meta = static_cast<int>(P.metas.size());
        P.metas.push_back(inf.meta);
    }
    return push(n, args, key);
}

std::vector<double> Builder::values(int id) const {
    const Node & n = node(id);
    return {P.pool.begin() + n.aux, P.pool.begin() + n.aux + n.size};
}

GeoValue Builder::geo(int id) const {
    const Node & n = node(id);
    GeoValue g;
    g.type = n.type;
    if(n.meta >= 0) g.kind = P.metas[uz(n.meta)].kind;
    g.data = values(id);
    return g;
}

}  // namespace gs::detail

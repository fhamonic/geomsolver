#pragma once

#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "exec.hpp"
#include "program.hpp"

namespace gs::detail {

// Returned by Builder::make when the arguments depend on two different free
// sweeps; Builder::conflict() names them.
inline constexpr int kTwoSweeps = -2;

// Hash-consed node DAG under construction. make() folds constant
// sub-expressions immediately (with the same op code the evaluator runs) and
// merges structurally identical nodes, so at() instantiations of shared
// sub-expressions are built once.
class Builder {
public:
    explicit Builder(Program & p) : P(p) {}

    const Node & node(int id) const { return P.nodes[uz(id)]; }
    std::span<const int> args(int id) const {
        const Node & n = node(id);
        return {P.node_args.data() + n.arg_begin, uz(n.arg_count)};
    }
    std::pair<int, int> conflict() const { return conflict_; }

    int constant(ValueType type, ShapeKind kind, std::span<const double> data);
    int scalar(double v) {
        return constant(ValueType::Scalar, ShapeKind::Polygon,
                        std::span<const double>(&v, 1));
    }
    int vec(double x, double y) {
        const double d[2] = {x, y};
        return constant(ValueType::Vec, ShapeKind::Polygon, d);
    }
    int var(int index, ValueType type);
    int sweep(int index);
    int make(Op op, std::span<const int> args, int aux = 0);
    int make(Op op, std::initializer_list<int> args, int aux = 0) {
        return make(op, std::span<const int>(args.begin(), args.size()), aux);
    }

    // Gaps (m) of the constant dyads folded since the last call whose
    // circles miss; folding keeps only the point, so the caller must report
    // them or the linkage would pass as assembled.
    std::vector<double> take_failed_dyads() {
        return std::exchange(failed_dyads_, {});
    }

    std::vector<double> values(int const_id) const;
    GeoValue geo(int const_id) const;
    ShapeKind kind(int id) const {
        return node(id).meta >= 0 ? P.metas[uz(node(id).meta)].kind
                                  : ShapeKind::Polygon;
    }
    int vertex_count(int id) const {
        return node(id).meta >= 0 ? P.metas[uz(node(id).meta)].n : 0;
    }

private:
    struct Inferred {
        ValueType type = ValueType::Scalar;
        int size = 1;
        ShapeMeta meta;
        bool has_meta = false;
    };
    Inferred infer(Op op, std::span<const int> args) const;
    int fold(Op op, std::span<const int> args, int aux, const Inferred & inf);
    int push(Node n, std::span<const int> args, const std::string & key);

    Program & P;
    Scratch scratch_;
    std::unordered_map<std::string, int> cse_;
    std::pair<int, int> conflict_{-1, -1};
    std::vector<double> failed_dyads_;
};

}  // namespace gs::detail

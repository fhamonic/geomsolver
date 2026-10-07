#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <numbers>
#include <set>
#include <tuple>
#include <unordered_map>

#include "builder.hpp"
#include "gs/engine/expression.hpp"
#include "gs/engine/instance.hpp"
#include "gs/engine/model.hpp"

namespace gs {

namespace detail {
namespace {

using Json = nlohmann::ordered_json;

enum class NameKind { Sweep, Design, Let, Param, Geometry, Builtin };

const char * kind_label(NameKind k) {
    switch(k) {
        case NameKind::Sweep:
            return "a sweep";
        case NameKind::Design:
            return "a design variable";
        case NameKind::Let:
            return "a let binding";
        case NameKind::Param:
            return "a param";
        case NameKind::Geometry:
            return "a geometry constant";
        case NameKind::Builtin:
            return "a builtin constant";
    }
    return "?";
}

struct Ctx {
    std::string path;
    bool constant_only = false;
    // Sweeps fixed by enclosing at() calls: a direct reference to the sweep
    // name inside the body resolves to the value node. Lets are compiled in
    // their own context, so their sweep dependence is removed by subst().
    std::vector<std::pair<int, int>> bound;
};

struct Side {
    int node = -1;
    AggKind agg = AggKind::None;
    int sweep = -1;
};

bool parse_color(const std::string & s, std::uint32_t & out) {
    if(s.size() != 7 && s.size() != 9) return false;
    if(s[0] != '#') return false;
    std::uint32_t v = 0;
    for(std::size_t i = 1; i < s.size(); ++i) {
        const char c = s[i];
        std::uint32_t d = 0;
        if(c >= '0' && c <= '9')
            d = static_cast<std::uint32_t>(c - '0');
        else if(c >= 'a' && c <= 'f')
            d = static_cast<std::uint32_t>(c - 'a' + 10);
        else if(c >= 'A' && c <= 'F')
            d = static_cast<std::uint32_t>(c - 'A' + 10);
        else
            return false;
        v = v * 16 + d;
    }
    out = s.size() == 7 ? (v << 8) | 0xffu : v;
    return true;
}

std::string number_text(const Json & j) {
    return j.is_string() ? j.get<std::string>() : j.dump();
}

// Column of the first token of `a`: a binary node's own column is its
// operator's, which points into the middle of an argument.
int first_column(const Ast & a) {
    const Ast * p = &a;
    while((p->kind == Ast::Kind::Binary || p->kind == Ast::Kind::Relation ||
           p->kind == Ast::Kind::Member) &&
          !p->args.empty())
        p = p->args[0].get();
    return p->column;
}

bool is_identifier(std::string_view s) {
    if(s.empty()) return false;
    auto alpha = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    if(!alpha(s[0])) return false;
    for(const char c : s)
        if(!alpha(c) && !(c >= '0' && c <= '9')) return false;
    return true;
}

bool all_finite(const std::vector<double> & v) {
    for(const double d : v)
        if(!std::isfinite(d)) return false;
    return true;
}

// Compiler::expr recursion through let bindings; the parser bounds each
// expression on its own (kMaxExpressionDepth), not a chain of lets.
constexpr int kMaxCompileDepth = 2 * kMaxExpressionDepth;

}  // namespace

class Compiler {
public:
    Compiler(const Instance & inst, const CompileOptions & opt, Program & prog)
        : inst_(inst), opt_(opt), P(prog), B(prog) {}

    std::vector<Diagnostic> diags;

    // Outputs consumed by ModelBuilder.
    std::vector<DesignVar> design;
    std::vector<int> design_node_;
    std::vector<SweepInfo> sweeps;
    std::vector<ParamInfo> params_info;
    std::vector<GeometryInfo> geometry_info;
    std::vector<LetInfo> lets_info;
    std::vector<ConstraintInfo> constraints;
    std::vector<CriterionInfo> criteria;
    std::vector<DisplayInfo> display;
    std::vector<ExprInfo> probes;
    std::vector<RowGroup> groups;
    int n_coords = 0;
    int values_size = 0;

    bool run();

private:
    struct Lazy {
        std::string name, path;
        const Json * src = nullptr;
        int state = 0;  // 0 pending, 1 compiling, 2 done
        int node = -1;
        bool failed = false;
    };
    struct NameEntry {
        NameKind kind;
        int index;
        std::string path;
    };

    void error(const std::string & path, int col, std::string msg) {
        diags.push_back(
            {Diagnostic::Severity::Error, path, col, std::move(msg)});
    }
    void warning(const std::string & path, std::string msg) {
        diags.push_back(
            {Diagnostic::Severity::Warning, path, -1, std::move(msg)});
    }
    const Json & doc() const { return inst_.doc(); }

    bool add_name(const std::string & name, NameKind kind, int index,
                  const std::string & path);
    void build_geometry(const Json & geo, const std::string & file);
    int geometry_entry(const Json & g, const std::string & path);
    int constant_expr(const Json & v, const std::string & path, ValueType want);
    void build_sweeps();
    void build_design();
    void build_constraints();
    void build_criteria();
    void build_display();
    void build_implicit();
    void finalize();

    int resolve_let(int i, int col, const Ctx & c);
    int resolve_param(int i, int col, const Ctx & c);
    int lazy_compile(Lazy & L, bool is_let);
    std::unique_ptr<Ast> parse(const std::string & text,
                               const std::string & path);

    int expr(const Ast & a, const Ctx & c);
    int call(const Ast & a, const Ctx & c);
    Side side(const Ast & a, const Ctx & c);
    int mk(Op op, std::initializer_list<int> args, int col, const Ctx & c,
           int aux = 0);
    int mkv(Op op, const std::vector<int> & args, int col, const Ctx & c,
            int aux = 0);
    int subst(int id, int s, int e);
    // Errors for constant dyads folded since the last call (their circles
    // miss): folding keeps only the point, so no assembly row would exist.
    bool report_dyads(const std::string & path, int col);
    int abs_arg(int id) const {
        return B.node(id).op == Op::Abs ? B.args(id)[0] : -1;
    }

    ValueType ty(int id) const { return B.node(id).type; }
    std::string tname(int id) const { return type_name(ty(id), B.kind(id)); }
    std::string dep_sweep_name(int id) const {
        const Node & n = B.node(id);
        return n.dep == DepKind::Sweep ? sweeps[uz(n.sweep)].name
                                       : std::string();
    }

    int term_index(Term t);
    void fill_expr_info(ExprInfo & e, int node);

    const Instance & inst_;
    const CompileOptions & opt_;
    Program & P;
    Builder B;
    std::map<std::string, NameEntry> names_;
    std::vector<Lazy> lets_, params_;
    std::vector<std::pair<bool, int>> stack_;
    std::vector<int> geometry_node_;
    std::vector<int> sweep_node_;
    std::map<std::tuple<int, int, int>, int> subst_memo_;
    std::map<int, int> dyad_origin_;
    std::map<int, std::pair<int, int>> dyad_at_;  // dyad -> (sweep, value node)
    std::map<std::tuple<int, int, int>, int> agg_index_;
    std::vector<int> criterion_nodes_;
    int depth_ = 0;
};

bool Compiler::add_name(const std::string & name, NameKind kind, int index,
                        const std::string & path) {
    if(!is_identifier(name)) {
        error(path, -1,
              std::format("'{}' is not a valid name: use letters, digits and "
                          "'_', not starting with a digit",
                          name));
        return false;
    }
    auto it = names_.find(name);
    if(it != names_.end()) {
        error(path, -1,
              std::format("name '{}' is already {} ({})", name,
                          kind_label(it->second.kind), it->second.path));
        return false;
    }
    names_.emplace(name, NameEntry{kind, index, path});
    return true;
}

std::unique_ptr<Ast> Compiler::parse(const std::string & text,
                                     const std::string & path) {
    ParseResult r = parse_expression(text);
    if(!r.ok()) {
        error(path, r.error_column, r.error);
        return nullptr;
    }
    return std::move(r.ast);
}

int Compiler::mkv(Op op, const std::vector<int> & args, int col, const Ctx & c,
                  int aux) {
    for(const int a : args)
        if(a < 0) return -1;
    int r = B.make(op, args, aux);
    if(r == kTwoSweeps && !c.bound.empty()) {
        // Inside at(): a let compiled on its own may still depend on the
        // sweep the at() binds. Substituting here, innermost binding first
        // as the at() calls themselves do, gives the same node they would.
        std::vector<int> sub = args;
        for(auto b = c.bound.rbegin(); b != c.bound.rend(); ++b)
            for(int & a : sub)
                if(a >= 0) a = subst(a, b->first, b->second);
        if(std::ranges::none_of(sub, [](int a) { return a < 0; }))
            r = B.make(op, sub, aux);
    }
    if(r == kTwoSweeps) {
        const auto [s1, s2] = B.conflict();
        error(
            c.path, col,
            std::format("expression depends on two free sweeps ('{}' and '{}')",
                        sweeps[uz(s1)].name, sweeps[uz(s2)].name));
        return -1;
    }
    if(!report_dyads(c.path, col)) return -1;
    return r;
}

bool Compiler::report_dyads(const std::string & path, int col) {
    const std::vector<double> gaps = B.take_failed_dyads();
    for(const double gap : gaps)
        error(path, col,
              std::format("dyad cannot assemble: its inputs are constant and "
                          "its circles miss by {:.6g} m",
                          gap));
    return gaps.empty();
}

int Compiler::mk(Op op, std::initializer_list<int> args, int col, const Ctx & c,
                 int aux) {
    return mkv(op, std::vector<int>(args), col, c, aux);
}

int Compiler::subst(int id, int s, int e) {
    const Node n = B.node(id);
    if(n.dep != DepKind::Sweep || n.sweep != s) return id;
    if(n.op == Op::Sweep) return e;
    const auto key = std::make_tuple(id, s, e);
    if(auto it = subst_memo_.find(key); it != subst_memo_.end())
        return it->second;
    const std::vector<int> src(B.args(id).begin(), B.args(id).end());
    std::vector<int> args;
    args.reserve(src.size());
    for(const int a : src) {
        const int r = subst(a, s, e);
        if(r == kTwoSweeps) return kTwoSweeps;
        args.push_back(r);
    }
    const int r = B.make(n.op, args, n.aux);
    if(r >= 0 && n.op == Op::Dyad && r != id) {
        auto it = dyad_origin_.find(id);
        dyad_origin_[r] = it != dyad_origin_.end() ? it->second : id;
        dyad_at_[r] = {s, e};
    }
    subst_memo_[key] = r;
    return r;
}

int Compiler::lazy_compile(Lazy & L, bool is_let) {
    if(L.state == 2) return L.failed ? -1 : L.node;
    const int self =
        static_cast<int>(&L - (is_let ? lets_.data() : params_.data()));
    if(L.state == 1) {
        std::size_t pos = 0;
        while(pos < stack_.size() &&
              stack_[pos] != std::make_pair(is_let, self))
            ++pos;
        std::string cyc;
        for(std::size_t k = pos; k < stack_.size(); ++k) {
            const auto [lt, idx] = stack_[k];
            Lazy & M = lt ? lets_[uz(idx)] : params_[uz(idx)];
            M.failed = true;
            cyc += M.name + " -> ";
        }
        error(L.path, -1, "cycle: " + cyc + L.name);
        return -1;
    }
    L.state = 1;
    stack_.emplace_back(is_let, self);
    int node = -1;
    const Ctx c{L.path, !is_let, {}};
    if(L.src->is_number()) {
        node = B.scalar(L.src->get<double>());
    } else if(L.src->is_string()) {
        auto ast = parse(L.src->get<std::string>(), L.path);
        if(ast) node = expr(*ast, c);
    }
    stack_.pop_back();
    // L is a reference into lets_/params_: valid only because neither vector
    // grows once names are registered. Appending during compilation would
    // leave it dangling after the recursive expr() above.
    if(L.failed) node = -1;
    L.node = node;
    L.failed = node < 0;
    L.state = 2;
    return node;
}

int Compiler::resolve_let(int i, int, const Ctx &) {
    return lazy_compile(lets_[uz(i)], true);
}
int Compiler::resolve_param(int i, int, const Ctx &) {
    return lazy_compile(params_[uz(i)], false);
}

int Compiler::expr(const Ast & a, const Ctx & c) {
    struct Depth {
        int & d;
        explicit Depth(int & v) : d(++v) {}
        ~Depth() { --d; }
        Depth(const Depth &) = delete;
        Depth & operator=(const Depth &) = delete;
    } depth(depth_);
    if(depth_ > kMaxCompileDepth) {
        error(c.path, a.column,
              std::format("expression nested too deeply through let bindings "
                          "(more than {} levels)",
                          kMaxCompileDepth));
        return -1;
    }
    switch(a.kind) {
        case Ast::Kind::Number:
            return B.scalar(a.number);
        case Ast::Kind::Name: {
            auto it = names_.find(a.text);
            if(it == names_.end()) {
                error(c.path, a.column,
                      std::format("unknown name '{}'", a.text));
                return -1;
            }
            const NameEntry & e = it->second;
            if(c.constant_only &&
               (e.kind == NameKind::Sweep || e.kind == NameKind::Design ||
                e.kind == NameKind::Let)) {
                error(
                    c.path, a.column,
                    std::format(
                        "'{}' is {} and cannot appear in a constant expression",
                        a.text, kind_label(e.kind)));
                return -1;
            }
            switch(e.kind) {
                case NameKind::Sweep:
                    for(auto b = c.bound.rbegin(); b != c.bound.rend(); ++b)
                        if(b->first == e.index) return b->second;
                    return sweep_node_[uz(e.index)];
                case NameKind::Design:
                    return design_node_[uz(e.index)];
                case NameKind::Let:
                    return resolve_let(e.index, a.column, c);
                case NameKind::Param:
                    return resolve_param(e.index, a.column, c);
                case NameKind::Geometry:
                    return geometry_node_[uz(e.index)];
                case NameKind::Builtin:
                    return B.scalar(std::numbers::pi);
            }
            return -1;
        }
        case Ast::Kind::Neg: {
            const int x = expr(*a.args[0], c);
            if(x < 0) return -1;
            if(ty(x) == ValueType::Scalar) return mk(Op::Neg, {x}, a.column, c);
            if(ty(x) == ValueType::Vec) return mk(Op::VNeg, {x}, a.column, c);
            error(c.path, a.column,
                  std::format("cannot negate a {}", tname(x)));
            return -1;
        }
        case Ast::Kind::Member: {
            const int x = expr(*a.args[0], c);
            if(x < 0) return -1;
            if(ty(x) != ValueType::Vec) {
                error(
                    c.path, a.column,
                    std::format("'.{}' needs a Vec, got {}", a.text, tname(x)));
                return -1;
            }
            return mk(a.text == "x" ? Op::GetX : Op::GetY, {x}, a.column, c);
        }
        case Ast::Kind::Binary: {
            const int x = expr(*a.args[0], c);
            const int y = expr(*a.args[1], c);
            if(x < 0 || y < 0) return -1;
            const ValueType tx = ty(x), ty_ = ty(y);
            const char op = a.text[0];
            const bool ss = tx == ValueType::Scalar && ty_ == ValueType::Scalar;
            const bool vv = tx == ValueType::Vec && ty_ == ValueType::Vec;
            switch(op) {
                case '+':
                    if(ss) return mk(Op::Add, {x, y}, a.column, c);
                    if(vv) return mk(Op::VAdd, {x, y}, a.column, c);
                    break;
                case '-':
                    if(ss) return mk(Op::Sub, {x, y}, a.column, c);
                    if(vv) return mk(Op::VSub, {x, y}, a.column, c);
                    break;
                case '*':
                    if(ss) return mk(Op::Mul, {x, y}, a.column, c);
                    if(tx == ValueType::Vec && ty_ == ValueType::Scalar)
                        return mk(Op::VScale, {x, y}, a.column, c);
                    if(tx == ValueType::Scalar && ty_ == ValueType::Vec)
                        return mk(Op::VScale, {y, x}, a.column, c);
                    break;
                case '/':
                    if(ss) return mk(Op::Div, {x, y}, a.column, c);
                    if(tx == ValueType::Vec && ty_ == ValueType::Scalar)
                        return mk(Op::VDiv, {x, y}, a.column, c);
                    break;
                case '^':
                    if(ss) return mk(Op::Pow, {x, y}, a.column, c);
                    break;
                default:
                    break;
            }
            std::string hint =
                vv && (op == '*' || op == '/') ? " (use dot() or cross())" : "";
            error(c.path, a.column,
                  std::format("type error: {} {} {}{}", tname(x), a.text,
                              tname(y), hint));
            return -1;
        }
        case Ast::Kind::Relation:
            error(c.path, a.column,
                  "a comparison is only allowed at the top level of a "
                  "constraint");
            return -1;
        case Ast::Kind::Call:
            return call(a, c);
    }
    return -1;
}

int Compiler::call(const Ast & a, const Ctx & c) {
    const std::string & f = a.text;
    const int col = a.column;
    const std::size_t na = a.args.size();
    if(f == "max_over" || f == "min_over") {
        error(c.path, col,
              std::format("{}() is only allowed as a whole constraint side or "
                          "as a whole criterion",
                          f));
        return -1;
    }
    if(f == "at") {
        if(na != 2 || a.arg_names[0].empty() || !a.arg_names[1].empty()) {
            error(c.path, col, "at() takes the form at(sweep = value, body)");
            return -1;
        }
        auto it = names_.find(a.arg_names[0]);
        if(it == names_.end() || it->second.kind != NameKind::Sweep) {
            error(c.path, a.arg_name_columns[0],
                  std::format("'{}' is not a sweep", a.arg_names[0]));
            return -1;
        }
        const int s = it->second.index;
        const int e = expr(*a.args[0], c);
        if(e < 0) return -1;
        Ctx inner = c;
        inner.bound.emplace_back(s, e);
        const int body = expr(*a.args[1], inner);
        if(body < 0) return -1;
        if(ty(e) != ValueType::Scalar) {
            error(c.path, first_column(*a.args[0]),
                  std::format("the value of a sweep must be a Scalar, got {}",
                              tname(e)));
            return -1;
        }
        if(B.node(e).dep == DepKind::Sweep && B.node(e).sweep == s) {
            error(c.path, first_column(*a.args[0]),
                  std::format(
                      "the value given to '{}' cannot depend on '{}' itself",
                      a.arg_names[0], a.arg_names[0]));
            return -1;
        }
        const int r = subst(body, s, e);
        if(r == kTwoSweeps) {
            const auto [s1, s2] = B.conflict();
            error(c.path, col,
                  std::format(
                      "expression depends on two free sweeps ('{}' and '{}')",
                      sweeps[uz(s1)].name, sweeps[uz(s2)].name));
            return -1;
        }
        if(!report_dyads(c.path, col)) return -1;
        return r;
    }
    for(std::size_t i = 0; i < na; ++i)
        if(!a.arg_names[i].empty()) {
            error(c.path, a.arg_name_columns[i],
                  "named arguments are only allowed in at()");
            return -1;
        }

    struct Sig {
        int min_args, max_args;  // max_args < 0: unbounded
    };
    static const std::map<std::string, Sig, std::less<>> sigs = {
        {"sqrt", {1, 1}},        {"sin", {1, 1}},
        {"cos", {1, 1}},         {"tan", {1, 1}},
        {"asin", {1, 1}},        {"acos", {1, 1}},
        {"atan", {1, 1}},        {"atan2", {2, 2}},
        {"abs", {1, 1}},         {"exp", {1, 1}},
        {"log", {1, 1}},         {"sq", {1, 1}},
        {"min", {1, -1}},        {"max", {1, -1}},
        {"clamp", {3, 3}},       {"vec", {2, 2}},
        {"dir", {1, 1}},         {"rotate", {2, 2}},
        {"perp", {1, 1}},        {"dot", {2, 2}},
        {"cross", {2, 2}},       {"norm", {1, 1}},
        {"normalize", {1, 1}},   {"dist", {2, 2}},
        {"angle", {1, 1}},       {"angle_between", {2, 2}},
        {"sin_between", {2, 2}}, {"mean", {1, -1}},
        {"box", {4, 4}},         {"rect", {4, 4}},
        {"polygon", {3, -1}},    {"polyline", {2, -1}},
        {"segment", {2, 2}},     {"circle", {2, 2}},
        {"place", {3, 3}},       {"translate", {2, 2}},
        {"vertex", {2, 2}},      {"center", {1, 1}},
        {"clearance", {2, 2}},   {"min_x", {1, -1}},
        {"max_x", {1, -1}},      {"min_y", {1, -1}},
        {"max_y", {1, -1}},      {"max_proj", {2, 2}},
        {"min_proj", {2, 2}},    {"dyad", {5, 5}},
        {"branch_of", {3, 3}},
    };
    auto sit = sigs.find(f);
    if(sit == sigs.end()) {
        error(c.path, col, std::format("unknown function '{}'", f));
        return -1;
    }
    const Sig sig = sit->second;
    if(static_cast<int>(na) < sig.min_args ||
       (sig.max_args >= 0 && static_cast<int>(na) > sig.max_args)) {
        std::string want =
            sig.max_args == sig.min_args ? std::to_string(sig.min_args)
            : sig.max_args < 0
                ? std::format("at least {}", sig.min_args)
                : std::format("{} to {}", sig.min_args, sig.max_args);
        error(
            c.path, col,
            std::format("{}() takes {} argument{}, got {}", f, want,
                        sig.min_args == 1 && sig.max_args <= 1 ? "" : "s", na));
        return -1;
    }
    std::vector<int> x(na);
    bool bad = false;
    for(std::size_t i = 0; i < na; ++i) {
        x[i] = expr(*a.args[i], c);
        if(x[i] < 0) bad = true;
    }
    if(bad) return -1;

    // Type requirement per argument: 'S' Scalar, 'V' Vec, 'G' Vec or Shape, 'H'
    // Shape.
    auto need = [&](std::size_t i, char k) {
        const ValueType t = ty(x[i]);
        bool ok = false;
        const char * what = "";
        switch(k) {
            case 'S':
                ok = t == ValueType::Scalar;
                what = "a Scalar";
                break;
            case 'V':
                ok = t == ValueType::Vec;
                what = "a Vec";
                break;
            case 'G':
                ok = t != ValueType::Scalar;
                what = "a Vec or a shape";
                break;
            case 'H':
                ok = t == ValueType::Shape;
                what = "a shape";
                break;
            default:
                break;
        }
        if(!ok)
            error(c.path, first_column(*a.args[i]),
                  std::format("argument {} of {}() must be {}, got {}", i + 1,
                              f, what, tname(x[i])));
        return ok;
    };
    auto needs = [&](const char * pattern) {
        bool ok = true;
        for(std::size_t i = 0; i < na; ++i) {
            const std::size_t k =
                std::min(i, std::char_traits<char>::length(pattern) - 1);
            ok = need(i, pattern[k]) && ok;
        }
        return ok;
    };

    static const std::map<std::string, Op, std::less<>> unary = {
        {"sqrt", Op::Sqrt}, {"sin", Op::Sin},   {"cos", Op::Cos},
        {"tan", Op::Tan},   {"asin", Op::Asin}, {"acos", Op::Acos},
        {"atan", Op::Atan}, {"abs", Op::Abs},   {"exp", Op::Exp},
        {"log", Op::Log},   {"sq", Op::Sq}};
    if(auto u = unary.find(f); u != unary.end())
        return needs("S") ? mk(u->second, {x[0]}, col, c) : -1;
    if(f == "atan2")
        return needs("SS") ? mk(Op::Atan2, {x[0], x[1]}, col, c) : -1;
    if(f == "min" || f == "max")
        return needs("S") ? mkv(f == "min" ? Op::Min : Op::Max, x, col, c) : -1;
    if(f == "clamp") return needs("SSS") ? mkv(Op::Clamp, x, col, c) : -1;
    if(f == "vec") return needs("SS") ? mkv(Op::MakeVec, x, col, c) : -1;
    if(f == "dir") return needs("S") ? mk(Op::Dir, {x[0]}, col, c) : -1;
    if(f == "rotate") {
        if(!needs("VS")) return -1;
        return mk(Op::Rotate, {x[0], mk(Op::Dir, {x[1]}, col, c)}, col, c);
    }
    static const std::map<std::string, Op, std::less<>> vec1 = {
        {"perp", Op::Perp},
        {"norm", Op::Norm},
        {"normalize", Op::Normalize},
        {"angle", Op::Angle}};
    if(auto u = vec1.find(f); u != vec1.end())
        return needs("V") ? mk(u->second, {x[0]}, col, c) : -1;
    static const std::map<std::string, Op, std::less<>> vec2 = {
        {"dot", Op::Dot},
        {"cross", Op::Cross},
        {"dist", Op::Dist},
        {"angle_between", Op::AngleBetween},
        {"sin_between", Op::SinBetween}};
    if(auto u = vec2.find(f); u != vec2.end())
        return needs("VV") ? mk(u->second, {x[0], x[1]}, col, c) : -1;
    if(f == "mean") {
        if(ty(x[0]) == ValueType::Scalar)
            return needs("S") ? mkv(Op::MeanS, x, col, c) : -1;
        return needs("V") ? mkv(Op::Mean, x, col, c) : -1;
    }
    if(f == "box") return needs("SSSS") ? mkv(Op::Box, x, col, c) : -1;
    if(f == "rect") {
        if(!needs("VSSS")) return -1;
        return mk(Op::Rect, {x[0], x[1], x[2], mk(Op::Dir, {x[3]}, col, c)},
                  col, c);
    }
    if(f == "polygon") return needs("V") ? mkv(Op::MakePolygon, x, col, c) : -1;
    if(f == "polyline" || f == "segment")
        return needs("V") ? mkv(Op::MakePolyline, x, col, c) : -1;
    if(f == "circle")
        return needs("VS") ? mk(Op::MakeCircle, {x[0], x[1]}, col, c) : -1;
    if(f == "place") {
        if(!needs("GVS")) return -1;
        return mk(Op::Place, {x[0], x[1], mk(Op::Dir, {x[2]}, col, c)}, col, c);
    }
    if(f == "translate") {
        if(!needs("GV")) return -1;
        if(ty(x[0]) == ValueType::Vec)
            return mk(Op::VAdd, {x[0], x[1]}, col, c);
        return mk(Op::Translate, {x[0], x[1]}, col, c);
    }
    if(f == "vertex") {
        if(!needs("HS")) return -1;
        if(B.kind(x[0]) == ShapeKind::Circle) {
            error(c.path, first_column(*a.args[0]),
                  "vertex() needs a polygon or a polyline, got Circle");
            return -1;
        }
        const Node & in = B.node(x[1]);
        const double v =
            in.dep == DepKind::Const ? B.values(x[1])[0] : std::nan("");
        if(in.dep != DepKind::Const || v != std::floor(v)) {
            error(c.path, first_column(*a.args[1]),
                  "the index of vertex() must be a constant integer");
            return -1;
        }
        const int nv = B.vertex_count(x[0]);
        if(v < 0 || v >= nv) {
            error(c.path, first_column(*a.args[1]),
                  std::format("vertex index {} out of range [0, {})", v, nv));
            return -1;
        }
        return mk(Op::Vertex, {x[0]}, col, c, static_cast<int>(v));
    }
    if(f == "center") {
        if(!needs("G")) return -1;
        return ty(x[0]) == ValueType::Vec ? x[0]
                                          : mk(Op::Center, {x[0]}, col, c);
    }
    if(f == "clearance")
        return needs("GG") ? mk(Op::Clearance, {x[0], x[1]}, col, c) : -1;
    if(f == "min_x" || f == "max_x" || f == "min_y" || f == "max_y") {
        if(!needs("G")) return -1;
        const Op op = f == "min_x"   ? Op::MinX
                      : f == "max_x" ? Op::MaxX
                      : f == "min_y" ? Op::MinY
                                     : Op::MaxY;
        return mkv(op, x, col, c);
    }
    if(f == "max_proj" || f == "min_proj")
        return needs("GV") ? mk(f == "max_proj" ? Op::MaxProj : Op::MinProj,
                                {x[0], x[1]}, col, c)
                           : -1;
    if(f == "dyad") return needs("VSVSS") ? mkv(Op::Dyad, x, col, c) : -1;
    if(f == "branch_of")
        return needs("VVV") ? mkv(Op::BranchOf, x, col, c) : -1;
    error(c.path, col, std::format("unknown function '{}'", f));
    return -1;
}

Side Compiler::side(const Ast & a, const Ctx & c) {
    Side r;
    if(a.kind == Ast::Kind::Call &&
       (a.text == "max_over" || a.text == "min_over")) {
        if(a.args.size() != 2 || a.args[0]->kind != Ast::Kind::Name ||
           !a.arg_names[0].empty() || !a.arg_names[1].empty()) {
            error(c.path, a.column,
                  std::format("{}() takes the form {}(sweep, body)", a.text,
                              a.text));
            return r;
        }
        auto it = names_.find(a.args[0]->text);
        if(it == names_.end() || it->second.kind != NameKind::Sweep) {
            error(c.path, first_column(*a.args[0]),
                  std::format("'{}' is not a sweep", a.args[0]->text));
            return r;
        }
        const int s = it->second.index;
        const int body = expr(*a.args[1], c);
        if(body < 0) return r;
        if(ty(body) != ValueType::Scalar) {
            error(c.path, first_column(*a.args[1]),
                  std::format("{}() needs a Scalar body, got {}", a.text,
                              tname(body)));
            return r;
        }
        const Node & bn = B.node(body);
        if(bn.dep == DepKind::Sweep && bn.sweep != s) {
            error(c.path, first_column(*a.args[1]),
                  std::format("the body of {}({}, ...) depends on another free "
                              "sweep '{}'",
                              a.text, a.args[0]->text,
                              sweeps[uz(bn.sweep)].name));
            return r;
        }
        r.node = body;
        r.agg = a.text == "max_over" ? AggKind::Max : AggKind::Min;
        r.sweep = s;
        return r;
    }
    r.node = expr(a, c);
    return r;
}

int Compiler::term_index(Term t) {
    if(t.agg == AggKind::None) return -1;
    const auto key = std::make_tuple(t.node, static_cast<int>(t.agg), t.sweep);
    if(auto it = agg_index_.find(key); it != agg_index_.end())
        return it->second;
    const int idx = static_cast<int>(P.aggs.size());
    P.aggs.push_back({t.node, t.agg, t.sweep});
    agg_index_.emplace(key, idx);
    return idx;
}

int Compiler::geometry_entry(const Json & g, const std::string & path) {
    const std::string type = g.at("type").get<std::string>();
    auto num = [&](const char * k, double def) {
        return g.contains(k) ? g.at(k).get<double>() : def;
    };
    if(type == "point") return B.vec(num("x", 0), num("y", 0));
    if(type == "circle") {
        const double d[3] = {num("x", 0), num("y", 0), num("r", 0)};
        return B.constant(ValueType::Shape, ShapeKind::Circle, d);
    }
    if(type == "rect") {
        const int cs = B.make(
            Op::Dir, {B.scalar(num("angle", 0) * (std::numbers::pi / 180.0))});
        return B.make(Op::Rect, {B.vec(num("x", 0), num("y", 0)),
                                 B.scalar(num("width", 0)),
                                 B.scalar(num("height", 0)), cs});
    }
    if(type == "polyline") {
        std::vector<double> d;
        for(const Json & p : g.at("points")) {
            d.push_back(p.at(0).get<double>());
            d.push_back(p.at(1).get<double>());
        }
        const bool closed = g.contains("closed") && g.at("closed").get<bool>();
        if(closed && d.size() < 6) {
            error(path, -1, "a closed polyline needs at least 3 points");
            return -1;
        }
        return B.constant(ValueType::Shape,
                          closed ? ShapeKind::Polygon : ShapeKind::Polyline, d);
    }
    error(path, -1, std::format("unknown geometry type '{}'", type));
    return -1;
}

void Compiler::build_geometry(const Json & geo, const std::string & file) {
    for(const auto & [name, g] : geo.items()) {
        const std::string path =
            file.empty() ? "geometry." + name : file + ": geometry." + name;
        const int id = static_cast<int>(geometry_node_.size());
        geometry_node_.push_back(-1);
        if(!add_name(name, NameKind::Geometry, id, path)) continue;
        int node = -1;
        try {
            node = geometry_entry(g, path);
        } catch(const nlohmann::json::exception & e) {
            error(path, -1, e.what());
        }
        geometry_node_[uz(id)] = node;
        if(node >= 0) {
            GeometryInfo gi;
            gi.name = name;
            gi.file = file;
            gi.note = g.contains("note") && g["note"].is_string()
                          ? g["note"].get<std::string>()
                          : "";
            gi.value = B.geo(node);
            geometry_info.push_back(std::move(gi));
        }
    }
}

int Compiler::constant_expr(const Json & v, const std::string & path,
                            ValueType want) {
    int node = -1;
    if(v.is_number()) {
        node = B.scalar(v.get<double>());
    } else if(v.is_string()) {
        auto ast = parse(v.get<std::string>(), path);
        if(!ast) return -1;
        node = expr(*ast, Ctx{path, true, {}});
    } else {
        error(path, -1, "expected a number or an expression string");
        return -1;
    }
    if(node < 0) return -1;
    if(ty(node) != want) {
        error(
            path, -1,
            std::format("expected a {}, got {}", type_name(want), tname(node)));
        return -1;
    }
    if(!all_finite(B.values(node))) {
        error(path, -1, "evaluates to a non-finite value (NaN or inf)");
        return -1;
    }
    return node;
}

void Compiler::build_sweeps() {
    if(!doc().contains("sweeps")) return;
    int k = 0;
    for(const auto & [name, s] : doc()["sweeps"].items()) {
        const std::string path = "sweeps." + name;
        SweepInfo si;
        si.name = name;
        const int lo =
            constant_expr(s.at("min"), path + ".min", ValueType::Scalar);
        const int hi =
            constant_expr(s.at("max"), path + ".max", ValueType::Scalar);
        if(lo >= 0) si.min = B.values(lo)[0];
        if(hi >= 0) si.max = B.values(hi)[0];
        if(lo >= 0 && hi >= 0 && !(si.max >= si.min))
            error(path, -1, "sweep max must be >= min");
        sweeps[uz(k)] = si;
        ++k;
    }
}

void Compiler::build_design() {
    if(!doc().contains("design")) return;
    int k = 0;
    int coord = 0, voff = 0;
    for(const auto & [name, d] : doc()["design"].items()) {
        DesignVar & v = design[uz(k)];
        const int index = k++;
        v.name = name;
        v.path = "design." + name;
        v.type = d.at("type").get<std::string>() == "point" ? VarType::Point
                                                            : VarType::Scalar;
        v.fixed = d.contains("fixed") && d["fixed"].get<bool>();
        v.unit = d.contains("unit") ? d["unit"].get<std::string>() : "";
        v.note = d.contains("note") && d["note"].is_string()
                     ? d["note"].get<std::string>()
                     : "";
        v.value_offset = voff;
        voff += v.size();
        bool ok = true;
        if(v.type == VarType::Scalar) {
            const int lo =
                constant_expr(d.at("min"), v.path + ".min", ValueType::Scalar);
            const int hi =
                constant_expr(d.at("max"), v.path + ".max", ValueType::Scalar);
            ok = lo >= 0 && hi >= 0;
            if(ok) {
                v.min = B.values(lo)[0];
                v.max = B.values(hi)[0];
                if(!(v.max >= v.min)) {
                    error(v.path, -1, "max must be >= min");
                    ok = false;
                } else if(!std::isfinite(v.max - v.min)) {
                    error(v.path, -1, "max - min overflows (not finite)");
                    ok = false;
                }
            }
            v.chart.kind = ChartKind::Interval;
            v.chart.lo = v.min;
            v.chart.hi = v.max;
            v.value = {d.contains("value") ? d["value"].get<double>()
                                           : 0.5 * (v.min + v.max)};
            if(ok && (v.value[0] < v.min || v.value[0] > v.max))
                warning(v.path + ".value",
                        "value lies outside [min, max]; the solver starts from "
                        "the clamped value");
        } else {
            const int dom = constant_expr(d.at("domain"), v.path + ".domain",
                                          ValueType::Shape);
            ok = dom >= 0;
            if(ok) {
                v.domain = B.geo(dom);
                const int nv = v.domain.vertex_count();
                Chart & ch = v.chart;
                if(v.domain.kind == ShapeKind::Polygon && nv == 4) {
                    const Vec2d p0 = v.domain.vertex(0),
                                p1 = v.domain.vertex(1),
                                p2 = v.domain.vertex(2),
                                p3 = v.domain.vertex(3);
                    double scale = 1.0;
                    for(const Vec2d q : {p0, p1, p2, p3})
                        scale =
                            std::max({scale, std::fabs(q.x), std::fabs(q.y)});
                    const double ex = p0.x + p2.x - p1.x - p3.x,
                                 ey = p0.y + p2.y - p1.y - p3.y;
                    const Vec2d e1{p1.x - p0.x, p1.y - p0.y},
                        e2{p3.x - p0.x, p3.y - p0.y};
                    const double area = e1.x * e2.y - e1.y * e2.x;
                    if(std::fabs(ex) <= 1e-9 * scale &&
                       std::fabs(ey) <= 1e-9 * scale && area != 0.0) {
                        ch.kind = ChartKind::Parallelogram;
                        ch.origin = p0;
                        ch.e1 = e1;
                        ch.e2 = e2;
                    }
                }
                if(ch.kind == ChartKind::Fixed) {
                    if(v.domain.kind == ShapeKind::Polyline && nv == 2) {
                        const Vec2d p0 = v.domain.vertex(0),
                                    p1 = v.domain.vertex(1);
                        ch.kind = ChartKind::Segment;
                        ch.origin = p0;
                        ch.e1 = {p1.x - p0.x, p1.y - p0.y};
                    } else if(v.domain.kind == ShapeKind::Polyline) {
                        error(v.path + ".domain", -1,
                              "a point domain must be a polygon, a circle or a "
                              "segment (2-point polyline)");
                        ok = false;
                    } else {
                        double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                        if(v.domain.kind == ShapeKind::Circle) {
                            const Vec2d cc = v.domain.circle_center();
                            const double r = v.domain.circle_radius();
                            x0 = cc.x - r;
                            x1 = cc.x + r;
                            y0 = cc.y - r;
                            y1 = cc.y + r;
                        } else {
                            x0 = y0 = std::numeric_limits<double>::infinity();
                            x1 = y1 = -x0;
                            for(int i = 0; i < nv; ++i) {
                                const Vec2d q = v.domain.vertex(i);
                                x0 = std::min(x0, q.x);
                                x1 = std::max(x1, q.x);
                                y0 = std::min(y0, q.y);
                                y1 = std::max(y1, q.y);
                            }
                        }
                        ch.kind = ChartKind::BoundingBox;
                        ch.origin = {x0, y0};
                        ch.e1 = {x1 - x0, 0.0};
                        ch.e2 = {0.0, y1 - y0};
                    }
                }
                if(ok &&
                   !(std::isfinite(ch.origin.x) && std::isfinite(ch.origin.y) &&
                     std::isfinite(ch.e1.x) && std::isfinite(ch.e1.y) &&
                     std::isfinite(ch.e2.x) && std::isfinite(ch.e2.y))) {
                    error(v.path + ".domain", -1,
                          "the domain's extent overflows (not finite)");
                    ok = false;
                }
                if(d.contains("value")) {
                    v.value = {d["value"][0].get<double>(),
                               d["value"][1].get<double>()};
                } else {
                    const int cen = B.make(Op::Center, {dom});
                    v.value = B.values(cen);
                }
                if(ok) {
                    const int pt = B.vec(v.value[0], v.value[1]);
                    const double gap =
                        B.values(B.make(Op::Clearance, {pt, dom}))[0];
                    if(gap > 1e-9)
                        warning(v.path + ".value",
                                std::format("value lies {:.6g} m outside its "
                                            "domain; the solver "
                                            "starts from the projected point",
                                            gap));
                }
            } else {
                v.value = {0.0, 0.0};
            }
        }
        if(!ok) {
            design_node_[uz(index)] = -1;
            continue;
        }
        if(v.fixed) {
            v.chart.kind = ChartKind::Fixed;
            design_node_[uz(index)] = v.type == VarType::Scalar
                                          ? B.scalar(v.value[0])
                                          : B.vec(v.value[0], v.value[1]);
        } else {
            v.coord = coord;
            coord += v.chart.dims();
            design_node_[uz(index)] =
                B.var(index, v.type == VarType::Scalar ? ValueType::Scalar
                                                       : ValueType::Vec);
        }
    }
    n_coords = coord;
    values_size = voff;
}

void Compiler::fill_expr_info(ExprInfo & e, int node) {
    if(node < 0) return;
    const Node & n = B.node(node);
    e.type = n.type;
    e.kind = B.kind(node);
    e.sweep = n.dep == DepKind::Sweep ? n.sweep : -1;
    e.constant = n.dep == DepKind::Const;
    e.ref.node = node;
}

void Compiler::build_constraints() {
    if(!doc().contains("constraints")) return;
    const Json & list = doc()["constraints"];
    for(std::size_t i = 0; i < list.size(); ++i) {
        const Json & cj = list[i];
        ConstraintInfo ci;
        ci.path = std::format("constraints[{}]", i);
        ci.name = cj.at("name").get<std::string>();
        ci.text = cj.at("expr").get<std::string>();
        ci.forall =
            cj.contains("forall") ? cj["forall"].get<std::string>() : "";
        ci.note = cj.contains("note") && cj["note"].is_string()
                      ? cj["note"].get<std::string>()
                      : "";
        ci.enabled = !cj.contains("enabled") || cj["enabled"].get<bool>();
        const std::string epath = ci.path + ".expr";
        const int index = static_cast<int>(constraints.size());
        for(const ConstraintInfo & other : constraints)
            if(other.name == ci.name)
                error(ci.path + ".name", -1,
                      std::format("duplicate constraint name '{}' (also {})",
                                  ci.name, other.path));
        constraints.push_back(ci);

        int forall = -1;
        if(!ci.forall.empty()) {
            auto it = names_.find(ci.forall);
            if(it == names_.end() || it->second.kind != NameKind::Sweep) {
                error(ci.path + ".forall", -1,
                      std::format("unknown sweep '{}'", ci.forall));
                continue;
            }
            forall = it->second.index;
        }
        auto ast = parse(ci.text, epath);
        if(!ast) continue;
        if(ast->kind != Ast::Kind::Relation) {
            error(epath, 0, "a constraint needs a comparison (<=, >= or ==)");
            continue;
        }
        const Ctx c{epath, false, {}};
        const Side L = side(*ast->args[0], c);
        const Side R = side(*ast->args[1], c);
        if(L.node < 0 || R.node < 0) continue;
        bool bad = false;
        for(const auto & [sd, ast_side] :
            {std::pair<const Side &, const Ast &>{L, *ast->args[0]},
             std::pair<const Side &, const Ast &>{R, *ast->args[1]}}) {
            if(ty(sd.node) != ValueType::Scalar) {
                error(epath, first_column(ast_side),
                      std::format("constraint sides must be Scalar, got {}",
                                  tname(sd.node)));
                bad = true;
                continue;
            }
            if(sd.agg != AggKind::None) {
                if(forall >= 0) {
                    error(
                        epath, first_column(ast_side),
                        "max_over/min_over cannot be combined with \"forall\"");
                    bad = true;
                }
                continue;
            }
            const Node & n = B.node(sd.node);
            if(n.dep == DepKind::Sweep && n.sweep != forall) {
                const std::string s = sweeps[uz(n.sweep)].name;
                if(forall < 0)
                    error(
                        epath, first_column(ast_side),
                        std::format("depends on sweep '{}': add \"forall\": "
                                    "\"{}\", wrap it in max_over/min_over, or "
                                    "fix the sweep with at()",
                                    s, s));
                else
                    error(epath, first_column(ast_side),
                          std::format(
                              "depends on sweep '{}' but \"forall\" is '{}'", s,
                              ci.forall));
                bad = true;
            }
        }
        if(bad) continue;
        if(!ci.enabled) continue;

        const std::string & rel = ast->text;
        GroupImpl G;
        G.equality = rel == "==";
        auto node_rows = [&](int lhs, int rhs) {
            // rows for lhs <= rhs, splitting a top-level abs on the left
            const int col = ast->column;
            const int e = abs_arg(lhs);
            if(e >= 0 && !G.equality) {
                G.rows.push_back({RowTemplate::Kind::Node,
                                  mk(Op::Sub, {e, rhs}, col, c),
                                  {},
                                  {},
                                  1.0});
                G.rows.push_back(
                    {RowTemplate::Kind::Node,
                     mk(Op::Sub, {mk(Op::Neg, {e}, col, c), rhs}, col, c),
                     {},
                     {},
                     1.0});
            } else {
                G.rows.push_back({RowTemplate::Kind::Node,
                                  mk(Op::Sub, {lhs, rhs}, col, c),
                                  {},
                                  {},
                                  1.0});
            }
        };
        if(L.agg == AggKind::None && R.agg == AggKind::None) {
            if(rel == ">=")
                node_rows(R.node, L.node);
            else
                node_rows(L.node, R.node);
        } else if(rel == "<=" && L.agg == AggKind::Max &&
                  R.agg == AggKind::None &&
                  B.node(R.node).dep != DepKind::Sweep) {
            node_rows(L.node, R.node);
        } else if(rel == "<=" && R.agg == AggKind::Min &&
                  L.agg == AggKind::None &&
                  B.node(L.node).dep != DepKind::Sweep) {
            node_rows(L.node, R.node);
        } else if(rel == ">=" && L.agg == AggKind::Min &&
                  R.agg == AggKind::None &&
                  B.node(R.node).dep != DepKind::Sweep) {
            node_rows(R.node, L.node);
        } else if(rel == ">=" && R.agg == AggKind::Max &&
                  L.agg == AggKind::None &&
                  B.node(L.node).dep != DepKind::Sweep) {
            node_rows(R.node, L.node);
        } else {
            RowTemplate t;
            t.kind = RowTemplate::Kind::Aggregate;
            Term tl{L.node, L.agg, L.sweep, -1}, tr{R.node, R.agg, R.sweep, -1};
            tl.agg_index = term_index(tl);
            tr.agg_index = term_index(tr);
            if(rel == ">=") std::swap(tl, tr);
            t.lhs = tl;
            t.rhs = tr;
            G.rows.push_back(t);
        }
        bool rows_ok = true;
        int sweep = -1;
        for(const RowTemplate & t : G.rows) {
            if(t.kind == RowTemplate::Kind::Node) {
                if(t.node < 0)
                    rows_ok = false;
                else if(B.node(t.node).dep == DepKind::Sweep)
                    sweep = B.node(t.node).sweep;
            }
        }
        if(!rows_ok) continue;
        bool constant = true;
        for(const RowTemplate & t : G.rows) {
            for(const int nd : {t.node, t.lhs.node, t.rhs.node})
                if(nd >= 0 && B.node(nd).dep != DepKind::Const)
                    constant = false;
        }
        if(constant)
            warning(epath,
                    "does not depend on any design variable: it is always "
                    "satisfied or always violated");
        G.sweep = sweep;
        RowGroup rg;
        rg.kind = GroupKind::Constraint;
        rg.source = index;
        rg.name = ci.name;
        rg.sweep = sweep;
        rg.split = static_cast<int>(G.rows.size());
        rg.equality = G.equality;
        rg.natural = "expression units (SI)";
        constraints[uz(index)].groups.push_back(
            static_cast<int>(groups.size()));
        groups.push_back(rg);
        P.groups.push_back(std::move(G));
    }
}

void Compiler::build_criteria() {
    if(!doc().contains("criteria")) return;
    const Json & list = doc()["criteria"];
    for(std::size_t i = 0; i < list.size(); ++i) {
        const Json & cj = list[i];
        CriterionInfo ci;
        ci.path = std::format("criteria[{}]", i);
        ci.name = cj.at("name").get<std::string>();
        ci.text = cj.at("expr").get<std::string>();
        ci.unit = cj.contains("unit") ? cj["unit"].get<std::string>() : "";
        const std::string role = cj.at("role").get<std::string>();
        ci.role = role == "minimize" ? CriterionRole::Minimize
                  : role == "max"    ? CriterionRole::Max
                  : role == "min"    ? CriterionRole::Min
                                     : CriterionRole::Report;
        ci.weight = cj.contains("weight") ? cj["weight"].get<double>() : 1.0;
        const int index = static_cast<int>(criteria.size());
        // Results and the CLI address criteria by name.
        for(const CriterionInfo & other : criteria)
            if(other.name == ci.name)
                error(ci.path + ".name", -1,
                      std::format("duplicate criterion name '{}' (also {})",
                                  ci.name, other.path));
        criteria.push_back(ci);
        P.criteria.push_back(Term{});
        criterion_nodes_.push_back(-1);
        const std::string epath = ci.path + ".expr";

        int bound = -1;
        if(cj.contains("bound")) {
            criteria[uz(index)].bound_text = number_text(cj["bound"]);
            bound = constant_expr(cj["bound"], ci.path + ".bound",
                                  ValueType::Scalar);
            if(bound >= 0) criteria[uz(index)].bound = B.values(bound)[0];
        }
        auto ast = parse(ci.text, epath);
        if(!ast) continue;
        if(ast->kind == Ast::Kind::Relation) {
            error(epath, ast->column,
                  "a criterion is an expression, not a comparison (use "
                  "\"role\" and \"bound\")");
            continue;
        }
        const Ctx c{epath, false, {}};
        const Side S = side(*ast, c);
        if(S.node < 0) continue;
        if(ty(S.node) != ValueType::Scalar) {
            error(epath, ast->column,
                  std::format("a criterion must be a Scalar, got {}",
                              tname(S.node)));
            continue;
        }
        if(S.agg == AggKind::None && B.node(S.node).dep == DepKind::Sweep) {
            const std::string s = dep_sweep_name(S.node);
            error(epath, ast->column,
                  std::format("depends on sweep '{}': wrap it in max_over({}, "
                              "...) / min_over({}, ...) or fix the "
                              "sweep with at()",
                              s, s, s));
            continue;
        }
        Term term{S.node, S.agg, S.sweep, -1};
        term.agg_index = term_index(term);
        P.criteria[uz(index)] = term;
        criterion_nodes_[uz(index)] = S.node;
        criteria[uz(index)].aggregate =
            S.agg == AggKind::Max   ? Aggregate::MaxOver
            : S.agg == AggKind::Min ? Aggregate::MinOver
                                    : Aggregate::None;
        criteria[uz(index)].sweep = S.agg != AggKind::None ? S.sweep : -1;

        if(ci.role == CriterionRole::Minimize) {
            ObjTerm o;
            o.criterion = index;
            o.weight = ci.weight;
            // Minimising w*z with w <= 0 pushes z up without limit (nothing
            // bounds it from above): such a criterion enters directly.
            o.epigraph = S.agg == AggKind::Max &&
                         B.node(S.node).dep == DepKind::Sweep &&
                         ci.weight > 0.0;
            o.term = term;
            P.objective.push_back(o);
            continue;
        }
        if(ci.role == CriterionRole::Report) continue;
        if(bound < 0) continue;
        GroupImpl G;
        const int col = ast->column;
        auto split_rows = [&](int lhs, int rhs) {
            const int e = abs_arg(lhs);
            if(e >= 0) {
                G.rows.push_back({RowTemplate::Kind::Node,
                                  mk(Op::Sub, {e, rhs}, col, c),
                                  {},
                                  {},
                                  1.0});
                G.rows.push_back(
                    {RowTemplate::Kind::Node,
                     mk(Op::Sub, {mk(Op::Neg, {e}, col, c), rhs}, col, c),
                     {},
                     {},
                     1.0});
            } else {
                G.rows.push_back({RowTemplate::Kind::Node,
                                  mk(Op::Sub, {lhs, rhs}, col, c),
                                  {},
                                  {},
                                  1.0});
            }
        };
        const bool is_max = ci.role == CriterionRole::Max;
        if(S.agg == AggKind::None || (is_max && S.agg == AggKind::Max) ||
           (!is_max && S.agg == AggKind::Min)) {
            if(is_max)
                split_rows(S.node, bound);
            else
                G.rows.push_back({RowTemplate::Kind::Node,
                                  mk(Op::Sub, {bound, S.node}, col, c),
                                  {},
                                  {},
                                  1.0});
        } else {
            RowTemplate t;
            t.kind = RowTemplate::Kind::Aggregate;
            const Term tb{bound, AggKind::None, -1, -1};
            t.lhs = is_max ? term : tb;
            t.rhs = is_max ? tb : term;
            G.rows.push_back(t);
        }
        int sweep = -1;
        bool ok = true;
        for(const RowTemplate & t : G.rows)
            if(t.kind == RowTemplate::Kind::Node) {
                if(t.node < 0)
                    ok = false;
                else if(B.node(t.node).dep == DepKind::Sweep)
                    sweep = B.node(t.node).sweep;
            }
        if(!ok) continue;
        G.sweep = sweep;
        RowGroup rg;
        rg.kind = GroupKind::CriterionBound;
        rg.source = index;
        rg.name = ci.name + ":bound";
        rg.sweep = sweep;
        rg.split = static_cast<int>(G.rows.size());
        rg.natural = "expression units (SI)";
        criteria[uz(index)].group = static_cast<int>(groups.size());
        groups.push_back(rg);
        P.groups.push_back(std::move(G));
    }
}

void Compiler::build_display() {
    if(doc().contains("display")) {
        const Json & list = doc()["display"];
        for(std::size_t i = 0; i < list.size(); ++i) {
            const Json & dj = list[i];
            DisplayInfo di;
            di.path = std::format("display[{}].expr", i);
            di.text = dj.at("expr").get<std::string>();
            if(dj.contains("color") &&
               !parse_color(dj["color"].get<std::string>(), di.color))
                error(std::format("display[{}].color", i), -1,
                      "expected #RRGGBB or #RRGGBBAA");
            di.fill = dj.contains("fill") && dj["fill"].get<bool>();
            di.width = dj.contains("width") ? dj["width"].get<double>() : 1.0;
            di.label =
                dj.contains("label") ? dj["label"].get<std::string>() : "";
            // The schema caps it; reading it as int64 keeps a value that
            // slipped past validation from wrapping around.
            di.ghosts = dj.contains("ghosts")
                            ? static_cast<int>(std::clamp<std::int64_t>(
                                  dj["ghosts"].get<std::int64_t>(), 0, 1000))
                            : 0;
            di.trace = dj.contains("trace") && dj["trace"].get<bool>();
            if(auto ast = parse(di.text, di.path))
                fill_expr_info(di, expr(*ast, Ctx{di.path, false, {}}));
            display.push_back(std::move(di));
        }
    }
    for(std::size_t i = 0; i < opt_.probes.size(); ++i) {
        ExprInfo pi;
        pi.path = std::format("probes[{}]", i);
        pi.text = opt_.probes[i];
        if(auto ast = parse(pi.text, pi.path))
            fill_expr_info(pi, expr(*ast, Ctx{pi.path, false, {}}));
        probes.push_back(std::move(pi));
    }
}

void Compiler::build_implicit() {
    std::vector<int> roots;
    for(const GroupImpl & G : P.groups)
        for(const RowTemplate & t : G.rows) {
            if(t.kind == RowTemplate::Kind::Node) roots.push_back(t.node);
            if(t.kind == RowTemplate::Kind::Aggregate) {
                roots.push_back(t.lhs.node);
                roots.push_back(t.rhs.node);
            }
        }
    for(const ObjTerm & o : P.objective) roots.push_back(o.term.node);
    std::vector<char> seen(P.nodes.size(), 0);
    std::vector<int> dyads;
    while(!roots.empty()) {
        const int id = roots.back();
        roots.pop_back();
        if(id < 0 || seen[uz(id)]) continue;
        seen[uz(id)] = 1;
        const Node & n = B.node(id);
        if(n.op == Op::Dyad) dyads.push_back(id);
        for(const int a : B.args(id)) roots.push_back(a);
    }
    std::sort(dyads.begin(), dyads.end());
    std::map<int, std::string> let_of;
    for(const LetInfo & l : lets_info)
        if(l.ref.node >= 0) let_of.emplace(l.ref.node, l.name);
    int ordinal = 0;
    for(const int d : dyads) {
        GroupImpl G;
        G.sweep = B.node(d).dep == DepKind::Sweep ? B.node(d).sweep : -1;
        G.dyad_node = d;
        RowTemplate t;
        t.kind = RowTemplate::Kind::NegDyadS2;
        t.node = d;
        G.rows.push_back(t);
        RowGroup rg;
        rg.kind = GroupKind::Assembly;
        rg.source = ordinal;
        auto origin = dyad_origin_.find(d);
        const int base = origin != dyad_origin_.end() ? origin->second : d;
        auto named = let_of.find(base);
        rg.name = named != let_of.end() ? "assembly of " + named->second
                                        : std::format("assembly #{}", ordinal);
        if(auto at = dyad_at_.find(d); at != dyad_at_.end()) {
            const auto [sw, value] = at->second;
            const Node & vn = B.node(value);
            std::string what = "...";
            if(vn.dep == DepKind::Const && vn.type == ValueType::Scalar)
                what = std::format("{:.6g}", B.values(value)[0]);
            else if(vn.op == Op::Var)
                what = design[uz(vn.aux)].name;
            rg.name += std::format(" at {}={}", sweeps[uz(sw)].name, what);
        }
        rg.sweep = G.sweep;
        rg.natural = "m (assembly gap)";
        groups.push_back(rg);
        P.groups.push_back(std::move(G));
        ++ordinal;
    }
    for(std::size_t i = 0; i < design.size(); ++i) {
        DesignVar & v = design[i];
        if(v.fixed || v.chart.kind != ChartKind::BoundingBox ||
           design_node_[i] < 0)
            continue;
        const int dom = [&] {
            if(v.domain.kind == ShapeKind::Circle)
                return B.constant(ValueType::Shape, ShapeKind::Circle,
                                  v.domain.data);
            return B.constant(ValueType::Shape, ShapeKind::Polygon,
                              v.domain.data);
        }();
        const int row = B.make(Op::Clearance, {design_node_[i], dom});
        GroupImpl G;
        G.rows.push_back({RowTemplate::Kind::Node, row, {}, {}, 1.0});
        RowGroup rg;
        rg.kind = GroupKind::Membership;
        rg.source = static_cast<int>(i);
        rg.name = v.name + " in domain";
        rg.natural = "m";
        v.membership_group = static_cast<int>(groups.size());
        groups.push_back(rg);
        P.groups.push_back(std::move(G));
    }
}

void Compiler::finalize() {
    const std::size_t N = P.nodes.size();
    std::vector<int> roots;
    auto add_term = [&](const Term & t) {
        if(t.node >= 0) roots.push_back(t.node);
    };
    for(const GroupImpl & G : P.groups)
        for(const RowTemplate & t : G.rows) {
            if(t.node >= 0) roots.push_back(t.node);
            add_term(t.lhs);
            add_term(t.rhs);
        }
    for(const ObjTerm & o : P.objective) add_term(o.term);
    const std::vector<int> nlp_roots = roots;
    for(const Term & t : P.criteria) add_term(t);
    const std::vector<int> verify_roots = roots;
    for(const LetInfo & l : lets_info) roots.push_back(l.ref.node);
    for(const DisplayInfo & d : display) roots.push_back(d.ref.node);
    for(const ExprInfo & p : probes) roots.push_back(p.ref.node);
    for(const int v : design_node_) roots.push_back(v);
    for(const int s : sweep_node_) roots.push_back(s);

    std::vector<char> reach(N, 0);
    std::vector<int> stack;
    for(const int r : roots)
        if(r >= 0) stack.push_back(r);
    while(!stack.empty()) {
        const int id = stack.back();
        stack.pop_back();
        if(reach[uz(id)]) continue;
        reach[uz(id)] = 1;
        for(const int a : B.args(id)) stack.push_back(a);
    }
    P.slot.assign(N, -1);
    int off = 0;
    for(std::size_t i = 0; i < N; ++i)
        if(reach[i] && P.nodes[i].op == Op::Const) {
            P.slot[i] = off;
            off += P.nodes[i].size;
        }
    P.const_region = off;
    P.const_init.assign(uz(off), 0.0);
    for(std::size_t i = 0; i < N; ++i)
        if(P.slot[i] >= 0) {
            const Node & n = P.nodes[i];
            std::copy(P.pool.begin() + n.aux, P.pool.begin() + n.aux + n.size,
                      P.const_init.begin() + P.slot[i]);
        }
    for(std::size_t i = 0; i < N; ++i)
        if(reach[i] && P.nodes[i].op != Op::Const) {
            P.slot[i] = off;
            off += P.nodes[i].size;
        }
    P.buffer_size = off;
    P.instrs.assign(N, Instr{});
    P.args.clear();
    for(std::size_t i = 0; i < N; ++i) {
        if(!reach[i]) continue;
        const Node & n = P.nodes[i];
        Instr I;
        I.op = n.op;
        I.out = P.slot[i];
        I.meta = n.meta;
        I.aux = n.aux;
        I.arg_begin = static_cast<int>(P.args.size());
        I.arg_count = n.arg_count;
        for(const int a : B.args(static_cast<int>(i))) {
            const Node & an = P.nodes[uz(a)];
            P.args.push_back({P.slot[uz(a)], an.type, an.meta});
        }
        P.instrs[i] = I;
    }
    P.inputs.clear();
    for(std::size_t i = 0; i < design.size(); ++i) {
        const DesignVar & v = design[i];
        if(v.fixed || design_node_[i] < 0) continue;
        P.inputs.push_back({static_cast<int>(i), P.slot[uz(design_node_[i])],
                            v.coord, v.chart});
    }
    P.sweeps = sweeps;
    P.sweep_slot.assign(sweeps.size(), -1);
    for(std::size_t k = 0; k < sweeps.size(); ++k)
        P.sweep_slot[k] = P.slot[uz(sweep_node_[k])];
    P.n = n_coords;
    P.nlp_plan = P.make_plan(nlp_roots);
    P.verify_plan = P.make_plan(verify_roots);
    std::set<int> nlp_aggs;
    for(const GroupImpl & G : P.groups)
        for(const RowTemplate & t : G.rows)
            for(const Term * tm : {&t.lhs, &t.rhs})
                if(tm->agg_index >= 0) nlp_aggs.insert(tm->agg_index);
    for(const ObjTerm & o : P.objective)
        if(o.term.agg_index >= 0 && !o.epigraph)
            nlp_aggs.insert(o.term.agg_index);
    P.nlp_aggs.assign(nlp_aggs.begin(), nlp_aggs.end());
    P.verify_aggs.clear();
    for(std::size_t a = 0; a < P.aggs.size(); ++a)
        P.verify_aggs.push_back(static_cast<int>(a));
}

bool Compiler::run() {
    diags = inst_.validate();
    // Load-time include problems (missing file, bad JSON, schema) would
    // otherwise compile silently without that geometry.
    for(const Diagnostic & d : inst_.include_diagnostics()) diags.push_back(d);
    if(has_errors(diags)) return false;
    for(const Instance::Include & inc : inst_.includes()) {
        if(inc.doc.contains("units") && inc.doc["units"] != "m")
            error(inc.spec + ": units", -1, "only metres are supported");
    }
    try {
        // Names, in precedence order. Each category's position decides which
        // definition a duplicate error points at.
        const Json & d = doc();
        // First, so that a user entry named "pi" gets the duplicate error.
        add_name("pi", NameKind::Builtin, 0, "builtin");
        if(d.contains("sweeps")) {
            int k = 0;
            for(const auto & [name, s] : d["sweeps"].items()) {
                add_name(name, NameKind::Sweep, k++, "sweeps." + name);
                sweeps.emplace_back();
                sweeps.back().name = name;
            }
            for(int i = 0; i < k; ++i) sweep_node_.push_back(B.sweep(i));
        }
        if(d.contains("design")) {
            int k = 0;
            for(const auto & [name, v] : d["design"].items())
                add_name(name, NameKind::Design, k++, "design." + name);
            design.resize(uz(k));
            design_node_.assign(uz(k), -1);
        }
        if(d.contains("let")) {
            int k = 0;
            for(const auto & [name, v] : d["let"].items()) {
                add_name(name, NameKind::Let, k++, "let." + name);
                Lazy L;
                L.name = name;
                L.path = "let." + name;
                L.src = &v;
                lets_.push_back(L);
            }
        }
        if(d.contains("params")) {
            int k = 0;
            for(const auto & [name, v] : d["params"].items()) {
                add_name(name, NameKind::Param, k++, "params." + name);
                Lazy L;
                L.name = name;
                L.path = "params." + name;
                L.src = &v;
                params_.push_back(L);
            }
        }
        for(const Instance::Include & inc : inst_.includes())
            if(inc.doc.contains("geometry"))
                build_geometry(inc.doc["geometry"], inc.spec);
        if(d.contains("geometry")) build_geometry(d["geometry"], "");
        if(has_errors(diags)) return false;

        build_sweeps();
        build_design();
        for(std::size_t i = 0; i < params_.size(); ++i) {
            const int node = resolve_param(static_cast<int>(i), -1, Ctx{});
            ParamInfo pi;
            pi.name = params_[i].name;
            pi.text = number_text(*params_[i].src);
            if(node >= 0) {
                pi.value = B.geo(node);
                if(!all_finite(pi.value.data))
                    error(params_[i].path, -1,
                          "evaluates to a non-finite value (NaN or inf)");
            }
            params_info.push_back(std::move(pi));
        }
        for(std::size_t i = 0; i < lets_.size(); ++i) {
            const int node = resolve_let(static_cast<int>(i), -1, Ctx{});
            LetInfo li;
            li.name = lets_[i].name;
            li.path = lets_[i].path;
            li.text = lets_[i].src->is_string()
                          ? lets_[i].src->get<std::string>()
                          : "";
            fill_expr_info(li, node);
            if(node >= 0 && B.node(node).dep == DepKind::Const &&
               !all_finite(B.values(node)))
                error(li.path, -1,
                      "evaluates to a non-finite value (NaN or inf)");
            lets_info.push_back(std::move(li));
        }
        build_constraints();
        build_criteria();
        build_display();
        if(P.objective.size() > 1)
            warning("criteria",
                    "several \"minimize\" criteria are summed with their "
                    "weights; a weighted sum cannot reach "
                    "concave parts of the trade-off front (bound all but one "
                    "criterion instead)");
        if(has_errors(diags)) return false;
        build_implicit();
        finalize();
    } catch(const nlohmann::json::exception & e) {
        error("", -1, std::string("malformed instance: ") + e.what());
        return false;
    }
    return !has_errors(diags);
}

Plan Program::make_plan(const std::vector<int> & roots) const {
    std::vector<char> mark(nodes.size(), 0);
    std::vector<int> stack;
    for(const int r : roots)
        if(r >= 0) stack.push_back(r);
    while(!stack.empty()) {
        const int id = stack.back();
        stack.pop_back();
        if(mark[uz(id)]) continue;
        mark[uz(id)] = 1;
        const Node & nd = nodes[uz(id)];
        for(int k = 0; k < nd.arg_count; ++k)
            stack.push_back(node_args[uz(nd.arg_begin + k)]);
    }
    Plan p;
    p.sweep.resize(sweeps.size());
    for(std::size_t i = 0; i < nodes.size(); ++i) {
        if(!mark[i]) continue;
        const Node & nd = nodes[i];
        if(nd.op == Op::Const || nd.op == Op::Var || nd.op == Op::Sweep)
            continue;
        if(nd.dep == DepKind::Design)
            p.design.push_back(static_cast<int>(i));
        else if(nd.dep == DepKind::Sweep)
            p.sweep[uz(nd.sweep)].push_back(static_cast<int>(i));
    }
    return p;
}

}  // namespace detail

struct ModelBuilder {
    static void fill(Model & m, detail::Compiler & c,
                     std::shared_ptr<detail::Program> prog,
                     const Instance & inst) {
        m.program_ = std::move(prog);
        m.design_ = std::move(c.design);
        m.sweeps_ = std::move(c.sweeps);
        m.params_ = std::move(c.params_info);
        m.geometry_ = std::move(c.geometry_info);
        m.lets_ = std::move(c.lets_info);
        m.constraints_ = std::move(c.constraints);
        m.criteria_ = std::move(c.criteria);
        m.display_ = std::move(c.display);
        m.probes_ = std::move(c.probes);
        m.groups_ = std::move(c.groups);
        m.solver_ = inst.doc().contains("solver")
                        ? inst.doc()["solver"]
                        : nlohmann::ordered_json::object();
        m.path_ = inst.path();
        m.n_ = c.n_coords;
        m.values_size_ = c.values_size;
    }
};

CompileResult compile(const Instance & instance,
                      const CompileOptions & options) {
    CompileResult r;
    auto prog = std::make_shared<detail::Program>();
    detail::Compiler c(instance, options, *prog);
    const bool ok = c.run();
    r.diagnostics = std::move(c.diags);
    if(!ok) return r;
    auto m = std::make_shared<Model>();
    ModelBuilder::fill(*m, c, std::move(prog), instance);
    r.model = std::move(m);
    return r;
}

}  // namespace gs

#include "gs/engine/builtins.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <numbers>

namespace gs {
namespace {

constexpr Builtin::Kind Function = Builtin::Kind::Function;
constexpr Builtin::Kind SpecialForm = Builtin::Kind::SpecialForm;
constexpr Builtin::Kind Constant = Builtin::Kind::Constant;
constexpr Builtin::Kind Predicate = Builtin::Kind::Predicate;

constexpr std::array kBuiltins = std::to_array<Builtin>({
    {"abs", Function, 1, 1},
    {"acos", Function, 1, 1},
    {"angle", Function, 1, 3},
    {"angle_between", Function, 2, 2},
    {"arc", Function, 4, 4},
    {"asin", Function, 1, 1},
    {"at", SpecialForm, 2, 2},
    {"atan", Function, 1, 1},
    {"atan2", Function, 2, 2},
    {"box", Function, 4, 4},
    {"branch_of", Function, 3, 3},
    {"center", Function, 1, 1},
    {"circle", Function, 2, 2},
    {"clamp", Function, 3, 3},
    {"clearance", Function, 2, 2},
    {"coincident", Predicate, 2, 2},
    {"cos", Function, 1, 1},
    {"cross", Function, 2, 2},
    {"dir", Function, 1, 1},
    {"dist", Function, 2, 2},
    {"dot", Function, 2, 2},
    {"dyad", Function, 5, 5},
    {"edge", Function, 2, 2},
    {"end", Function, 1, 1},
    {"end_angle", Function, 1, 1},
    {"exp", Function, 1, 1},
    {"horizontal", Predicate, 1, 2},
    {"len", Function, 1, 1},
    {"line_dist", Function, 2, 3},
    {"local", Function, 2, 2},
    {"log", Function, 1, 1},
    {"max", Function, 1, -1},
    {"max_over", SpecialForm, 2, 2},
    {"max_proj", Function, 2, 2},
    {"max_x", Function, 1, -1},
    {"max_y", Function, 1, -1},
    {"mean", Function, 1, -1},
    {"mid", Function, 1, 1},
    {"min", Function, 1, -1},
    {"min_over", SpecialForm, 2, 2},
    {"min_proj", Function, 2, 2},
    {"min_x", Function, 1, -1},
    {"min_y", Function, 1, -1},
    {"norm", Function, 1, 1},
    {"normalize", Function, 1, 1},
    {"on", Predicate, 2, 2},
    {"on_line", Predicate, 2, 2},
    {"on_segment", Predicate, 2, 2},
    {"origin", Function, 1, 1},
    {"parallel", Predicate, 2, 3},
    {"perp", Function, 1, 1},
    {"perpendicular", Predicate, 2, 3},
    {"pi", Constant, 0, 0, std::numbers::pi},
    {"place", Function, 3, 3},
    {"polygon", Function, 3, -1},
    {"polyline", Function, 2, -1},
    {"radius", Function, 1, 1},
    {"rect", Function, 4, 4},
    {"rotate", Function, 2, 2},
    {"segment", Function, 2, 2},
    {"sin", Function, 1, 1},
    {"sin_between", Function, 2, 2},
    {"span", Function, 1, 1},
    {"sq", Function, 1, 1},
    {"sqrt", Function, 1, 1},
    {"start", Function, 1, 1},
    {"start_angle", Function, 1, 1},
    {"symmetric", Predicate, 3, 3},
    {"tan", Function, 1, 1},
    {"tangent", Predicate, 3, 3},
    {"tangent_at", Predicate, 4, 4},
    {"translate", Function, 2, 2},
    {"vec", Function, 2, 2},
    {"vertex", Function, 2, 2},
    {"vertical", Predicate, 1, 2},
    {"visible_fraction", Function, 3, -1},
});

static_assert(std::ranges::adjacent_find(kBuiltins,
                                         std::ranges::greater_equal{},
                                         &Builtin::name) == kBuiltins.end(),
              "find_builtin() needs the names sorted and unique");

}  // namespace

std::span<const Builtin> builtin_functions() { return kBuiltins; }

const Builtin * find_builtin(std::string_view name) {
    const auto it =
        std::ranges::lower_bound(kBuiltins, name, std::less{}, &Builtin::name);
    return it != kBuiltins.end() && it->name == name ? &*it : nullptr;
}

}  // namespace gs

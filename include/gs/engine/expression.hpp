#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gs {

// Syntax tree of one expression (see the grammar in the architecture
// contract, section 2). Numbers carry their unit suffix already applied, so
// "15deg" parses to Number with value 15*pi/180.
struct Ast {
    enum class Kind { Number, Name, Call, Neg, Binary, Member, Relation };
    Kind kind = Kind::Number;
    // Column of the token that defines the node (operator, name, number).
    int column = 0;
    // The node's text, [begin, end) in bytes, with the parentheses written
    // around it: replacing it by another operand keeps the expression's
    // structure, which a span cut at `column` or at a leaf would not do in
    // "2 * (W + 1)".
    int begin = 0, end = 0;
    double number = 0.0;
    // Name: an identifier, or a body's member "tv.c" (column: the body's,
    // "tv"). Call: identifier. Member: "x" or "y". Binary: "+ - * / ^".
    // Relation: "<=", ">=" or "==" ("<" and ">" are stored as "<=" / ">=").
    std::string text;
    // A Relation has 2 args, or 3 for a chain `a <= b <= c` (`>=` chains keep
    // the written order; both comparisons are `text`).
    std::vector<std::unique_ptr<Ast>> args;
    // Relation chain: column of the second comparison, -1 otherwise.
    int second_column = -1;
    // Call arguments written as `name = expr` (only valid for at()); empty
    // for positional arguments. Same size as args for Call nodes.
    std::vector<std::string> arg_names;
    std::vector<int> arg_name_columns;
    // Nodes on the longest path down to a leaf (a leaf is 1).
    int height = 1;
};

// The parser rejects deeper expressions (and deeper parenthesis nesting): the
// compiler walks the tree recursively, so an unbounded depth would end in a
// stack overflow instead of a diagnostic.
inline constexpr int kMaxExpressionDepth = 500;

struct ParseResult {
    std::unique_ptr<Ast> ast;
    int error_column = -1;
    std::string error;
    bool ok() const { return ast != nullptr; }
};

// Parses `relation`: an expression with at most one top-level comparison,
// or a chain of two in the same direction (`lo <= e <= hi`, no `==`).
// Whether a comparison is allowed is decided by the compiler, not here.
ParseResult parse_expression(std::string_view text);

// A valid name for params, geometry, design variables, sweeps and lets:
// ASCII letters, digits and '_', not starting with a digit.
bool is_identifier(std::string_view s);

// Fully parenthesised rendering, for tests and debugging: "(1 + (2 * 3))".
std::string to_string(const Ast & ast);

}  // namespace gs

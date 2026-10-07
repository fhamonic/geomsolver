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
    double number = 0.0;
    // Name / Call: identifier. Member: "x" or "y". Binary: "+ - * / ^".
    // Relation: "<=", ">=" or "==" ("<" and ">" are stored as "<=" / ">=").
    std::string text;
    std::vector<std::unique_ptr<Ast>> args;
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

// Parses `relation` (an expression with at most one top-level comparison).
// Whether a comparison is allowed is decided by the compiler, not here.
ParseResult parse_expression(std::string_view text);

// Fully parenthesised rendering, for tests and debugging: "(1 + (2 * 3))".
std::string to_string(const Ast & ast);

}  // namespace gs

#include "gs/engine/expression.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <numbers>
#include <utility>

namespace gs {
namespace {

struct Token {
    enum class Kind { Number, Ident, Op, End };
    Kind kind = Kind::End;
    int column = 0;
    double number = 0.0;
    std::string text;
};

struct SyntaxError {
    int column;
    std::string message;
};

bool ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}
bool ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}
bool digit(char c) { return c >= '0' && c <= '9'; }

double unit_suffix_factor(std::string_view u) {
    if(u == "deg") return std::numbers::pi / 180.0;
    if(u == "rad" || u == "m") return 1.0;
    if(u == "cm") return 0.01;
    if(u == "mm") return 0.001;
    return 0.0;
}

// Quotes the whole UTF-8 sequence starting at byte i: a lone lead byte
// would make the message itself invalid UTF-8, and serialising it to JSON
// then throws.
std::string unexpected_character(std::string_view s, int i) {
    const auto lead =
        static_cast<unsigned char>(s[static_cast<std::size_t>(i)]);
    int len = 1;
    if(lead >= 0xF0 && lead <= 0xF4)
        len = 4;
    else if(lead >= 0xE0 && lead <= 0xEF)
        len = 3;
    else if(lead >= 0xC2 && lead <= 0xDF)
        len = 2;
    if(lead < 0x80)
        return std::format("unexpected character '{}'",
                           s[static_cast<std::size_t>(i)]);
    bool valid = len > 1 && i + len <= static_cast<int>(s.size());
    for(int k = 1; valid && k < len; ++k)
        valid =
            (static_cast<unsigned char>(s[static_cast<std::size_t>(i + k)]) &
             0xC0) == 0x80;
    if(!valid)
        return std::format("unexpected byte 0x{:02X} (not valid UTF-8)", lead);
    return std::format(
        "unexpected character '{}'",
        s.substr(static_cast<std::size_t>(i), static_cast<std::size_t>(len)));
}

std::vector<Token> lex(std::string_view s) {
    std::vector<Token> out;
    const int n = static_cast<int>(s.size());
    auto at = [&](int i) {
        return i < n ? s[static_cast<std::size_t>(i)] : '\0';
    };
    int i = 0;
    while(i < n) {
        const char c = at(i);
        if(std::isspace(static_cast<unsigned char>(c)) != 0) {
            ++i;
            continue;
        }
        Token t;
        t.column = i;
        if(digit(c) || (c == '.' && digit(at(i + 1)))) {
            int j = i;
            while(digit(at(j))) ++j;
            if(at(j) == '.') {
                ++j;
                while(digit(at(j))) ++j;
            }
            if((at(j) == 'e' || at(j) == 'E') &&
               (digit(at(j + 1)) ||
                ((at(j + 1) == '+' || at(j + 1) == '-') && digit(at(j + 2))))) {
                j += 2;
                while(digit(at(j))) ++j;
            }
            const std::string_view lit = s.substr(
                static_cast<std::size_t>(i), static_cast<std::size_t>(j - i));
            double v = 0.0;
            const auto res =
                std::from_chars(lit.data(), lit.data() + lit.size(), v);
            if(res.ec != std::errc{} || res.ptr != lit.data() + lit.size())
                throw SyntaxError{i, std::format("invalid number '{}'", lit)};
            if(ident_start(at(j))) {
                int k = j;
                while(ident_char(at(k))) ++k;
                const std::string_view u =
                    s.substr(static_cast<std::size_t>(j),
                             static_cast<std::size_t>(k - j));
                const double f = unit_suffix_factor(u);
                if(f == 0.0)
                    throw SyntaxError{j,
                                      std::format("unknown unit suffix '{}' "
                                                  "(use deg, rad, m, cm or mm)",
                                                  u)};
                v *= f;
                j = k;
            }
            t.kind = Token::Kind::Number;
            t.number = v;
            t.text = std::string(s.substr(static_cast<std::size_t>(i),
                                          static_cast<std::size_t>(j - i)));
            i = j;
        } else if(ident_start(c)) {
            int j = i;
            while(ident_char(at(j))) ++j;
            t.kind = Token::Kind::Ident;
            t.text = std::string(s.substr(static_cast<std::size_t>(i),
                                          static_cast<std::size_t>(j - i)));
            i = j;
        } else {
            t.kind = Token::Kind::Op;
            const char d = at(i + 1);
            if((c == '<' || c == '>' || c == '=') && d == '=') {
                t.text = std::string{c, '='};
                i += 2;
            } else if(c == '<' || c == '>') {
                t.text = std::string{c, '='};
                ++i;
            } else if(std::string_view("+-*/^(),.=").find(c) !=
                      std::string_view::npos) {
                t.text = std::string(1, c);
                ++i;
            } else {
                throw SyntaxError{i, unexpected_character(s, i)};
            }
        }
        out.push_back(std::move(t));
    }
    Token end;
    end.kind = Token::Kind::End;
    end.column = n;
    out.push_back(std::move(end));
    return out;
}

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : toks_(std::move(tokens)) {}

    std::unique_ptr<Ast> parse() {
        if(peek().kind == Token::Kind::End)
            throw SyntaxError{0, "empty expression"};
        auto lhs = expr();
        if(is_relop(peek())) {
            const Token op = next();
            auto rhs = expr();
            auto rel = node(Ast::Kind::Relation, op.column);
            rel->text = op.text;
            adopt(*rel, std::move(lhs));
            adopt(*rel, std::move(rhs));
            lhs = std::move(rel);
            if(is_relop(peek()))
                throw SyntaxError{peek().column,
                                  "only one comparison is allowed"};
        }
        if(peek().kind != Token::Kind::End) unexpected(peek());
        return lhs;
    }

private:
    static bool is_relop(const Token & t) {
        return t.kind == Token::Kind::Op &&
               (t.text == "<=" || t.text == ">=" || t.text == "==");
    }
    static bool is_op(const Token & t, std::string_view op) {
        return t.kind == Token::Kind::Op && t.text == op;
    }

    const Token & peek(int k = 0) const {
        const std::size_t i =
            std::min(pos_ + static_cast<std::size_t>(k), toks_.size() - 1);
        return toks_[i];
    }
    Token next() {
        Token t = toks_[pos_];
        if(pos_ + 1 < toks_.size()) ++pos_;
        return t;
    }
    [[noreturn]] static void unexpected(const Token & t) {
        if(t.kind == Token::Kind::End)
            throw SyntaxError{t.column, "unexpected end of expression"};
        if(is_relop(t))
            throw SyntaxError{t.column,
                              "a comparison is only allowed at the top level "
                              "of a constraint"};
        if(t.text == "=")
            throw SyntaxError{t.column,
                              "'=' is only allowed in at(sweep = value, body)"};
        throw SyntaxError{t.column, std::format("unexpected '{}'", t.text)};
    }
    void expect(std::string_view op) {
        if(!is_op(peek(), op)) {
            const Token & t = peek();
            if(t.kind == Token::Kind::End || is_relop(t) || t.text == "=")
                unexpected(t);
            throw SyntaxError{
                t.column,
                std::format("expected '{}' but found '{}'", op, t.text)};
        }
        next();
    }
    static std::unique_ptr<Ast> node(Ast::Kind k, int col) {
        auto a = std::make_unique<Ast>();
        a->kind = k;
        a->column = col;
        return a;
    }
    static void adopt(Ast & parent, std::unique_ptr<Ast> child) {
        parent.height = std::max(parent.height, child->height + 1);
        if(parent.height > kMaxExpressionDepth)
            throw SyntaxError{
                parent.column,
                std::format(
                    "expression nested too deeply (more than {} levels)",
                    kMaxExpressionDepth)};
        parent.args.push_back(std::move(child));
    }
    // Guards the recursion itself: "((((...))))" and "----x" recurse without
    // building a node per level.
    struct Nest {
        explicit Nest(Parser & p) : p_(p) {
            if(++p_.nest_ > kMaxExpressionDepth)
                throw SyntaxError{
                    p_.peek().column,
                    std::format(
                        "expression nested too deeply (more than {} levels)",
                        kMaxExpressionDepth)};
        }
        ~Nest() { --p_.nest_; }
        Nest(const Nest &) = delete;
        Nest & operator=(const Nest &) = delete;
        Parser & p_;
    };

    std::unique_ptr<Ast> expr() {
        const Nest guard(*this);
        auto lhs = term();
        while(is_op(peek(), "+") || is_op(peek(), "-")) {
            const Token op = next();
            auto rhs = term();
            lhs = binary(op, std::move(lhs), std::move(rhs));
        }
        return lhs;
    }
    std::unique_ptr<Ast> term() {
        auto lhs = unary();
        while(is_op(peek(), "*") || is_op(peek(), "/")) {
            const Token op = next();
            auto rhs = unary();
            lhs = binary(op, std::move(lhs), std::move(rhs));
        }
        return lhs;
    }
    std::unique_ptr<Ast> unary() {
        if(is_op(peek(), "-")) {
            const Nest guard(*this);
            const Token op = next();
            auto a = node(Ast::Kind::Neg, op.column);
            adopt(*a, unary());
            return a;
        }
        return power();
    }
    std::unique_ptr<Ast> power() {
        auto base = postfix();
        if(is_op(peek(), "^")) {
            const Nest guard(*this);
            const Token op = next();
            auto ex = unary();
            return binary(op, std::move(base), std::move(ex));
        }
        return base;
    }
    std::unique_ptr<Ast> postfix() {
        auto a = primary();
        while(is_op(peek(), ".")) {
            const Token dot = next();
            const Token & f = peek();
            if(f.kind != Token::Kind::Ident || (f.text != "x" && f.text != "y"))
                throw SyntaxError{f.column, "expected 'x' or 'y' after '.'"};
            auto m = node(Ast::Kind::Member, dot.column);
            m->text = f.text;
            next();
            adopt(*m, std::move(a));
            a = std::move(m);
        }
        return a;
    }
    std::unique_ptr<Ast> primary() {
        const Token & t = peek();
        if(t.kind == Token::Kind::Number) {
            auto a = node(Ast::Kind::Number, t.column);
            a->number = t.number;
            a->text = t.text;
            next();
            return a;
        }
        if(t.kind == Token::Kind::Ident) {
            const Token id = next();
            if(!is_op(peek(), "(")) {
                auto a = node(Ast::Kind::Name, id.column);
                a->text = id.text;
                return a;
            }
            next();
            auto call = node(Ast::Kind::Call, id.column);
            call->text = id.text;
            if(!is_op(peek(), ")")) {
                for(;;) {
                    std::string name;
                    int name_col = -1;
                    if(peek().kind == Token::Kind::Ident &&
                       is_op(peek(1), "=")) {
                        name = peek().text;
                        name_col = peek().column;
                        next();
                        next();
                    }
                    adopt(*call, expr());
                    call->arg_names.push_back(std::move(name));
                    call->arg_name_columns.push_back(name_col);
                    if(is_op(peek(), ",")) {
                        next();
                        continue;
                    }
                    break;
                }
            }
            expect(")");
            return call;
        }
        if(is_op(t, "(")) {
            next();
            auto a = expr();
            expect(")");
            return a;
        }
        unexpected(t);
    }
    static std::unique_ptr<Ast> binary(const Token & op, std::unique_ptr<Ast> a,
                                       std::unique_ptr<Ast> b) {
        auto n = node(Ast::Kind::Binary, op.column);
        n->text = op.text;
        adopt(*n, std::move(a));
        adopt(*n, std::move(b));
        return n;
    }

    std::vector<Token> toks_;
    std::size_t pos_ = 0;
    int nest_ = 0;
};

}  // namespace

ParseResult parse_expression(std::string_view text) {
    ParseResult r;
    try {
        Parser p(lex(text));
        r.ast = p.parse();
    } catch(const SyntaxError & e) {
        r.ast.reset();
        r.error_column = e.column;
        r.error = e.message;
    }
    return r;
}

std::string to_string(const Ast & a) {
    switch(a.kind) {
        case Ast::Kind::Number:
            return std::format("{}", a.number);
        case Ast::Kind::Name:
            return a.text;
        case Ast::Kind::Neg:
            return "(-" + to_string(*a.args[0]) + ")";
        case Ast::Kind::Member:
            return to_string(*a.args[0]) + "." + a.text;
        case Ast::Kind::Binary:
        case Ast::Kind::Relation:
            return "(" + to_string(*a.args[0]) + " " + a.text + " " +
                   to_string(*a.args[1]) + ")";
        case Ast::Kind::Call: {
            std::string s = a.text + "(";
            for(std::size_t i = 0; i < a.args.size(); ++i) {
                if(i) s += ", ";
                if(!a.arg_names[i].empty()) s += a.arg_names[i] + " = ";
                s += to_string(*a.args[i]);
            }
            return s + ")";
        }
    }
    return {};
}

}  // namespace gs

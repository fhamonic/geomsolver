#include <numbers>

#include "gs/engine/expression.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

std::string parsed(const char * text) {
    ParseResult r = parse_expression(text);
    INFO(text << ": " << r.error << " at " << r.error_column);
    REQUIRE(r.ok());
    return to_string(*r.ast);
}

ParseResult failed(const char * text) {
    ParseResult r = parse_expression(text);
    INFO(text);
    REQUIRE_FALSE(r.ok());
    return r;
}

double number(const char * text) {
    ParseResult r = parse_expression(text);
    REQUIRE(r.ok());
    REQUIRE(r.ast->kind == Ast::Kind::Number);
    return r.ast->number;
}

}  // namespace

TEST_CASE("parser: precedence and associativity") {
    CHECK(parsed("1 + 2 * 3") == "(1 + (2 * 3))");
    CHECK(parsed("1 - 2 - 3") == "((1 - 2) - 3)");
    CHECK(parsed("8 / 4 / 2") == "((8 / 4) / 2)");
    CHECK(parsed("(1 + 2) * 3") == "((1 + 2) * 3)");
    CHECK(parsed("-2^2") == "(-(2 ^ 2))");
    CHECK(parsed("2^3^2") == "(2 ^ (3 ^ 2))");
    CHECK(parsed("2^-1") == "(2 ^ (-1))");
    CHECK(parsed("--a") == "(-(-a))");
    CHECK(parsed("a * -b") == "(a * (-b))");
    CHECK(parsed("p.x * 2 + q.y") == "((p.x * 2) + q.y)");
    CHECK(parsed("f(a, b + 1).x") == "f(a, (b + 1)).x");
    CHECK(parsed("-p.x") == "(-p.x)");
    CHECK(parsed("at(tau = 0, phi)") == "at(tau = 0, phi)");
    CHECK(parsed("g()") == "g()");
    CHECK(parsed("a + b <= c * d") == "((a + b) <= (c * d))");
    CHECK(parsed("a < b") == "(a <= b)");
    CHECK(parsed("a > b") == "(a >= b)");
    CHECK(parsed("a == b") == "(a == b)");
    CHECK(parsed("  a\t+\nb  ") == "(a + b)");
}

TEST_CASE("parser: numbers and unit suffixes") {
    CHECK(number("12") == 12.0);
    CHECK(number("1.5") == 1.5);
    CHECK(number("1e-3") == 1e-3);
    CHECK(number("2.5E+2") == 250.0);
    CHECK(number(".5") == 0.5);
    CHECK(number("15deg") ==
          doctest::Approx(15.0 * std::numbers::pi / 180.0).epsilon(1e-15));
    CHECK(number("2rad") == 2.0);
    CHECK(number("3m") == 3.0);
    CHECK(number("2cm") == doctest::Approx(0.02).epsilon(1e-15));
    CHECK(number("7mm") == doctest::Approx(0.007).epsilon(1e-15));
    CHECK(number("1e2mm") == doctest::Approx(0.1).epsilon(1e-15));
    // A space ends the number: "15 deg" is a number followed by a name.
    CHECK(parse_expression("15 deg").ok() == false);
}

TEST_CASE("parser: errors carry the 0-based column of the offending token") {
    ParseResult r = failed("1 +");
    CHECK(r.error_column == 3);
    CHECK(r.error.find("end of expression") != std::string::npos);

    r = failed("2xyz + 1");
    CHECK(r.error_column == 1);
    CHECK(r.error.find("unknown unit suffix 'xyz'") != std::string::npos);

    r = failed("f(a, b");
    CHECK(r.error_column == 6);

    r = failed("a ** b");
    CHECK(r.error_column == 3);
    CHECK(r.error.find("unexpected '*'") != std::string::npos);

    r = failed("a = 3");
    CHECK(r.error_column == 2);
    CHECK(r.error.find("at(") != std::string::npos);

    r = failed("p.z");
    CHECK(r.error_column == 2);
    CHECK(r.error.find("'x' or 'y'") != std::string::npos);

    r = failed("(a <= b)");
    CHECK(r.error_column == 3);
    CHECK(r.error.find("top level") != std::string::npos);

    r = failed("a <= b <= c");
    CHECK(r.error_column == 7);
    CHECK(r.error.find("one comparison") != std::string::npos);

    r = failed("a $ b");
    CHECK(r.error_column == 2);

    r = failed("");
    CHECK(r.error_column == 0);

    r = failed("+1");
    CHECK(r.error_column == 0);

    r = failed("f(a,)");
    CHECK(r.error_column == 4);
}

}  // namespace gs::test

// HS071 through casadi::nlpsol("solver", "ipopt", ...): checks that the C++
// API links and that the ipopt plugin is found at run time.
#include <casadi/casadi.hpp>
#include <casadi/config.h>

#include <cmath>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    using casadi::SX;
    // argv[1], when given, is handed to GlobalOptions::setCasadiPath.
    if (argc > 1) casadi::GlobalOptions::setCasadiPath(argv[1]);

    SX x = SX::sym("x", 4);
    SX f = x(0) * x(3) * (x(0) + x(1) + x(2)) + x(2);
    SX g = SX::vertcat({x(0) * x(1) * x(2) * x(3), SX::sumsqr(x)});
    casadi::SXDict nlp{{"x", x}, {"f", f}, {"g", g}};
    casadi::Dict opts{{"ipopt.print_level", 0}, {"print_time", false}, {"ipopt.tol", 1e-9}};
    casadi::Function solver = casadi::nlpsol("solver", "ipopt", nlp, opts);

    casadi::DMDict arg{{"x0", std::vector<double>{1, 5, 5, 1}},
                       {"lbx", 1}, {"ubx", 5},
                       {"lbg", std::vector<double>{25, 40}}, {"ubg", std::vector<double>{casadi::inf, 40}}};
    casadi::DMDict res = solver(arg);
    double fopt = static_cast<double>(res.at("f"));
    std::vector<double> xopt = static_cast<std::vector<double>>(res.at("x"));
    std::cout << "casadi " << CASADI_VERSION_STRING << "  status=" << solver.stats().at("return_status")
              << "  f=" << fopt << "  x1=" << xopt[1] << "\n";
    bool ok = std::abs(fopt - 17.0140173) < 1e-5 && std::abs(xopt[1] - 4.74299963) < 1e-4;
    std::cout << "casadi HS071 " << (ok ? "OK" : "FAILED") << "\n";
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

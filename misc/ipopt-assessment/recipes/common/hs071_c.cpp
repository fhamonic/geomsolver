// HS071 through Ipopt's C interface (IpStdCInterface.h), compiled as C++26.
//   min x1 x4 (x1 + x2 + x3) + x3
//   s.t. x1 x2 x3 x4 >= 25, x1^2 + x2^2 + x3^2 + x4^2 = 40, 1 <= x <= 5
// Known optimum: x = (1, 4.74299963, 3.82114998, 1.37940829), f = 17.0140173.
#include <IpStdCInterface.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <print>

namespace {

bool eval_f(ipindex, ipnumber* x, bool, ipnumber* f, UserDataPtr) {
    *f = x[0] * x[3] * (x[0] + x[1] + x[2]) + x[2];
    return true;
}

bool eval_grad_f(ipindex, ipnumber* x, bool, ipnumber* g, UserDataPtr) {
    g[0] = x[0] * x[3] + x[3] * (x[0] + x[1] + x[2]);
    g[1] = x[0] * x[3];
    g[2] = x[0] * x[3] + 1;
    g[3] = x[0] * (x[0] + x[1] + x[2]);
    return true;
}

bool eval_g(ipindex, ipnumber* x, bool, ipindex, ipnumber* g, UserDataPtr) {
    g[0] = x[0] * x[1] * x[2] * x[3];
    g[1] = x[0] * x[0] + x[1] * x[1] + x[2] * x[2] + x[3] * x[3];
    return true;
}

bool eval_jac_g(ipindex, ipnumber* x, bool, ipindex, ipindex, ipindex* irow, ipindex* jcol,
                ipnumber* values, UserDataPtr) {
    if (values == nullptr) {
        for (ipindex k = 0; k < 8; ++k) {
            irow[k] = k / 4;
            jcol[k] = k % 4;
        }
        return true;
    }
    values[0] = x[1] * x[2] * x[3];
    values[1] = x[0] * x[2] * x[3];
    values[2] = x[0] * x[1] * x[3];
    values[3] = x[0] * x[1] * x[2];
    for (int i = 0; i < 4; ++i) values[4 + i] = 2 * x[i];
    return true;
}

bool eval_h(ipindex, ipnumber*, bool, ipnumber, ipindex, ipnumber*, bool, ipindex, ipindex*,
            ipindex*, ipnumber*, UserDataPtr) {
    return false;  // quasi-Newton: hessian_approximation=limited-memory
}

}  // namespace

int main() {
    std::array<ipnumber, 4> xl{1, 1, 1, 1}, xu{5, 5, 5, 5};
    std::array<ipnumber, 2> gl{25, 40}, gu{2e19, 40};
    IpoptProblem nlp = CreateIpoptProblem(4, xl.data(), xu.data(), 2, gl.data(), gu.data(), 8, 0, 0,
                                          &eval_f, &eval_g, &eval_grad_f, &eval_jac_g, &eval_h);
    AddIpoptNumOption(nlp, const_cast<char*>("tol"), 1e-9);
    AddIpoptStrOption(nlp, const_cast<char*>("hessian_approximation"), const_cast<char*>("limited-memory"));
    AddIpoptStrOption(nlp, const_cast<char*>("linear_solver"), const_cast<char*>("mumps"));
    AddIpoptIntOption(nlp, const_cast<char*>("print_level"), 0);

    std::array<ipnumber, 4> x{1, 5, 5, 1};
    ipnumber f = 0;
    ApplicationReturnStatus status = IpoptSolve(nlp, x.data(), nullptr, &f, nullptr, nullptr, nullptr, nullptr);
    FreeIpoptProblem(nlp);

    // Header macro: the 3.14.11 in the CasADi wheel has no GetIpoptVersion().
    std::println("Ipopt {}  status={}  f={:.7f}  x=({:.6f}, {:.6f}, {:.6f}, {:.6f})", IPOPT_VERSION,
                 static_cast<int>(status), f, x[0], x[1], x[2], x[3]);
    bool ok = status == Solve_Succeeded && std::abs(f - 17.0140173) < 1e-5 && std::abs(x[1] - 4.74299963) < 1e-4;
    std::println("HS071 {}", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}

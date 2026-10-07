#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gs/solve/settings.hpp"

namespace gs {

// Outputs of LocalProblem::evaluate. f and the value arrays are always set;
// gradient pointers are null when nlopt does not need derivatives.
// Jacobians are row-major: dg[row * n + j].
struct LocalBuffers {
    double * f = nullptr;
    double * df = nullptr;
    double * g = nullptr;  // m_ineq() values
    double * dg = nullptr;
    double * h = nullptr;  // m_eq() values
    double * dh = nullptr;
};

// min f(x) subject to g(x) <= 0 and h(x) == 0, within the box bounds of
// LocalOptions.
class LocalProblem {
public:
    virtual ~LocalProblem() = default;
    virtual int n() const = 0;
    virtual int m_ineq() const = 0;
    virtual int m_eq() const { return 0; }
    // May throw: minimize() stops nlopt and rethrows the exception.
    virtual void evaluate(const double * x, const LocalBuffers & out) = 0;
};

enum class LocalStatus : std::uint8_t {
    Converged,  // nlopt SUCCESS, FTOL_REACHED or XTOL_REACHED
    StopvalReached,
    MaxevalReached,
    MaxtimeReached,
    RoundoffLimited,  // a soft success: the point still has to be verified
    Failure,
    InvalidArgs,
    Stopped,    // the stop flag was raised
    NonFinite,  // the returned point evaluates to NaN or inf
};

std::string_view status_name(LocalStatus s);

struct LocalOptions {
    Algorithm algorithm = Algorithm::SLSQP;
    std::vector<double> lb, ub;  // size n; infinite entries allowed
    int maxeval = 3000;
    double maxtime = 0.0;  // seconds, 0 = unlimited
    double xtol_rel = 1e-7;
    // Keep 0 with an epigraph objective: its relative change stalls long
    // before the design converges.
    double ftol_rel = 0.0;
    double stopval = -std::numeric_limits<double>::infinity();
    double constraint_tol = 1e-8;
    std::uint64_t seed = 0;  // nlopt::srand, set right before optimize()
    // (coordinate, step) pairs replacing nlopt's default initial step, which
    // is meaningless for unbounded coordinates.
    std::vector<std::pair<int, double>> initial_step;
    // Polled in every callback; raising it ends the solve within one
    // evaluation. nlopt's own
    // force_stop() must not be called from another thread.
    const std::atomic<bool> * stop = nullptr;
};

struct LocalResult {
    std::vector<double> x;  // the best point nlopt reports
    double f = std::numeric_limits<double>::quiet_NaN();  // f(x), re-evaluated
    int nlopt_code = 0;
    LocalStatus status = LocalStatus::Failure;
    long evaluations = 0;  // calls of LocalProblem::evaluate
    long gradient_evaluations = 0;
    // Evaluations with a NaN / inf value or derivative. nlopt sees such
    // values as kNonFiniteValue (derivatives as 0) because it would otherwise
    // report garbage with a success code.
    long nonfinite = 0;
    std::string message;  // nlopt's error message, if any
};

inline constexpr double kNonFiniteValue = 1e10;

// One nlopt solve from x0 (clamped to the bounds). An exception thrown by the
// problem is rethrown here, with its own type and message, after nlopt has
// returned. Reentrant: concurrent calls on different problems are safe.
LocalResult minimize(LocalProblem & problem, std::vector<double> x0,
                     const LocalOptions & options);

}  // namespace gs

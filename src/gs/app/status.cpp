#include "gs/app/status.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

#include "gs/app/format.hpp"

namespace gs::app {
namespace {

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

int count(const Document & doc, Diagnostic::Severity severity) {
    return static_cast<int>(
        std::ranges::count(doc.diagnostics(), severity, &Diagnostic::severity));
}

}  // namespace

Status group_status(const Model & m, const Scene & s,
                    const std::vector<int> & groups, double tol) {
    Status st;
    if(!s.verification || groups.empty()) {
        st.text = groups.empty() ? "disabled" : "-";
        return st;
    }
    double worst = -std::numeric_limits<double>::infinity();
    int wg = -1;
    for(const int g : groups) {
        const double v = s.verification->groups[uz(g)].violation;
        if(v > worst || std::isnan(v)) {
            worst = v;
            wg = g;
        }
    }
    const GroupCheck & c = s.verification->groups[uz(wg)];
    const RowGroup & rg = m.groups()[uz(wg)];
    const std::string where =
        rg.sweep >= 0 && std::isfinite(c.sweep_value)
            ? std::format(" at {}={}", m.sweeps()[uz(rg.sweep)].name,
                          format_number(c.sweep_value, 4))
            : "";
    if(std::isnan(worst) || worst > tol) {
        st.tone = Tone::Bad;
        st.text = std::format("violated {}{}", format_number(worst, 3), where);
    } else if(worst > -1e-6) {
        st.tone = Tone::Active;
        // Inside the tolerance the margin's sign is rounding: printed as is,
        // "active -6.1e-12" reads as a violation.
        st.text =
            std::format("active {}{}",
                        format_number(worst > -tol ? 0.0 : -worst, 2), where);
    } else {
        st.tone = Tone::Ok;
        st.text = std::format("margin {}{}", format_number(-worst, 3), where);
    }
    st.tip = std::format(
        "Worst row over the {}-sample grid, in the row's natural unit ({}). "
        "Positive margin = satisfied.",
        s.verify_samples, rg.natural);
    if(c.between) {
        const int k = rg.sweep;
        const std::string samples =
            k >= 0 ? std::format("the {} samples of the fixed sweep {}",
                                 s.verification->t[uz(k)].size(),
                                 m.sweeps()[uz(k)].name)
                   : std::string("the samples of a fixed sweep");
        const std::string at =
            k >= 0 && std::isfinite(c.between->sweep_value)
                ? std::format(" at {}={}", m.sweeps()[uz(k)].name,
                              format_number(c.between->sweep_value, 4))
                : "";
        st.tip += std::format(
            "\nJudged on {}. Between them, on the {}-sample grid: margin "
            "{}{}.",
            samples, s.verify_samples, format_number(-c.between->value, 3), at);
        if(!(c.between->value <= tol))
            st.text += std::format(", missed {} between samples",
                                   format_number(c.between->value, 3));
    }
    return st;
}

Status excluded_status(const ExcludedItem & e) {
    return {Tone::Bad, "excluded",
            std::format("Excluded from the model: {}\nSolve, Polish and the "
                        "Pareto study wait until it compiles.",
                        e.reason)};
}

DiagnosticText diagnostic_text(const Diagnostic & d,
                               std::string_view expression) {
    const bool error = d.severity == Diagnostic::Severity::Error;
    DiagnosticText out;
    out.tone = error ? Tone::Bad : Tone::Warn;
    out.message =
        d.column >= 0
            ? std::format("{} at column {}: {}", error ? "error" : "warning",
                          d.column, d.message)
            : std::format("{}: {}", error ? "error" : "warning", d.message);
    const auto col = static_cast<std::size_t>(d.column);
    if(d.column >= 0 && col <= expression.size() && !expression.empty()) {
        out.marked = true;
        out.head = expression.substr(0, col);
        out.tail = expression.substr(col);
    }
    return out;
}

Status compile_status(const Document & doc) {
    if(!doc.has_instance()) return {Tone::Dim, "no instance", {}};
    const int errors = count(doc, Diagnostic::Severity::Error);
    if(!doc.model())
        return {Tone::Bad,
                std::format("does not compile: {} error(s)", errors),
                {}};
    if(doc.stale())
        return {Tone::Bad,
                std::format("{} error(s): the last good model stays active",
                            errors),
                {}};
    if(!doc.excluded().empty())
        return {Tone::Bad,
                std::format("{} error(s): {} item(s) excluded from the model",
                            errors, doc.excluded().size()),
                {}};
    return {Tone::Ok,
            std::format("compiled: {} coordinates, {} row groups",
                        doc.model()->n(), doc.model()->groups().size()),
            {}};
}

std::string warning_count(const Document & doc) {
    const int warnings = count(doc, Diagnostic::Severity::Warning);
    return warnings > 0 ? std::format("{} warning(s)", warnings)
                        : std::string();
}

}  // namespace gs::app

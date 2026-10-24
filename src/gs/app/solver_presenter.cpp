#include "gs/app/solver_presenter.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "gs/app/app.hpp"
#include "gs/app/format.hpp"

namespace gs::app {
namespace {

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

const char * const kAlgorithms[] = {"SLSQP", "COBYLA"};

struct CountRange {
    int SolverSettings::*field;
    int lo, hi;
};
const CountRange kCounts[] = {
    {&SolverSettings::starts, 1, 100000},
    {&SolverSettings::threads, 0, 1024},
    {&SolverSettings::initial_samples, 1, 100000},
    {&SolverSettings::verify_samples, 2, 1000000},
    {&SolverSettings::max_exchange_iterations, 0, 10000},
    {&SolverSettings::maxeval, 1, 100000000},
};

const char * kind_name(SolveKind k) {
    switch(k) {
        case SolveKind::Multistart:
            return "multistart";
        case SolveKind::Polish:
            return "polish";
        case SolveKind::Pareto:
            return "Pareto study";
    }
    return "?";
}

std::string joined_names(const Model & m, const std::vector<int> & criteria) {
    std::string s;
    for(const int c : criteria)
        if(c >= 0 && uz(c) < m.criteria().size())
            s += (s.empty() ? "" : ", ") + m.criteria()[uz(c)].name;
    return s;
}

std::string with_unit(const std::string & name, const std::string & unit) {
    return unit.empty() ? name : std::format("{} [{}]", name, unit);
}

std::string duration(const SolverSession & session, double seconds,
                     int decimals) {
    if(!session.show_durations()) return "\u2014";
    return std::format("{:.{}f} s", seconds, decimals);
}

void set_cell(Cell & c, Cell::Kind kind, std::string text,
              Tone tone = Tone::Normal, std::string_view tip = {}) {
    c.kind = kind;
    c.text = std::move(text);
    c.tone = tone;
    c.tip = tip;
}

}  // namespace

bool SolutionsTable::begin() {
    const SolverResults & r = session_.results();
    if(!r.model || (r.solutions.empty() && r.pareto.empty())) return false;
    const Model & m = *r.model;
    pareto_ = r.kind == SolveKind::Pareto;
    bound_unit_ = pareto_ && !r.pareto_criteria.empty() &&
                          uz(r.pareto_criteria[0]) < m.criteria().size()
                      ? m.criteria()[uz(r.pareto_criteria[0])].unit
                      : std::string();
    objective_unit_ = objective_unit(m);
    headers_.clear();
    headers_.push_back(!pareto_ ? std::string("#")
                                : with_unit("bound", bound_unit_));
    headers_.push_back(with_unit("objective", objective_unit_));
    for(const char * h : {"feasible", "max viol.", "worst", "hits"})
        headers_.emplace_back(h);
    for(const CriterionInfo & c : m.criteria())
        headers_.push_back(with_unit(c.name, c.unit));
    return true;
}

std::string SolutionsTable::summary() const {
    const SolverResults & r = session_.results();
    const std::string took = duration(session_, r.wall_seconds, 1);
    if(pareto_)
        return std::format("{} of {}, {} point(s), {}", kind_name(r.kind),
                           joined_names(*r.model, r.pareto_criteria),
                           r.pareto.size(), took);
    return std::format("{}, {} distinct solution(s), {}", kind_name(r.kind),
                       r.solutions.size(), took);
}

bool SolutionsTable::stale() const {
    return session_.results().model != doc_.model();
}

std::size_t SolutionsTable::rows() const {
    const SolverResults & r = session_.results();
    return pareto_ ? r.pareto.size() : r.solutions.size();
}

const SolverSolution & SolutionsTable::solution(std::size_t i) const {
    const SolverResults & r = session_.results();
    return pareto_ ? r.pareto[i].solution : r.solutions[i];
}

void SolutionsTable::row(std::size_t i, TableRow & out) const {
    const SolverResults & r = session_.results();
    const SolverSolution & s = solution(i);
    const auto & crits = r.model->criteria();
    out.label =
        pareto_
            ? std::format("{:.6g}", to_display(r.pareto[i].bound, bound_unit_))
            : std::format("{}", i + 1);
    out.selected = session_.selected() == static_cast<int>(i);
    std::string details = s.status;
    if(!std::isnan(s.seconds))
        details +=
            (details.empty() ? "" : "\n") + duration(session_, s.seconds, 3);
    out.tip = details.empty() ? "click to load" : "click to load\n\n" + details;
    out.cells.resize(5 + crits.size());
    set_cell(out.cells[0], Cell::Kind::Text,
             format_number(to_display(s.objective, objective_unit_), 8));
    set_cell(out.cells[1], Cell::Kind::Text, s.feasible ? "yes" : "no",
             s.feasible ? Tone::Ok : Tone::Bad);
    set_cell(out.cells[2], Cell::Kind::Text, format_number(s.max_violation, 3));
    set_cell(out.cells[3], Cell::Kind::Text, s.worst);
    if(s.variants.size() > 1)
        set_cell(out.cells[4], Cell::Kind::Choices,
                 std::format("{} ({} variants)", s.hits, s.variants.size()),
                 Tone::Normal,
                 "Click to list the variants and load one. The runs ended at "
                 "different design points with the same objective and the "
                 "same values of the bounds that are active in either: a "
                 "mirror image with the labels swapped, a variable the "
                 "optimum leaves free, or another design with those values");
    else
        set_cell(out.cells[4], Cell::Kind::Text, std::format("{}", s.hits));
    for(std::size_t c = 0; c < crits.size(); ++c) {
        if(c < s.criteria.size())
            set_cell(
                out.cells[5 + c], Cell::Kind::Text,
                format_number(to_display(s.criteria[c], crits[c].unit), 7));
        else
            set_cell(out.cells[5 + c], Cell::Kind::Empty, {});
    }
}

void SolutionsTable::activate(std::size_t i) {
    session_.select(static_cast<int>(i));
    session_.load(doc_, solution(i).x);
}

void SolutionsTable::choices(std::size_t i, std::size_t,
                             std::vector<std::string> & out) const {
    const SolverSolution & s = solution(i);
    out.clear();
    for(std::size_t v = 0; v < s.variants.size(); ++v) {
        const SolverSolution::Variant & var = s.variants[v];
        out.push_back(
            v == 0
                ? std::format("variant 1, {} hits: the row's design", var.hits)
                : std::format("variant {}, {} hits: {}", v + 1, var.hits,
                              var.differs));
    }
}

void SolutionsTable::choose(std::size_t i, std::size_t, std::size_t choice) {
    session_.select(static_cast<int>(i));
    session_.load(doc_, solution(i).variants[choice].x);
}

SolverPresenter::SolverPresenter(App & app)
    : session_(app.solver())
    , doc_(app.document())
    , solutions_(app.solver(), app.document()) {}

void SolverPresenter::begin_frame() {
    session_.follow(doc_);
    if(session_.connected()) session_.refresh();
    busy_ = session_.running();
}

std::optional<SolverPresenter::Blocker> SolverPresenter::blocker() const {
    if(doc_.stale()) return Blocker{"fix the errors first", true};
    if(!doc_.excluded().empty()) return Blocker{excluded_message(doc_), false};
    if(nothing_to_optimise(doc_)) return Blocker{kNothingToOptimise, false};
    return std::nullopt;
}

std::string_view SolverPresenter::solve_tip() const {
    return nothing_to_optimise(doc_) ? kNothingToOptimise : kSolveTip;
}

SolverPresenter::Progress SolverPresenter::progress() const {
    Progress out;
    if(!session_.connected()) {
        out.overlay = "not connected";
        return out;
    }
    const SolverProgress p = session_.progress();
    if(p.total > 0) {
        out.fraction = static_cast<float>(p.done) / static_cast<float>(p.total);
        out.overlay = std::format("{}/{} runs", p.done, p.total);
    } else {
        out.overlay = busy_ ? "running" : "idle";
    }
    out.line = p.phase;
    if(std::isfinite(p.best_objective))
        out.line += std::format(
            "{}best feasible {}", out.line.empty() ? "" : "  |  ",
            objective_text(doc_.model().get(), p.best_objective, 8));
    out.message = p.message;
    if(!std::isnan(p.seconds))
        out.message += (out.message.empty() ? "" : ", ") +
                       duration(session_, p.seconds, 1);
    return out;
}

std::string SolverPresenter::algorithm_note() const {
    const std::string & note = session_.settings().note;
    return note.empty() ? std::string() : "solver.algorithm: " + note;
}

std::span<const char * const> SolverPresenter::algorithms() {
    return kAlgorithms;
}

void SolverPresenter::set_algorithm(std::string_view algorithm) {
    SolverSettings & s = session_.settings();
    s.algorithm = algorithm;
    s.note.clear();
}

void SolverPresenter::set_count(int SolverSettings::*field, int value) {
    for(const CountRange & c : kCounts)
        if(c.field == field) value = std::clamp(value, c.lo, c.hi);
    session_.settings().*field = value;
}

void SolverPresenter::reset_settings() {
    if(doc_.model())
        session_.settings() =
            SolverSettings::from_json(doc_.model()->solver_settings());
}

std::string SolverPresenter::tab_label() const {
    return connected() ? std::string("Solver")
                       : std::string("Solver (not connected)");
}

void SolverPresenter::store_settings() {
    if(!doc_.has_instance()) return;
    session_.follow(doc_);
    EditBatch batch("store solver settings");
    for(const auto & [k, v] : session_.settings().to_json().items())
        batch.set("solver." + k, v);
    doc_.apply(std::move(batch));
}

std::optional<SolverPresenter::ParetoForm> SolverPresenter::pareto_form() {
    const std::shared_ptr<const Model> m = doc_.model();
    if(!m) return std::nullopt;
    std::vector<std::string> & chosen = session_.pareto_criteria();
    std::erase_if(chosen, [&](const std::string & name) {
        const int c = m->find_criterion(name);
        return c < 0 || !bounded(m->criteria()[uz(c)]);
    });
    ParetoForm f;
    f.model = m;
    for(std::size_t c = 0; c < m->criteria().size(); ++c)
        if(bounded(m->criteria()[c])) f.bounded.push_back(static_cast<int>(c));
    if(!chosen.empty())
        f.unit = m->criteria()[uz(m->find_criterion(chosen[0]))].unit;
    for(const std::string & name : chosen)
        f.preview += (f.preview.empty() ? "" : ", ") + name;
    f.hint = std::format("bounds in {}, e.g. 0, 1, 2, 4",
                         f.unit.empty() ? "SI" : f.unit);
    return f;
}

SolverPresenter::ParetoChoice SolverPresenter::pareto_choice(
    const ParetoForm & form, std::size_t k) const {
    const CriterionInfo & c = form.model->criteria()[uz(form.bounded[k])];
    const std::vector<std::string> & chosen = session_.pareto_criteria();
    ParetoChoice out;
    out.path = c.path;
    out.label = with_unit(c.name, c.unit);
    out.on = std::ranges::find(chosen, c.name) != chosen.end();
    // The bounds are typed in one unit and applied to every selected
    // criterion: another unit would get a meaningless bound.
    out.enabled = out.on || chosen.empty() || c.unit == form.unit;
    return out;
}

void SolverPresenter::toggle_pareto(const ParetoForm & form, std::size_t k) {
    const std::string & name = form.model->criteria()[uz(form.bounded[k])].name;
    std::vector<std::string> & chosen = session_.pareto_criteria();
    if(const auto it = std::ranges::find(chosen, name); it != chosen.end())
        chosen.erase(it);
    else
        chosen.push_back(name);
}

bool SolverPresenter::can_run_pareto() const {
    return !session_.running() && !session_.pareto_criteria().empty() &&
           doc_.complete() && !nothing_to_optimise(doc_);
}

void SolverPresenter::run_pareto() { session_.start(doc_, SolveKind::Pareto); }

std::optional<SolverPresenter::ParetoPlot> SolverPresenter::pareto_plot()
    const {
    const SolverResults & r = session_.results();
    if(r.pareto.empty() || !r.model) return std::nullopt;
    const Model & m = *r.model;
    const bool rc_ok = !r.pareto_criteria.empty() &&
                       uz(r.pareto_criteria[0]) < m.criteria().size();
    const std::string rname =
        rc_ok ? joined_names(m, r.pareto_criteria) : "bound";
    const std::string runit =
        rc_ok ? m.criteria()[uz(r.pareto_criteria[0])].unit : "";
    const std::string ounit = objective_unit(m);
    ParetoPlot plot;
    for(const ParetoPoint & p : r.pareto) {
        const double bx = to_display(p.bound, runit);
        const double by = to_display(p.solution.objective, ounit);
        (p.solution.feasible ? plot.feasible_x : plot.infeasible_x)
            .push_back(bx);
        (p.solution.feasible ? plot.feasible_y : plot.infeasible_y)
            .push_back(by);
        plot.points.push_back({bx, by});
    }
    plot.x_label = std::format("{} bound{}", rname,
                               runit.empty() ? "" : " [" + runit + "]");
    plot.y_label =
        std::format("objective{}", ounit.empty() ? "" : " [" + ounit + "]");
    // A relaxed bound improves the objective: for a minimised one the upper
    // right stays empty, for a maximised one the lower right.
    plot.legend_south_east = m.objective_sign() < 0;
    return plot;
}

void SolverPresenter::load_pareto_point(std::size_t i) {
    session_.load(doc_, session_.results().pareto[i].solution.x);
}

}  // namespace gs::app

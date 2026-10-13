#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gs/app/document.hpp"
#include "gs/app/solver_session.hpp"
#include "gs/app/table.hpp"

namespace gs::app {

class App;

// The latest results of the solver session as a table: one row per
// distinct solution, or per point of a Pareto study; a click loads it.
class SolutionsTable final : public TablePresenter {
public:
    SolutionsTable(SolverSession & session, Document & doc)
        : session_(session), doc_(doc) {}

    // Reads the session's results; false when there are none.
    bool begin();
    // "multistart, 3 distinct solution(s), 0.1 s"
    std::string summary() const;
    // The results come from an earlier compile: loading one maps the
    // variables by name.
    bool stale() const;

    const std::vector<std::string> & headers() const override {
        return headers_;
    }
    std::size_t rows() const override;
    void row(std::size_t i, TableRow & out) const override;
    void activate(std::size_t i) override;
    // The variants of row i's design, in the hits cell.
    void choices(std::size_t i, std::size_t column,
                 std::vector<std::string> & out) const override;
    void choose(std::size_t i, std::size_t column, std::size_t choice) override;

private:
    const SolverSolution & solution(std::size_t i) const;

    SolverSession & session_;
    Document & doc_;
    bool pareto_ = false;
    std::string bound_unit_, objective_unit_;
    std::vector<std::string> headers_;
};

// The Solver tab without its widgets: what blocks a job, its progress, the
// settings of the next one, the solutions table and the Pareto study. Its
// buttons are the App's solver.* actions.
class SolverPresenter {
public:
    explicit SolverPresenter(App & app);

    // The texts of the Solver tab's widgets, in the order they are drawn.
    static constexpr const char * kNotConnected = "Solver not connected";
    static constexpr const char * kNotConnectedNote =
        "This build has no solver service (make_solver_backend() returned "
        "null). The settings below are the instance defaults; Solve, Polish "
        "and the Pareto study are disabled. Design edits, the canvas and the "
        "checks work without it.";
    static constexpr const char * kSolveTip =
        "Multistart from seeded uniform starts (plus the current design when "
        "enabled); the best solution is listed first";
    static constexpr const char * kPolishTip =
        "One local run from the current design";
    static constexpr const char * kSettings = "Settings";
    static constexpr const char * kAlgorithm = "algorithm";
    static constexpr const char * kStarts = "starts";
    static constexpr const char * kSeed = "seed";
    static constexpr const char * kThreads = "threads (0 = all)";
    static constexpr const char * kPhase1 = "phase 1 (feasibility first)";
    static constexpr const char * kIncludeCurrent =
        "include the current design as a start";
    static constexpr const char * kInitialSamples = "initial samples";
    static constexpr const char * kVerifySamples = "verify samples";
    static constexpr const char * kExchangeIterations =
        "max exchange iterations";
    static constexpr const char * kFeasTol = "feasibility tolerance";
    static constexpr const char * kMaxeval = "max evaluations per run";
    static constexpr const char * kXtolRel = "xtol_rel";
    static constexpr const char * kInstanceDefaults = "Instance defaults";
    static constexpr const char * kStore = "Store in instance";
    static constexpr const char * kStoreTip =
        "Write these settings into the instance's \"solver\" object";
    static constexpr const char * kSolutions = "Solutions";
    static constexpr const char * kNoResults = "No results yet.";
    static constexpr const char * kStaleResults =
        "Results belong to an earlier compile; loading maps variables by "
        "name.";
    static constexpr const char * kPareto = "Pareto study";
    static constexpr const char * kBoundedCriteria = "bounded criteria";
    static constexpr const char * kChoose = "(choose)";
    static constexpr const char * kBoundedTip =
        "Each selected criterion gets the same bound at every point, e.g. "
        "both view angles; only criteria with the same unit combine";
    static constexpr const char * kRunPareto = "Run Pareto study";
    static constexpr const char * kRunParetoTip =
        "Epsilon-constraint sweep: one smaller multistart per bound, "
        "warm-started from the previous best";
    static constexpr const char * kFeasible = "feasible";
    static constexpr const char * kInfeasible = "infeasible";
    static constexpr const char * kParetoHint =
        "Click a point to load its design.";
    // "Solver", or "Solver (not connected)" without a solver service.
    std::string tab_label() const;

    // At the start of a frame: the settings follow a new instance, and the
    // results of a running job are copied.
    void begin_frame();
    bool connected() const { return session_.connected(); }

    struct Blocker {
        std::string text;
        // A short reason on the buttons' line; else a paragraph under them.
        bool brief = false;
    };
    // Why Solve, Polish and the Pareto study cannot start.
    std::optional<Blocker> blocker() const;
    struct Progress {
        float fraction = 0.0f;
        std::string overlay;  // "12/64 runs", "running", "idle"
        std::string line;     // the phase and the best objective so far
        std::string message;
    };
    // The job as it was when the frame began.
    Progress progress() const;
    // The last start or load, "" for none.
    const std::string & message() const { return session_.message(); }
    // Why the instance's algorithm was replaced, "" for none.
    std::string algorithm_note() const;

    SolverSettings & settings() { return session_.settings(); }
    static std::span<const char * const> algorithms();
    void set_algorithm(std::string_view algorithm);
    // Sets one of the counts of settings(), clamped to its range.
    void set_count(int SolverSettings::*field, int value);
    // Back to the instance's "solver" settings, once it compiles.
    void reset_settings();
    // Writes the settings into the instance's "solver" object. It follows
    // the document first: before any frame, settings() still holds the
    // defaults or the previous instance's, which would overwrite its own.
    void store_settings();
    // True once after a --pareto run: open the study.
    bool take_open_pareto() { return session_.take_open_pareto(); }

    SolutionsTable & solutions() { return solutions_; }

    // The Pareto study's form, without a model: none.
    struct ParetoForm {
        std::shared_ptr<const Model> model;
        std::vector<int> bounded;  // the criteria it can bound
        std::string unit;          // of the chosen ones
        std::string preview;       // "view_couch, view_kitchen", "" for none
        std::string hint;          // what the bounds field expects
    };
    // Drops the chosen criteria the model no longer bounds.
    std::optional<ParetoForm> pareto_form();
    struct ParetoChoice {
        std::string path, label;
        bool on = false;
        // Another unit than the chosen ones' cannot share their bounds.
        bool enabled = true;
    };
    ParetoChoice pareto_choice(const ParetoForm & form, std::size_t k) const;
    void toggle_pareto(const ParetoForm & form, std::size_t k);
    std::string & pareto_bounds() { return session_.pareto_bounds(); }
    bool can_run_pareto() const;
    void run_pareto();

    struct ParetoPlot {
        std::string x_label, y_label;
        std::vector<double> feasible_x, feasible_y;
        std::vector<double> infeasible_x, infeasible_y;
        // In the corner the points leave empty: the lower right, else the
        // upper right.
        bool legend_south_east = false;
        // Every point in the results' order, in the axes' units.
        std::vector<std::array<double, 2>> points;
    };
    std::optional<ParetoPlot> pareto_plot() const;
    void load_pareto_point(std::size_t i);

private:
    SolverSession & session_;
    Document & doc_;
    SolutionsTable solutions_;
    bool busy_ = false;
};

}  // namespace gs::app

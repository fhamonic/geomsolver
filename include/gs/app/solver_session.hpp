#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gs/app/document.hpp"
#include "gs/app/solver_backend.hpp"
#include "gs/engine/model.hpp"

namespace gs::app {

// The Solver tab without its drawing: the settings of the next job, starting
// and stopping jobs, the latest results with the clicked row, and the Pareto
// study's choices. Everything solver-specific goes through SolverBackend;
// without one, every start fails with "solver not connected".
class SolverSession {
public:
    explicit SolverSession(std::unique_ptr<SolverBackend> backend);
    ~SolverSession();
    SolverSession(const SolverSession &) = delete;
    SolverSession & operator=(const SolverSession &) = delete;

    bool connected() const { return backend_ != nullptr; }
    bool running() const { return backend_ && backend_->running(); }
    // Resets settings() to the "solver" object of another instance, once it
    // compiles; edits of the settings are kept until then.
    void follow(const Document & doc);
    // Copies the backend's results when they changed; the selected row
    // follows its design point.
    void refresh();
    // False, with the reason in message(), when the job cannot start. It
    // follows `doc` first: an action may start a job before any view has
    // called follow(), and the job would then take the defaults or the
    // settings of the previous instance.
    bool start(Document & doc, SolveKind kind);
    // Multistart with the current settings, waiting for the end, then loads
    // the best solution (--screenshot --solve). False with a message when no
    // solver is connected or nothing was found.
    bool solve_blocking(Document & doc, std::string * message);
    // Pareto study of `criteria` (names) over `bounds` (text in their display
    // unit, "0, 1, 2"), waiting for the end; take_open_pareto() then asks
    // the Solver tab to open the study (--screenshot --pareto).
    bool pareto_blocking(Document & doc, std::vector<std::string> criteria,
                         std::string bounds, std::string * message);
    // Loads a design point of results().model into the document.
    void load(Document & doc, const std::vector<double> & x);
    void request_stop();
    SolverProgress progress() const;
    // Whether the job moved since the last poll: it runs (its progress
    // moves), it stopped, or it published results. Such a poll bumps
    // generation(). It copies nothing: refresh() does.
    bool poll();
    std::uint64_t generation() const { return generation_; }

    SolverSettings & settings() { return settings_; }
    const std::string & message() const { return message_; }
    const SolverResults & results() const { return results_; }
    // Row of the results table last clicked, -1 for none.
    int selected() const { return selected_; }
    void select(int row) { selected_ = row; }
    std::vector<std::string> & pareto_criteria() { return pareto_criteria_; }
    // Text that start() parses: numbers in the first criterion's display
    // unit, separated by commas, semicolons or spaces.
    std::string & pareto_bounds() { return pareto_bounds_; }
    // True once after pareto_blocking().
    bool take_open_pareto();
    // Whether the results show how long their job and runs took. A scripted
    // run turns it off: the same script must draw the same pixels however
    // loaded the machine is.
    bool show_durations() const { return show_durations_; }
    void set_show_durations(bool on) { show_durations_ = on; }

private:
    bool run_blocking(Document & doc, SolveKind kind, std::string * message);

    std::unique_ptr<SolverBackend> backend_;
    SolverSettings settings_;
    const Instance * settings_source_ = nullptr;
    std::string message_;
    SolverResults results_;
    int selected_ = -1;
    // Names, not indices: an edit that adds or removes a criterion shifts the
    // indices of the next compile.
    std::vector<std::string> pareto_criteria_;
    std::string pareto_bounds_;
    bool open_pareto_ = false;
    bool show_durations_ = true;
    bool polled_running_ = false;
    std::uint64_t polled_serial_ = 0;
    std::uint64_t generation_ = 0;
};

// The criteria a Pareto study can bound: enabled, role max or min.
bool bounded(const CriterionInfo & c);
// Unit of the objective's criterion, "" without an objective.
std::string objective_unit(const Model & m);
// "104.37293 cm": the objective in its criterion's display unit.
std::string objective_text(const Model * m, double v, int digits);
// Why no job starts while the model lacks excluded items.
std::string excluded_message(const Document & doc);

}  // namespace gs::app

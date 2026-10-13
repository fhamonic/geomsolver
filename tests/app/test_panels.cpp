#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/format.hpp"
#include "gs/app/margins.hpp"
#include "gs/app/messages.hpp"
#include "gs/app/solver_presenter.hpp"
#include "gs/app/status_bar.hpp"
#include "gs/app/sweep_bar.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

// Publishes whatever results and progress the test sets.
class ScriptedBackend final : public SolverBackend {
public:
    SolverProgress now;
    bool busy = false;
    SolveRequest last;
    int starts = 0;

    void publish(SolverResults r) {
        r.serial = ++serial_;
        results_ = std::move(r);
    }
    bool start(const SolveRequest & r, std::string *) override {
        last = r;
        ++starts;
        return true;
    }
    void request_stop() override {}
    bool running() const override { return busy; }
    SolverProgress progress() const override { return now; }
    SolverResults results() const override { return results_; }
    std::uint64_t results_serial() const override { return results_.serial; }

private:
    SolverResults results_;
    std::uint64_t serial_ = 0;
};

struct Rig {
    ScriptedBackend * backend = nullptr;
    App app{[this] {
        auto b = std::make_unique<ScriptedBackend>();
        backend = b.get();
        return b;
    }()};
    Document & doc = app.document();
    SolverPresenter solver{app};

    Rig() {
        if(!app.open(tv_file()) || !doc.model())
            FAIL("panels: the instance compiles");
    }
};

std::size_t criterion_index(const Model & m, const char * name) {
    return static_cast<std::size_t>(m.find_criterion(name));
}

SolverSolution solution(std::vector<double> x, double objective, bool feasible,
                        std::size_t criteria) {
    SolverSolution s;
    s.x = std::move(x);
    s.objective = objective;
    s.feasible = feasible;
    s.max_violation = feasible ? 0.0 : 0.25;
    s.worst = feasible ? "" : "couch";
    s.criteria.assign(criteria, 0.5);
    return s;
}

}  // namespace

TEST_CASE("solver panel: blockers, progress and settings") {
    Rig r;
    SolverPresenter & sp = r.solver;
    sp.begin_frame();
    SolverPresenter::Progress p = sp.progress();
    CHECK((!sp.blocker() && p.fraction == 0.0f && p.overlay == "idle" &&
           p.line.empty() && p.message.empty()));
    r.backend->now = {3, 8, 1.04, "multistart", "", 1.23};
    r.backend->busy = true;
    sp.begin_frame();
    p = sp.progress();
    CHECK_MESSAGE(
        (p.fraction == 0.375f && p.overlay == "3/8 runs" &&
         p.line.starts_with("multistart  |  best feasible 104") &&
         p.line.ends_with(" cm") && p.message == "1.2 s"),
        "the job's runs, phase and best objective in the objective's unit: "
            << p.line);
    r.backend->now = {};
    CHECK_MESSAGE((sp.progress().overlay == "running"),
                  "a job without a run count yet");
    r.backend->busy = false;
    CHECK_MESSAGE((sp.progress().overlay == "running"),
                  "the job as it was when the frame began");
    sp.begin_frame();
    CHECK((sp.progress().overlay == "idle"));
    {
        App none(nullptr);
        SolverPresenter off(none);
        off.begin_frame();
        CHECK((!off.connected() && off.progress().overlay == "not connected" &&
               off.progress().line.empty()));
    }

    r.doc.edit("constraints[0].expr", "clearance(tv, couh) >= clr");
    std::optional<SolverPresenter::Blocker> b = sp.blocker();
    CHECK_MESSAGE(
        (b && !b->brief &&
         b->text.starts_with("Solve, Polish and the Pareto study wait for 1 "
                             "item(s) excluded from the model: ")),
        "an excluded item blocks every job, said in a paragraph");
    r.doc.undo();
    r.doc.edit("design.c.domain", "boxx(-W/2, -bracket_depth, W/2, 0)");
    b = sp.blocker();
    CHECK((b && b->brief && b->text == "fix the errors first"));
    r.doc.undo();

    SolverSettings & s = sp.settings();
    sp.set_count(&SolverSettings::starts, 0);
    sp.set_count(&SolverSettings::threads, 5000);
    sp.set_count(&SolverSettings::verify_samples, 1);
    sp.set_count(&SolverSettings::maxeval, 1234);
    CHECK_MESSAGE((s.starts == 1 && s.threads == 1024 &&
                   s.verify_samples == 2 && s.maxeval == 1234),
                  "counts are clamped to their range");
    s.note = "MMA is no longer offered: SLSQP instead";
    CHECK((sp.algorithm_note() ==
           "solver.algorithm: MMA is no longer offered: SLSQP instead"));
    sp.set_algorithm("COBYLA");
    CHECK((s.algorithm == "COBYLA" && sp.algorithm_note().empty()));
    sp.store_settings();
    const Json * stored = r.doc.instance()->get("solver.starts");
    CHECK_MESSAGE((stored && *stored == 1 &&
                   *r.doc.instance()->get("solver.algorithm") == "COBYLA" &&
                   r.doc.undo_label() == "store solver settings"),
                  "the settings go into the instance's solver object");
    r.doc.undo();
    sp.reset_settings();
    CHECK_MESSAGE((s.starts == 64 && s.algorithm == "SLSQP"),
                  "the instance's settings again");
}

TEST_CASE("solver panel: the solutions table") {
    Rig r;
    SolverPresenter & sp = r.solver;
    SolutionsTable & t = sp.solutions();
    sp.begin_frame();
    CHECK_FALSE(t.begin());

    const Model & m = *r.doc.model();
    const std::size_t nc = m.criteria().size();
    std::vector<double> x0 = x_of(r.doc);
    std::vector<double> x1 = x0;
    x1[0] = x0[0] * 0.5;
    SolverResults res;
    res.model = r.doc.model();
    res.wall_seconds = 0.46;
    SolverSolution best = solution(x0, 1.04, true, nc);
    best.hits = 3;
    best.variants = {{x0, 2, ""}, {x1, 1, "A differs"}};
    SolverSolution other = solution(x1, 1.2, false, 1);
    other.status = "maxeval reached";
    res.solutions = {best, other};
    r.backend->publish(res);
    sp.begin_frame();
    REQUIRE(t.begin());
    CHECK((t.summary() == "multistart, 2 distinct solution(s), 0.5 s" &&
           !t.stale() && t.rows() == 2));
    const std::vector<std::string> & h = t.headers();
    CHECK_MESSAGE(
        (h.size() == 6 + nc && h[0] == "#" && h[1] == "objective [cm]" &&
         h[2] == "feasible" && h[3] == "max viol." && h[4] == "worst" &&
         h[5] == "hits" && h[6] == "protrusion [cm]"),
        "the first column, then the objective in its unit, then one column "
        "per criterion");

    TableRow row;
    t.row(0, row);
    CHECK_MESSAGE(
        (row.label == "1" && !row.selected && row.tip == "click to load" &&
         row.cells.size() == 5 + nc && row.cells[0].text == "104.00000" &&
         row.cells[1].text == "yes" && row.cells[1].tone == Tone::Ok &&
         row.cells[4].kind == Cell::Kind::Choices &&
         row.cells[4].text == "3 (2 variants)" && !row.cells[4].tip.empty() &&
         row.cells[5].text ==
             format_number(to_display(0.5, m.criteria()[0].unit), 7)),
        "a row: number, objective, verdict, the variants' button: "
            << row.cells[0].text);
    std::vector<std::string> choices;
    t.choices(0, 4, choices);
    CHECK((choices ==
           std::vector<std::string>{"variant 1, 2 hits: the row's design",
                                    "variant 2, 1 hits: A differs"}));
    t.row(1, row);
    CHECK_MESSAGE(
        (row.label == "2" && row.tip == "click to load\n\nmaxeval reached" &&
         row.cells[1].text == "no" && row.cells[1].tone == Tone::Bad &&
         row.cells[3].text == "couch" &&
         row.cells[4].kind == Cell::Kind::Text && row.cells[4].text == "1" &&
         row.cells[5].kind == Cell::Kind::Text &&
         row.cells[6].kind == Cell::Kind::Empty),
        "a criterion the solution lacks has an empty cell");

    t.activate(1);
    t.row(1, row);
    CHECK_MESSAGE(
        (row.selected && r.app.solver().selected() == 1 && x_of(r.doc) != x0 &&
         sp.message().starts_with("loaded a solution")),
        "a click on a row selects and loads it");
    t.choose(0, 4, 0);
    CHECK_MESSAGE((r.app.solver().selected() == 0 && x_of(r.doc) == x0),
                  "picking a variant loads it");

    r.doc.edit("params.clr", 0.03);
    REQUIRE(t.begin());
    CHECK_MESSAGE(t.stale(),
                  "a recompile makes the results an earlier "
                  "compile's");
}

TEST_CASE("solver panel: durations, hidden in a scripted run") {
    Rig r;
    SolutionsTable & t = r.solver.solutions();
    SolverResults res;
    res.model = r.doc.model();
    res.wall_seconds = 0.46;
    SolverSolution run = solution(x_of(r.doc), 1.04, true, 1);
    run.status = "phase 2: nlopt code 4";
    run.seconds = 0.0123;
    SolverSolution bare = solution(x_of(r.doc), 1.1, true, 1);
    bare.seconds = 0.5;
    res.solutions = {run, bare};
    r.backend->publish(res);
    r.solver.begin_frame();
    REQUIRE(t.begin());
    TableRow row, other;
    t.row(0, row);
    t.row(1, other);
    CHECK((t.summary() == "multistart, 2 distinct solution(s), 0.5 s" &&
           row.tip == "click to load\n\nphase 2: nlopt code 4\n0.012 s" &&
           other.tip == "click to load\n\n0.500 s"));

    r.backend->now = {2, 2, 1.04, "done", "2/4 points", 0.46};
    r.solver.begin_frame();
    CHECK((r.solver.progress().message == "2/4 points, 0.5 s"));

    r.app.solver().set_show_durations(false);
    CHECK((r.solver.progress().message == "2/4 points, \u2014"));
    t.row(0, row);
    CHECK_MESSAGE(
        (t.summary() == "multistart, 2 distinct solution(s), \u2014" &&
         row.tip == "click to load\n\nphase 2: nlopt code 4\n\u2014"),
        "neither the job's time nor a run's: " << t.summary());
}

TEST_CASE("solver panel: the Pareto study") {
    Rig r;
    SolverPresenter & sp = r.solver;
    const Model & m = *r.doc.model();
    r.app.solver().pareto_criteria() = {"ghost"};
    std::optional<SolverPresenter::ParetoForm> form = sp.pareto_form();
    REQUIRE(form);
    CHECK_MESSAGE(
        (r.app.solver().pareto_criteria().empty() && form->preview.empty() &&
         form->unit.empty() && form->hint == "bounds in SI, e.g. 0, 1, 2, 4"),
        "a criterion the model no longer bounds is dropped");
    std::size_t couch = form->bounded.size(), link = couch;
    for(std::size_t k = 0; k < form->bounded.size(); ++k) {
        const std::string & name =
            m.criteria()[static_cast<std::size_t>(form->bounded[k])].name;
        if(name == "view_couch") couch = k;
        if(name == "link_1") link = k;
    }
    REQUIRE((couch < form->bounded.size() && link < form->bounded.size()));
    CHECK((sp.pareto_choice(*form, link).enabled && !sp.can_run_pareto()));
    sp.toggle_pareto(*form, couch);
    form = sp.pareto_form();
    const SolverPresenter::ParetoChoice chosen = sp.pareto_choice(*form, couch);
    const SolverPresenter::ParetoChoice other = sp.pareto_choice(*form, link);
    CHECK_MESSAGE(
        (form->preview == "view_couch" && form->unit == "deg" &&
         form->hint == "bounds in deg, e.g. 0, 1, 2, 4" && chosen.on &&
         chosen.enabled && chosen.label == "view_couch [deg]" && !other.on &&
         !other.enabled && other.label == "link_1 [cm]"),
        "the bounds are in the chosen criteria's unit: another unit is off");

    sp.pareto_bounds() = "1, 2";
    REQUIRE(sp.can_run_pareto());
    sp.run_pareto();
    CHECK_MESSAGE(
        (r.backend->starts == 1 && r.backend->last.kind == SolveKind::Pareto &&
         r.backend->last.pareto_bounds.size() == 2 &&
         near(r.backend->last.pareto_bounds[1], from_display(2.0, "deg"),
              1e-15)),
        "Run starts a Pareto job over the typed bounds");

    SolverResults res;
    res.model = r.doc.model();
    res.kind = SolveKind::Pareto;
    res.pareto_criteria = {
        static_cast<int>(criterion_index(m, "view_couch")),
        static_cast<int>(criterion_index(m, "view_kitchen"))};
    std::vector<double> x = x_of(r.doc);
    res.pareto = {{from_display(1.0, "deg"),
                   solution(x, 1.10, true, m.criteria().size())},
                  {from_display(2.0, "deg"),
                   solution(x, 1.05, false, m.criteria().size())}};
    r.backend->publish(res);
    sp.begin_frame();
    SolutionsTable & t = sp.solutions();
    REQUIRE(t.begin());
    TableRow row;
    t.row(0, row);
    CHECK((t.summary() ==
               "Pareto study of view_couch, view_kitchen, 2 point(s), 0.0 s" &&
           t.headers()[0] == "bound [deg]" && row.label == "1"));
    const std::optional<SolverPresenter::ParetoPlot> plot = sp.pareto_plot();
    REQUIRE(plot);
    CHECK_MESSAGE(
        (plot->x_label == "view_couch, view_kitchen bound [deg]" &&
         plot->y_label == "objective [cm]" && plot->feasible_x.size() == 1 &&
         plot->infeasible_x.size() == 1 && plot->points.size() == 2 &&
         near(plot->points[1][0], 2.0, 1e-12) &&
         near(plot->points[1][1], 105.0, 1e-9) &&
         plot->legend_south_east == (m.objective_sign() < 0)),
        "the points in the axes' units, feasible apart");
    sp.load_pareto_point(1);
    CHECK(sp.message().starts_with("loaded a solution"));
}

TEST_CASE("solver panel: a Pareto study never bounds a disabled criterion") {
    Rig r;
    SolverPresenter & sp = r.solver;
    const std::size_t couch = criterion_index(*r.doc.model(), "view_couch");
    r.app.solver().pareto_criteria() = {"view_couch"};
    REQUIRE(r.doc.edit(std::format("criteria[{}].enabled", couch), false));
    const std::optional<SolverPresenter::ParetoForm> form = sp.pareto_form();
    REQUIRE(form);
    CHECK(std::ranges::find(form->bounded, static_cast<int>(couch)) ==
          form->bounded.end());
    CHECK_MESSAGE(r.app.solver().pareto_criteria().empty(),
                  "a chosen criterion is dropped once disabled");
    r.app.solver().pareto_criteria() = {"view_couch"};
    sp.pareto_bounds() = "1, 2";
    sp.run_pareto();
    CHECK((r.backend->starts == 0 &&
           sp.message() == "'view_couch' is not a bounded criterion"));
}

TEST_CASE("margins: one plot per sweep") {
    App app(nullptr);
    if(!app.open(tv_file())) FAIL("margins: the instance compiles");
    Document & doc = app.document();
    MarginsPresenter margins(doc);
    CHECK_FALSE(MarginsPresenter::available(Scene{}));
    const Scene & s = app.scene(false);
    REQUIRE(MarginsPresenter::available(s));
    CHECK((margins.caption(s) ==
           "margin = -violation, natural units; 2001 samples; drag the "
           "vertical line to move the sweep"));

    MarginsPresenter::Plots plots = margins.plots(s);
    REQUIRE(plots.plots.size() == 1);
    const MarginsPresenter::Plot & tau = plots.plots[0];
    bool all_tau = !tau.curves.empty();
    for(const MarginsPresenter::Curve & c : tau.curves)
        all_tau &= c.y.size() == tau.xs.size();
    CHECK_MESSAGE((plots.refit && tau.name == "tau" && tau.x_label == "tau" &&
                   tau.y_label == "margin" && !tau.fixed &&
                   tau.xs.size() == 2001 && all_tau && tau.x_min == 0.0 &&
                   tau.x_max == 1.0 && tau.y_min <= -0.02 * 1.15 &&
                   tau.y_max > 0.0 && tau.marker == doc.sweep_t()[0]),
                  "tau's rows on the fine grid, margins up");
    CHECK_MESSAGE(!margins.plots(s).refit, "fitted once");
    margins.request_fit();
    CHECK(margins.plots(s).refit);

    margins.normalise() = true;
    plots = margins.plots(s);
    bool within = true;
    for(const MarginsPresenter::Curve & c : plots.plots[0].curves)
        for(const double y : c.y)
            within &= !std::isfinite(y) || std::fabs(y) <= 1.0;
    CHECK_MESSAGE((within && plots.plots[0].y_label == "margin / max|margin|" &&
                   plots.plots[0].y_max == 1.05),
                  "normalised: each curve within [-1, 1]");

    margins.set_marker(plots.plots[0], 0.25);
    CHECK(doc.sweep_t()[0] == 0.25);

    doc.edit("sweeps.tau.samples", 17);
    doc.edit("sweeps.tau.sampling", "fixed");
    const Scene & fixed = app.scene(false);
    plots = margins.plots(fixed);
    CHECK_MESSAGE((plots.refit && plots.plots[0].fixed &&
                   plots.plots[0].x_label == "tau (fixed: its 17 samples)" &&
                   plots.plots[0].xs.size() == 17),
                  "a new model refits; a fixed sweep plots its samples");
}

TEST_CASE("messages: the problems, then the log") {
    App app(nullptr);
    if(!app.open(tv_file())) FAIL("messages: the instance compiles");
    Document & doc = app.document();
    const MessagesPresenter & messages = app.panels().messages;
    CHECK((messages.view().problems.empty() &&
           messages.tab_label() == "Messages"));

    doc.edit("constraints[0].expr", "clearance(tv, couh) >= clr");
    MessagesPresenter::View v = messages.view();
    CHECK_MESSAGE(
        (v.problems.size() == 2 && v.problems[0].tone == Tone::Bad &&
         v.problems[0].text.starts_with("error  constraints[0].expr") &&
         v.problems[1].tone == Tone::Bad &&
         v.problems[1].text.starts_with("excluded  constraint couch: ") &&
         messages.tab_label() == "Messages (1 error)"),
        "an error, then the item it excludes: " << v.problems[0].text);
    doc.edit("constraints[1].expr", "clearance(tv, fridg) >= clr");
    CHECK((messages.tab_label() == "Messages (2 errors)"));

    doc.log(LogEntry::Level::Warning, "careful");
    doc.log(LogEntry::Level::Error, "broken");
    doc.log(LogEntry::Level::Info, "fine");
    v = messages.view();
    const std::size_t n = v.log.size();
    REQUIRE(n >= 3);
    CHECK((v.log[n - 3].tone == Tone::Warn && v.log[n - 3].text == "careful" &&
           v.log[n - 2].tone == Tone::Bad && v.log[n - 1].tone == Tone::Dim));
}

TEST_CASE("messages: a problem is a link to its entry's field") {
    App app(nullptr);
    if(!app.open(tv_file())) FAIL("messages: the instance compiles");
    app.sync();
    Document & doc = app.document();
    MessagesPresenter & messages = app.panels().messages;
    using K = SelKey::Kind;
    doc.edit("constraints[0].expr", "clearance(tv, couh) >= clr");
    doc.log(LogEntry::Level::Info, "fine");
    MessagesPresenter::View v = messages.view();
    REQUIRE(v.problems.size() == 2);
    const SelKey couch{K::Constraint, "couch"};
    CHECK_MESSAGE(
        (v.problems[0].key == couch &&
         v.problems[0].path == "constraints[0].expr" &&
         v.problems[0].tip ==
             "Click to show couch in its list and in Properties" &&
         v.problems[1].path == "constraints[0]" && v.log.back().path.empty() &&
         !messages.activate(2 + v.log.size())),
        "an error and the item it excludes link to the constraint; the log "
        "does not");
    app.set_panel_open(PanelId::Constraints, false);
    CHECK(messages.activate(0));
    CHECK_MESSAGE(
        (app.selection().keys() == std::vector<SelKey>{couch} &&
         app.selection().to_reveal() == couch &&
         app.panel_open(PanelId::Constraints) &&
         app.take_panel_focus() == PanelId::Constraints &&
         app.take_panel_focus() == PanelId::Properties &&
         app.formula().is_open() &&
         app.formula().title() == "Edit constraint couch" &&
         !app.take_field_focus("constraints[0].note")),
        "a click selects the constraint, shows it in Constraints and opens "
        "the formula pop-up on it");
    app.formula().cancel();
    CHECK(messages.activate(1));
    CHECK_MESSAGE((!app.formula().is_open() &&
                   !app.take_field_focus("constraints[0].note") &&
                   app.take_panel_focus() == PanelId::Constraints),
                  "the excluded item: shown, its formula not opened");
    doc.undo();

    doc.edit("design.A.unit", "furlong");
    v = messages.view();
    REQUIRE(!v.problems.empty());
    CHECK(v.problems[0].path == "design.A.unit");
    CHECK(messages.activate(0));
    CHECK_MESSAGE(
        (app.selection().keys() == std::vector<SelKey>{{K::Design, "A"}} &&
         app.take_field_focus("design.A.unit")),
        "a key no editor of the sheet owns: its raw JSON field");
    doc.undo();
    doc.edit("sweeps.tau.sampling", "sometimes");
    CHECK(messages.activate(0));
    CHECK_MESSAGE(app.take_field_focus("sweeps.tau.min"),
                  "a key a choice edits: the sheet's first field");
    doc.undo();

    doc.edit("include[0]", "nowhere.json");
    v = messages.view();
    const auto inc = std::ranges::find(v.problems, std::string("include[0]"),
                                       &MessagesPresenter::Line::path);
    REQUIRE(inc != v.problems.end());
    while(app.take_panel_focus()) {
    }
    CHECK(
        messages.activate(static_cast<std::size_t>(inc - v.problems.begin())));
    CHECK_MESSAGE(
        (!inc->key &&
         inc->tip == "Click to edit include[0] in the Document panel" &&
         app.take_panel_focus() == PanelId::Document &&
         app.take_field_focus("include[0]")),
        "an include: the Document panel and its field");
}

TEST_CASE("status bar: compile status, selection and solver") {
    using Segment = StatusBarPresenter::Segment;
    auto texts = [](const std::vector<Segment> & segments) {
        std::vector<std::string> out;
        for(const Segment & s : segments) out.push_back(s.status.text);
        return out;
    };
    {
        App empty(nullptr);
        const std::vector<Segment> none = empty.status_bar().segments();
        CHECK((none.size() == 1 && none[0].status.text == "no instance" &&
               none[0].status.tone == Tone::Dim && !none[0].link));
    }
    Rig rig;
    App & app = rig.app;
    StatusBarPresenter & bar = app.status_bar();
    const Model & m = *rig.doc.model();
    std::vector<Segment> s = bar.segments();
    CHECK_MESSAGE(
        (texts(s) == std::vector<std::string>{std::format(
                         "compiled: {} coordinates, {} row groups", m.n(),
                         m.groups().size())} &&
         s[0].status.tone == Tone::Ok && !s[0].link),
        "a clean compile, nothing selected, no job: " << s[0].status.text);

    Selection & sel = app.selection();
    sel.set({SelKey::Kind::Design, "A"});
    sel.add({SelKey::Kind::Constraint, "couch"});
    s = bar.segments();
    CHECK((s.size() == 2 && s[1].status.text == "2 selected: A, couch" &&
           s[1].status.tip.empty() && !s[1].link));
    sel.add({SelKey::Kind::Display, "", 2});
    sel.add({SelKey::Kind::Design, "B"});
    s = bar.segments();
    CHECK_MESSAGE(
        (s[1].status.text == "4 selected: A, couch, display[2], ..." &&
         s[1].status.tip == "A, couch, display[2], B"),
        "past three names the tooltip lists them all");
    sel.clear();

    rig.doc.edit("criteria[0].weigth", 1);
    rig.doc.edit("constraints[0].expr", "clearance(tv, couh) >= clr");
    s = bar.segments();
    CHECK((texts(s) ==
               std::vector<std::string>{
                   "1 error(s): 1 item(s) excluded from the model",
                   "1 warning(s)"} &&
           s[0].status.tone == Tone::Bad && s[0].link == PanelId::Messages &&
           s[1].status.tone == Tone::Warn && s[1].link == PanelId::Messages));
    app.set_panel_open(PanelId::Messages, false);
    bar.activate(0);
    CHECK_MESSAGE((app.panel_open(PanelId::Messages) &&
                   app.take_panel_focus() == PanelId::Messages),
                  "a click on the errors opens Messages");
    bar.activate(9);
    CHECK(!app.take_panel_focus());

    rig.backend->busy = true;
    rig.backend->now = {.done = 3, .total = 8};
    s = bar.segments();
    CHECK((s.back().status.text == "solving: 3/8 runs" &&
           s.back().status.tone == Tone::Active &&
           s.back().link == PanelId::Solver));
    rig.backend->now = {};
    CHECK(bar.segments().back().status.text == "solving");
    // The session's destructor waits for the job to stop.
    rig.backend->busy = false;
}

TEST_CASE("sweep bar: rows and players") {
    App app(nullptr);
    SweepBarPresenter bar(app);
    CHECK(bar.begin() == 0);
    if(!app.open(tv_file())) FAIL("sweep bar: the instance compiles");
    Document & doc = app.document();
    REQUIRE(bar.begin() == 1);
    CHECK(
        (app.players().size() == 1 && SweepBarPresenter::modes().size() == 3));
    SweepBarPresenter::Row row = bar.row(0);
    CHECK((row.name == "tau" && !row.playing && row.lo == 0.0 &&
           row.hi == 1.0 && row.period == 4.0f && row.mode == 0));

    bar.set_value(0, 0.25);
    CHECK((doc.sweep_t()[0] == 0.25 && bar.row(0).value == 0.25));
    bar.set_period(0, 2.5f);
    bar.set_mode(0, 2);
    CHECK((app.players()[0].period == 2.5 &&
           app.players()[0].mode == SweepPlayer::Mode::Once));
    bar.toggle_play(0);
    app.tick(10.0);
    CHECK((doc.sweep_t()[0] == 1.0 && !bar.row(0).playing));
    bar.toggle_play(0);
    CHECK_MESSAGE((bar.row(0).playing && doc.sweep_t()[0] == 0.0),
                  "playing once from the end starts over");
    bar.toggle_play(0);
    CHECK((!bar.row(0).playing && bar.row(0).play == "Play"));
    bar.toggle_play(0);
    CHECK_MESSAGE(bar.row(0).play == "Pause",
                  "the player's button says what a click does");
}

TEST_CASE("texts: the views show what the presenters say") {
    App app(nullptr);
    PathDialog & path = app.path_dialog();
    path.start(PathDialog::Kind::Open);
    CHECK((std::string(path.title()) == "Open instance" &&
           std::string(path.submit_label()) == "Open"));
    path.start(PathDialog::Kind::SaveAs);
    CHECK((std::string(path.title()) == "Save instance as" &&
           std::string(path.submit_label()) == "Save"));
    SolverPresenter solver(app);
    CHECK_MESSAGE(solver.tab_label() == "Solver (not connected)",
                  "no solver service: the tab says so");
    App with_backend(std::make_unique<FakeBackend>());
    CHECK(SolverPresenter(with_backend).tab_label() == "Solver");
    for(const char * text : {PanelHost::kNoInstance,
                             RenameEditor::kTip,
                             FieldDrafts::kRawLabel,
                             FieldDrafts::kRemoveTip,
                             FieldDrafts::kRevert,
                             FieldDrafts::kRevertTip,
                             SweepBarPresenter::kNoSweeps,
                             SweepBarPresenter::kPeriodTip,
                             MarginsPresenter::kNoModel,
                             MarginsPresenter::kNoCurves,
                             PathDialog::kPrompt,
                             UnsavedChanges::kQuestion,
                             DeleteRequest::kTitle,
                             ControlsHelp::kTitle,
                             FormulaDraft::kCancel,
                             FormulaDraft::kFormula,
                             FormulaDraft::kKeysTip,
                             SolverPresenter::kNotConnectedNote,
                             SolverPresenter::kStaleResults,
                             SolverPresenter::kParetoHint})
        CHECK(std::string(text).size() > 2);
    CHECK(kCanvasLabel == "Canvas");
}

}  // namespace gs::test

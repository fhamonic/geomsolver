#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gs/engine/diagnostics.hpp"
#include "gs/engine/instance.hpp"
#include "gs/engine/model.hpp"
#include "gs/engine/rows.hpp"

// The sketch solver (plan 3): it moves the free parameters of a sketch, the
// columns of q of a model compiled in the Free binding, until its Solve rows
// hold, and tells which rows depend on others and how many degrees of
// freedom are left. Steps, tolerances and the rank are taken in scaled
// columns on rows weighted into metres (RowWeight). A scaled column is
// q / SketchCoord::scale, except a body's free pose columns, which are
// scaled together so that a step's length is the root mean square move of
// the body's points, as they lie in its frame where the commit or the drag
// starts: least change, the rank and the free directions then do not depend
// on where the body's origin is drawn. A step that moves all three moves
// the body rigidly about the centroid of its points. Not so while part of
// a pose is held: a body with no fixed member has its origin held as a
// gauge (Settled::held), and its angle alone is weighed as a turn about
// the origin, wherever that is drawn.

namespace gs::sketch {

namespace detail {
struct SystemData;
}

struct Options {
    // Converged when every weighted row is within tol, in metres.
    double tol = 1e-10;
    // The ordered QR's band, on a row's remainder relative to its own norm:
    // at most `dependent`, it depends on the earlier rows; up to
    // `near_dependent`, it is kept and the configuration is near singular.
    double dependent = 1e-8, near_dependent = 1e-5;
    // A row whose weighted gradient in scaled columns is shorter is
    // degenerate: Malformed, never dependent, since a remainder relative to
    // a vanishing norm means nothing. A row that holds and is as flat 1e-3
    // L_c away is constant, and satisfied: a world length between two
    // constants of one body.
    double degenerate = 1e-8;
    // The band where the solve stopped, stationary off the set: no solve
    // reaches an inconsistent set's least-squares point exactly, so the
    // rows that conflict are dependent to within this, not `dependent`.
    double conflict = 1e-4;
    // Components of at most this many columns take the Gauss-Newton step
    // from the ordered QR, and settle() takes their rank; larger ones take
    // the damped LDLT step, and only the diagnosis takes their rank.
    int sync_columns = 150;
    // Components of more columns are ranked by Eigen's SparseQR, which
    // orders the rows for fill: the row it names dependent is one of its
    // group, not always the latest, and it has no near-dependent band.
    int dense_columns = 1500;
    // Trial steps per component and solve, plus 4 per column of the
    // component.
    int max_iterations = 100;
    // What settle() does when the geometry constraints conflict (open
    // question Q4).
    enum class Conflicts : std::uint8_t {
        // The latest constraint of the conflict group is left out and the
        // rest solved again, at most 3 solves in all, so that dragging
        // still works.
        LeaveOut,
        // Nothing moves until the conflict is fixed, as in FreeCAD.
        Refuse,
    };
    Conflicts conflicts = Conflicts::LeaveOut;
};

// A connected component of the graph of the kept Solve rows and the columns
// they read, the held columns left out: solved and ranked alone. A body's
// free pose columns are all in the component of any one of them, read or
// not: "in no row" never names part of a pose that a row moves.
struct Component {
    std::vector<int> columns;  // ascending
    std::vector<RowRef> rows;  // in document order
    // L_c: the diagonal of the bounding box of the world values, where the
    // commit starts, of the elements its columns move and of those its rows
    // name, fixed ones included; at least 1e-3 m. Scales its angle rows,
    // tolerances, continuation steps and second point.
    double length = 1e-3;
};

enum class Status {
    // No free parameter and no geometry constraint; also a settle() whose
    // compile failed, with a null Settled::model.
    Empty,
    // The geometry constraints have no common solution: Settled::conflicting
    // holds the constraints left out when the rest was solved, else
    // nothing moved.
    Conflicting,
    Malformed,  // solved without the Malformed constraints
    Redundant,  // solved; some rows depend on earlier ones
    NotConverged,
    // Solved, but settle() did not rank every component (Rank::ranked) and
    // found no dependent row in those it ranked: the diagnosis tells
    // Redundant, UnderConstrained or Full.
    Solved,
    UnderConstrained,  // solved, with free degrees of freedom left
    Full,              // solved, no degree of freedom left
};

// A row that depends on earlier rows of its component: `group` holds the
// earlier rows it is a combination of, in document order.
struct Dependency {
    RowRef row;
    std::vector<RowRef> group;
    // Its residual is within Options::tol: a redundancy, harmless.
    bool consistent = true;
    // Found dependent at a second point as well (plan 3.5), so not an
    // accidental configuration. settle()'s rank never confirms.
    bool confirmed = false;
};

// The ordered QR of each component at a polished converged point (plan
// 3.5), and the degrees of freedom it leaves (plan 3.6): for each component
// its columns less its rank, plus the free columns in no row, plus the held
// gauge directions.
struct Rank {
    // Per component: false for one settle() left to the diagnosis (above
    // Options::sync_columns, or its polish fell short), whose `independent`
    // is empty and whose dependent rows are unknown, not absent.
    std::vector<bool> ranked;
    std::vector<std::vector<RowRef>> independent;  // per component, in order
    std::vector<Dependency> dependent;
    std::vector<RowRef> near_dependent;
    std::optional<int> dof;  // when every component is ranked
};

struct MalformedConstraint {
    enum class Reason {
        Segment,  // a segment whose direction it reads has zero length
        Radius,   // a radius it reads, or radius(k) == r sets, is not positive
        Span,     // a span it reads or sets is outside (0, 2 pi)
        Inner,    // an internal tangency whose first circle is not larger
        NotFinite,  // a row is NaN or infinite
        // A row reads no free parameter and is off its value: nothing can
        // solve it.
        NoParameter,
        Degenerate,  // a row's gradient vanishes
    };
    int constraint = -1;  // index into Model::constraints()
    Reason reason = Reason::Degenerate;
    std::string why;  // "a segment it reads has zero length"
};

struct SettleOptions {
    Options solver;
    // Solve, but write nothing into the instance.
    bool dry_run = false;
    // CompileOptions::isolate_items for the sketch-mode compile: the
    // Document sets it, as for its design-mode compile, or one broken
    // criterion leaves it with no sketch model. Sketch errors stay fatal.
    bool isolate_items = false;
    // The sketch-mode model of the previous commit (a previous
    // Settled::model), or null. A row whose value at the drawn values has
    // moved since then by more than 0.05 L_c or, for a length dimension, 5 %
    // of what it measures there (10 degrees for an angle row), a typed
    // dimension, is reached by continuation: up to 10 solves, each from the
    // last, with the dimension stepped from its old value to its new one.
    // Rows are matched by constraint name; a model with other columns gives
    // none, and so does a row the drawn values already hold (a flipped
    // tangent_at whose segment was mirrored onto it in the same edit).
    const Model * previous = nullptr;
    // When not empty, a settle of the components that hold a row of one of
    // these constraints (by ConstraintInfo::path: "constraints[7]") alone:
    // only they are solved and ranked, and `components`, `conflicting`,
    // `conflict_groups`, `residual`, `converged` and `rank` speak of them
    // only (Rank::ranked per component, Rank::dof unset). `malformed` may
    // still name a constraint elsewhere that fails the checks made before
    // any solve (a row not finite, or off its value with no parameter). The
    // other columns keep their drawn values in `q`, and a write-back writes
    // nothing of them: what a rank test of new constraints (try_constrain)
    // reads, at a cost that does not grow with the components it leaves
    // alone.
    std::vector<std::string> only;
};

// What settle() did: a pure function of the instance and the options.
struct Settled {
    Status status = Status::Empty;
    // Every component converged without the constraints left out: q
    // satisfies every geometry constraint but those.
    bool converged = false;
    // The options it was solved under: the report judges "not solved"
    // against their tol.
    Options options;
    // The sketch-mode compile (Free binding, row plans). Null when it
    // failed: then only `diagnostics` and `excluded` are set, and the
    // status reads Empty. Its free values are inputs, so it stays valid
    // after the write-back, but its drawn values (initial_q(),
    // SketchCoord::value, an Evaluator's default q) are those before the
    // solve: evaluate it at `q`, or it shows the drawing before the commit.
    std::shared_ptr<const Model> model;
    // The compile's; with isolate_items, errors may sit next to a model.
    std::vector<Diagnostic> diagnostics;
    // The items isolate_items left out. Constraint indices below are the
    // model's, which skip them: match them to the instance by path.
    std::vector<ExcludedItem> excluded;
    // The settled columns; the drawn ones when the solve failed.
    std::vector<double> q;
    // JSON paths of the values written ("sketch.elements.P2.value"); empty
    // for a dry run and a failed solve.
    std::vector<std::string> written;
    // Constraints left out of the solve, in document order.
    std::vector<MalformedConstraint> malformed;
    // The conflicting constraints left out, ascending: the latest of each
    // conflict group, when the rest converged. Empty when nothing moved.
    std::vector<int> conflicting;
    // Each conflict found, one per inconsistent solve: the constraints
    // whose rows depend on each other off their set, ascending.
    std::vector<std::vector<int>> conflict_groups;
    // The columns held in the solve (ascending): the free gauge parts of the
    // pose of each body that is not anchored (plan 1.3). Each counts as one
    // degree of freedom.
    std::vector<int> held;
    std::vector<Component> components;
    // When the solve converged, unless no component was ranked: settle()
    // ranks those of at most Options::sync_columns columns whose polish
    // reached its target. With no component, the DOFs are the free columns.
    std::optional<Rank> rank;
    // The largest weighted geometry row at the values the instance holds
    // after the call, in metres: the drawn values unless written. The
    // constraints left out do not count.
    double residual = 0.0;
};

// Compiles `instance` in sketch mode, solves its geometry constraints from
// the drawn values (least change), and writes the moved values back per
// element: a point's [x, y], a circle's radius, an arc's radius, start and
// end, a pose's origin and angle. Angles keep the turn they were drawn in,
// never wrapped into (-pi, pi]. An unmoved number keeps its written form
// ("320cm"). A conflict is handled as Options::conflicts says; when a
// component stays inconsistent or does not converge, nothing is written.
Settled settle(Instance & instance, const SettleOptions & options = {});

// A value settle() writes: the JSON path of an element's or a pose's
// value, and the value.
struct WrittenValue {
    std::string path;
    nlohmann::ordered_json value;
};
// What settle() writes when the columns of `sketch_model` move from q0, the
// values `instance` holds, to q, in its forms: one value per element or
// pose part that moved, an unmoved coordinate of a point kept as written,
// an angle as solved. Empty when nothing moved. A drag's
// release applies them as one edit. q0 must be what `instance` holds: an
// unmoved coordinate is copied from it. Throws std::invalid_argument when
// q0 or q has another size than the columns.
std::vector<WrittenValue> written_values(const Instance & instance,
                                         const Model & sketch_model,
                                         std::span<const double> q0,
                                         std::span<const double> q);

// What an element's world position may still do (plan 3.6), read on the
// null space of its component's rows in scaled columns, held columns
// excluded.
struct FreeBlock {
    int element = -1;  // index into Model::sketch()
    // It reads no column that is free: nothing moves it.
    bool fixed = false;
    // Its free DOFs: the eigenvalues above 1e-9 of J T P T^T J^T, J the
    // derivative of its world points (a point; a segment's ends; a box's
    // corners; a polyline's or polygon's vertices; a circle's centre; an
    // arc's centre, start and end, never its polyline's vertices) in the
    // columns of q, T the move of q per scaled column, P the null-space
    // projector in scaled columns; plus 1 for a circle's free radius, which
    // moves no point of it. 0 for an element its constraints determine.
    int count = 0;
    // The arrows, largest first: per free direction, how each of its
    // points moves, (dx0, dy0, dx1, dy1, ...), of unit length.
    std::vector<std::vector<double>> directions;
    bool radius = false;  // a circle whose radius is free
};

// The full diagnosis of a sketch at q, for any size.
struct Diagnosis {
    std::vector<Component> components;
    // The free gauge parts held, as Settled::held: each counts as a DOF and
    // marks its body as not anchored. Never the caller's held columns.
    std::vector<int> held;
    // Its dependencies are confirmed at a second point (plan 3.5). The DOFs
    // are first order at q: a singular row counts as dependent there.
    Rank rank;
    // Rows dependent at q but not at the second point, where every driving
    // dimension is perturbed and the drawing moved along its free
    // directions: an accidental configuration (circles that happen to
    // touch), a warning, never a redundancy.
    std::vector<RowRef> singular;
    std::vector<FreeBlock> free;  // one per element of Model::sketch()
    // A component above Options::dense_columns was ranked by SparseQR.
    bool sparse = false;
    Status status =
        Status::Empty;  // Empty, Redundant, UnderConstrained or Full
};

// The D4 status of a Monitor group (plan 3.8): evaluated, never enforced.
struct MonitorStatus {
    int group = -1;  // index into Model::groups(), a GroupRole::Monitor one
    // Its largest row in its natural unit, as verify() reports it: > 0
    // violated by that much (an equality counts |row|), else the margin.
    double violation = 0.0;
    bool violated = false;  // !(violation <= feas_tol): NaN is violated
};

// What follows the pointer in a drag (plan 3.7). A drag's handles hold at
// most 5 coordinates in all.
struct Handle {
    enum class Kind : std::uint8_t {
        // One or two columns of q, set to the target's x (and y): a
        // radius, a pose angle, a point's x and y, a pose origin. An angle
        // column's target is wrapped onto its current turn.
        Columns,
        // Point `vertex` of a world value of the sketch, numbers 2 vertex
        // and 2 vertex + 1 of its data (RowEvaluator::value_plan): a point
        // element, a segment's end, a box's corner, a circle's centre, an
        // arc's start or end (SketchArc).
        WorldPoint,
        // The point `local` of a body's frame, in the world: where a press
        // on the body landed.
        BodyPoint,
    };
    Kind kind = Kind::Columns;
    std::vector<int> columns;  // Columns
    ExprRef value;             // WorldPoint
    int vertex = 0;            // WorldPoint
    int body = -1;             // BodyPoint: index into Model::sketch_bodies()
    Vec2d local;               // BodyPoint
};

// One drag (plan 3.7): a priority solve per frame, the rows hard, then the
// handles as close to their targets as the rows let them, then least
// change in scaled columns. It owns its factorisation and workspace: one
// thread at a time, and the System's model outlives it.
class Drag {
public:
    ~Drag();
    Drag(Drag &&) noexcept;
    Drag & operator=(Drag &&) noexcept;

    // Moves q, the state of the last frame, towards one target per handle
    // (a one-column handle reads x); a jump above 0.05 L_c is split into
    // up to 10 steps. A handle direction the rows determine stays put, and
    // moves nothing else (a body's angle a constraint fixes). False
    // when a step failed: its corrections did not converge in 10
    // iterations, or it flipped an orientation recorded at the start; q
    // then holds the last good state, and the handle lags. q is a state of
    // the System's model: after a recompile, begin a new drag, since a q of
    // another model with as many columns goes undetected. Throws
    // std::invalid_argument for a q of another size, or a target count
    // other than the handles'.
    bool move(std::span<const Vec2d> targets, std::span<double> q);
    // The columns the drag moves, ascending: the dragged components' and
    // the handles' own, less the held ones. Empty when nothing the handles
    // read can move, as for a fixed body's gestures: move() then returns
    // true and leaves q as it is, so a GUI that starts the drag anyway
    // shows a handle that never follows.
    std::span<const int> free_columns() const;
    // The handles' values where the drag started, to which the GUI adds
    // the pointer's motion.
    std::span<const Vec2d> start() const;
    // The columns held, the fallback ones included unless they were freed.
    std::span<const int> held() const;

private:
    friend class System;
    struct Impl;
    explicit Drag(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// The structure of a sketch-mode model compiled with row plans. Immutable:
// const members keep their workspace per call, so any number of threads may
// share one, and a copy shares the model.
class System {
public:
    // Throws std::invalid_argument for a null model, or one not compiled in
    // the Free binding with row plans: a design-mode model with row plans
    // would diagnose a sketch with free DOFs as fully constrained.
    explicit System(std::shared_ptr<const Model> sketch_model,
                    Options options = {});

    // Polishes each component from q (a copy), then takes its rank, the
    // second point of a component with a dependent row, and the free
    // directions. `left_out`: what settle() left out (left_out()). A row
    // that is not finite or degenerate at the polished point is left out
    // of the rank, so diagnose at a point settle() converged to.
    // `held`: the columns a gesture holds (plan 3.6), as begin_drag() takes
    // them, a Gesture's held plus its fallback when Drag::held() kept it:
    // they are neither free nor DOFs, and the free gauge parts are held as
    // begin_drag() holds them. Passing Drag::held() instead makes the
    // gauge parts the caller's, and they drop out of the DOF.
    // `only`, when not empty: the components that hold a row of one of these
    // constraints (ConstraintInfo::path), as SettleOptions::only takes them,
    // are diagnosed alone: `components`, `rank`, `singular` and `status`
    // speak of them only (Rank::dof unset), and `free` is empty. The second
    // point of a rank test (try_constrain), at a cost that does not grow
    // with the components it leaves alone.
    Diagnosis diagnose(std::span<const double> x, std::span<const double> q,
                       std::span<const int> left_out,
                       std::span<const int> held = {},
                       std::span<const std::string> only = {}) const;
    // The status of every Monitor group at x and q, in group order.
    std::vector<MonitorStatus> monitor(std::span<const double> x,
                                       std::span<const double> q,
                                       double feas_tol) const;
    // Starts a drag from q: the rows of the components the handles move,
    // less those of `left_out`, with `held` held, the free gauge parts of
    // every body none of whose pose or point columns is held, and
    // `fallback` unless the handles cannot follow with it held (G^T G
    // rank-deficient). Throws std::invalid_argument for more than 5 handle
    // coordinates or a handle that names nothing of the model.
    Drag begin_drag(std::span<const Handle> handles, std::span<const int> held,
                    std::span<const int> fallback, std::span<const double> x,
                    std::span<const double> q,
                    std::span<const int> left_out) const;

private:
    std::shared_ptr<const detail::SystemData> data_;
};

// Every constraint settle() left out, Malformed and conflicting,
// ascending: what System::diagnose() and begin_drag() take as left_out.
std::vector<int> left_out(const Settled & settled);

// The gestures of plan 3.7's table, as begin_drag()'s handles, held and
// fallback columns. `edit` is the body Edit body shows, or -1 in world
// mode, where every body is rigid and only poses move.
struct Gesture {
    std::vector<Handle> handles;
    std::vector<int> held, fallback;
};
// A point element follows the pointer: a world point in world mode, a
// member of `edit` in Edit body. Targets are world positions.
Gesture drag_point(const Model & model, int element, int edit = -1);
// Edge `edge` of a segment, polyline, polygon or box: both its ends follow
// the pointer's motion.
Gesture drag_edge(const Model & model, int element, int edge = 0,
                  int edit = -1);
// A circle's edge: its radius follows the pointer's distance to the
// centre, which is held unless the radius cannot follow.
Gesture drag_circle_edge(const Model & model, int element, int edit = -1);
// An arc's start (`end` false) or end follows the pointer: its angle and
// radius move, the centre only when they cannot follow.
Gesture drag_arc_end(const Model & model, int element, bool end, int edit = -1);
// An arc's edge: its radius follows the pointer's distance to the centre,
// its angles held; the centre only when the radius cannot follow.
Gesture drag_arc_edge(const Model & model, int element, int edit = -1);
// A press on any element of a body in world mode, at `local` in its
// frame: the body follows, translating when it can, turning about its pins
// when they force it.
Gesture drag_body(const Model & model, int body, Vec2d local);
// The pose gizmo's handles: the origin with the angle held, the angle with
// the origin held.
Gesture move_body(const Model & model, int body);
Gesture turn_body(const Model & model, int body);

// The Sketch section of a report: "sketch: 23 parameters in 3 components;
// 2 free DOFs (P7, door); redundant: v2 (with h1, h3, v1); conflicting: none;
// malformed: none; largest geometry residual 4e-14 m", then a line per
// note (a singular configuration, a body that is not anchored). The DOFs
// and the redundancy come from `diagnosis`, else from Settled::rank, and are
// left out without either; a rank of some components only gives no DOFs,
// and no "redundant: none". `verbose` adds a line per component.
std::string format_report(const Settled & settled, const Diagnosis * diagnosis,
                          bool verbose);
// The same as data, for a results file: status, parameters, components,
// dof, redundant, conflicting, left_out, conflict_groups, malformed,
// singular, unanchored, residual, report. Without a diagnosis the status of
// a converged settle() can be "solved" (some component unranked, Rank::dof
// unset), which docs/reference/cli.md does not list: the CLI always passes
// one.
nlohmann::ordered_json report_json(const Settled & settled,
                                   const Diagnosis * diagnosis);

}  // namespace gs::sketch

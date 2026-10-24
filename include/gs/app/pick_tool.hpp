#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gs/app/sketch_edits.hpp"
#include "gs/app/tool.hpp"

namespace gs::app {

// FreeCAD's DrawSketchHandlerGenConstraint (plan 4.6), for a constraint
// tool, beside StateTool: what the tool takes is a sequence of picks that
// completes one of its signatures, one PickFilter per pick.
// - Tool first: each press on a pick that extends a signature's prefix
//   takes it (pick_filter() is the kinds that would, so the canvas offers
//   no other); a press on nothing, on a pick already taken, or on one
//   refuse() refuses, takes nothing. A pick that completes a signature
//   applies the tool (apply()), one batch through ctx.commit(), its
//   refusal in the notice; the picks are then dropped and the tool waits
//   for the next (continuous mode), or asks to be quit.
// - Select first (on_start()): the selection's sketch entries, as
//   ToolContext::pick_of() reads them, in their order or, failing that, the
//   first order that fits: a whole signature applies the tool, which then
//   asks to be quit; a signature's prefix becomes the first picks. A
//   selection no signature takes, one with no sketch element nor body (a
//   constraint the last commit selected) and the one a tool's commit made
//   (ToolContext::selection_from_tool(): a box just drawn, then Fix) start
//   it tool first (the Steps 9-10 ruling), the notice saying why refuse()
//   refused a pick of it ("Fix: end(door) is derived from door: ...").
// - M cycles the method and keeps the picks (on_method_changed()): Point on
//   object's on_line() or on_segment() is chosen before its last pick.
// - Esc and a right click drop the picks; Backspace drops the last one; a
//   change of the document drops them all, since a name they hold may now
//   name another element.
// The signatures must be prefix-free: a sequence that completes one and
// starts another would apply before the longer one could be picked. A tool
// whose picks complete only by a click that places something
// (Completion::Place, Smart Dimension's label) need not be.
class PickTool : public Tool {
public:
    using Signature = std::vector<PickFilter>;
    // How the picks complete a signature (plan 4.6).
    enum class Completion : std::uint8_t {
        // With the pick that completes it: apply() at once.
        Pick,
        // With a left press that takes no pick once the picks complete
        // one (place()): Smart Dimension's picks accumulate, a point then a
        // point or a line, and the click that places the label completes
        // them. A pick that completes a signature is taken and waits, and so
        // does a selection that completes one (on_start()).
        Place,
    };

    // What a click with picks that complete a signature does, which the
    // hint and the preview show while the pointer is over the last pick and
    // apply() then writes.
    struct Proposal {
        // The constraint written ("horizontal(L1)"), named prefix<k>; "" for
        // an edit that is no formula (a merge, a fix).
        std::string formula, prefix;
        // For the hint: "adds horizontal(L1)", "merges E into P1".
        std::string does;
        std::string glyph;  // the preview's, at `anchor`
        Vec2d anchor;
        // The commit's report adds it: "world horizontal: fixes tv's angle".
        std::string note;
        // Written into the constraint beside its name and formula: Edit
        // body's "frame" (plan 4.6, step 12).
        Json keys = Json::object();
        // Why the click changes nothing; the rest is then unused.
        std::string refusal;
    };

    std::string_view id() const override { return id_; }
    // "<label>: <prompt>", the method after the label when there are two;
    // over a pick that completes a signature, what the click does, or why
    // it does nothing.
    std::string hint() const override;
    std::span<const std::string> methods() const override { return methods_; }
    int method() const override { return method_; }
    void set_method(int method) override;
    std::span<const ValueField> fields() const override { return {}; }
    // Nothing: a constraint tool picks, it places no point.
    SnapFilter snap_filter() const override;
    PickFilter pick_filter() const override;
    void on_event(const ToolEvent & event, ToolContext & ctx) override;
    void on_field(std::string_view, std::string_view, ToolContext &) override {}
    void on_start(ToolContext & ctx) override;
    // The picks taken, and the glyph of the constraint the pick under the
    // pointer would complete, as a Suggestion.
    PreviewList preview() const override;
    void on_document_changed(ToolContext & ctx) override;
    bool in_progress() const override { return !picks_.empty(); }
    bool escape(ToolContext & ctx) override;
    bool done() const override { return done_; }

    bool continuous() const { return continuous_; }
    void set_continuous(bool on) { continuous_ = on; }
    std::span<const ToolPick> picks() const { return picks_; }
    std::span<const Signature> signatures() const { return signatures_; }
    Completion completion() const { return completion_; }

protected:
    // `label` starts the hint ("Horizontal") and, lower case, the undo
    // label ("add horizontal h1").
    PickTool(std::string id, std::string label,
             std::vector<Signature> signatures,
             std::vector<std::string> methods = {},
             Completion completion = Completion::Pick);

    const std::string & label() const { return label_; }
    // What the signatures take, for the first hint ("pick a segment, or two
    // points") and the refusal of a selection ("select a segment, ...").
    virtual std::string wants() const = 0;
    // What the tool waits for after `picks`, a signature's prefix, not
    // empty: "pick the second point".
    virtual std::string prompt(std::span<const ToolPick> picks) const = 0;
    // What the tool does with `picks`, which complete signature `signature`
    // (an index of signatures()), at the drawing; cheap: it runs while the
    // pointer moves.
    virtual Proposal propose(std::size_t signature,
                             std::span<const ToolPick> picks,
                             const ToolContext & ctx) const = 0;
    // The edit of `p`: by default its refusal, or its formula added as a
    // constraint named p.prefix<k> (one undo step "add <label> h1", the
    // constraint selected, p.note in the report).
    virtual SketchEdit apply(std::size_t signature,
                             std::span<const ToolPick> picks,
                             const Proposal & p, ToolContext & ctx);
    // M changed the method: the picks stay.
    virtual void on_method_changed() {}
    // Why `next`, whose kind fits, cannot follow `picks`: by default, when
    // it is one of them already. An empty reason refuses it silently: the
    // hint then says what the picks taken wait for, as over nothing.
    virtual std::optional<std::string> refuse(
        const ToolPick & next, std::span<const ToolPick> picks) const;
    // Completion::Place: a left press at `press` that takes no pick (on
    // nothing, on a pick taken already or refused, or on one no signature
    // goes on with), while `picks` complete signature `signature`. The tool
    // commits through `ctx` and drops the picks itself (forget()). Nothing
    // by default.
    virtual void place(std::size_t, std::span<const ToolPick>,
                       const ToolEvent &, ToolContext &) {}
    // The picks changed: one taken, the last dropped (Backspace), all
    // dropped (Esc, a commit, a change of the document), or a selection
    // taken as the first picks. Nothing by default.
    virtual void on_picks_changed() {}
    // The methods M cycles from now on, the first chosen: a tool whose
    // alternatives depend on its picks sets them from on_picks_changed().
    void set_methods(std::vector<std::string> methods);
    // The signature `picks` completes; nullopt for a prefix or no match.
    std::optional<std::size_t> completed(std::span<const ToolPick> picks) const;
    // Whether `picks` start a longer signature.
    bool prefix(std::span<const ToolPick> picks) const;
    // The pick under the pointer that a press would take, the last move's;
    // nullptr over nothing, or over a pick no signature takes next.
    const ToolPick * taken_at_pointer() const {
        return hover_ && !refused_ ? &*hover_ : nullptr;
    }
    // Drops the picks, as Esc does.
    void forget();
    // A commit ended the picks: out of continuous mode, the tool asks to be
    // quit.
    void committed() { done_ = !continuous_; }

private:
    static bool prefix_of(const Signature & s, std::span<const ToolPick> picks);
    // propose(), refused when its formula is off its value and nothing it
    // reads can move (immovable()).
    Proposal proposed(std::size_t signature, std::span<const ToolPick> picks,
                      const ToolContext & ctx) const;
    // The pointer is over `pick`: what a press there would do.
    void hover(std::optional<ToolPick> pick, const ToolContext & ctx);
    // Takes `pick`; applies the tool when it completes a signature, unless
    // it completes by a placement.
    void take(const ToolPick & pick, ToolContext & ctx);

    std::string id_, label_;
    std::vector<Signature> signatures_;
    std::vector<std::string> methods_;
    Completion completion_ = Completion::Pick;
    int method_ = 0;
    std::vector<ToolPick> picks_;
    std::optional<ToolPick> hover_;
    std::optional<std::string> refused_;  // hover_ refused
    std::optional<Proposal> proposal_;    // hover_ completes a signature
    bool continuous_ = true;
    bool done_ = false;
};

// The Steps 9-10 ruling on geometry nothing can move: why a constraint tool
// refuses `formula` on `picks`, "nothing horizontal(A, B) reads can move",
// when it is off its value and nothing it reads can move it there (a
// Malformed constraint at once); nullopt when it holds, as FreeCAD adds it,
// or something moves. Settles a copy (try_constrain()) only when no pick
// reads a free value of its own (a free point, a line ending at one, a
// circle or an arc not fixed), so that a pointer over free geometry pays
// nothing.
std::optional<std::string> immovable(const ToolContext & ctx,
                                     std::span<const ToolPick> picks,
                                     const std::string & formula);

// The constraint `formula` added to the document as one batch, one undo
// step "add <what> <name>", named the first unused prefix<k>
// (unused_constraint_name()), `keys` beside its name and formula, the
// constraint selected after it, `note` in its report ("h1: world
// horizontal: fixes tv's angle"). Written through a ShapeDraft of `ctx`,
// as a creation tool's constraints are: in the frame it draws in.
SketchEdit constraint_edit(ToolContext & ctx, std::string_view prefix,
                           std::string formula, std::string_view what,
                           std::string_view note = {},
                           Json keys = Json::object());

}  // namespace gs::app

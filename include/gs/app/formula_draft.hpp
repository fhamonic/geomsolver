#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gs/app/changes.hpp"
#include "gs/app/keys.hpp"
#include "gs/app/selection.hpp"
#include "gs/app/sheet.hpp"
#include "gs/app/status.hpp"
#include "gs/engine/model.hpp"

namespace gs::app {

class App;

// The formula a new constraint starts from, after what is selected:
// "dist(A, B) >= " for two points, "clearance(tv, couch) >= " for two
// entries one of which is a shape, with `forall` the sweep either moves
// with, "A.x >= " for one point, "clearance(tv, )" with the caret before
// ")" for one shape, "span >= " for one scalar; an anonymous display
// item's expression stands in for a name. Empty for anything else.
struct Prefill {
    std::string text;
    std::size_t caret = 0;
    std::string forall;  // a sweep, "" for none

    bool operator==(const Prefill &) const = default;
};
Prefill constraint_prefill(const Document & doc,
                           std::span<const SelKey> selection);

// A name the formula field completes to.
struct Completion {
    std::string name;
    // "param", "design", "sweep", "let", "geometry", "function", "special
    // form" or "constant".
    std::string kind;
    // Short: "0.02", "(0.16, 0.175)", "[0, 1]", "polygon", "2 args".
    std::string value;

    bool operator==(const Completion &) const = default;
};

// The formula pop-up without its widgets: the entry being added or edited,
// its formula with the caret and the names it may complete to, the key
// rules of the field, a check of the formula against the document as it
// is typed, and the commit as one undo step. The App owns one; a front-end
// shows it when take_open_request() says so and closes it once is_open()
// turns false.
class FormulaDraft {
public:
    enum class Mode : std::uint8_t {
        AddConstraint,
        EditConstraint,
        AddCriterion,
        EditCriterion,
        AddQuantity,
        EditQuantity,
    };
    // What key() did.
    enum class Outcome : std::uint8_t {
        None,       // nothing: a key the field keeps for itself
        Accept,     // the highlighted candidate replaced the name typed
        Commit,     // commit() ran: is_open() says whether it took
        Cancel,     // the draft closed with no edit
        HideList,   // the candidates are hidden, the text unchanged
        NextField,  // the keyboard moves on; the candidates are hidden
        Moved,      // another candidate is highlighted
    };
    struct TextUpdate {
        std::string text;
        std::size_t caret = 0;
    };
    // What the formula is worth as typed.
    struct Check {
        // "value now 0.3412, margin 0.12 at tau=0.5" in the tone of its
        // verdict; "dist(A, B) = 0.3412 now" for a formula that does not
        // parse yet, through its left side.
        Status status;
        // The first error of the draft entry, marked at its column.
        std::optional<DiagnosticText> diagnostic;
        // Why less is checked: "The document does not compile: only the
        // syntax of the formula is checked".
        std::string note;

        bool operator==(const Check &) const = default;
    };

    static constexpr const char * kCancel = "Cancel";
    static constexpr const char * kFormula = "Formula";
    // The formula field's keys (key()).
    static constexpr const char * kKeysTip =
        "Names complete as you type. Enter accepts the highlighted one, or "
        "adds once the formula's last name is complete; Up and Down move in "
        "the list; Esc hides it, then cancels; Tab moves to the next field.";

    explicit FormulaDraft(App & app);
    FormulaDraft(const FormulaDraft &) = delete;
    FormulaDraft & operator=(const FormulaDraft &) = delete;

    // A new constraint, under an unused name, its formula pre-filled from
    // the selection (constraint_prefill()).
    void add_constraint();
    // A new criterion, reported only until its role is chosen.
    void add_criterion();
    // A new let.
    void add_quantity();
    // The formula of `key`'s constraint, criterion or let, under its name.
    // False, opening nothing, for any other entry or one that is gone.
    bool edit(const SelKey & key);
    // True once after each opening: the front-end shows the pop-up.
    bool take_open_request() { return std::exchange(open_request_, false); }
    bool is_open() const { return open_; }
    Mode mode() const { return mode_; }
    // "Add constraint", "Edit constraint couch".
    std::string title() const;
    // "Add", "Save".
    std::string commit_label() const;

    const std::string & text() const { return text_; }
    // A byte offset in text().
    std::size_t caret() const { return caret_; }
    // The field's text and caret after the user typed. The candidates
    // follow, shown again if hidden, and the check is due.
    void set_text(std::string text, std::size_t caret);
    // The caret moved, the text did not.
    void set_caret(std::size_t caret);
    // The text and caret the draft set itself (an opening, accept(),
    // insert()), once: the front-end puts them in its field, which keeps
    // its own buffer.
    std::optional<TextUpdate> take_text_update();

    // The fields besides the formula, as sheet items with these ids: the
    // name ("name", a Field whose text is the name); for a constraint, the
    // sweep it holds for ("forall", a Choice) and, when added, "enabled";
    // for a criterion, "enabled" when added, its role ("role"), its bound
    // ("bound", or "from" and "to" for a range) and its unit ("unit").
    std::vector<DetailItem> fields() const;
    // Edits one of fields(): a Field takes a string, a Choice an index, a
    // Checkbox a bool. False for another id or type.
    bool set(std::string_view id, const DetailValue & value);

    // The names that complete the identifier at the caret, the best first,
    // at most 12: those that start with what is typed, the document's
    // entries before the builtins, the sweeps first in the first argument
    // of at(), max_over() and min_over(); then those that hold it. After
    // a '.', the coordinates x and y.
    const std::vector<Completion> & candidates() const { return candidates_; }
    // The bytes of text() that accept() replaces: the identifier at the
    // caret.
    std::pair<std::size_t, std::size_t> replaced() const { return replaced_; }
    // The candidates are shown: some match, and the text was edited since
    // the opening and since a key last hid them.
    bool list_shown() const { return !candidates_.empty() && !hidden_; }
    std::size_t highlighted() const { return highlighted_; }
    // A key in the formula field:
    // - Up, Down: Moved while the list is shown, else None;
    // - Enter: while the list is shown, Accept the highlighted name, or
    //   Commit when it is already the whole identifier at the caret, so
    //   that a complete formula commits with one Enter; else Commit;
    // - Escape: HideList while it is shown, else Cancel;
    // - Tab: NextField, hiding the list.
    // The list stays shown on an exact match: "c" is a name and the start
    // of "couch", and hiding it there would make it flicker while typing.
    Outcome key(Key k);
    // Replaces replaced() with candidate `i`; a function or a special form
    // also gets "(" with the caret after it.
    void accept(std::size_t i);
    // Inserts the name of `key`'s entry at the caret, the expression of a
    // display item; false for a constraint or a criterion, which no
    // formula names.
    bool insert(const SelKey & key);

    // Compiled again when the text or a field changed, or the document
    // did; evaluated again when x or a sweep moved: not on every call.
    const Check & check();

    // The entry, as one undo step: "add constraint c7", "edit constraint
    // couch"; a new name renames every use. The new entry is selected and
    // revealed in its list. False, staying open with refusal() saying why,
    // for an empty formula, a name that is taken or not valid, or an edit
    // the document refuses. Then the draft closes. An edit that changes
    // nothing closes the draft and applies nothing: the document stays
    // as it was, unmodified if it was.
    bool commit();
    void cancel();
    const std::string & refusal() const { return refusal_; }

private:
    void open(Mode mode, std::string name, std::string text, std::size_t caret);
    bool constraint() const;
    bool criterion() const;
    bool adding() const;
    // Where the entry is, or will be: "constraints[6]", "let.q1".
    std::string path() const;
    // The entry as the document will hold it, without its name for a let.
    Json entry() const;
    // Why the name cannot be committed, "" when it can.
    std::string name_problem() const;
    void complete();
    void compile();
    void evaluate();

    App & app_;
    bool open_ = false, open_request_ = false;
    Mode mode_ = Mode::AddConstraint;
    // The entry edited, as it was opened: its path and name, and the
    // document's path generation then.
    std::string target_, old_name_;
    std::uint64_t paths_ = 0;

    std::string name_, text_;
    std::size_t caret_ = 0;
    std::optional<TextUpdate> update_;
    std::string forall_;
    bool enabled_ = true;
    std::string role_ = "report";
    std::string bound_, from_, to_;
    int unit_ = 0;  // in unit_choice()'s choices; 0 for none

    std::vector<Completion> candidates_;
    std::pair<std::size_t, std::size_t> replaced_{0, 0};
    std::size_t highlighted_ = 0;
    bool hidden_ = false;

    Check check_;
    bool compile_due_ = true, evaluate_due_ = true;
    // The draft's compile, while it fits the document's x: the same
    // coordinates and sweeps. Its value is read at the document's x and
    // sweep positions.
    std::shared_ptr<const Model> model_;
    // The draft entry's path in that compile: a let under a name that is
    // no name yet is compiled under another.
    std::string compiled_path_;
    // A probe of the left side of a formula that does not parse: the
    // model's probes()[0].
    std::string probe_;
    std::string refusal_;
    Signal<Changes>::Connection link_;
};

}  // namespace gs::app

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gs/app/changes.hpp"
#include "gs/app/keys.hpp"
#include "gs/app/tool.hpp"

namespace gs::app {

class App;

// The toolbar's groups (plan 4.12).
enum class ToolGroup : std::uint8_t { Create, Constrain, Body };

// A tool of the toolbar, implemented or not yet: its action "tool.*" is
// registered either way, so that the toolbar and the chords are whole.
struct ToolSpec {
    std::string_view id;     // "tool.segment", its action's id too
    std::string_view label;  // "Segment"
    // The toolbar's button when the label is too long for it.
    std::string_view caption;
    KeyChord chord;
    ToolGroup group = ToolGroup::Create;
};
// Every tool, in toolbar order: Point, Segment, Polyline, Box, Circle,
// Arc; Coincident, Point on object, Horizontal, Vertical, Parallel,
// Perpendicular, Tangent, Equal, Symmetric, Smart dimension, Fix; New
// body, Body from selection.
std::span<const ToolSpec> tool_specs();

// The active tool and what it shares with the views (plan 4.2), owned by
// App: starts and quits tools, takes their part of the Esc chain, feeds
// them the canvas's events and the document's changes, and keeps the value
// field the user types in.
//
// The field contract is FormulaDraft's, so that a retained toolkit can bind
// it: a view draws every field of active()->fields() at its anchor, the one
// editing() names with that text, the others with their own. Only that one
// takes input: an earlier state's field stays at its anchor, under the
// click that places the next point. The view gives the keyboard to
// take_field_focus()'s field, puts take_text_update()'s text in it, reports
// each edit with set_field_text() and Enter, Esc and Tab with field_key(). A
// field is open from the seed on, before the view gives it the keyboard: until
// then, its keys reach the host through the actions and typed(), and the
// characters its toolkit drops on the frame the field takes the keyboard must
// go to typed() too, or they are lost.
class ToolHost {
public:
    using Factory = std::function<std::unique_ptr<Tool>()>;

    explicit ToolHost(App & app);
    ToolHost(const ToolHost &) = delete;
    ToolHost & operator=(const ToolHost &) = delete;
    ~ToolHost();

    // Plugs in the implementation of tool `id` of tool_specs(); its action
    // is enabled from then on. Throws std::invalid_argument for an id that
    // tool_specs() lacks.
    void provide(std::string_view id, Factory make);
    bool provided(std::string_view id) const;

    // Null without one.
    Tool * active() const { return tool_.get(); }
    // Quits the active tool, dropping its shape, then starts `id`
    // (Tool::on_start(), after which a tool done() quits at once). False,
    // and nothing quit, for a tool not provided or without drawable().
    bool start(std::string_view id);
    // A document whose last compile succeeded: one that fails draws no
    // sketch, or the last good one, and a tool's shape would go where
    // nothing shows it. A tool quits when its document stops compiling.
    bool drawable() const;
    // Quits the active tool, dropping what it had in progress.
    void cancel();
    // Steps 3 and 4 of the Esc chain (plan 4.3): the shape in progress is
    // dropped (a polyline finishes open), else the tool quits. A text left
    // in a field that lost the keyboard goes first, one step of its own.
    // False without a tool: the chain goes on to the selection.
    bool escape();
    // A right click on the canvas: the chain from the shape (plan 4.3), a
    // text left in a field going with the shape it is a value of, in one
    // step; else the tool quits. False without a tool.
    bool right_click();

    // An event of the canvas, which routes them while a tool is active.
    void event(const ToolEvent & event);
    // Characters typed over the canvas while no text field has the
    // keyboard: a digit, '-', '.' or '(' opens the current state's first
    // unset dimensional field with it, '=' opens its first unset field
    // empty (ValueField::placed: not a field of a state placed already,
    // which neither opens nor Tab moves into); letters are left to the
    // actions, so that T starts Tangent and seeds nothing. An open field
    // takes every character until its view reports the keyboard
    // (field_focused()), the letters of "30cm" typed in a burst included,
    // whose keys fire no action meanwhile (App::key_focus()); then only
    // those that would seed one: a blank or a letter typed over the canvas
    // leaves a text that lost the keyboard alone, so that Space + drag
    // pans. A text a tool opened selected (ToolContext::open_field()) is
    // replaced by the first characters typed, before the view has it too.
    void typed(std::string_view text);
    // The view's field `id` took the keyboard: ImGui's once its InputText
    // is active and takes the text input (WantTextInput), wx's on
    // EVT_SET_FOCUS. Reported before the keys of that frame see the
    // toolkit's field as having the keyboard, the letters typed on it run
    // their actions (App::key_focus()). Not reported, the field
    // keeps taking every character typed over the canvas after it lost the
    // keyboard, a blank among them, which asks the keyboard back.
    void field_focused(std::string_view id);
    // The keys that need no pointer: Enter and Backspace to the tool
    // (tool.finish, tool.back), the next method (tool.method), the current
    // state's first unset field opened empty, positional ones included
    // (tool.field). While a field is open they are its keys: Enter and Tab
    // do what field_key() does, Backspace erases its last character, M does
    // nothing: a method change would restart the shape under the text typed.
    void finish();
    void back();
    void next_method();
    void open_field();
    // A document change since the last call reaches the tool's
    // on_document_changed(), a reload of its file included; another
    // document, or one that no longer compiles, quits it. App::tick() calls
    // it, and so does each call into the tool, since a presenter edits the
    // document without any action.
    void tick();

    // The field being typed in, with its text; nullopt for none. It
    // closes when applied or reverted, or when the tool's fields no longer
    // have it.
    struct Edit {
        std::string id, text;

        bool operator==(const Edit &) const = default;
    };
    const std::optional<Edit> & editing() const { return edit_; }
    // The open field asked its view for the keyboard and the view has not
    // reported it (field_focused()): from a seed, a refused Enter or a digit
    // asking it back, to the view's report. Its keys fire no action then
    // (App::key_focus()); a text left in a field that lost the keyboard is
    // not waiting, and blocks nothing.
    bool field_waiting() const { return edit_.has_value() && waiting_; }
    // The field to give the keyboard to, once: the view that draws it
    // takes it.
    std::optional<std::string> take_field_focus();
    // The text and caret (a byte offset) the host set itself (a seed, a
    // revert, a refused text kept), once, for the field's own buffer: a
    // toolkit's field that takes the keyboard programmatically may select
    // its whole text, which the next key typed would replace.
    struct TextUpdate {
        std::string id, text;
        std::size_t caret = 0;
        // The whole text selected, the caret at its end: the next key typed
        // replaces it (ToolContext::open_field()).
        bool select = false;

        bool operator==(const TextUpdate &) const = default;
    };
    std::optional<TextUpdate> take_text_update();
    // The view reports an edit of field `id`; a field that was not open
    // opens with it.
    void set_field_text(std::string_view id, std::string text);
    // What field_key() did.
    enum class Outcome : std::uint8_t {
        None,      // no such field, Enter in one not open, a key it keeps
        Applied,   // the tool took the text; the field closed
        Refused,   // the field stays open, ValueField::refusal says why
        Reverted,  // Esc: the field closed, showing the tool's text again
        Moved,     // Tab: applied, and the next field opened empty
    };
    // Enter applies the field's text, Esc reverts that field only, Tab
    // applies it (an empty or untouched text is left alone) and opens the
    // next field of the current state, positional ones included, wrapping
    // around (an untouched field that is the state's only one stays as it
    // is: None); when the text applied completes the state, nothing opens
    // (Applied), unless the tool places that state again (a polyline's
    // next edge), whose first field then opens (Moved). A field the toolkit
    // gave the keyboard to without the host
    // (a click, in a view whose closed fields are widgets too) is not open
    // until its first edit: there Enter gives None, Esc Reverted and Tab
    // Moved, so a view must take the keyboard from a field on Reverted even
    // when editing() never named it.
    Outcome field_key(std::string_view id, Key key);

    const ToolOptions & options() const { return options_; }
    void set_options(const ToolOptions & options);
    // The status bar's tool segment: the active tool's hint, or "".
    std::string hint() const;

    // Tool, once per public call that changed the active tool, its state,
    // hint, method, fields, preview, the open field, a request for the
    // field's keyboard or text (raised again included: a refused Enter asks
    // the keyboard back) or the options. A slot must not change the host.
    Signal<Changes> & changed() { return changed_; }

private:
    friend class ToolContext;
    class Notify;
    struct Seen {
        const void * instance = nullptr;
        // Another instance of the same file is a reload: a change under the
        // tool (plan 4.2), not another document.
        std::filesystem::path file;
        std::uint64_t paths = 0, compiles = 0, restores = 0;
        bool operator==(const Seen &) const = default;
    };

    Seen seen_now() const;
    // Brings the tool up to date with the document before a call into it.
    void follow();
    void quit_if_done();
    // Opens field `id` with `text`, the caret at its end, the text selected
    // with `select`, and asks for the keyboard.
    void open(const std::string & id, std::string text, bool select = false);
    // The open field asks its view for the keyboard (again).
    void ask_keyboard();
    // Field `id` closed unapplied: Tool::on_field_closed().
    void closed(const std::string & id);
    // The open field's text to the tool; false when it refused it.
    bool apply();
    // Before a press: apply(), or, for a text with nothing typed into it
    // (typed_into()), close the field unapplied: the tool refuses an empty
    // text ("empty expression"), which would keep the press from placing.
    bool apply_pending();
    // Whether the open field `f` holds a text typed into it: neither empty
    // nor the field's own (Tab or '=' opened it, nothing typed since).
    bool typed_into(const ValueField & f) const;
    const ValueField * field(std::string_view id) const;
    void close_edit();
    // Esc's step 3 or 4: the tool drops its shape, or quits.
    void drop_or_quit();
    void drop_closed_edit();

    App & app_;
    std::vector<Factory> factories_;  // by tool_specs() index
    std::unique_ptr<Tool> tool_;
    Seen seen_;
    ToolOptions options_;
    std::optional<Edit> edit_;
    std::optional<std::string> focus_;
    // The open field asked the keyboard and its view has not reported it
    // yet: it takes every character typed.
    bool waiting_ = false;
    // Its text is the one a tool opened it with, selected (open_field()),
    // and the view has not had it yet: the first character typed replaces
    // it, as in the view's field, rather than following the measured value
    // ("0.35" for 5 typed over "0.3").
    bool selected_ = false;
    std::optional<TextUpdate> update_;
    int notify_depth_ = 0;
    Signal<Changes> changed_;
};

}  // namespace gs::app

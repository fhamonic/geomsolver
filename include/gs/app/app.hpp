#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gs/app/actions.hpp"
#include "gs/app/canvas.hpp"
#include "gs/app/changes.hpp"
#include "gs/app/dialogs.hpp"
#include "gs/app/document.hpp"
#include "gs/app/formula_draft.hpp"
#include "gs/app/panel_host.hpp"
#include "gs/app/panels.hpp"
#include "gs/app/scene_cache.hpp"
#include "gs/app/selection.hpp"
#include "gs/app/settings.hpp"
#include "gs/app/sketch_edits.hpp"
#include "gs/app/sketch_session.hpp"
#include "gs/app/solver_backend.hpp"
#include "gs/app/solver_session.hpp"
#include "gs/app/status_bar.hpp"
#include "gs/app/tone.hpp"
#include "gs/app/tool_host.hpp"
#include "gs/app/view.hpp"
#include "gs/app/visibility.hpp"

namespace gs::app {

// The application without its toolkit: the open document, its selection,
// scene, sweep players and solver session, the actions of the menus, keys
// and buttons, and the questions they ask. A front-end draws it, binds the
// actions and feeds it time; it changes nothing but through App, its
// actions and their dialogs.
class App {
public:
    // `settings` holds what outlives a run, such as which panels are open;
    // a scripted run passes a NullSettingsStore, so it neither depends on
    // nor changes the user's.
    explicit App(std::unique_ptr<SolverBackend> backend = make_solver_backend(),
                 std::unique_ptr<SettingsStore> settings =
                     std::make_unique<NullSettingsStore>());
    App(const App &) = delete;
    App & operator=(const App &) = delete;

    Document & document() { return doc_; }
    Selection & selection() { return selection_; }
    SolverSession & solver() { return session_; }
    // A sketch drag's live state, the monitored statuses and the diagnosis
    // worker.
    SketchSession & sketch() { return sketch_; }
    // One per sweep of the model, once tick() has run.
    std::vector<SweepPlayer> & players() { return players_; }
    // The canvas's tools: the active one, its value fields, the options.
    ToolHost & tools() { return tools_; }
    const ToolHost & tools() const { return tools_; }
    const ActionRegistry & actions() const { return actions_; }
    std::span<const Menu> menus() const { return menus_; }
    // The toolbar (plan 4.12): the tools' actions in the groups Create,
    // Constrain and Body, then the toggles of Options, each a Menu of
    // action ids. A tool's button is checked while it is active, and
    // disabled, with a tooltip (Action::tip), until it is provided.
    std::span<const Menu> toolbar() const { return toolbar_; }
    // The changes of the Document, the Selection, the canvas and the
    // SketchSession, Solver from tick(), Scene from scene(), Player from
    // toggle_play(), Panels, Hover and Visibility; a slot must not change
    // the App synchronously.
    // A list's Filters is on that list's own changed() only.
    Signal<Changes> & changed() { return changed_; }

    // The eyes of the names; follows the document's file (sync()).
    Visibility & visibility() { return visibility_; }
    // The entry under the pointer, which the canvas and the lists highlight.
    const std::optional<SelKey> & hot() const { return hot_.key(); }
    // Emits Hover when hot() changes.
    void set_hot(HotSource source, std::optional<SelKey> key) {
        hot_.set(source, std::move(key));
    }
    // After an edit renamed the entry `from` names, or named an unnamed
    // one: the selection, the eyes and the hot key follow the name. Call it
    // after every rename the App did not make itself, or the renamed entry
    // drops out of the selection at the next sync() and loses its eye. A
    // body's element takes its new name within its body ("c2") or
    // qualified ("tv.c2"); a body takes its elements along ("tv.c" becomes
    // "screen.c"), and a constraint its glyph's eye.
    void entry_renamed(const SelKey & from, std::string_view to);
    // Erases the display items that draw the names of `keys`
    // (drawings_of()), one step. True when it erased some.
    bool remove_drawings(std::span<const SelKey> keys);
    // After an edit erased display[index]: the display items after it move
    // up by one in the selection and the hot key. Call it once per erased
    // item, the last first.
    void display_erased(int index);

    CanvasController & canvas() { return canvas_; }
    // A right click on `key` (a canvas pick, a row), or on nothing: selects
    // the key alone unless it is selected already, then asks the front-end
    // for the context menu at `at`, in screen pixels.
    void context_click(const std::optional<SelKey> & key, Vec2d at);
    // Where the context menu was asked for, once; it lists
    // context_actions().
    std::optional<Vec2d> take_context_menu();
    // A double click on `key`: reveals it in its home panel, which opens
    // and comes to the front (take_panel_focus()), its filters cleared when
    // they hide the entry; then Properties comes to the front too, and its
    // first field takes the keyboard.
    void reveal(const SelKey & key);
    // After an Add appended `key`'s entry: selects it, reveals it in the
    // list that ran the Add, the focused one when it shows the entry, else
    // its home, which comes forward with the filters that would hide it
    // cleared; then, when `rename`, asks to rename it there. A display
    // item, which has no name, is not renamed.
    void show_added(const SelKey & key, bool rename);
    // The entry F2 or an Add asked to rename in `panel`, once; nullopt for
    // another panel, which leaves the request to its own. F2 asks the
    // focused panel when it shows the entry, else Properties, which comes to
    // the front; an Add asks its list. The request lasts until it is taken,
    // or until the selection is no longer that entry alone or a path moves
    // (Document::path_generation()): then it gives nullopt.
    std::optional<SelKey> take_rename_request(PanelId panel);
    // The actions a context menu lists for the selection, enabled ones only,
    // in groups separated by "": rename and delete; hide, show, the
    // drawings and Fit selection; enable, disable, release and fix; the
    // selection's own (associated, referenced, clear).
    Menu context_actions() const;

    // Whether the panel's window is shown. The front-end follows it, and
    // reports a window the user closed with set_panel_open(id, false).
    bool panel_open(PanelId id) const {
        return panels_open_[static_cast<std::size_t>(id)];
    }
    // Emits Panels and saves the open panels when it changes something.
    void set_panel_open(PanelId id, bool open);
    // Opens the panel and asks the front-end to bring it to the front.
    void focus_panel(PanelId id);
    // The panels focus_panel() asked for, the oldest first, each once: a
    // front-end brings them to the front in this order, so that the last
    // keeps the keyboard and the others stay the tabs shown in their dock
    // nodes. A panel asked for again moves to the end.
    std::optional<PanelId> take_panel_focus();
    // View > Reset layout asked for the default layout, once.
    bool take_layout_reset();
    // The layout blob a front-end saved under `key` ("layout.imgui") while
    // its default layout had `version`, kept under "<key>.version", so that
    // each front-end has its own. Nullopt when there is none or the
    // version differs, and then every panel is open: the front-end builds
    // its default layout instead, where every panel has its place. Bump the
    // version when the default layout gains or loses a window: an older
    // layout does not know the new one, which would open floating.
    std::optional<std::string> load_layout(std::string_view key, int version);
    void save_layout(std::string_view key, int version, std::string_view blob);
    StatusBarPresenter & status_bar() { return status_bar_; }
    Panels & panels() { return panels_; }
    // The panel that has the keyboard, which the front-end reports; nullopt
    // for the canvas or no window. F2 renames in it, and the Panel scope's
    // keys act on its list.
    std::optional<PanelId> focused_panel() const { return focused_; }
    void set_focused_panel(std::optional<PanelId> id) { focused_ = id; }
    // The focus to run the registry's keys in (ActionRegistry::run_pressed),
    // from what the toolkit says: `typing` while a text field of its own has
    // the keyboard, `popup` while a popup or a dialog is open. A tool's value
    // field waiting for the keyboard adds KeyFocus::field, which only Enter,
    // Tab, Backspace and Esc get through: a front-end that builds KeyFocus
    // itself runs a tool's chord on the letter of a unit typed into it.
    KeyFocus key_focus(KeyScope scope, bool typing, bool popup) const;
    // Asks the view that draws the text field of `path` to give it the
    // keyboard: a status bar link, Enter on a row. take_field_focus() is
    // true once, for that path; the request goes stale when a path moves.
    void request_field_focus(std::string path);
    bool take_field_focus(std::string_view path);

    // Loads the instance and fits the view to its sketch's extent, else to
    // its design.
    bool open(const std::filesystem::path & file);
    // Command-line edits PATH=VALUE, applied as one step: a VALUE that is
    // not JSON is a string, null erases the key. Returns the outcome:
    // "compiled", why the batch was refused, "compiled, 1 item(s)
    // excluded: ..." or "does not compile".
    std::string apply_edits(
        std::span<const std::pair<std::string, std::string>> edits);
    // Brings the selection and the eyes up to date with the document. Call
    // it once the actions and presenter operations of an input have run,
    // before anything reads the selection, and not from a changed() slot: a
    // rename or an erased display item fixes the selection up after the
    // document has changed, and a sync() in between drops the entry
    // instead.
    void sync();
    // Advances the sweep players by dt seconds and polls the solver job,
    // copying its new results before it emits Solver: a slot reads them in
    // results(), and the Solver view's own refresh would come too late.
    // Then the SketchSession's tick(): the sketch diagnosis; then the tools
    // learn of the document's changes (ToolHost::tick()).
    void tick(double dt);
    // tick() still has work: a job runs, a sweep plays, a sketch diagnosis
    // waits or runs, or the monitored statuses wait for a commit, a move of
    // x or a drag's end (SketchSession::busy()).
    bool busy() const;
    // Plays or pauses sweep k of the model, emitting Player; playing "once"
    // from the end starts over.
    void toggle_play(std::size_t k);
    // The cache's scene, brought up to date; interactive while something is
    // dragged. Emits Scene when that computed it again.
    const Scene & scene(bool interactive);
    // The scene the last scene() computed, as it is: what a list's value
    // column reads. Calling scene(false) there instead would start the
    // fine check in the middle of a drag, which the frame's scene(true)
    // left on the coarse grid. Empty before the first scene().
    const Scene & scene_now() const { return cache_.scene(); }
    // Bumped whenever scene_now() changed: a pointer into it that was kept
    // over a bump may dangle.
    std::uint64_t scene_generation() const { return cache_.generation(); }
    // The window's close button: true when it may close now; otherwise
    // asks, and quit_confirmed() turns true once answered. The Quit action
    // asks the same way, and sets quit_confirmed() at once when clean.
    bool request_quit();
    bool quit_confirmed() const { return quit_ok_; }
    std::string window_title() const;  // "geomsolver - tv.json *"
    struct StatusText {
        Tone tone;
        std::string text;
    };
    // "unsaved changes", "solving...": shown after the menus.
    std::vector<StatusText> menu_bar_status() const;
    void request_delete() { delete_.request(); }
    // One step of the Esc chain (the action app.escape, plan 4.3), wherever
    // the pointer is; a text field and a popup handle their own Esc before
    // it. A tool drops the shape it has in progress, else quits
    // (ToolHost::escape()); without a tool, the selection is cleared. Esc
    // never leaves Edit body.
    void escape();
    // escape() has a step to take.
    bool escapable() const;

    // What the last edit of the sketch has to say: why it was refused, or
    // what it deleted with it. The status bar shows it until the selection
    // or the document changes; Messages' log keeps it.
    struct Notice {
        Tone tone = Tone::Normal;
        std::string text;
    };
    std::optional<Notice> notice() const;
    // Shows `text` as the notice and logs it, as a warning when `tone` is
    // Tone::Warn (a refusal).
    void notify(Tone tone, std::string text);
    // Applies a sketch edit worked out from the document (sketch_edits.hpp)
    // as one step, then selects the entries it created (SketchEdit::created:
    // points, an element, a constraint) and drops the point it merged away;
    // a refused one changes nothing and says why in the notice. With `reveal`,
    // the first entry it created is revealed (Selection::reveal()) before its
    // report is set: a reveal moves the selection's generation, which the
    // notice lasts for. True when the document changed.
    bool apply_sketch_edit(SketchEdit edit, bool reveal = false);
    // Whether the selection is still the one the last tool commit made (a
    // revealed apply_sketch_edit(), ToolContext::commit()): a constraint
    // tool started on it starts tool first, as on no selection (the Steps
    // 9-10 ruling), and its own key starts it again only on another one.
    bool selection_from_tool() const {
        return tool_selection_ == selection_.generation();
    }
    // Splits edge `edge` of the polyline or polygon `element` at `at`, in
    // its frame (insert_vertex()), and selects the new point: the canvas's
    // tool calls it from plan step 8.
    bool insert_vertex(const SelKey & element, int edge, Vec2d at);

    PathDialog & path_dialog() { return path_dialog_; }
    // The formula pop-up: the Adds of constraints, criteria and lets, and
    // Edit formula.
    FormulaDraft & formula() { return formula_; }
    UnsavedChanges & unsaved_changes() { return unsaved_; }
    DeleteRequest & delete_request() { return delete_; }
    ControlsHelp & controls_help() { return controls_; }

private:
    struct RenameRequest {
        SelKey key;
        PanelId panel;
        std::uint64_t paths = 0;  // doc_.path_generation() when asked
    };
    struct FieldFocus {
        std::string path;
        std::uint64_t paths = 0;  // doc_.path_generation() when asked
    };

    void add_actions();
    // The sketch edits of the context menu: merge, detach, delete vertex,
    // unfix, driving and reference, flip side.
    void add_sketch_actions();
    // The tools of the toolbar, the keys they need no pointer for and the
    // toolbar's toggles.
    void add_tool_actions();
    // Enter, Backspace, Tab and M are the active tool's, and not the list
    // that has the keyboard, while the pointer is over the canvas, as the
    // Canvas chords and the typed digits are. A list keeps the keyboard
    // after the pointer leaves it: gated on the focus alone, M and Tab over
    // the canvas would do nothing until a click, which places a point.
    bool tool_has_keys() const;
    // The selection as a pre-fill's operands (sketch_prefills()): every
    // entry an element of the sketch a formula names, a point or a line as
    // a constraint tool picks it in the last scene, a polyline, a polygon or
    // a box whole as a shape; empty for any other selection.
    std::vector<SketchOperand> sketch_operands() const;
    // Whether the model's display items sit where the document has them: a
    // stale model's may sit elsewhere, and an edit at their places would
    // hit another item.
    bool drawn_model() const;
    void set_panels_open(const std::array<bool, kPanelCount> & open);
    void open_all_panels();
    // Applies `batch`, which adds `key`'s entry, then show_added() with a
    // rename.
    void add_entry(EditBatch batch, const SelKey & key);
    // The Adds of the toolbars that need no formula, each under the first
    // unused name of its prefix: param p1; point P1, whose domain is the box
    // of the canvas's view and whose value is its centre; scalar s1 in
    // [0, 1]; range t1 over [0, 1]; a display item of the selected name,
    // else vec(0, 0).
    void add_param();
    void add_point();
    void add_scalar();
    void add_range();
    void add_display_item();

    std::unique_ptr<SettingsStore> settings_;
    Document doc_;
    std::uint64_t restores_ = 0;  // doc_.restore_generation() at sync()
    Selection selection_;
    Visibility visibility_{*settings_};
    HotKey hot_;
    SceneCache cache_;
    SketchSession sketch_{doc_};
    SolverSession session_;
    std::vector<SweepPlayer> players_;
    CanvasController canvas_{*this};
    ToolHost tools_{*this};
    // Declared after doc_: its PanelHost connects to the document's signal
    // when it is constructed.
    Panels panels_{*this};
    std::optional<RenameRequest> rename_;
    std::optional<FieldFocus> field_focus_;
    std::optional<PanelId> focused_;
    std::optional<Vec2d> context_menu_;
    struct HeldNotice {
        Notice notice;
        // While both are as they were when it was set.
        std::uint64_t selection = 0, compile = 0;
    };
    std::optional<HeldNotice> notice_;
    // selection_.generation() after the last tool commit.
    std::optional<std::uint64_t> tool_selection_;
    std::array<bool, kPanelCount> panels_open_;
    std::vector<PanelId> panel_focus_;
    bool layout_reset_ = false;
    bool quit_ok_ = false;
    ActionRegistry actions_;
    std::vector<Menu> menus_;
    std::vector<Menu> toolbar_;
    PathDialog path_dialog_{*this};
    UnsavedChanges unsaved_{doc_};
    DeleteRequest delete_{*this};
    ControlsHelp controls_;
    StatusBarPresenter status_bar_{*this};
    Signal<Changes> changed_;
    Signal<Changes>::Connection document_link_, selection_link_, canvas_link_,
        visibility_link_, hot_link_, sketch_link_, tools_link_;
    // Declared after changed_, to which it connects when it is constructed.
    FormulaDraft formula_{*this};
};

}  // namespace gs::app

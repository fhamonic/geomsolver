#include "gs/app/app.hpp"

#include <algorithm>
#include <format>
#include <functional>
#include <initializer_list>
#include <ranges>
#include <string_view>
#include <tuple>
#include <utility>

#include "gs/app/associated.hpp"
#include "gs/app/constraint_tools.hpp"
#include "gs/app/creation_tools.hpp"
#include "gs/app/entry_edits.hpp"
#include "gs/app/format.hpp"
#include "gs/app/picker.hpp"

namespace gs::app {
namespace {

auto fixed(const char * text) {
    return [text] { return std::string(text); };
}

constexpr std::string_view kPanelsKey = "panels.open";

// Beside its blob: one version key for every front-end's blob, in the one
// settings file, would make each front-end discard the other's layout, and
// accept a stale blob beside a version the other one wrote.
std::string layout_version_key(std::string_view key) {
    return std::format("{}.version", key);
}

}  // namespace

App::App(std::unique_ptr<SolverBackend> backend,
         std::unique_ptr<SettingsStore> settings)
    : settings_(settings ? std::move(settings)
                         : std::make_unique<NullSettingsStore>())
    , session_(std::move(backend)) {
    panels_open_.fill(true);
    if(const std::optional<std::string> open = settings_->read(kPanelsKey)) {
        panels_open_.fill(false);
        for(const auto key : std::views::split(*open, ','))
            if(const std::optional<PanelId> id =
                   find_panel(std::string_view(key)))
                panels_open_[static_cast<std::size_t>(*id)] = true;
    }
    document_link_ =
        doc_.changed().connect([this](Changes c) { changed_.emit(c); });
    selection_link_ =
        selection_.changed().connect([this](Changes c) { changed_.emit(c); });
    canvas_link_ =
        canvas_.changed().connect([this](Changes c) { changed_.emit(c); });
    visibility_link_ =
        visibility_.changed().connect([this](Changes c) { changed_.emit(c); });
    hot_link_ = hot_.changed().connect([this](Changes c) { changed_.emit(c); });
    sketch_link_ =
        sketch_.changed().connect([this](Changes c) { changed_.emit(c); });
    tools_link_ =
        tools_.changed().connect([this](Changes c) { changed_.emit(c); });
    provide_creation_tools(tools_);
    provide_constraint_tools(tools_);
    add_actions();
}

void App::add_actions() {
    auto has_instance = [this] { return doc_.has_instance(); };
    auto ask_open = [this] {
        unsaved_.ask([this] { path_dialog_.start(PathDialog::Kind::Open); });
    };
    auto can_start = [this] {
        return session_.connected() && doc_.complete() && !session_.running() &&
               !nothing_to_optimise(doc_);
    };
    actions_.add({.id = "file.save",
                  .label = fixed("Save"),
                  .chords = {{Key::S, Mods::Ctrl}},
                  .in_text = true,
                  .enabled = has_instance,
                  .run = [this] { doc_.save(); }});
    actions_.add({.id = "file.open",
                  .label = fixed("Open..."),
                  .chords = {{Key::O, Mods::Ctrl}},
                  .in_text = true,
                  .run = ask_open});
    actions_.add({.id = "file.reload",
                  .label = fixed("Reload"),
                  .chords = {{Key::R, Mods::Ctrl}},
                  .in_text = true,
                  .enabled = has_instance,
                  .run = [this] { unsaved_.ask([this] { doc_.reload(); }); }});
    actions_.add(
        {.id = "file.quit",
         .label = fixed("Quit"),
         .chords = {{Key::Q, Mods::Ctrl}},
         .in_text = true,
         .run = [this] { unsaved_.ask([this] { quit_ok_ = true; }); }});
    // A sketch drag is released before undo() and redo(), which cannot see
    // it: undo() alone would take back the step before the drag, and the
    // drag, of an older sketch generation then, would be dropped.
    auto dragging = [this] { return doc_.in_gesture() || sketch_.dragging(); };
    actions_.add(
        {.id = "edit.redo",
         .label =
             [this] {
                 const std::string_view step = doc_.redo_label();
                 return step.empty() ? std::string("Redo")
                                     : std::format("Redo {}", step);
             },
         .chords = {{Key::Z, Mods::Ctrl | Mods::Shift}, {Key::Y, Mods::Ctrl}},
         .in_popup = false,
         .enabled = [this, dragging] { return doc_.can_redo() || dragging(); },
         .run =
             [this] {
                 sketch_.end();
                 doc_.redo();
             }});
    actions_.add(
        {.id = "edit.undo",
         .label =
             [this] {
                 const std::string_view step = doc_.undo_label();
                 return step.empty() ? std::string("Undo")
                                     : std::format("Undo {}", step);
             },
         .chords = {{Key::Z, Mods::Ctrl}},
         .in_popup = false,
         // Also during a drag with an empty history: undo() ends the gesture
         // first, which records the step it then takes back, and redo()
         // ends it too.
         .enabled = [this, dragging] { return doc_.can_undo() || dragging(); },
         .run =
             [this] {
                 sketch_.end();
                 doc_.undo();
             }});
    actions_.add({.id = "edit.delete",
                  .label = fixed("Delete selection"),
                  .chords = {{Key::Delete}},
                  .in_popup = false,
                  .enabled = [this] { return !selection_.empty(); },
                  .run =
                      [this] {
                          // A sketch drag held under the key is released
                          // first, as by Undo: its commit at the release
                          // would drop the plan the dialog shows, and a
                          // deletion applied under it would drop the drag.
                          sketch_.end();
                          // Ctrl+Z of the same frame ran before: plan on the
                          // selection it left.
                          sync();
                          request_delete();
                      }});
    actions_.add(
        {.id = "file.save_as",
         .label = fixed("Save As..."),
         .enabled = has_instance,
         .run = [this] { path_dialog_.start(PathDialog::Kind::SaveAs); }});
    actions_.add({.id = "edit.reset_values",
                  .label = fixed("Reset design values"),
                  .enabled = [this] { return doc_.model() != nullptr; },
                  .run = [this] { doc_.reset_values(); }});
    actions_.add({.id = "edit.rename",
                  .label = fixed("Rename"),
                  .chords = {{Key::F2}},
                  .in_popup = false,
                  .enabled =
                      [this] {
                          if(selection_.size() != 1 || !doc_.has_instance())
                              return false;
                          const SelKey & key = selection_.keys().front();
                          return key.kind != SelKey::Kind::Display &&
                                 !entry_paths(*doc_.instance(), key).empty();
                      },
                  .run =
                      [this] {
                          const SelKey & key = selection_.keys().front();
                          const PanelId panel =
                              focused_ && panels_.shows(*focused_, key)
                                  ? *focused_
                                  : PanelId::Properties;
                          rename_ =
                              RenameRequest{key, panel, doc_.path_generation()};
                          if(panel == PanelId::Properties)
                              focus_panel(PanelId::Properties);
                      }});
    actions_.add({.id = "edit.clear_selection",
                  .label = fixed("Clear selection"),
                  .in_popup = false,
                  .enabled = [this] { return !selection_.empty(); },
                  .run = [this] { selection_.clear(); }});
    // Esc's only action: a second one bound to Esc would take a second step
    // of the chain in the same press.
    actions_.add({.id = "app.escape",
                  .label = fixed("Escape"),
                  .chords = {{Key::Escape}},
                  .in_field = true,
                  .in_popup = false,
                  .enabled = [this] { return escapable(); },
                  .run = [this] { escape(); }});
    // An eye of the selection that is open, or closed, when `open`.
    auto any_eye = [this](bool open) {
        return std::ranges::any_of(selection_.keys(), [&](const SelKey & k) {
            const std::optional<SelKey> eye = eye_of(doc_, k);
            return eye && eye_open(doc_, visibility_, *eye) == open;
        });
    };
    actions_.add({.id = "view.hide",
                  .label = fixed("Hide"),
                  .enabled = [any_eye] { return any_eye(true); },
                  .run =
                      [this] {
                          set_eyes(doc_, visibility_, selection_.keys(), false);
                      }});
    actions_.add(
        {.id = "view.show",
         .label = fixed("Show"),
         .enabled = [any_eye] { return any_eye(false); },
         .run =
             [this] { set_eyes(doc_, visibility_, selection_.keys(), true); }});
    actions_.add(
        {.id = "view.remove_drawing",
         .label = fixed("Remove drawing"),
         .enabled =
             [this] {
                 return drawn_model() &&
                        !drawings_of(*doc_.model(), selection_.keys()).empty();
             },
         .run = [this] { remove_drawings(selection_.keys()); }});
    actions_.add({.id = "quantity.draw",
                  .label = fixed("Draw as display item"),
                  .enabled =
                      [this] {
                          return drawn_model() &&
                                 !draw_batch(*doc_.instance(), *doc_.model(),
                                             selection_.keys())
                                      .ops.empty();
                      },
                  .run =
                      [this] {
                          doc_.apply(draw_batch(*doc_.instance(), *doc_.model(),
                                                selection_.keys()));
                      }});
    auto has_kind = [this](std::initializer_list<SelKey::Kind> kinds) {
        return std::ranges::any_of(selection_.keys(), [&](const SelKey & k) {
            return std::ranges::contains(kinds, k.kind);
        });
    };
    using Kind = SelKey::Kind;
    actions_.add({.id = "selection.associated",
                  .label = fixed("Select associated constraints and criteria"),
                  .enabled =
                      [has_kind] {
                          return has_kind({Kind::Param, Kind::Design,
                                           Kind::Sweep, Kind::Let,
                                           Kind::Geometry, Kind::Display,
                                           Kind::Element, Kind::Body});
                      },
                  .run =
                      [this] {
                          selection_.assign(associated_items(
                              *doc_.instance(), selection_.keys()));
                      }});
    actions_.add({.id = "selection.referenced",
                  .label = fixed("Select referenced entries"),
                  .enabled =
                      [has_kind] {
                          return has_kind({Kind::Let, Kind::Constraint,
                                           Kind::Criterion, Kind::Display});
                      },
                  .run =
                      [this] {
                          selection_.assign(referenced_entries(
                              *doc_.instance(), selection_.keys()));
                      }});
    // Applies a batch built from the selection; the action is enabled when
    // the batch would change something.
    auto batch_action = [this](const char * id, const char * label, auto make) {
        actions_.add({.id = id,
                      .label = fixed(label),
                      .enabled =
                          [this, make] {
                              return doc_.has_instance() && !make().ops.empty();
                          },
                      .run = [this, make] { doc_.apply(make()); }});
    };
    batch_action("item.enable", "Enable", [this] {
        return enable_batch(*doc_.instance(), selection_.keys(), true);
    });
    batch_action("item.disable", "Disable", [this] {
        return enable_batch(*doc_.instance(), selection_.keys(), false);
    });
    batch_action("design.release", "Release", [this] {
        return fix_batch(*doc_.instance(), selection_.keys(), false);
    });
    // One Fix for design variables and sketch entries alike: two entries
    // of one label in a menu would not say which is which.
    batch_action("design.fix", "Fix", [this] {
        EditBatch batch = fix_batch(*doc_.instance(), selection_.keys(), true);
        EditBatch sketch = fix_sketch(doc_, selection_.keys(), true).batch;
        if(batch.ops.empty()) return sketch;
        if(!sketch.ops.empty()) {
            batch.ops.insert(batch.ops.end(), sketch.ops.begin(),
                             sketch.ops.end());
            // Each fixes one entry with one operation.
            batch.label = std::format("fix {} entries", batch.ops.size());
        }
        return batch;
    });
    add_sketch_actions();
    actions_.add({.id = "view.fit_all",
                  .label = fixed("Fit all"),
                  .chords = {{Key::F}},
                  .exact_mods = true,
                  .scope = KeyScope::Canvas,
                  .repeat = true,
                  .run = [this] { canvas_.fit_all(); }});
    actions_.add({.id = "view.fit_design",
                  .label = fixed("Fit design"),
                  .chords = {{Key::G}},
                  .exact_mods = true,
                  .scope = KeyScope::Canvas,
                  .repeat = true,
                  .run = [this] { canvas_.fit_design(); }});
    actions_.add({.id = "view.fit_selection",
                  .label = fixed("Fit selection"),
                  .enabled = [any_eye] { return any_eye(true); },
                  .run = [this] { canvas_.fit_selection(); }});
    const struct {
        const char * id;
        const char * label;
        bool CanvasOptions::*flag;
    } toggles[] = {
        {"view.grid", "Grid", &CanvasOptions::grid},
        {"view.geometry", "Constant geometry", &CanvasOptions::geometry},
        {"view.labels", "Labels", &CanvasOptions::labels},
        {"view.domains", "Design domains", &CanvasOptions::domains},
        {"view.handles", "Design handles", &CanvasOptions::handles},
        {"view.ghosts", "Ghosts", &CanvasOptions::ghosts},
        {"view.traces", "Traces", &CanvasOptions::traces},
    };
    for(const auto & t : toggles)
        actions_.add(
            {.id = t.id,
             .label = fixed(t.label),
             .checked = [this,
                         flag = t.flag] { return canvas_.options().*flag; },
             .run =
                 [this, flag = t.flag] {
                     CanvasOptions options = canvas_.options();
                     options.*flag = !(options.*flag);
                     canvas_.set_options(options);
                 }});
    std::vector<std::string> panel_entries;
    for(const PanelInfo & p : all_panels()) {
        panel_entries.push_back(std::format("view.panel.{}", p.key));
        actions_.add(
            {.id = panel_entries.back(),
             .label = [label = std::string(p.label)] { return label; },
             .checked = [this, id = p.id] { return panel_open(id); },
             .run = [this,
                     id = p.id] { set_panel_open(id, !panel_open(id)); }});
    }
    actions_.add({.id = "view.reset_layout",
                  .label = fixed("Reset layout"),
                  .run = [this] {
                      layout_reset_ = true;
                      open_all_panels();
                  }});
    // The Adds of the panels' toolbars.
    auto add_action = [&](const char * id, const char * label,
                          void (App::*add)()) {
        actions_.add({.id = id,
                      .label = fixed(label),
                      .enabled = has_instance,
                      .run = [this, add] { (this->*add)(); }});
    };
    add_action("param.add", "Add param", &App::add_param);
    add_action("design.add_point", "Add point", &App::add_point);
    add_action("design.add_scalar", "Add scalar", &App::add_scalar);
    add_action("range.add", "Add range", &App::add_range);
    add_action("display.add", "Add display item", &App::add_display_item);
    // The formula pop-up's: the label of Add constraint is what it would
    // add for the selection, and its toolbar button keeps a short caption.
    // On the sketch's entries, the bound or the clearance of plan 4.6.
    auto prefill = [this] {
        const Prefill p = sketch_prefills(sketch_operands()).bound;
        return p.text.empty() ? constraint_prefill(doc_, selection_.keys()) : p;
    };
    actions_.add(
        {.id = "formula.add_constraint",
         .label =
             [prefill] {
                 const Prefill p = prefill();
                 if(p.text.empty()) return std::string("Add constraint...");
                 return prefill_label(p) +
                        (p.forall.empty() ? "" : " for all " + p.forall);
             },
         .caption = "Add constraint...",
         .enabled = has_instance,
         .run = [this, prefill] { formula_.add_constraint(prefill()); }});
    // The other bounds of plan 4.6 on two points or a line of the sketch,
    // the user's D4 examples.
    for(const auto & [id, fallback, part] :
        {std::tuple{"formula.add_range", "Add range...",
                    &SketchPrefills::range},
         std::tuple{"formula.add_order_x", "Add order along x...",
                    &SketchPrefills::order_x},
         std::tuple{"formula.add_order_y", "Add order along y...",
                    &SketchPrefills::order_y}}) {
        auto bound = [this, part] {
            return sketch_prefills(sketch_operands()).*part;
        };
        actions_.add(
            {.id = id,
             .label =
                 [bound, fallback] {
                     const Prefill p = bound();
                     return p.text.empty() ? std::string(fallback)
                                           : prefill_label(p);
                 },
             .enabled = [bound] { return !bound().text.empty(); },
             .run = [this, bound] { formula_.add_constraint(bound()); }});
    }
    actions_.add({.id = "formula.add_criterion",
                  .label = fixed("Add criterion..."),
                  .enabled = has_instance,
                  .run = [this] { formula_.add_criterion(); }});
    actions_.add({.id = "formula.add_quantity",
                  .label = fixed("Add let..."),
                  .enabled = has_instance,
                  .run = [this] { formula_.add_quantity(); }});
    // One selected let, constraint or criterion of the document's own.
    auto formula_key = [this]() -> std::optional<SelKey> {
        if(selection_.size() != 1 || !doc_.has_instance()) return std::nullopt;
        const SelKey & k = selection_.keys().front();
        const bool formula = k.kind == SelKey::Kind::Let ||
                             k.kind == SelKey::Kind::Constraint ||
                             k.kind == SelKey::Kind::Criterion;
        if(!formula || entry_paths(*doc_.instance(), k).empty())
            return std::nullopt;
        return k;
    };
    actions_.add(
        {.id = "formula.edit",
         .label = fixed("Edit formula..."),
         .enabled = [formula_key] { return formula_key().has_value(); },
         .run = [this, formula_key] { formula_.edit(*formula_key()); }});
    actions_.add({.id = "list.activate",
                  .label = fixed("Edit in Properties"),
                  .chords = {{Key::Enter}},
                  .scope = KeyScope::Panel,
                  .in_popup = false,
                  .enabled =
                      [this] {
                          return focused_ && panels_.current(*focused_) &&
                                 !tool_has_keys();
                      },
                  .run =
                      [this] {
                          panels_.activate(*focused_,
                                           RowId{*panels_.current(*focused_)},
                                           std::nullopt);
                      }});
    for(const auto & [id, label, key, delta] :
        {std::tuple{"list.previous", "Select the previous row", Key::Up, -1},
         std::tuple{"list.next", "Select the next row", Key::Down, 1}})
        actions_.add(
            {.id = id,
             .label = fixed(label),
             .chords = {{key}},
             .scope = KeyScope::Panel,
             .repeat = true,
             .in_popup = false,
             .enabled =
                 [this] {
                     return focused_ && panels_.list(*focused_) != nullptr;
                 },
             .run = [this, d = delta] { panels_.step(*focused_, d); }});
    actions_.add({.id = "help.controls",
                  .label = fixed("Controls"),
                  .run = [this] { controls_.request(); }});
    actions_.add(
        {.id = "solver.solve",
         .label = fixed("Solve"),
         .enabled = can_start,
         .run = [this] { session_.start(doc_, SolveKind::Multistart); }});
    actions_.add({.id = "solver.polish",
                  .label = fixed("Polish"),
                  .enabled = can_start,
                  .run = [this] { session_.start(doc_, SolveKind::Polish); }});
    // In no menu: an entry there would change the Edit menu of every file.
    // The context menu lists it while it is enabled, so only for a sketch.
    actions_.add({.id = "sketch.solve",
                  .label = fixed("Solve sketch"),
                  .enabled =
                      [this] {
                          return doc_.sketch_model() != nullptr &&
                                 !doc_.sketch_stale();
                      },
                  .run =
                      [this] {
                          EditBatch batch("solve sketch");
                          batch.solve_sketch = true;
                          doc_.apply(std::move(batch));
                      }});
    actions_.add({.id = "solver.stop",
                  .label = fixed("Stop"),
                  .enabled = [this] { return session_.running(); },
                  .run = [this] { session_.request_stop(); }});
    add_tool_actions();

    menus_ = {
        {"File",
         {"file.open", "file.reload", "file.save", "file.save_as", "",
          "file.quit"}},
        {"Edit",
         {"edit.undo", "edit.redo", "", "edit.delete", "edit.rename",
          "edit.clear_selection", "", "edit.reset_values"}},
        {"View",
         {"view.fit_all", "view.fit_design", "view.fit_selection", "",
          "view.hide", "view.show", "", "view.grid", "view.geometry",
          "view.labels", "view.domains", "view.handles", "view.ghosts",
          "view.traces", "", "Panels", "view.reset_layout"},
         {{"Panels", std::move(panel_entries)}}},
        {"Help", {"help.controls"}},
    };
}

std::optional<SelKey> App::take_rename_request(PanelId panel) {
    if(!rename_ || rename_->panel != panel) return std::nullopt;
    const RenameRequest r = *std::exchange(rename_, std::nullopt);
    if(r.paths != doc_.path_generation() ||
       selection_.keys() != std::vector<SelKey>{r.key})
        return std::nullopt;
    return r.key;
}

void App::request_field_focus(std::string path) {
    field_focus_ = FieldFocus{std::move(path), doc_.path_generation()};
}

bool App::take_field_focus(std::string_view path) {
    if(field_focus_ && field_focus_->paths != doc_.path_generation())
        field_focus_.reset();
    if(!field_focus_ || field_focus_->path != path) return false;
    field_focus_.reset();
    return true;
}

void App::add_entry(EditBatch batch, const SelKey & key) {
    std::string err;
    doc_.apply(std::move(batch), &err);
    if(err.empty()) show_added(key, true);
}

void App::show_added(const SelKey & key, bool rename) {
    selection_.set(key);
    selection_.reveal(key);
    if(key.kind == SelKey::Kind::Display) return;
    PanelId panel = home_panel(doc_, key);
    if(focused_ && panels_.shows(*focused_, key)) panel = *focused_;
    panels_.unhide(panel, key);
    if(focused_ != panel) focus_panel(panel);
    if(rename) rename_ = RenameRequest{key, panel, doc_.path_generation()};
}

void App::add_param() {
    const std::string name = unused_name(*doc_.instance(), "p");
    add_entry(EditBatch("add param " + name).set("params." + name, 0),
              {SelKey::Kind::Param, name});
}

void App::add_point() {
    const std::string name = unused_name(*doc_.instance(), "P");
    const View2d & view = canvas_.view();
    const Vec2d lo = view.to_world({view.ox, view.oy + view.h});
    const Vec2d hi = view.to_world({view.ox + view.w, view.oy});
    Json point = Json::object();
    point["type"] = "point";
    point["domain"] = std::format("box({}, {}, {}, {})", format_number(lo.x),
                                  format_number(lo.y), format_number(hi.x),
                                  format_number(hi.y));
    point["value"] = Json::array({(lo.x + hi.x) / 2, (lo.y + hi.y) / 2});
    add_entry(EditBatch("add point " + name).set("design." + name, point),
              {SelKey::Kind::Design, name});
}

void App::add_scalar() {
    const std::string name = unused_name(*doc_.instance(), "s");
    add_entry(EditBatch("add scalar " + name)
                  .set("design." + name, Json{{"type", "scalar"},
                                              {"min", 0},
                                              {"max", 1},
                                              {"value", 0.5}}),
              {SelKey::Kind::Design, name});
}

void App::add_range() {
    const std::string name = unused_name(*doc_.instance(), "t");
    add_entry(EditBatch("add range " + name)
                  .set("sweeps." + name, Json{{"min", 0}, {"max", 1}}),
              {SelKey::Kind::Sweep, name});
}

void App::add_display_item() {
    const Json * display = doc_.instance()->get("display");
    const int n =
        display && display->is_array() ? static_cast<int>(display->size()) : 0;
    Json item = Json::object();
    const std::vector<SelKey> & keys = selection_.keys();
    item["expr"] = keys.size() == 1 && eye_of(doc_, keys[0]) &&
                           keys[0].kind != SelKey::Kind::Display
                       ? keys[0].name
                       : std::string("vec(0, 0)");
    item["color"] = "#ffffff";
    add_entry(EditBatch("add display item")
                  .set(std::format("display[{}]", n), std::move(item)),
              {SelKey::Kind::Display, {}, n});
}

Menu App::context_actions() const {
    static const std::vector<std::vector<std::string_view>> groups = {
        {"formula.add_constraint", "formula.add_range", "formula.add_order_x",
         "formula.add_order_y", "formula.add_criterion", "formula.edit"},
        {"edit.rename", "edit.delete"},
        {"view.hide", "view.show", "view.remove_drawing", "quantity.draw",
         "view.fit_selection"},
        {"item.enable", "item.disable", "design.release", "design.fix",
         "sketch.unfix", "sketch.driving", "sketch.reference",
         "sketch.flip_side"},
        {"sketch.merge", "sketch.detach", "sketch.delete_vertex"},
        {"selection.associated", "selection.referenced",
         "edit.clear_selection"},
        {"sketch.solve"},
    };
    Menu menu;
    for(const std::vector<std::string_view> & group : groups) {
        bool first = true;
        for(const std::string_view id : group) {
            if(!actions_.find(id)->is_enabled()) continue;
            if(first && !menu.entries.empty()) menu.entries.emplace_back();
            first = false;
            menu.entries.emplace_back(id);
        }
    }
    return menu;
}

bool App::drawn_model() const { return doc_.model() && !doc_.stale(); }

std::vector<SketchOperand> App::sketch_operands() const {
    using K = SketchOperand::Kind;
    const std::vector<SelKey> & keys = selection_.keys();
    // sketch_prefills() takes one entry or two: more would build a Picker
    // for none, in each frame that reads Add constraint's label.
    if(keys.empty() || keys.size() > 2 || !doc_.has_instance() ||
       !doc_.has_sketch() || std::ranges::any_of(keys, [](const SelKey & k) {
           return k.kind != SelKey::Kind::Element;
       }))
        return {};
    const Scene & scene = scene_now();
    if(!scene.model) return {};
    const Picker picker(scene, canvas_.shown(scene));
    std::vector<SketchOperand> out;
    for(const SelKey & k : keys) {
        if(const std::optional<ToolPick> p = picker.of(k)) {
            if(p->kind == ToolPick::Kind::Body) return {};
            out.push_back({.kind = p->kind == ToolPick::Kind::Vertex ? K::Point
                                   : p->kind == ToolPick::Kind::Line ? K::Line
                                                                     : K::Shape,
                           .ref = p->ref,
                           .at = p->at});
            continue;
        }
        if(!k.sub.empty() || entry_paths(*doc_.instance(), k).empty())
            return {};
        out.push_back({.kind = K::Shape, .ref = k.name});
    }
    return out;
}

bool App::remove_drawings(std::span<const SelKey> keys) {
    if(!drawn_model()) return false;
    const Model & model = *doc_.model();
    const std::vector<int> items = drawings_of(model, keys);
    if(items.empty()) return false;
    EditBatch batch(std::format("remove {} drawings", items.size()));
    for(const int i : items) batch.erase(std::format("display[{}]", i));
    const std::vector<SelKey> drawn = display_keys(model);
    for(std::size_t j = 0; j < drawn.size() && items.size() == 1; ++j)
        if(display_index(model.display()[j]) == items[0])
            batch.label = "remove drawing of " + drawn[j].name;
    std::string err;
    doc_.apply(std::move(batch), &err);
    if(!err.empty()) return false;
    for(const int i : items) display_erased(i);
    return true;
}

void App::context_click(const std::optional<SelKey> & key, Vec2d at) {
    if(key && !selection_.contains(*key)) selection_.set(*key);
    context_menu_ = at;
}

std::optional<Vec2d> App::take_context_menu() {
    return std::exchange(context_menu_, std::nullopt);
}

void App::reveal(const SelKey & key) {
    const PanelId home = home_panel(doc_, key);
    panels_.unhide(home, key);
    selection_.reveal(key);
    focus_panel(home);
    panels_.focus_properties(key);
}

void App::set_panel_open(PanelId id, bool open) {
    std::array<bool, kPanelCount> panels = panels_open_;
    panels[static_cast<std::size_t>(id)] = open;
    set_panels_open(panels);
}

void App::set_panels_open(const std::array<bool, kPanelCount> & open) {
    if(open == panels_open_) return;
    panels_open_ = open;
    std::string keys;
    for(const PanelInfo & p : all_panels())
        if(panel_open(p.id)) {
            if(!keys.empty()) keys += ',';
            keys += p.key;
        }
    settings_->write(kPanelsKey, keys);
    changed_.emit(Change::Panels);
}

void App::open_all_panels() {
    std::array<bool, kPanelCount> all;
    all.fill(true);
    set_panels_open(all);
}

void App::focus_panel(PanelId id) {
    set_panel_open(id, true);
    std::erase(panel_focus_, id);
    panel_focus_.push_back(id);
}

std::optional<PanelId> App::take_panel_focus() {
    if(panel_focus_.empty()) return std::nullopt;
    const PanelId id = panel_focus_.front();
    panel_focus_.erase(panel_focus_.begin());
    return id;
}

bool App::take_layout_reset() { return std::exchange(layout_reset_, false); }

std::optional<std::string> App::load_layout(std::string_view key, int version) {
    std::optional<std::string> blob;
    if(settings_->read(layout_version_key(key)) == std::to_string(version))
        blob = settings_->read(key);
    if(!blob) open_all_panels();
    return blob;
}

void App::save_layout(std::string_view key, int version,
                      std::string_view blob) {
    // The version last: a run stopped between the two writes must find a
    // version that does not match, not a matching one beside an older blob.
    settings_->write(key, blob);
    settings_->write(layout_version_key(key), std::to_string(version));
}

bool App::open(const std::filesystem::path & file) {
    const bool ok = doc_.load(file);
    if(ok) {
        visibility_.follow(doc_);
        canvas_.fit_extent();
    }
    return ok;
}

std::string App::apply_edits(
    std::span<const std::pair<std::string, std::string>> edits) {
    EditBatch batch("command-line edits");
    for(const auto & [path, text] : edits) {
        Json value = Json::parse(text, nullptr, false);
        if(value.is_discarded()) value = text;
        if(value.is_null())
            batch.erase(path);
        else
            batch.set(path, std::move(value));
    }
    std::string err;
    if(doc_.apply(std::move(batch), &err)) return "compiled";
    if(!err.empty()) return err;
    if(doc_.model() && !doc_.stale())
        return std::format("compiled, {} item(s) excluded: {}",
                           doc_.excluded().size(), excluded_labels(doc_));
    return "does not compile";
}

void App::sync() {
    // Before follow(), which would drop a renamed entry from the selection.
    // An unnamed key is skipped: follow() drops it after a restore anyway,
    // and renaming to "" would leave a key that names nothing.
    if(restores_ != doc_.restore_generation()) {
        restores_ = doc_.restore_generation();
        for(const Document::Renamed & r : doc_.restored_renames())
            if(const std::optional<SelKey> key =
                   entry_key(*doc_.instance(), r.entry);
               key && !r.from.empty() && !r.to.empty())
                entry_renamed({key->kind, r.from}, r.to);
    }
    selection_.follow(doc_);
    visibility_.follow(doc_);
}

void App::entry_renamed(const SelKey & from, std::string_view to) {
    using Kind = SelKey::Kind;
    std::string name(to);
    if(const std::size_t dot = from.name.find('.');
       from.kind == Kind::Element && dot != std::string::npos &&
       name.find('.') == std::string::npos)
        name = from.name.substr(0, dot + 1) + name;
    selection_.renamed(from, name);
    if(from.kind == Kind::Design || from.kind == Kind::Let ||
       from.kind == Kind::Geometry || from.kind == Kind::Element ||
       from.kind == Kind::Body)
        visibility_.renamed(from.name, name);
    if(from.kind == Kind::Constraint && !from.name.empty())
        visibility_.glyph_renamed(from.name, name);
    hot_.renamed(from, name);
    if(from.kind != Kind::Body) return;
    const std::string before = from.name + ".", after = name + ".";
    selection_.renamed_prefix(Kind::Element, before, after);
    visibility_.renamed_prefix(before, after);
    hot_.renamed_prefix(Kind::Element, before, after);
}

void App::add_sketch_actions() {
    using Kind = SelKey::Kind;
    // The JSON type of a selected element, "" for anything else.
    auto type_of = [this](const SelKey & k) {
        if(k.kind != Kind::Element || !doc_.has_instance())
            return std::string();
        const std::vector<std::string> paths = entry_paths(*doc_.instance(), k);
        const Json * e =
            paths.empty() ? nullptr : doc_.instance()->get(paths.front());
        const Json * t = e && e->is_object() && e->contains("type")
                             ? &(*e)["type"]
                             : nullptr;
        return t && t->is_string() ? t->get<std::string>() : std::string();
    };
    auto point = [type_of](const SelKey & k) {
        return k.sub.empty() && type_of(k) == "point";
    };
    // Each is enabled by the kind of the selection, and says in the notice
    // why it does not apply to the selection it has, as the constraint tools
    // will in their select-first mode.
    auto edit_action = [this](const char * id, const char * label,
                              std::function<bool()> enabled,
                              std::function<SketchEdit()> make) {
        actions_.add({.id = id,
                      .label = [label] { return std::string(label); },
                      .enabled =
                          [this, enabled] {
                              return doc_.has_instance() && doc_.has_sketch() &&
                                     enabled();
                          },
                      .run = [this, make] { apply_sketch_edit(make()); }});
    };
    edit_action(
        "sketch.merge", "Merge points",
        [this, point] {
            return selection_.size() == 2 &&
                   std::ranges::all_of(selection_.keys(), point);
        },
        [this] { return merge_points(doc_, selection_.keys()); });
    edit_action(
        "sketch.detach", "Detach",
        [this, point] {
            return std::ranges::any_of(
                selection_.keys(), [&](const SelKey & k) {
                    return point(k) ||
                           slot_path(*doc_.instance(), k).has_value();
                });
        },
        [this] { return detach(doc_, selection_.keys()); });
    edit_action(
        "sketch.delete_vertex", "Delete vertex",
        [this, point] {
            auto in_chain = [&](const SelKey & k) {
                if(k.sub.starts_with("points "))
                    return slot_path(*doc_.instance(), k).has_value();
                if(!point(k)) return false;
                // A polyline or polygon of the point's frame that names it.
                const std::size_t dot = k.name.find('.');
                const Json * map = doc_.instance()->get(
                    dot == std::string::npos
                        ? std::string("sketch.elements")
                        : std::format("sketch.bodies.{}.elements",
                                      k.name.substr(0, dot)));
                if(!map || !map->is_object()) return false;
                return std::ranges::any_of(map->items(), [&](const auto & e) {
                    const Json & j = e.value();
                    if(!j.is_object() || !j.contains("points") ||
                       !j["points"].is_array())
                        return false;
                    return std::ranges::any_of(
                        j["points"], [&](const Json & v) {
                            if(!v.is_string()) return false;
                            const std::string & t =
                                v.get_ref<const std::string &>();
                            const std::size_t b = t.find_first_not_of(' ');
                            return b != std::string::npos &&
                                   t.substr(b, t.find_last_not_of(' ') + 1 -
                                                   b) == k.name;
                        });
                });
            };
            return std::ranges::any_of(selection_.keys(), in_chain);
        },
        [this] { return delete_vertex(doc_, selection_.keys()); });
    auto fixed_sketch = [this, type_of](bool fixed) {
        return std::ranges::any_of(selection_.keys(), [&](const SelKey & k) {
            if((k.kind != Kind::Element && k.kind != Kind::Body) ||
               !k.sub.empty())
                return false;
            const std::string type = k.kind == Kind::Body ? "body" : type_of(k);
            if(type != "point" && type != "circle" && type != "arc" &&
               type != "body")
                return false;
            const std::vector<std::string> paths =
                entry_paths(*doc_.instance(), k);
            const Json * f =
                paths.empty() ? nullptr
                              : doc_.instance()->get(paths.front() + ".fixed");
            return (f && *f == true) == fixed;
        });
    };
    edit_action(
        "sketch.unfix", "Unfix", [fixed_sketch] { return fixed_sketch(true); },
        [this] { return fix_sketch(doc_, selection_.keys(), false); });
    // A constraint of the selection that is driving when `want`, a reference
    // otherwise; a driving one counts only when it is a dimension. Any
    // reference counts: a file may write "driving": false on a predicate,
    // which Make driving must still undo.
    auto driving = [this](bool want) {
        return std::ranges::any_of(selection_.keys(), [&](const SelKey & k) {
            if(k.kind != Kind::Constraint) return false;
            for(const std::string & base : entry_paths(*doc_.instance(), k)) {
                const Json * d = doc_.instance()->get(base + ".driving");
                if(!(d && *d == false) == want &&
                   (!want || dimension_constraint(*doc_.instance(), base)))
                    return true;
            }
            return false;
        });
    };
    edit_action(
        "sketch.driving", "Make driving", [driving] { return driving(false); },
        [this] { return set_driving(doc_, selection_.keys(), true); });
    edit_action(
        "sketch.reference", "Make reference",
        [driving] { return driving(true); },
        [this] { return set_driving(doc_, selection_.keys(), false); });
    edit_action(
        "sketch.flip_side", "Flip side",
        [this] {
            return std::ranges::any_of(
                selection_.keys(), [&](const SelKey & k) {
                    if(k.kind != Kind::Constraint) return false;
                    for(const std::string & base :
                        entry_paths(*doc_.instance(), k))
                        if(flippable(*doc_.instance(), base)) return true;
                    return false;
                });
        },
        [this] { return flip_side(doc_, selection_.keys()); });
}

std::optional<App::Notice> App::notice() const {
    if(!notice_ || notice_->selection != selection_.generation() ||
       notice_->compile != doc_.compile_generation())
        return std::nullopt;
    return notice_->notice;
}

void App::notify(Tone tone, std::string text) {
    doc_.log(
        tone == Tone::Warn ? LogEntry::Level::Warning : LogEntry::Level::Info,
        text);
    notice_ = HeldNotice{{tone, std::move(text)},
                         selection_.generation(),
                         doc_.compile_generation()};
}

bool App::apply_sketch_edit(SketchEdit edit, bool reveal) {
    if(!edit.refusal.empty()) {
        notify(Tone::Warn, std::move(edit.refusal));
        return false;
    }
    if(edit.batch.ops.empty()) return false;
    // An erased entry moves no path when it is an object's key: the drafts
    // of its fields must go by hand.
    for(const EditBatch::Op & op : edit.batch.ops)
        if(op.kind == EditBatch::Op::Kind::Erase)
            panels_.host.fields().forget(op.path);
    // A slot's key is a place in its element, which may hold another point
    // once the element is edited.
    std::vector<SelKey> keys = selection_.keys();
    std::erase_if(keys, [&](const SelKey & k) {
        return slot_sub(k) &&
               std::ranges::any_of(
                   edit.batch.ops, [&](const EditBatch::Op & op) {
                       const std::vector<std::string> paths =
                           entry_paths(*doc_.instance(), {k.kind, k.name});
                       return !paths.empty() &&
                              path_is_under(op.path, paths[0]);
                   });
    });
    std::string err;
    doc_.apply(std::move(edit.batch), &err);
    if(!err.empty()) {
        notify(Tone::Warn, std::move(err));
        return false;
    }
    if(edit.merged) {
        const auto & [gone, survivor] = *edit.merged;
        std::erase(keys, gone);
        hot_.renamed(gone, survivor.name);
    }
    const std::optional<SelKey> shown =
        reveal && !edit.created.empty() ? std::optional(edit.created.front())
                                        : std::nullopt;
    selection_.assign(edit.created.empty() ? std::move(keys)
                                           : std::move(edit.created));
    // Before the notice: it lasts while the selection stays, and the
    // entries the edit erased leave it here rather than at the next sync().
    sync();
    if(shown) selection_.reveal(*shown);
    if(reveal) tool_selection_ = selection_.generation();
    if(!edit.report.empty())
        notify(Tone::Normal, std::move(edit.report));
    else
        notice_.reset();
    return true;
}

bool App::escapable() const {
    return tools_.active() != nullptr || !selection_.empty();
}

KeyFocus App::key_focus(KeyScope scope, bool typing, bool popup) const {
    return {.scope = scope,
            .typing = typing,
            .popup = popup,
            .field = !typing && tools_.field_waiting()};
}

bool App::tool_has_keys() const {
    return tools_.active() != nullptr && (!focused_ || canvas_.hovered());
}

void App::escape() {
    if(!tools_.escape()) selection_.clear();
}

void App::add_tool_actions() {
    std::vector<Menu> groups = {
        {"Create", {}}, {"Constrain", {}}, {"Body", {}}};
    for(const ToolSpec & spec : tool_specs()) {
        const std::string id(spec.id);
        groups[static_cast<std::size_t>(spec.group)].entries.push_back(id);
        actions_.add(
            {.id = id,
             .label = [label = std::string(spec.label)] { return label; },
             .caption = std::string(spec.caption),
             .chords = {spec.chord},
             .exact_mods = true,
             .scope = KeyScope::Canvas,
             .in_popup = false,
             .enabled =
                 [this, id] {
                     return tools_.provided(id) && tools_.drawable();
                 },
             .checked =
                 [this, id] {
                     return tools_.active() && tools_.active()->id() == id;
                 },
             // A second press of the active tool's button or chord quits
             // it, as a toggle does, unless a constraint tool's selection
             // is new: then it starts again on it (finding 3), or a
             // select-first Smart Dimension would quit on its second
             // selection.
             .run =
                 [this, id, group = spec.group] {
                     const bool again =
                         group == ToolGroup::Constrain &&
                         !selection_from_tool() &&
                         std::ranges::any_of(
                             selection_.keys(), [](const SelKey & k) {
                                 return k.kind == SelKey::Kind::Element ||
                                        k.kind == SelKey::Kind::Body;
                             });
                     if(tools_.active() && tools_.active()->id() == id &&
                        !again)
                         tools_.cancel();
                     else
                         tools_.start(id);
                 },
             .tip =
                 [this, id, label = std::string(spec.label),
                  chord = chord_text(spec.chord)] {
                     if(tools_.provided(id) && !tools_.drawable())
                         return std::format(
                             "{} ({}): the document does not compile; fix "
                             "the errors listed in Messages first",
                             label, chord);
                     if(tools_.provided(id))
                         return std::format("{} ({})", label, chord);
                     return std::format("{} ({}): not available yet", label,
                                        chord);
                 }});
    }
    auto tool_keys = [this] { return tool_has_keys(); };
    const struct {
        const char * id;
        const char * label;
        Key key;
        void (ToolHost::*run)();
    } keys[] = {
        {"tool.finish", "Finish the shape", Key::Enter, &ToolHost::finish},
        {"tool.back", "Remove the last vertex", Key::Backspace,
         &ToolHost::back},
        {"tool.field", "Type a value", Key::Tab, &ToolHost::open_field},
        {"tool.method", "Next method", Key::M, &ToolHost::next_method},
    };
    for(const auto & k : keys)
        actions_.add(
            {.id = k.id,
             .label = fixed(k.label),
             .chords = {{k.key}},
             .exact_mods = true,
             .in_field = k.key != Key::M,
             .in_popup = false,
             .enabled = k.key == Key::M
                            ? std::function<bool()>([this, tool_keys] {
                                  return tool_keys() &&
                                         tools_.active()->methods().size() > 1;
                              })
                            : std::function<bool()>(tool_keys),
             .run = [this, run = k.run] { (tools_.*run)(); }});
    const struct {
        const char * id;
        const char * label;
        const char * tip;
        bool ToolOptions::*flag;
    } toggles[] = {
        {"tool.auto_constraints", "Auto constraints",
         "The creation tools propose the constraints the pointer suggests "
         "(horizontal, on a line...) and add them with the shape",
         &ToolOptions::auto_constraints},
        {"tool.helper", "Helper",
         "The creation tools draw helper elements, as construction geometry",
         &ToolOptions::helper},
        {"tool.grid_snap", "Grid snap",
         "The pointer snaps to the grid, shown or not",
         &ToolOptions::grid_snap},
    };
    Menu options{.title = "Options", .options = true};
    for(const auto & t : toggles) {
        options.entries.emplace_back(t.id);
        actions_.add(
            {.id = t.id,
             .label = fixed(t.label),
             .checked = [this,
                         flag = t.flag] { return tools_.options().*flag; },
             .run =
                 [this, flag = t.flag] {
                     ToolOptions o = tools_.options();
                     o.*flag = !(o.*flag);
                     tools_.set_options(o);
                 },
             .tip = fixed(t.tip)});
    }
    groups.push_back(std::move(options));
    toolbar_ = std::move(groups);
}

bool App::insert_vertex(const SelKey & element, int edge, Vec2d at) {
    return apply_sketch_edit(gs::app::insert_vertex(doc_, element, edge, at));
}

void App::display_erased(int index) {
    selection_.display_erased(index);
    hot_.display_erased(index);
}

void App::tick(double dt) {
    if(doc_.model()) {
        players_.resize(doc_.model()->sweeps().size());
        for(std::size_t k = 0; k < players_.size(); ++k)
            if(players_[k].playing)
                doc_.set_sweep_t(static_cast<int>(k),
                                 players_[k].advance(doc_.sweep_t()[k], dt));
    }
    if(session_.poll()) {
        session_.refresh();
        changed_.emit(Change::Solver);
    }
    sketch_.tick(dt);
    tools_.tick();
}

bool App::busy() const {
    return session_.running() || sketch_.busy() ||
           (doc_.model() &&
            std::ranges::any_of(players_, &SweepPlayer::playing));
}

void App::toggle_play(std::size_t k) {
    if(!doc_.model() || k >= doc_.model()->sweeps().size()) return;
    players_.resize(doc_.model()->sweeps().size());
    SweepPlayer & p = players_[k];
    p.playing = !p.playing;
    if(p.playing && p.mode == SweepPlayer::Mode::Once &&
       doc_.sweep_t()[k] >= 1.0)
        doc_.set_sweep_t(static_cast<int>(k), 0.0);
    changed_.emit(Change::Player);
}

const Scene & App::scene(bool interactive) {
    const std::uint64_t before = cache_.generation();
    const Scene & s = cache_.update(doc_, interactive);
    if(cache_.generation() != before) changed_.emit(Change::Scene);
    return s;
}

bool App::request_quit() {
    if(!doc_.dirty()) return true;
    unsaved_.ask([this] { quit_ok_ = true; });
    return false;
}

std::string App::window_title() const {
    if(!doc_.has_instance()) return "geomsolver";
    return std::format("geomsolver - {}{}", doc_.path().filename().string(),
                       doc_.dirty() ? " *" : "");
}

std::vector<App::StatusText> App::menu_bar_status() const {
    std::vector<StatusText> out;
    if(doc_.dirty()) out.push_back({Tone::Warn, "unsaved changes"});
    if(session_.running()) out.push_back({Tone::Active, "solving..."});
    return out;
}

}  // namespace gs::app

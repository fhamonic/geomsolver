#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/document_panel.hpp"
#include "gs/app/properties.hpp"
#include "gs/app/sheet.hpp"
#include "gs/app/sweep_bar.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

using K = SelKey::Kind;
using DK = DetailItem::Kind;
using Vec = std::array<double, 2>;

// A retained property grid in miniature, as a wxPropertyGrid binding would
// be: it caches a sheet's items and changes its cache only as the sheet
// contract says, on App::changed(), and after each operation it sends back
// it reads the sheet again, as an editor shows the value it was given. After
// every change its cache must equal a fresh read: a change the contract
// misses leaves a property the toolkit would show stale.
class FakePropertyGrid {
public:
    using Read = std::function<std::optional<DetailSheet>()>;
    FakePropertyGrid(App & app, Read read) : read_(std::move(read)) {
        link_ = app.changed().connect([this](Changes c) { changed(c); });
        sheet_ = read_();
    }

    int rebuilds = 0;   // every property made again
    int refreshes = 0;  // values set in place: an editor in use stays

    bool matches_fresh_read() const { return read_() == sheet_; }
    const std::optional<DetailSheet> & sheet() const { return sheet_; }
    const DetailItem & item(std::string_view id) const {
        const DetailItem * found = sheet_ ? sheet_->find(id) : nullptr;
        REQUIRE(found);
        return *found;
    }
    // After one of its own operations.
    template <class Op>
    bool edited(Op op) {
        const bool changed = op();
        refresh();
        return changed;
    }

private:
    static std::vector<std::string> ids(const std::optional<DetailSheet> & s) {
        std::vector<std::string> out;
        if(s)
            for(const DetailItem & i : s->items) out.push_back(i.id);
        return out;
    }

    void changed(Changes c) {
        if(c.has_any(Change::Selection | Change::Instance |
                     Change::Structure)) {
            sheet_ = read_();
            ++rebuilds;
        } else if(c.has_any(Change::Content | Change::Model | Change::Values |
                            Change::Sweep | Change::Visibility |
                            Change::Solver | Change::Scene | Change::Player)) {
            refresh();
        }
    }

    void refresh() {
        std::optional<DetailSheet> now = read_();
        if(!now || !sheet_ || now->key != sheet_->key ||
           ids(now) != ids(sheet_)) {
            sheet_ = std::move(now);
            ++rebuilds;
            return;
        }
        for(std::size_t i = 0; i < now->items.size(); ++i)
            sheet_->items[i] = std::move(now->items[i]);
        sheet_->title = now->title;
        ++refreshes;
    }

    Read read_;
    std::optional<DetailSheet> sheet_;
    Signal<Changes>::Connection link_;
};

// A sheet of every kind of item over design point A of the TV instance,
// bound to real keys: the item kinds no panel shows yet are checked through
// the same operations their panels will use.
class EveryItemSheet final : public SheetBuilder {
public:
    using SheetBuilder::SheetBuilder;

protected:
    std::optional<DetailSheet> build(const SelKey & key) override {
        const Document & doc = app().document();
        if(key != a_ || !doc.instance()->get("design.A")) return std::nullopt;
        const Json * fixed = doc.instance()->get("design.A.fixed");
        const Json * sampling = doc.instance()->get("sweeps.tau.sampling");
        const Model & m = *doc.model();
        const std::vector<double> at = doc.var_value(m.find_var("A"));
        const Json * width = doc.instance()->get("display[1].width");
        DetailSheet s;
        s.key = key;
        s.title = "design variable A";
        s.path = "design.A";
        auto add = [&](DetailItem item) { s.items.push_back(std::move(item)); };
        add({.kind = DK::Section, .id = "general", .label = "General"});
        add({.kind = DK::Name,
             .id = "name",
             .label = "Name",
             .field = {"design.A", FieldKind::Text},
             .text = "A"});
        add({.kind = DK::Field,
             .id = "domain",
             .label = "Domain",
             .field = {"design.A.domain", FieldKind::Expression}});
        add({.kind = DK::Field,
             .id = "clr",
             .label = "Clearance",
             .field = {"params.clr", FieldKind::Number}});
        add({.kind = DK::Value, .id = "state", .label = "State", .text = "ok"});
        add({.kind = DK::Note, .id = "note", .text = "A hint"});
        add({.kind = DK::Checkbox,
             .id = "released",
             .label = "Released",
             .field = {"design.A.fixed"},
             .value = !(fixed && fixed->is_boolean() && fixed->get<bool>())});
        add({.kind = DK::Choice,
             .id = "sampling",
             .label = "Sampling",
             .field = {"sweeps.tau.sampling"},
             .value = !sampling                 ? 0
                      : *sampling == "adaptive" ? 1
                      : *sampling == "fixed"    ? 2
                                                : -1,
             .choices = {"", "adaptive", "fixed"}});
        add({.kind = DK::Drag,
             .id = "width",
             .label = "Width",
             .field = {"display[1].width"},
             .value = width ? width->get<double>() : 1.0,
             .min = 0.5,
             .max = 10.0,
             .speed = 0.05});
        add({.kind = DK::Drag,
             .id = "position",
             .label = "Position",
             .field = {"design.A.value"},
             .value = Vec{at[0], at[1]},
             .speed = 0.001,
             .unit = "m"});
        add({.kind = DK::Slider,
             .id = "tau",
             .label = "tau",
             .value = doc.sweep_t()[0],
             .min = 0.0,
             .max = 1.0});
        add({.kind = DK::Colour,
             .id = "colour",
             .label = "Colour",
             .field = {"display[1].color"},
             .value = colour(doc)});
        add({.kind = DK::Button, .id = "reset", .label = "Reset values"});
        add({.kind = DK::Checkbox,
             .id = "locked",
             .label = "Fill",
             .field = {"display[0].fill"},
             .value = true,
             .read_only = true});
        return s;
    }

    bool write(const SelKey & key, const DetailItem & item,
               const DetailValue & value) override {
        Document & doc = app().document();
        if(item.id == "released") {
            doc.edit("design.A.fixed", !std::get<bool>(value));
            return true;
        }
        if(item.id == "tau") {
            doc.set_sweep_t(0, std::get<double>(value));
            return true;
        }
        return item.id != "position" && SheetBuilder::write(key, item, value);
    }
    bool run(const SelKey &, const DetailItem & item) override {
        if(item.id != "reset") return false;
        app().document().reset_values();
        return true;
    }
    // The position moves A in x while it is held: one Document gesture.
    void drag_started(const SelKey & key, const DetailItem & item) override {
        if(item.id == "position")
            app().document().begin_gesture("move A");
        else
            SheetBuilder::drag_started(key, item);
    }
    void dragged(const SelKey &, const DetailItem & item,
                 const DetailValue & value) override {
        Document & doc = app().document();
        if(item.id == "position") {
            const Vec v = std::get<Vec>(value);
            doc.set_var_value(doc.model()->find_var("A"), v);
        } else if(item.id == "tau") {
            doc.set_sweep_t(0, std::get<double>(value));
        }
    }
    bool drag_ended(const SelKey & key, const DetailItem * item,
                    const DetailValue & value) override {
        Document & doc = app().document();
        if(doc.in_gesture()) {
            const std::size_t steps = doc.undo_count();
            doc.end_gesture();
            return doc.undo_count() > steps;
        }
        return SheetBuilder::drag_ended(key, item, value);
    }

private:
    static Rgba colour(const Document & doc) {
        const Json * c = doc.instance()->get("display[1].color");
        return c && *c == "#ff0000" ? Rgba{1, 0, 0, 1} : Rgba{1, 1, 0, 1};
    }

    const SelKey a_{K::Design, "A"};
};

struct Rig {
    App app{nullptr};
    Document & doc = app.document();
    Selection & sel = app.selection();
    EveryItemSheet sheets{app};
    const SelKey a{K::Design, "A"};

    Rig() {
        if(!app.open(tv_file()) || !doc.model())
            FAIL("sheet adapter: the instance compiles");
        app.sync();
        sel.set(a);
    }
    FakePropertyGrid grid() {
        return FakePropertyGrid(app, [this]() -> std::optional<DetailSheet> {
            if(sel.size() != 1) return std::nullopt;
            return sheets.sheet(sel.keys().front());
        });
    }
    double num(const char * path) const {
        const Json * j = doc.instance()->get(path);
        return j && j->is_number() ? j->get<double>() : -1.0;
    }
    std::string at(const char * path) const {
        const Json * j = doc.instance()->get(path);
        return j ? json_text(*j) : std::string("-");
    }
};

}  // namespace

TEST_CASE("sheet adapter: the items, their ids and the keys they own") {
    Rig r;
    const std::optional<DetailSheet> s = r.sheets.sheet(r.a);
    REQUIRE(s);
    std::set<DK> kinds;
    std::set<std::string> ids;
    for(const DetailItem & i : s->items) {
        kinds.insert(i.kind);
        ids.insert(i.id);
    }
    CHECK_MESSAGE((kinds.size() == 11 && ids.size() == s->items.size()),
                  "every kind of item, each id once");
    CHECK_MESSAGE(
        (s->owned() ==
             std::vector<std::string>{"design.A.domain", "params.clr",
                                      "design.A.fixed", "sweeps.tau.sampling",
                                      "display[1].width", "design.A.value",
                                      "display[1].color", "display[0].fill"} &&
         s->first_field() == "design.A.domain"),
        "the keys of its fields, checkboxes, choices, drags and "
        "colours");
    CHECK_MESSAGE((s->find("domain")->text == "pivot_zone" &&
                   s->find("clr")->text == "0.02" &&
                   s->find("name")->refusal.empty() && !s->find("nothing")),
                  "a field shows its key's text");

    r.doc.edit("design.A.fixed", "yes");
    std::vector<std::string> owned = s->owned();
    FieldDrafts::Diagnostics ds =
        r.app.panels().host.fields().diagnostics(r.doc, "design.A", owned);
    auto raw = [&](const char * path) {
        return std::ranges::any_of(ds.items, [&](const FieldDrafts::Item & i) {
            return i.path == path && i.raw_editor;
        });
    };
    CHECK_MESSAGE((!ds.items.empty() && !raw("design.A.fixed")),
                  "an error at a checkbox's key: no second, raw editor");
    std::erase(owned, "design.A.fixed");
    ds = r.app.panels().host.fields().diagnostics(r.doc, "design.A", owned);
    CHECK(raw("design.A.fixed"));
}

TEST_CASE("sheet adapter: set() and press(), keyed by entry and item") {
    Rig r;
    FakePropertyGrid grid = r.grid();
    SheetBuilder & sheets = r.sheets;
    const SelKey & a = r.a;
    REQUIRE(grid.matches_fresh_read());
    std::size_t steps = r.doc.undo_count();

    CHECK(grid.edited([&] { return sheets.set(a, "released", false); }));
    CHECK_MESSAGE(
        (r.at("design.A.fixed") == "true" && r.doc.undo_count() == steps + 1 &&
         std::get<bool>(grid.item("released").value) == false &&
         grid.matches_fresh_read() && grid.rebuilds == 0),
        "a checkbox: one step, its value refreshed in place");
    ++steps;
    CHECK_MESSAGE(
        (!sheets.set(a, "released", false) && r.doc.undo_count() == steps),
        "the value it has already: nothing");

    CHECK(sheets.set(a, "sampling", 2));
    CHECK_MESSAGE(
        (r.at("sweeps.tau.sampling") == "fixed" &&
         r.doc.undo_count() == steps + 1 && grid.matches_fresh_read()),
        "a choice writes its text");
    ++steps;
    CHECK(sheets.set(a, "sampling", 0));
    CHECK_MESSAGE((r.at("sweeps.tau.sampling") == "-" &&
                   r.doc.undo_label() == "delete sweeps.tau.sampling"),
                  "an empty choice erases the key");
    ++steps;
    CHECK_MESSAGE(
        (!sheets.set(a, "sampling", 3) && !sheets.set(a, "sampling", -1) &&
         !sheets.set(a, "sampling", 1.0) && !sheets.set(a, "released", 1) &&
         !sheets.set(a, "width", std::string("2")) &&
         !sheets.set(a, "position", 0.2) && r.doc.undo_count() == steps),
        "a choice it does not have, a value of another type");

    CHECK(sheets.set(a, "width", 20.0));
    CHECK_MESSAGE((r.num("display[1].width") == 10.0 &&
                   r.doc.undo_label() == "edit display[1].width"),
                  "a drag's bounds hold for set() too");
    CHECK(sheets.set(a, "colour", Rgba{1, 0, 0, 1}));
    CHECK(r.at("display[1].color") == "#ff0000");
    CHECK(sheets.set(a, "tau", 0.25));
    CHECK(r.doc.sweep_t()[0] == 0.25);
    steps += 2;
    CHECK_MESSAGE(r.doc.undo_count() == steps, "a sweep position is no step");

    CHECK(grid.edited([&] {
        return sheets.set(a, "domain", std::string("box(0, 0, 1, 1)"));
    }));
    CHECK_MESSAGE((r.at("design.A.domain") == "box(0, 0, 1, 1)" &&
                   r.doc.undo_count() == steps + 1 &&
                   grid.item("domain").text == "box(0, 0, 1, 1)" &&
                   grid.matches_fresh_read()),
                  "a field applies its text as Enter does");
    ++steps;
    CHECK(!grid.edited(
        [&] { return sheets.set(a, "clr", std::string("0.3x")); }));
    CHECK_MESSAGE((r.at("params.clr") == "0.02" &&
                   grid.item("clr").refusal ==
                       "'0.3x' is not a number; this key takes a number, not "
                       "an expression" &&
                   r.doc.undo_count() == steps),
                  "a refused text: its reason on the item");
    CHECK_MESSAGE((r.app.panels().host.fields().pending(r.doc).empty() &&
                   r.app.status_bar().segments().back().field.empty() &&
                   grid.item("clr").text == "0.02"),
                  "the refused text is the editor's: no unapplied edit "
                  "outlives it, which the sheet would not show");
    CHECK(!grid.edited(
        [&] { return sheets.set(a, "clr", std::string("0.02")); }));
    CHECK_MESSAGE(grid.item("clr").refusal.empty(),
                  "the document's own text clears the reason");

    CHECK_MESSAGE(
        (!sheets.set(a, "state", std::string("x")) &&
         !sheets.set(a, "general", true) && !sheets.set(a, "locked", false) &&
         !sheets.press(a, "width") && !sheets.press(a, "locked") &&
         r.doc.undo_count() == steps),
        "a value, a section and a read-only item take nothing; "
        "only a button is pressed");
    r.doc.set_x(std::vector<double>(r.doc.x().size(), 0.5));
    CHECK(sheets.press(a, "reset"));
    CHECK_MESSAGE((r.doc.undo_label() == "reset design values" &&
                   grid.matches_fresh_read()),
                  "a button runs its command");

    CHECK(grid.edited([&] { return sheets.set(a, "name", std::string("P")); }));
    const SelKey p{K::Design, "P"};
    CHECK_MESSAGE((r.doc.undo_label() == "rename A to P" &&
                   r.sel.keys() == std::vector<SelKey>{p} && !grid.sheet() &&
                   grid.matches_fresh_read()),
                  "a name renames the entry; this sheet knows only A");
    steps = r.doc.undo_count();
    CHECK_MESSAGE((!sheets.set(a, "released", true) &&
                   !sheets.set(a, "name", std::string("Q")) &&
                   !sheets.press(a, "reset") && r.doc.undo_count() == steps),
                  "a key that names nothing now: every operation is refused");
    REQUIRE(r.doc.undo());
    r.app.sync();
    r.sel.set(a);
    CHECK(sheets.set(a, "name", std::string("B")) == false);
    CHECK_MESSAGE(
        (r.doc.undo_count() == steps - 1 &&
         r.sheets.sheet(a)->find("name")->refusal ==
             "cannot rename design.A: name 'B' is already a design variable "
             "(design.B)"),
        "a name in use: refused, with the reason on the Name item: "
            << r.sheets.sheet(a)->find("name")->refusal);
}

TEST_CASE("sheet adapter: a drag is one undo step") {
    Rig r;
    FakePropertyGrid grid = r.grid();
    SheetBuilder & sheets = r.sheets;
    const SelKey & a = r.a;
    std::size_t steps = r.doc.undo_count();

    CHECK_MESSAGE(
        (!sheets.update_drag(a, "width", 2.0) && !sheets.end_drag(a, "width") &&
         !sheets.begin_drag(a, "released") && !sheets.begin_drag(a, "locked") &&
         !sheets.begin_drag(a, "reset")),
        "no drag open, or an item that is not dragged");

    REQUIRE(grid.edited([&] { return sheets.begin_drag(a, "width"); }));
    for(const double w : {2.0, 3.0, 4.0})
        CHECK(grid.edited([&] { return sheets.update_drag(a, "width", w); }));
    CHECK_MESSAGE((std::get<double>(grid.item("width").value) == 4.0 &&
                   r.num("display[1].width") == 1.5 &&
                   r.doc.undo_count() == steps && grid.matches_fresh_read()),
                  "the sheet shows the dragged value, the document waits");
    CHECK_MESSAGE((!sheets.update_drag(a, "colour", Rgba{}) &&
                   !sheets.end_drag(a, "colour")),
                  "only the dragged item");
    CHECK(grid.edited([&] { return sheets.end_drag(a, "width"); }));
    CHECK_MESSAGE(
        (r.num("display[1].width") == 4.0 && r.doc.undo_count() == steps + 1 &&
         r.doc.undo_label() == "edit display[1].width" &&
         grid.matches_fresh_read() && grid.rebuilds == 0),
        "three moves, one step");
    ++steps;

    const Vec before = std::get<Vec>(grid.item("position").value);
    REQUIRE(sheets.begin_drag(a, "position"));
    for(const double dx : {0.01, 0.02, 0.03})
        CHECK(grid.edited([&] {
            return sheets.update_drag(a, "position",
                                      Vec{before[0] + dx, before[1]});
        }));
    CHECK_MESSAGE((r.doc.in_gesture() && r.doc.undo_count() == steps &&
                   near(r.doc.var_value(r.doc.model()->find_var("A"))[0],
                        before[0] + 0.03, 1e-9) &&
                   grid.matches_fresh_read()),
                  "a released point moves in x while it is held");
    CHECK(sheets.end_drag(a, "position"));
    CHECK_MESSAGE((!r.doc.in_gesture() && r.doc.undo_count() == steps + 1 &&
                   r.doc.undo_label() == "move A"),
                  "one gesture, one step");
    ++steps;

    REQUIRE(sheets.begin_drag(a, "tau"));
    CHECK(sheets.update_drag(a, "tau", 1.5));
    CHECK_MESSAGE(
        (r.doc.sweep_t()[0] == 1.0 &&
         std::get<double>(r.sheets.sheet(a)->find("tau")->value) == 1.0),
        "a slider's value stays in its range");
    REQUIRE(sheets.begin_drag(a, "width"));
    CHECK_MESSAGE(r.doc.undo_count() == steps,
                  "a new drag ends the open one: a sweep position is no step");
    CHECK(sheets.update_drag(a, "width", 5.0));
    sheets.finish_drag();
    CHECK_MESSAGE(
        (r.num("display[1].width") == 5.0 && r.doc.undo_count() == steps + 1 &&
         !sheets.end_drag(a, "width")),
        "a lost release: finish_drag() applies the drag once");
    ++steps;

    REQUIRE(sheets.begin_drag(a, "width"));
    CHECK(sheets.update_drag(a, "width", 6.0));
    REQUIRE(sheets.set(a, "name", std::string("P")));
    ++steps;
    CHECK_MESSAGE(
        (!sheets.end_drag(a, "width") && r.num("display[1].width") == 5.0 &&
         r.doc.undo_count() == steps),
        "the entry is gone at the release: nothing is written");
}

TEST_CASE("sheet adapter: Properties follows the selection and every change") {
    App app(std::make_unique<FakeBackend>());
    REQUIRE(app.open(tv_file()));
    app.sync();
    Document & doc = app.document();
    Selection & sel = app.selection();
    PropertiesPresenter & properties = app.panels().properties;
    FakePropertyGrid grid(app, [&] { return properties.sheet(); });
    REQUIRE((grid.matches_fresh_read() && !grid.sheet()->key));

    const SelKey clr{K::Param, "clr"};
    sel.set(clr);
    CHECK_MESSAGE((grid.matches_fresh_read() && grid.rebuilds == 1 &&
                   grid.sheet()->key == clr),
                  "a selection: the sheet of the selected entry");
    const std::string si = grid.item("si").text;
    CHECK(grid.edited(
        [&] { return properties.set(clr, "value", std::string("3cm")); }));
    CHECK_MESSAGE(
        (*doc.instance()->get("params.clr") == "3cm" &&
         grid.item("value").text == "3cm" && grid.item("si").text != si &&
         grid.matches_fresh_read() && grid.rebuilds == 1),
        "a value set through Properties, refreshed in place");
    const std::size_t steps = doc.undo_count();
    CHECK_MESSAGE(
        (!properties.set(clr, "si", std::string("1")) &&
         !properties.set(clr, "value", 1.0) &&
         !properties.press(clr, "value") &&
         !properties.begin_drag(clr, "value") &&
         !properties.update_drag(clr, "value", 1.0) &&
         !properties.end_drag(clr, "value") && doc.undo_count() == steps),
        "a param's sheet has no button and nothing to drag");

    CHECK(properties.set(clr, "name", std::string("gap")));
    app.sync();
    const SelKey gap{K::Param, "gap"};
    CHECK_MESSAGE((sel.keys() == std::vector<SelKey>{gap} &&
                   grid.sheet()->key == gap && grid.matches_fresh_read()),
                  "a rename: the selection and the sheet follow the name");
    CHECK_MESSAGE((!properties.set(clr, "value", std::string("1")) &&
                   doc.undo_count() == steps + 1),
                  "the old name is a stale key");

    const SelKey fridge{K::Constraint, "fridge"};
    sel.set(fridge);
    CHECK((grid.matches_fresh_read() && grid.sheet()->key == fridge &&
           grid.sheet()->title == "constraint fridge"));
    CHECK_MESSAGE((properties.set(fridge, "name", std::string("cold")) &&
                   doc.undo_label() == "rename fridge to cold"),
                  "a constraint, renamed in its sheet");
    app.sync();
    CHECK(grid.matches_fresh_read());
    const SelKey couch{K::Geometry, "couch"};
    sel.set(couch);
    CHECK_MESSAGE((!properties.set(couch, "name", std::string("sofa")) &&
                   grid.matches_fresh_read()),
                  "an entry of an included file is not renamed");

    REQUIRE(doc.undo());
    app.sync();
    CHECK(grid.matches_fresh_read());
    sel.set({K::Design, "A"});
    doc.set_x(std::vector<double>(doc.x().size(), 0.5));
    doc.set_sweep_t(0, 0.3);
    doc.edit("params.W", "1.23 +");
    REQUIRE(doc.stale());
    CHECK_MESSAGE(grid.matches_fresh_read(),
                  "values, a sweep move and a failed compile");
    REQUIRE(doc.undo());
    sel.assign({{K::Design, "B"}, {K::Param, "W"}});
    CHECK_MESSAGE((grid.matches_fresh_read() && !grid.sheet()->key),
                  "several entries: the hint");
    sel.set({K::Param, "W"});
    REQUIRE(app.actions().run("solver.solve"));
    app.tick(0.0);
    CHECK_MESSAGE(grid.matches_fresh_read(), "solver results");
    REQUIRE(app.open(fs::path(GS_TEST_DATA_DIR) /
                     "../../docs/instances/ref-instance.json"));
    app.sync();
    CHECK_MESSAGE((grid.matches_fresh_read() && !grid.sheet()->key),
                  "another instance");
}

TEST_CASE("sheet adapter: the panels' sheets follow every change") {
    App app(std::make_unique<FakeBackend>());
    REQUIRE(app.open(tv_file()));
    app.sync();
    Document & doc = app.document();
    Selection & sel = app.selection();
    PropertiesPresenter & properties = app.panels().properties;
    FakePropertyGrid grid(app, [&] { return properties.sheet(); });
    const SelKey a{K::Design, "A"};
    sel.set(a);
    REQUIRE((grid.matches_fresh_read() && grid.rebuilds == 1));

    const Vec at = std::get<Vec>(grid.item("value").value);
    REQUIRE(grid.edited([&] { return properties.begin_drag(a, "value"); }));
    for(const double dx : {0.01, 0.02})
        CHECK(grid.edited([&] {
            return properties.update_drag(a, "value", Vec{at[0] + dx, at[1]});
        }));
    CHECK(grid.edited([&] { return properties.end_drag(a, "value"); }));
    CHECK_MESSAGE((grid.matches_fresh_read() && grid.rebuilds == 1 &&
                   doc.undo_label() == "set A"),
                  "a released point dragged: its values refreshed in place");
    CHECK(grid.edited([&] { return properties.set(a, "released", false); }));
    CHECK_MESSAGE((grid.matches_fresh_read() && grid.rebuilds == 1 &&
                   grid.item("released").value == DetailValue(false)),
                  "fixed: the same items, refreshed");
    REQUIRE(doc.undo());
    CHECK(grid.matches_fresh_read());

    const SelKey tau{K::Sweep, "tau"};
    sel.set(tau);
    CHECK(grid.edited([&] { return properties.set(tau, "position", 0.75); }));
    doc.set_sweep_t(0, 0.2);
    CHECK_MESSAGE((grid.matches_fresh_read() &&
                   std::get<double>(grid.item("position").value) == 0.2),
                  "a sweep moved elsewhere: the slider follows");
    CHECK(grid.edited([&] { return properties.set(tau, "sampling", 1); }));
    CHECK(grid.matches_fresh_read());
    CHECK(grid.edited([&] { return properties.press(tau, "play"); }));
    CHECK_MESSAGE(
        (grid.item("play").label == "Pause" && grid.matches_fresh_read()),
        "Play changes no document: read back after the press");
    app.tick(0.25);
    CHECK(grid.matches_fresh_read());
    SweepBarPresenter bar(app);
    REQUIRE(bar.begin() == 1);
    bar.toggle_play(0);
    CHECK_MESSAGE(
        (grid.item("play").label == "Play" && grid.matches_fresh_read()),
        "paused from the sweep bar, another view");
    bar.toggle_play(0);
    CHECK(grid.matches_fresh_read());

    sel.set({K::Geometry, "couch"});
    REQUIRE(grid.matches_fresh_read());
    REQUIRE(app.open(fs::path(GS_TEST_DATA_DIR) /
                     "../../docs/instances/guide-a-domains.json"));
    app.sync();
    const SelKey nook{K::Geometry, "nook"};
    sel.set(nook);
    const int rebuilds = grid.rebuilds;
    CHECK(grid.edited([&] { return properties.press(nook, "add_vertex"); }));
    CHECK_MESSAGE((grid.matches_fresh_read() && grid.rebuilds == rebuilds + 1 &&
                   grid.sheet()->find("vertex6")),
                  "a vertex added: new ids, the properties made again");
    REQUIRE(doc.undo());
    CHECK(grid.matches_fresh_read());

    DocumentPresenter & panel = app.panels().document;
    FakePropertyGrid document(app, [&] { return panel.sheet(); });
    CHECK(document.edited(
        [&] { return panel.set("description", std::string("Two\nlines")); }));
    CHECK(document.matches_fresh_read());
    REQUIRE(app.open(tv_file()));
    CHECK_MESSAGE(document.matches_fresh_read(), "another instance");
    REQUIRE(doc.edit("sweeps.tau.samples", 3));
    CHECK_MESSAGE((document.matches_fresh_read() &&
                   document.item("format").text == "geomsolver-instance/2"),
                  "the format raised by an edit elsewhere");
    REQUIRE(doc.erase("sweeps.tau.samples"));
    CHECK_MESSAGE((document.matches_fresh_read() &&
                   document.sheet()->find("use_format1")),
                  "Use format /1 offered once no key needs /2");
    CHECK(document.edited([&] { return panel.press("use_format1"); }));
    CHECK(document.matches_fresh_read());
}

TEST_CASE("sheet adapter: the lets', display items' and requirements' sheets") {
    App app(std::make_unique<FakeBackend>());
    REQUIRE(app.open(tv_file()));
    app.sync();
    app.scene(false);
    Document & doc = app.document();
    Selection & sel = app.selection();
    PropertiesPresenter & properties = app.panels().properties;
    FakePropertyGrid grid(app, [&] { return properties.sheet(); });
    const SelKey tv{K::Let, "tv"};
    sel.set(tv);
    REQUIRE(grid.matches_fresh_read());

    const std::size_t steps = doc.undo_count();
    REQUIRE(grid.edited([&] { return properties.begin_drag(tv, "ghosts"); }));
    for(const int g : {7, 9})
        CHECK(grid.edited(
            [&] { return properties.update_drag(tv, "ghosts", g); }));
    CHECK_MESSAGE((grid.item("ghosts").value == DetailValue(9) &&
                   *doc.instance()->get("display[0].ghosts") == 6 &&
                   doc.undo_count() == steps),
                  "a whole-number drag holds its value while it is held");
    CHECK(grid.edited([&] { return properties.end_drag(tv, "ghosts"); }));
    CHECK_MESSAGE((grid.matches_fresh_read() && doc.undo_count() == steps + 1 &&
                   *doc.instance()->get("display[0].ghosts") == 9),
                  "and writes it at the release, one undo step");
    REQUIRE(grid.edited([&] { return properties.begin_drag(tv, "color"); }));
    CHECK(grid.edited([&] {
        return properties.update_drag(tv, "color",
                                      Rgba{1.0f, 0.0f, 0.0f, 1.0f});
    }));
    properties.finish_drag();
    CHECK_MESSAGE((*doc.instance()->get("display[0].color") == "#ff0000" &&
                   doc.undo_count() == steps + 2 && grid.matches_fresh_read()),
                  "a colour picked, ended by the frame's safety net: one step");
    CHECK_MESSAGE(
        (!properties.set(tv, "formula", std::string("0")) &&
         grid.edited([&] { return properties.press(tv, "edit_formula"); }) &&
         app.formula().is_open() && grid.matches_fresh_read()),
        "the formula is read-only: its button opens the pop-up, "
        "which changes no document");
    CHECK(grid.edited([&] {
        const std::string text = "place(box(-W/2, 0, W/2, T), p, phi + 0.1)";
        app.formula().set_text(text, text.size());
        return app.formula().commit();
    }));
    CHECK_MESSAGE((grid.matches_fresh_read() &&
                   grid.item("formula").text ==
                       "place(box(-W/2, 0, W/2, T), p, phi + 0.1)"),
                  "the pop-up's commit: the formula in place");
    const int rebuilds = grid.rebuilds;
    CHECK(grid.edited([&] { return properties.press(tv, "remove_drawing"); }));
    CHECK_MESSAGE((grid.matches_fresh_read() && grid.rebuilds > rebuilds &&
                   grid.sheet()->find("draw")),
                  "the drawing removed: other ids, the properties made again");
    REQUIRE(doc.undo());
    CHECK(grid.matches_fresh_read());

    const SelKey link{K::Display, {}, 2};
    sel.set(link);
    CHECK(grid.edited([&] { return properties.set(link, "visible", false); }));
    CHECK(grid.edited([&] { return properties.set(link, "width", 4.5); }));
    CHECK(grid.matches_fresh_read());

    const SelKey couch{K::Constraint, "couch"};
    sel.set(couch);
    CHECK(grid.edited([&] { return properties.set(couch, "quantifier", 0); }));
    CHECK(grid.edited([&] { return properties.set(couch, "enabled", false); }));
    doc.set_sweep_t(0, 0.4);
    app.scene(false);
    CHECK_MESSAGE(grid.matches_fresh_read(),
                  "a sweep moved, the scene that follows: its value now");
    CHECK(grid.edited([&] { return properties.set(couch, "quantifier", 1); }));

    const SelKey link_1{K::Criterion, "link_1"};
    sel.set(link_1);
    CHECK(grid.edited([&] { return properties.set(link_1, "role", 4); }));
    CHECK_MESSAGE((grid.matches_fresh_read() && grid.sheet()->find("from")),
                  "a role switch: other items");
    CHECK(grid.edited([&] { return properties.set(link_1, "unit", 3); }));
    CHECK(
        grid.edited([&] { return properties.set(link_1, "enabled", false); }));
    CHECK(grid.matches_fresh_read());
    REQUIRE(app.actions().run("solver.solve"));
    app.tick(0.0);
    app.scene(false);
    CHECK_MESSAGE(grid.matches_fresh_read(), "solver results");
}

}  // namespace gs::test

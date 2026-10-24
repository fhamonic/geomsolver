#include <algorithm>
#include <format>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/list.hpp"

#include "../sketch/corpus.hpp"
#include "fake_tool.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

// A retained toolkit's list model in miniature, as a wxDataViewModel adapter
// would be: it caches the rows by RowId and changes its cache only as
// ListPresenter's contract says, on App::changed() and on the list's own
// Filters. After every change its cache must equal a fresh read of the
// presenter: a change the contract misses leaves a row the toolkit would
// show stale.
class FakeRetainedAdapter {
public:
    FakeRetainedAdapter(App & app, ListPresenter & list) : list_(list) {
        link_ = app.changed().connect([this](Changes c) { changed(c); });
        filters_link_ = list.changed().connect([this](Changes c) {
            if(c.has(Change::Filters)) reset();
        });
        read_all(children_, rows_);
        footer_ = list_.footer();
        filters_ = list_.filters();
        shown_ = columns_shown();
    }

    // How it followed the changes: the toolkit calls it would have made.
    int resets = 0;     // Cleared()
    int added = 0;      // ItemAdded()
    int deleted = 0;    // ItemDeleted()
    int refreshes = 0;  // ItemChanged() on every row shown
    int stale = 0;      // rows it asked for that row() no longer gives

    bool matches_fresh_read() {
        Children children;
        Rows rows;
        read_all(children, rows);
        return children == children_ && rows == rows_ &&
               footer_ == list_.footer() && filters_ == list_.filters() &&
               shown_ == columns_shown();
    }
    const std::vector<ListCell> & footer() const { return footer_; }
    std::vector<std::string> names() const {
        std::vector<std::string> out;
        for(const auto & [id, row] : rows_) out.push_back(id.key.name);
        return out;
    }

private:
    using Children = std::map<std::optional<RowId>, std::vector<RowId>>;
    using Rows = std::map<RowId, ListRow>;

    void read_all(Children & children, Rows & rows) {
        std::function<void(const std::optional<RowId> &)> read =
            [&](const std::optional<RowId> & parent) {
                children[parent] = list_.children(parent);
                for(const RowId & id : children[parent]) {
                    if(const std::optional<ListRow> r = list_.row(id))
                        rows.insert_or_assign(id, *r);
                    read(id);
                }
            };
        read(std::nullopt);
    }

    // Which columns it draws: a wxDataViewColumn's SetHidden().
    std::vector<bool> columns_shown() const {
        std::vector<bool> out;
        for(std::size_t c = 0; c < list_.columns().size(); ++c)
            out.push_back(list_.column_shown(c));
        return out;
    }

    void reset() {
        filters_ = list_.filters();
        children_.clear();
        rows_.clear();
        read_all(children_, rows_);
        footer_ = list_.footer();
        shown_ = columns_shown();
        ++resets;
    }

    void changed(Changes c) {
        if(c.has_any(list_.resets_on())) {
            reset();
            return;
        }
        if(c.has(Change::Selection)) filters_ = list_.filters();
        if(c.has(Change::Content)) diff(std::nullopt);
        if(c.has_any(Change::Content | Change::Model | Change::Values |
                     Change::Sweep | Change::Visibility | Change::Solver |
                     Change::Scene | Change::Sketch)) {
            for(auto & [id, row] : rows_) {
                if(std::optional<ListRow> now = list_.row(id))
                    row = std::move(*now);
                else
                    ++stale;
            }
            footer_ = list_.footer();
            shown_ = columns_shown();
            ++refreshes;
        }
    }

    void erase(const RowId & id) {
        rows_.erase(id);
        for(const RowId & child : children_[id]) erase(child);
        children_.erase(id);
        ++deleted;
    }

    void insert(const RowId & id) {
        if(const std::optional<ListRow> r = list_.row(id))
            rows_.insert_or_assign(id, *r);
        ++added;
        children_[id] = list_.children(id);
        for(const RowId & child : children_[id]) insert(child);
    }

    // Every removal, under every parent, before any addition: a row may
    // move to another parent with its id (a constraint between Geometry and
    // Design), and added first it would be found cached, then erased with
    // its old parent.
    void diff(const std::optional<RowId> & parent) {
        prune(parent);
        add(parent);
    }
    void prune(const std::optional<RowId> & parent) {
        const std::vector<RowId> now = list_.children(parent);
        std::vector<RowId> kept;
        for(const RowId & id : children_[parent]) {
            if(std::ranges::contains(now, id)) {
                kept.push_back(id);
                prune(id);
            } else {
                erase(id);
            }
        }
        children_[parent] = std::move(kept);
    }
    void add(const std::optional<RowId> & parent) {
        const std::vector<RowId> now = list_.children(parent);
        for(const RowId & id : now) {
            if(std::ranges::contains(children_[parent], id))
                add(id);
            else
                insert(id);
        }
        children_[parent] = now;
    }

    ListPresenter & list_;
    Children children_;
    Rows rows_;
    std::vector<ListCell> footer_;
    std::vector<Filter> filters_;
    std::vector<bool> shown_;
    Signal<Changes>::Connection link_, filters_link_;
};

// The TV instance's constraints under one group header, with the toggles,
// dim rows, tooltips, filters and intents Params has none of: the shape of
// the Constraints panel, checked against the same contract.
class RequirementsList final : public ListPresenter {
public:
    explicit RequirementsList(App & app) : app_(app) {}

    std::span<const Column> columns() const override { return kColumns; }
    std::vector<RowId> children(const std::optional<RowId> & parent) override {
        if(!parent) return {header_};
        if(*parent != header_) return {};
        std::vector<SelKey> keys;
        for(const Shown & s : shown()) keys.push_back(s.key);
        return row_ids(keys);
    }
    std::optional<ListRow> row(const RowId & id) override {
        if(!app_.document().has_instance()) return std::nullopt;
        if(id == header_) {
            const std::size_t n = shown().size();
            return ListRow{.id = id,
                           .expandable = n > 0,
                           .label = std::format("Design requirements ({})", n)};
        }
        const std::optional<Shown> s = find(id);
        if(!s) return std::nullopt;
        const Json & c = *s->entry;
        const bool on = c.value("enabled", true);
        return ListRow{
            .id = id,
            .parent = header_,
            .depth = 1,
            .path = s->path,
            .cells = {{.kind = ListCell::Kind::Check, .on = on},
                      {ListCell::Kind::Name, id.key.name},
                      {ListCell::Kind::Text, c.value("expr", "")},
                      {.kind = ListCell::Kind::Eye,
                       .on = !app_.visibility().hidden(id.key.name)}},
            .dim = !on,
            .tooltip = s->path};
    }
    std::vector<std::string> toolbar() const override { return {}; }
    Changes resets_on() const override {
        return Change::Instance | Change::Structure;
    }

    bool toggle(const RowId & id, std::size_t column) override {
        const std::optional<ListRow> r = id.header() ? std::nullopt : row(id);
        if(!r) return false;
        if(column == 0)
            app_.document().edit(r->path + ".enabled", !r->cells[0].on);
        else if(column == 3)
            app_.visibility().set_hidden(id.key.name, r->cells[3].on);
        return column == 0 || column == 3;
    }
    Intent activate(const RowId & id,
                    std::optional<std::size_t> column) override {
        if(column == 2 && !id.header() && row(id)) return Intent::OpenFormula;
        return ListPresenter::activate(id, column);
    }
    std::vector<Filter> filters() const override {
        return {{.kind = Filter::Kind::Text, .label = "Name", .value = name_},
                {.kind = Filter::Kind::Check,
                 .label = "Hide disabled",
                 .value = hide_disabled_},
                {.kind = Filter::Kind::Choice,
                 .label = "Quantifier",
                 .choices = {"any", "for all", "none"},
                 .value = quantifier_}};
    }

protected:
    bool apply_filter(std::size_t i, const DetailValue & value) override {
        if(i == 0) name_ = std::get<std::string>(value);
        if(i == 1) hide_disabled_ = std::get<bool>(value);
        if(i == 2) quantifier_ = std::get<int>(value);
        return true;
    }
    bool reset_filters() override {
        const bool any = !name_.empty() || hide_disabled_ || quantifier_ != 0;
        name_.clear();
        hide_disabled_ = false;
        quantifier_ = 0;
        return any;
    }

private:
    static constexpr Column kColumns[] = {{"", 1.5f, ListCell::Kind::Check},
                                          {"Name", 7.0f, ListCell::Kind::Name},
                                          {"Formula", 0.0f},
                                          {"", 1.5f, ListCell::Kind::Eye}};
    struct Shown {
        SelKey key;
        std::string path;
        const Json * entry;
    };

    std::vector<Shown> shown() const {
        std::vector<Shown> out;
        const Json * list = app_.document().instance()->get("constraints");
        for(std::size_t i = 0; list && i < list->size(); ++i) {
            const Json & c = (*list)[i];
            const std::string name = c.value("name", "?");
            const bool on = c.value("enabled", true);
            const bool forall = c.contains("forall");
            if(name.find(name_) == std::string::npos ||
               (hide_disabled_ && !on) || (quantifier_ == 1 && !forall) ||
               (quantifier_ == 2 && forall))
                continue;
            out.push_back({{SelKey::Kind::Constraint, name},
                           std::format("constraints[{}]", i),
                           &c});
        }
        return out;
    }
    std::optional<Shown> find(const RowId & id) const {
        std::uint32_t seen = 0;
        for(const Shown & s : shown())
            if(s.key == id.key && seen++ == id.ordinal) return s;
        return std::nullopt;
    }

    App & app_;
    const RowId header_ = header_row("requirements");
    std::string name_;
    bool hide_disabled_ = false;
    int quantifier_ = 0;
};

// A row of an included file's entry, which has no path to rename.
class Included final : public ListPresenter {
public:
    std::span<const Column> columns() const override { return kColumns; }
    std::vector<RowId> children(const std::optional<RowId> & p) override {
        return p ? std::vector<RowId>{} : std::vector{id};
    }
    std::optional<ListRow> row(const RowId & r) override {
        if(r != id) return std::nullopt;
        return ListRow{.id = id, .cells = {{ListCell::Kind::Name, "couch"}}};
    }
    std::vector<std::string> toolbar() const override { return {}; }
    Changes resets_on() const override { return Change::Instance; }

    const RowId id{{SelKey::Kind::Geometry, "couch"}};

private:
    static constexpr Column kColumns[] = {{"Name", 0.0f, ListCell::Kind::Name}};
};

// Every row of `list`, read from the top as a front-end draws them.
std::vector<ListRow> rows_of(ListPresenter & list,
                             const std::optional<RowId> & parent = {}) {
    std::vector<ListRow> out;
    for(const RowId & id : list.children(parent)) {
        if(std::optional<ListRow> r = list.row(id)) out.push_back(*r);
        std::ranges::move(rows_of(list, id), std::back_inserter(out));
    }
    return out;
}

// The rows `list` kept from its last read equal those it reads once its
// last filter, a checkbox, set and set back made it build them again.
bool kept_rows_hold(ListPresenter & list) {
    const std::vector<ListRow> kept = rows_of(list);
    const std::size_t last = list.filters().size() - 1;
    const bool on = std::get<bool>(list.filters()[last].value);
    list.set_filter(last, !on);
    list.set_filter(last, on);
    return rows_of(list) == kept;
}

// The first row of `list` a wxDataViewCtrl could not show, "" for none. It
// has one renderer per column, chosen from Column::kind before any row
// exists, and draws nothing for a value of another type; it spans no
// columns, so a header's label goes in the Name column.
std::string misfit(ListPresenter & list) {
    using CK = ListCell::Kind;
    const std::span<const Column> columns = list.columns();
    const bool named = std::ranges::contains(columns, CK::Name, &Column::kind);
    for(const ListRow & r : rows_of(list)) {
        if(r.id.header()) {
            if(r.label.empty() || !r.cells.empty() || !named)
                return std::format("the header of {}", r.id.group);
            continue;
        }
        if(r.cells.size() != columns.size())
            return std::format("{}: {} cells", key_name(r.id.key),
                               r.cells.size());
        for(std::size_t c = 0; c < columns.size(); ++c) {
            const CK kind = r.cells[c].kind, want = columns[c].kind;
            if(kind != CK::Empty && kind != want &&
               !(want == CK::Name && kind == CK::Text))
                return std::format("{}, column {}", key_name(r.id.key), c);
        }
    }
    return {};
}

}  // namespace

TEST_CASE("list adapter: a retained cache follows every kind of change") {
    App app(std::make_unique<FakeBackend>());
    REQUIRE(app.open(tv_file()));
    app.sync();
    Document & doc = app.document();
    Selection & sel = app.selection();
    PanelHost & host = app.panels().host;
    ListPresenter & params = app.panels().params;
    FakeRetainedAdapter adapter(app, params);
    REQUIRE(adapter.matches_fresh_read());

    REQUIRE(doc.edit("params.clr", "3cm"));
    CHECK_MESSAGE(
        (adapter.matches_fresh_read() && adapter.resets == 0 &&
         adapter.refreshes == 1),
        "a new value, and a new SI value from the new model: rows refreshed");

    REQUIRE(app.actions().run("param.add"));
    CHECK_MESSAGE((adapter.matches_fresh_read() && adapter.resets == 0 &&
                   adapter.added == 1),
                  "an append moves no path: the diff by RowId adds its row");

    REQUIRE(doc.path_generation() == 0);
    host.renamer().open("params.p1", PanelId::Params);
    REQUIRE(host.rename({SelKey::Kind::Param, "p1"}, "gap"));
    CHECK_MESSAGE((adapter.matches_fresh_read() && adapter.resets == 1),
                  "a rename moves paths: the rows are read again");

    sel.set({SelKey::Kind::Param, "gap"});
    REQUIRE(app.actions().run("edit.delete"));
    CHECK_MESSAGE((adapter.matches_fresh_read() && adapter.resets == 1 &&
                   adapter.deleted == 1),
                  "an erased param moves no path: the diff removes its row");

    REQUIRE(doc.undo());
    CHECK((adapter.matches_fresh_read() && adapter.resets == 2));
    REQUIRE(doc.redo());
    CHECK((adapter.matches_fresh_read() && adapter.resets == 3));

    // Changes that leave the params' rows as they are: the cache must not
    // drift either.
    const int resets = adapter.resets;
    sel.set({SelKey::Kind::Design, "A"});
    sel.reveal({SelKey::Kind::Design, "A"});
    app.set_hot(HotSource::Canvas, SelKey{SelKey::Kind::Design, "A"});
    CHECK_MESSAGE((adapter.matches_fresh_read() && adapter.resets == resets),
                  "a canvas selection and a hover change no row");
    REQUIRE(app.actions().run("view.hide"));
    CHECK_MESSAGE(adapter.matches_fresh_read(), "the eye of a name");
    set_eyes(doc, app.visibility(), {{SelKey::Kind::Display, {}, 9}}, false);
    CHECK_MESSAGE(adapter.matches_fresh_read(), "a display item's eye");
    doc.set_sweep_t(0, 0.3);
    CHECK(adapter.matches_fresh_read());

    REQUIRE(doc.erase("display[9]"));
    app.display_erased(9);
    CHECK_MESSAGE(
        (adapter.matches_fresh_read() && adapter.resets == resets + 1),
        "an erased display item moves paths");
    doc.edit("constraints[1].name", "couch");
    CHECK_MESSAGE(adapter.matches_fresh_read(),
                  "a compile error that makes two constraints one name");
    doc.edit("params.W", "1.23 +");
    REQUIRE(doc.stale());
    CHECK_MESSAGE(adapter.matches_fresh_read(),
                  "a failed compile keeps the last good model's SI values");
    doc.undo();
    doc.undo();

    REQUIRE(app.actions().run("solver.solve"));
    app.tick(0.0);
    CHECK_MESSAGE(adapter.matches_fresh_read(), "solver results");

    REQUIRE(app.open(fs::path(GS_TEST_DATA_DIR) /
                     "../../docs/instances/ref-instance.json"));
    CHECK_MESSAGE(
        (adapter.matches_fresh_read() &&
         adapter.names() ==
             std::vector<std::string>{"clr", "r_max", "reach", "walker_r"}),
        "another instance: every row is read again");
    CHECK(adapter.stale == 0);
}

TEST_CASE("list adapter: a stale id names no row, and ids count repeats") {
    App app(nullptr);
    REQUIRE(app.open(tv_file()));
    ListPresenter & params = app.panels().params;
    using K = SelKey::Kind;
    CHECK((params.row({{K::Param, "clr"}}) &&
           !params.row({{K::Param, "gone"}}) &&
           !params.row({{K::Param, "clr"}, 1}) &&
           !params.row({{K::Design, "A"}}) &&
           params.children(RowId{{K::Param, "clr"}}).empty()));

    const std::vector<SelKey> keys = {{K::Constraint, "couch"},
                                      {K::Constraint, "?"},
                                      {K::Constraint, "couch"},
                                      {K::Constraint, "?"},
                                      {K::Criterion, "couch"}};
    CHECK_MESSAGE((row_ids(keys) == std::vector<RowId>{{keys[0], 0},
                                                       {keys[1], 0},
                                                       {keys[2], 1},
                                                       {keys[3], 1},
                                                       {keys[4], 0}}),
                  "a repeated key gets the next ordinal, per kind and name");
}

TEST_CASE("list adapter: toggles, headers, filters and intents") {
    App app(std::make_unique<FakeBackend>());
    REQUIRE(app.open(tv_file()));
    app.sync();
    Document & doc = app.document();
    using K = SelKey::Kind;
    RequirementsList list(app);
    FakeRetainedAdapter adapter(app, list);
    FakeRetainedAdapter params(app, app.panels().params);
    int filtered = 0;
    const Signal<Changes>::Connection count = list.changed().connect(
        [&](Changes c) { filtered += c.has(Change::Filters); });

    const RowId header = header_row("requirements");
    const RowId couch{{K::Constraint, "couch"}};
    const std::optional<ListRow> head = list.row(header);
    REQUIRE(head);
    CHECK_MESSAGE(
        (list.children(std::nullopt) == std::vector{header} &&
         header.header() && !couch.header() && head->path.empty() &&
         head->expandable && head->label == "Design requirements (6)" &&
         list.children(header).size() == 6),
        "a group header: no entry, no path, its rows under it");
    std::optional<ListRow> r = list.row(couch);
    REQUIRE(r);
    CHECK((r->parent == header && r->depth == 1 &&
           r->cells[0].kind == ListCell::Kind::Check && r->cells[0].on &&
           !r->dim && r->tooltip == "constraints[0]"));

    const std::size_t steps = doc.undo_count();
    CHECK(list.toggle(couch, 0));
    r = list.row(couch);
    CHECK_MESSAGE((doc.undo_count() == steps + 1 && !r->cells[0].on && r->dim &&
                   adapter.matches_fresh_read() && adapter.resets == 0),
                  "a toggle: one step, its row refreshed");
    CHECK(list.toggle(couch, 3));
    CHECK_MESSAGE(
        (!list.row(couch)->cells[3].on && doc.undo_count() == steps + 1 &&
         adapter.matches_fresh_read() && adapter.resets == 0),
        "an eye of a name: no step, its row refreshed");
    CHECK_MESSAGE((!list.toggle(couch, 1) && !list.toggle(header, 0) &&
                   !list.toggle({{K::Constraint, "gone"}}, 0) &&
                   !list.toggle({{K::Constraint, "couch"}, 1}, 0) &&
                   doc.undo_count() == steps + 1),
                  "a cell that is no toggle, a header, a stale id");
    CHECK_MESSAGE(
        (list.activate(couch, 1) == Intent::Rename &&
         list.activate(couch, 2) == Intent::OpenFormula &&
         list.activate(couch, 0) == Intent::FocusProperties &&
         list.activate(couch, std::nullopt) == Intent::FocusProperties &&
         list.activate(header, 0) == Intent::None &&
         list.activate({{K::Constraint, "gone"}}, 1) == Intent::None),
        "a double click asks for the editor of its cell");

    const std::vector<Filter> filters = list.filters();
    REQUIRE(filters.size() == 3);
    CHECK((filters[0].kind == Filter::Kind::Text &&
           filters[1].kind == Filter::Kind::Check &&
           filters[2].kind == Filter::Kind::Choice));
    CHECK(list.set_filter(1, true));
    CHECK_MESSAGE((filtered == 1 && adapter.resets == 1 &&
                   adapter.matches_fresh_read() && params.resets == 0 &&
                   !list.row(couch) && list.children(header).size() == 5),
                  "a filter: Filters, a reset of this list, not of Params");
    CHECK(list.set_filter(0, std::string("c")));
    CHECK(list.set_filter(2, 2));
    CHECK_MESSAGE((adapter.resets == 3 && adapter.matches_fresh_read() &&
                   list.filters()[2].value == DetailValue(2)),
                  "a text and a choice");
    CHECK_MESSAGE(
        (!list.set_filter(1, true) && !list.set_filter(2, 3) &&
         !list.set_filter(2, -1) && !list.set_filter(0, true) &&
         !list.set_filter(1, 1) && !list.set_filter(3, true) && filtered == 3),
        "the value it has, a choice it does not have, another type or index");
    CHECK(list.clear_filters());
    CHECK_MESSAGE(
        (filtered == 4 && adapter.resets == 4 && adapter.matches_fresh_read() &&
         !list.clear_filters() && list.children(header).size() == 6),
        "cleared, every row is back");

    REQUIRE(doc.apply(EditBatch("add c7").set(
        "constraints[6]", Json{{"name", "c7"}, {"expr", "0 <= 1"}})));
    CHECK_MESSAGE((adapter.matches_fresh_read() && adapter.added == 1 &&
                   adapter.resets == 4),
                  "an append: the diff adds its row under the header");
    doc.edit("constraints[1].name", "couch");
    const RowId second{{K::Constraint, "couch"}, 1};
    CHECK_MESSAGE((adapter.matches_fresh_read() && list.row(second) &&
                   list.row(second)->path == "constraints[1]"),
                  "a name used twice: two ids");
    CHECK(list.toggle(second, 0));
    CHECK(doc.instance()->get("constraints[1].enabled")->get<bool>() == false);
    REQUIRE(app.panels().host.rename({K::Constraint, "table"}, "desk"));
    CHECK((adapter.matches_fresh_read() && adapter.resets == 5));
    doc.erase("constraints[2]");
    CHECK((adapter.matches_fresh_read() && adapter.resets == 6));
    REQUIRE(doc.undo());
    REQUIRE(doc.redo());
    CHECK(adapter.matches_fresh_read());

    app.selection().set({K::Design, "A"});
    app.set_hot(HotSource::Canvas, SelKey{K::Design, "A"});
    REQUIRE(app.actions().run("view.hide"));
    doc.set_sweep_t(0, 0.6);
    doc.edit("params.W", "1.23 +");
    CHECK_MESSAGE((adapter.matches_fresh_read() && adapter.stale == 0),
                  "selection, hover, an eye, a sweep, a failed compile");
    app.reveal({K::Param, "clr"});
    CHECK_MESSAGE(filtered == 4,
                  "a reveal in a list without filters clears nothing");
    const int resets = adapter.resets;
    int app_filtered = 0;
    const Signal<Changes>::Connection app_count = app.changed().connect(
        [&](Changes c) { app_filtered += c.has(Change::Filters); });
    REQUIRE(app.panels().constraints.set_filter(0, std::string("c")));
    CHECK_MESSAGE((app_filtered == 0 && adapter.resets == resets),
                  "another list's filter: App::changed() does not carry it");
    REQUIRE(app.open(fs::path(GS_TEST_DATA_DIR) /
                     "../../docs/instances/ref-instance.json"));
    CHECK_MESSAGE(
        (adapter.matches_fresh_read() && list.children(header).size() == 5),
        "another instance");
}

TEST_CASE("list adapter: an entry without a path is never renamed in place") {
    Included included;
    CHECK_MESSAGE(included.activate(included.id, 0) == Intent::FocusProperties,
                  "a double click on its name shows its sheet instead");

    RenameEditor editor;
    editor.open("params.clr", PanelId::Params);
    editor.close();
    CHECK_MESSAGE((!editor.editing("", PanelId::Params) && !editor.panel() &&
                   editor.entry().empty()),
                  "a closed editor is open on no entry, in no panel");
    editor.open("", PanelId::Params);
    CHECK_MESSAGE(
        (editor.entry().empty() && !editor.editing("", PanelId::Params) &&
         !editor.take_focus()),
        "an empty path opens nothing");

    editor.open("params.clr", PanelId::Params);
    REQUIRE(editor.take_focus());
    editor.refuse("params.W", "taken");
    CHECK_MESSAGE(
        (editor.refusal("params.W") == "taken" &&
         editor.refusal("params.clr").empty() && !editor.take_focus()),
        "a refusal is kept with its entry; the open editor keeps "
        "the keyboard it has");
    editor.refuse("params.clr", "taken");
    CHECK(editor.take_focus());
}

TEST_CASE("list adapter: the Elements, Variables and Ranges lists") {
    App app(std::make_unique<FakeBackend>());
    REQUIRE(app.open(tv_file()));
    app.sync();
    Document & doc = app.document();
    Panels & panels = app.panels();
    using K = SelKey::Kind;
    FakeRetainedAdapter elements(app, panels.elements);
    FakeRetainedAdapter variables(app, panels.variables);
    FakeRetainedAdapter ranges(app, panels.ranges);
    auto fresh = [&] {
        return elements.matches_fresh_read() &&
               variables.matches_fresh_read() && ranges.matches_fresh_read();
    };
    REQUIRE(fresh());
    REQUIRE(ranges.footer().size() == 2);

    app.scene(false);
    CHECK_MESSAGE(fresh(), "the frame's scene: the derived values");
    CHECK(panels.variables.toggle({{K::Design, "A"}}, 0));
    CHECK_MESSAGE((fresh() && elements.resets == 0 && variables.resets == 0),
                  "Released, in both lists");
    CHECK(panels.elements.toggle({{K::Design, "B"}}, 0));
    CHECK_MESSAGE(fresh(), "the eye of a name");
    doc.set_var_value(doc.model()->find_var("p0"), std::vector{0.3, 0.6});
    doc.set_sweep_t(0, 0.6);
    CHECK(fresh());
    app.scene(true);
    CHECK_MESSAGE(fresh(), "values, a sweep move, then the scene that follows");
    REQUIRE(doc.edit("sweeps.tau.samples", 9));
    CHECK_MESSAGE((fresh() && ranges.resets == 0),
                  "the problem's size in the footer");
    CHECK(panels.elements.set_filter(2, true));
    CHECK_MESSAGE((fresh() && elements.resets == 1 && variables.resets == 0),
                  "a filter resets its own list");
    REQUIRE(doc.apply(
        EditBatch("add").set("design.Q", Json{{"type", "point"},
                                              {"domain", "pivot_zone"},
                                              {"value", {0.1, 0.1}}})));
    CHECK_MESSAGE((fresh() && elements.added >= 1 && variables.added == 1),
                  "an added entry: a row by the diff");
    REQUIRE(panels.host.rename({K::Design, "Q"}, "R"));
    CHECK((fresh() && variables.resets == 1));
    REQUIRE(doc.erase("design.R"));
    CHECK_MESSAGE((fresh() && variables.deleted == 1),
                  "an erased object key: the diff removes its row");
    REQUIRE(doc.undo());
    REQUIRE(doc.redo());
    doc.edit("params.W", "1.23 +");
    REQUIRE(doc.stale());
    CHECK_MESSAGE(fresh(), "undo, redo, a failed compile");
    REQUIRE(doc.undo());
    REQUIRE(app.actions().run("solver.solve"));
    app.tick(0.0);
    CHECK(fresh());
    app.reveal({K::Geometry, "couch"});
    CHECK(fresh());
    REQUIRE(app.open(fs::path(GS_TEST_DATA_DIR) /
                     "../../docs/instances/guide-a-domains.json"));
    app.scene(false);
    CHECK_MESSAGE((fresh() && elements.stale == 0 && variables.stale == 0),
                  "another instance");
}

TEST_CASE("list adapter: a collapsed group hides its rows") {
    App app(std::make_unique<FakeBackend>());
    REQUIRE(app.open(tv_file()));
    app.sync();
    using K = SelKey::Kind;
    Panels & panels = app.panels();
    ListPresenter & elements = panels.elements;
    FakeRetainedAdapter adapter(app, elements);
    const RowId points = header_row("points");
    const SelKey corner{K::Geometry, "corner"}, p0{K::Design, "p0"};

    CHECK(elements.expanded(points));
    CHECK(elements.set_expanded(points, false));
    CHECK_MESSAGE(
        (!elements.set_expanded(points, false) && !elements.expanded(points) &&
         elements.children(points).size() == 5 &&
         adapter.matches_fresh_read() && adapter.resets == 0 &&
         !panels.shows(PanelId::Elements, p0)),
        "collapsed: still its children, but no longer shown");
    app.set_focused_panel(PanelId::Elements);
    app.selection().set(corner);
    REQUIRE(app.actions().run("list.previous"));
    CHECK_MESSAGE(app.selection().keys() == std::vector{corner},
                  "Up skips the rows of a collapsed group");
    app.selection().set(p0);
    REQUIRE(app.actions().run("edit.rename"));
    CHECK_MESSAGE((!app.actions().find("list.activate")->is_enabled() &&
                   !app.take_rename_request(PanelId::Elements) &&
                   app.take_rename_request(PanelId::Properties) == p0),
                  "F2 on a row it hides renames in Properties");

    REQUIRE(elements.set_filter(0, 2));
    app.reveal({K::Design, "A"});
    CHECK_MESSAGE((elements.expanded(points) &&
                   panels.shows(PanelId::Elements, {K::Design, "A"}) &&
                   elements.filters()[0].value == DetailValue(0)),
                  "a reveal clears the filters and expands the group");
    REQUIRE(elements.set_expanded(points, false));
    app.reveal(corner);
    CHECK_MESSAGE((!elements.expanded(points) &&
                   elements.expanded(header_row("geometry"))),
                  "only the group of the revealed row");
    REQUIRE(app.actions().run("design.add_point"));
    const SelKey added = app.selection().keys().front();
    CHECK_MESSAGE((elements.expanded(points) &&
                   app.take_rename_request(PanelId::Elements) == added),
                  "an added point opens its name in a row that is shown");
}

TEST_CASE("list adapter: the Quantities, Constraints and Criteria lists") {
    App app(std::make_unique<FakeBackend>());
    REQUIRE(app.open(tv_file()));
    app.sync();
    Document & doc = app.document();
    Selection & sel = app.selection();
    Panels & panels = app.panels();
    using K = SelKey::Kind;
    FakeRetainedAdapter quantities(app, panels.quantities);
    FakeRetainedAdapter constraints(app, panels.constraints);
    FakeRetainedAdapter criteria(app, panels.criteria);
    auto fresh = [&] {
        return quantities.matches_fresh_read() &&
               constraints.matches_fresh_read() &&
               criteria.matches_fresh_read();
    };
    REQUIRE(fresh());
    app.scene(false);
    CHECK_MESSAGE(fresh(), "the frame's scene: values, margins, verdicts");

    CHECK(panels.quantities.toggle({{K::Let, "phi"}}, 0));
    CHECK(panels.quantities.toggle({{K::Display, {}, 2}}, 0));
    CHECK(panels.constraints.toggle({{K::Constraint, "couch"}}, 0));
    CHECK(panels.criteria.toggle({{K::Criterion, "protrusion"}}, 0));
    CHECK_MESSAGE((fresh() && quantities.resets == 0 &&
                   constraints.resets == 0 && criteria.resets == 0),
                  "the eyes and Enabled: rows refreshed, no reset");

    FakeRetainedAdapter elements(app, panels.elements);
    CHECK(panels.constraints.set_filter(0, std::string("c")));
    CHECK(panels.constraints.set_filter(0, std::string("co")));
    CHECK_MESSAGE(
        (fresh() && elements.matches_fresh_read() && constraints.resets == 2 &&
         criteria.resets == 0 && elements.resets == 0),
        "two keystrokes in Constraints' text filter: Constraints "
        "resets, and no other list");
    CHECK(panels.constraints.clear_filters());
    sel.set({K::Design, "A"});
    CHECK(panels.constraints.set_filter(1, true));
    CHECK_MESSAGE((fresh() && constraints.resets == 4 && criteria.resets == 0),
                  "a filter resets its own list only");
    sel.set({K::Design, "c"});
    CHECK_MESSAGE((fresh() && constraints.resets == 5 && criteria.resets == 0),
                  "with Associated on, a selection resets that list only");
    sel.set({K::Constraint, "fridge"});
    CHECK_MESSAGE((fresh() && constraints.resets == 6),
                  "a click on one of its rows, which stays");

    sel.clear();
    FormulaDraft & f = app.formula();
    for(const auto & [id, text] :
        {std::pair{"formula.add_constraint", "0 <= 1"},
         std::pair{"formula.add_criterion", "0"},
         std::pair{"formula.add_quantity", "0"}}) {
        REQUIRE(app.actions().run(id));
        f.set_text(text, 1);
        REQUIRE(f.commit());
    }
    sel.clear();
    REQUIRE(app.actions().run("display.add"));
    CHECK_MESSAGE((fresh() && quantities.added == 2 && criteria.added == 1),
                  "the Adds append: rows by the diff");
    CHECK(panels.constraints.clear_filters());
    const int resets = criteria.resets;
    REQUIRE(panels.host.rename({K::Constraint, "table"}, "desk"));
    CHECK((fresh() && criteria.resets == resets + 1));
    sel.set({K::Let, "q1"});
    REQUIRE(app.actions().run("edit.delete"));
    CHECK_MESSAGE((fresh() && quantities.deleted >= 1),
                  "an erased let moves no path: the diff removes its row");
    REQUIRE(doc.erase("display[9]"));
    app.display_erased(9);
    CHECK(fresh());
    REQUIRE(doc.undo());
    REQUIRE(doc.redo());
    doc.edit("constraints[1].name", "couch");
    CHECK_MESSAGE(fresh(), "undo, redo, a name used twice");
    doc.edit("params.W", "1.23 +");
    REQUIRE(doc.stale());
    CHECK_MESSAGE(fresh(),
                  "a failed compile: every display item becomes a row");
    doc.undo();
    doc.undo();
    REQUIRE(app.actions().run("solver.solve"));
    app.tick(0.0);
    app.scene(false);
    CHECK(fresh());
    REQUIRE(app.open(fs::path(GS_TEST_DATA_DIR) /
                     "../../docs/instances/guide-b-desk-errors-broken.json"));
    app.scene(false);
    CHECK_MESSAGE((fresh() && quantities.stale == 0 && constraints.stale == 0 &&
                   criteria.stale == 0),
                  "another instance, which does not compile");
}

TEST_CASE(
    "list adapter: each filter of the lists, under what its rows follow") {
    App app(std::make_unique<FakeBackend>());
    REQUIRE(app.open(tv_file()));
    app.sync();
    app.scene(false);
    Document & doc = app.document();
    Selection & sel = app.selection();
    Panels & panels = app.panels();
    using K = SelKey::Kind;
    FakeRetainedAdapter elements(app, panels.elements);
    FakeRetainedAdapter constraints(app, panels.constraints);
    FakeRetainedAdapter criteria(app, panels.criteria);
    auto fresh = [&] {
        return elements.matches_fresh_read() &&
               constraints.matches_fresh_read() &&
               criteria.matches_fresh_read();
    };
    const RowId requirements = header_row("requirements");
    auto violated = [&] {
        return std::pair{panels.constraints.children(requirements).size(),
                         panels.criteria.children(std::nullopt).size()};
    };

    REQUIRE(panels.elements.set_filter(0, 1));
    sel.set({K::Design, "A"});
    REQUIRE(app.actions().run("design.fix"));
    CHECK_MESSAGE((fresh() && !panels.elements.row({{K::Design, "A"}})),
                  "Show variables: a point fixed leaves the list");
    REQUIRE(panels.elements.set_filter(0, 2));
    REQUIRE(app.actions().run("design.release"));
    CHECK_MESSAGE((fresh() && !panels.elements.row({{K::Design, "A"}})),
                  "Show constants: a point released leaves it");
    REQUIRE(panels.elements.set_filter(1, false));
    REQUIRE(doc.edit("geometry.box", Json{{"type", "rect"},
                                          {"x", 0.5},
                                          {"y", 0.5},
                                          {"width", 0.2},
                                          {"height", 0.2}}));
    CHECK_MESSAGE((fresh() && panels.elements.row({{K::Geometry, "box"}}) &&
                   !panels.elements.row({{K::Geometry, "couch"}})),
                  "Included off: the instance's own geometry only");
    REQUIRE(panels.elements.clear_filters());
    REQUIRE(panels.elements.set_filter(2, true));
    REQUIRE(doc.edit("let.v1", "vec(1, 2)"));
    CHECK_MESSAGE((fresh() && panels.elements.row({{K::Let, "v1"}})),
                  "Vectors: a new let of type Vec joins the derived ones");

    REQUIRE(panels.constraints.set_filter(0, std::string("pivot")));
    REQUIRE(panels.criteria.set_filter(0, std::string("link")));
    REQUIRE(doc.edit("constraints[0].name", "pivot_couch"));
    REQUIRE(doc.edit("criteria[1].name", "link_view"));
    CHECK_MESSAGE(
        (fresh() && panels.constraints.children(requirements).size() == 2 &&
         panels.criteria.children(std::nullopt).size() == 6),
        "Text: an entry whose name or formula comes to hold it joins");
    REQUIRE(panels.constraints.clear_filters());
    REQUIRE(panels.criteria.clear_filters());
    REQUIRE(panels.constraints.set_filter(4, true));
    REQUIRE(panels.criteria.set_filter(4, true));
    CHECK(panels.constraints.children(std::nullopt).empty());
    REQUIRE(app.actions().run("edit.undo"));
    REQUIRE(app.actions().run("edit.undo"));
    sel.set({K::Constraint, "fridge"});
    REQUIRE(app.actions().run("item.disable"));
    sel.set({K::Criterion, "wall_x"});
    REQUIRE(app.actions().run("item.disable"));
    CHECK_MESSAGE(
        (fresh() && panels.constraints.children(requirements).size() == 1 &&
         panels.criteria.children(std::nullopt).size() == 1),
        "Disabled: an entry disabled joins");

    REQUIRE(panels.constraints.clear_filters());
    sel.set({K::Design, "A"});
    REQUIRE(panels.constraints.set_filter(1, true));
    REQUIRE(panels.constraints.set_filter(2, true));
    sel.set({K::Design, "c"});
    CHECK_MESSAGE(fresh(), "Associated and Direct only: another basis");
    REQUIRE(doc.edit("constraints[0].expr",
                     "clearance(tv, couch) + 0 * c.x >= clr"));
    CHECK_MESSAGE(
        (fresh() && panels.constraints.children(requirements).size() == 2),
        "an entry that comes to use the basis directly joins");
    sel.set({K::Design, "A"});
    CHECK_MESSAGE(
        (fresh() && panels.constraints.filters()[1].tip.find("now A") !=
                        std::string::npos),
        "Associated's tip names the basis: filters() follows Selection");

    REQUIRE(panels.constraints.clear_filters());
    REQUIRE(panels.criteria.clear_filters());
    REQUIRE(panels.constraints.set_filter(3, true));
    REQUIRE(panels.criteria.set_filter(3, true));
    const auto before = violated();
    doc.set_var_value(doc.model()->find_var("A"), std::vector{0.9, 0.9});
    CHECK_MESSAGE(fresh(), "Violated: a move, before the frame's scene");
    app.scene(false);
    CHECK_MESSAGE((fresh() && violated() != before),
                  "Violated: the scene that follows the move");
    REQUIRE(doc.undo());
    app.scene(false);
    doc.set_sweep_t(0, 0.4);
    app.scene(true);
    REQUIRE(app.actions().run("solver.solve"));
    app.tick(0.0);
    app.scene(false);
    CHECK_MESSAGE((fresh() && elements.stale == 0 && constraints.stale == 0 &&
                   criteria.stale == 0),
                  "Violated: undo, a sweep move, solver results");
}

TEST_CASE("list adapter: each cell is of its column's kind") {
    App app(std::make_unique<FakeBackend>());
    REQUIRE(app.open(tv_file()));
    app.sync();
    app.scene(false);
    RequirementsList requirements(app);
    Included included;
    auto fit = [&] {
        std::string out;
        for(const PanelInfo & p : all_panels())
            if(ListPresenter * list = app.panels().list(p.id))
                if(const std::string m = misfit(*list); !m.empty())
                    out += std::format("{}: {}; ", p.key, m);
        for(ListPresenter * list :
            std::initializer_list<ListPresenter *>{&requirements, &included})
            out += misfit(*list);
        return out;
    };
    CHECK_MESSAGE(fit().empty(), fit());
    app.document().edit("params.W", "1.23 +");
    REQUIRE(app.document().stale());
    CHECK_MESSAGE(fit().empty(),
                  "a failed compile, every display item a row: " << fit());
    REQUIRE(app.open(fs::path(GS_TEST_DATA_DIR) /
                     "../../docs/instances/guide-b-desk-errors-broken.json"));
    app.scene(false);
    CHECK_MESSAGE(fit().empty(),
                  "an instance that does not compile: " << fit());
}

TEST_CASE("list adapter: a toggle column is named for what it toggles") {
    App app(nullptr);
    std::vector<std::string> toggles;
    for(const PanelInfo & p : all_panels())
        if(ListPresenter * list = app.panels().list(p.id))
            for(const Column & c : list->columns())
                if(c.kind == ListCell::Kind::Check ||
                   c.kind == ListCell::Kind::Eye)
                    toggles.push_back(std::format("{}: {}", p.key, c.label));
    CHECK_MESSAGE(
        (toggles ==
         std::vector<std::string>{"elements: Eye", "elements: Helper",
                                  "variables: Released", "quantities: Eye",
                                  "constraints: On", "constraints: Eye",
                                  "criteria: On"}),
        "an eye, Released and Enabled look alike without their header");
}

TEST_CASE("list adapter: the rows a list keeps follow what it is built from") {
    App app(std::make_unique<FakeBackend>());
    REQUIRE(app.open(tv_file()));
    app.sync();
    app.scene(false);
    Document & doc = app.document();
    Selection & sel = app.selection();
    Panels & panels = app.panels();
    using K = SelKey::Kind;
    auto hold = [&] {
        return kept_rows_hold(panels.constraints) &&
               kept_rows_hold(panels.criteria) &&
               kept_rows_hold(panels.elements);
    };
    REQUIRE(hold());

    sel.set({K::Design, "A"});
    REQUIRE(panels.constraints.set_filter(1, true));
    const std::vector<ListRow> of_a = rows_of(panels.constraints);
    sel.set({K::Design, "c"});
    CHECK_MESSAGE((hold() && rows_of(panels.constraints) != of_a),
                  "Associated: another basis");
    REQUIRE(doc.edit("constraints[0].expr",
                     "clearance(tv, couch) + 0 * c.x >= clr"));
    CHECK_MESSAGE(hold(), "Associated: an entry comes to use the basis");
    REQUIRE(panels.constraints.clear_filters());

    REQUIRE(panels.constraints.set_filter(3, true));
    REQUIRE(panels.criteria.set_filter(3, true));
    const std::vector<ListRow> before = rows_of(panels.criteria);
    doc.set_var_value(doc.model()->find_var("A"), std::vector{0.9, 0.9});
    app.scene(false);
    CHECK_MESSAGE((hold() && rows_of(panels.criteria) != before),
                  "Violated: the scene that follows a move");
    REQUIRE(doc.undo());
    app.scene(true);
    CHECK_MESSAGE(hold(), "Violated: undo, on the coarse grid");
    REQUIRE(panels.constraints.clear_filters());
    REQUIRE(panels.criteria.clear_filters());

    REQUIRE(doc.erase("constraints[0]"));
    REQUIRE(doc.edit("criteria[0].name", "renamed"));
    CHECK_MESSAGE(hold(), "an erased entry shifts the paths after it");
    CHECK_FALSE(doc.erase("geometry.nothing"));
    CHECK_MESSAGE(hold(),
                  "a refused batch: the same JSON, rebuilt, no compile");
    REQUIRE(doc.edit("let.v1", "vec(1, 2)"));
    REQUIRE(panels.elements.set_filter(2, true));
    REQUIRE(panels.elements.set_filter(0, 1));
    sel.set({K::Design, "A"});
    REQUIRE(app.actions().run("design.fix"));
    CHECK_MESSAGE(hold(), "Elements: a point fixed, a vector let");
    doc.set_sweep_t(0, 0.4);
    app.scene(false);
    CHECK_MESSAGE(hold(), "Elements: the derived values of a new scene");
    REQUIRE(app.open(fs::path(GS_TEST_DATA_DIR) /
                     "../../docs/instances/guide-b-desk-errors-broken.json"));
    app.scene(false);
    CHECK_MESSAGE(hold(), "another instance, which does not compile");
}

}  // namespace gs::test

namespace gs::test {

TEST_CASE("list adapter: Sketch changes are read again, Tool changes none") {
    // A strip of 100 points settled first: one component of 238 columns,
    // diagnosed at a tick, the only change that tick brings.
    LoadResult strip = Instance::from_json(
        strip_doc(100, 3, 1, 0), fs::temp_directory_path() / "strip.json");
    REQUIRE(strip.instance);
    REQUIRE(sketch::settle(*strip.instance).converged);
    App app(std::make_unique<FakeBackend>());
    Document & doc = app.document();
    REQUIRE(doc.load_json(strip.instance->doc(),
                          fs::temp_directory_path() / "test_app_strip.json"));
    app.sync();
    app.scene(false);
    Panels & panels = app.panels();
    FakeRetainedAdapter constraints(app, panels.constraints);
    FakeRetainedAdapter elements(app, panels.elements);
    FakeRetainedAdapter params(app, panels.params);
    auto fresh = [&] {
        return constraints.matches_fresh_read() &&
               elements.matches_fresh_read() && params.matches_fresh_read();
    };
    REQUIRE(fresh());
    Changes seen;
    const Signal<Changes>::Connection link =
        app.changed().connect([&](Changes c) { seen |= c; });

    const int refreshes = constraints.refreshes;
    app.tick(0.0);
    CHECK(seen == Changes(Change::Sketch));
    CHECK_MESSAGE((fresh() && constraints.refreshes == refreshes + 1 &&
                   constraints.resets == 0),
                  "a diagnosis arriving: Sketch alone, the rows read again");

    const Model & m = *doc.sketch_model();
    const auto p =
        std::ranges::find(m.sketch(), std::string("P50"), &SketchInfo::name);
    REQUIRE(app.sketch().begin(
        sketch::drag_point(m, static_cast<int>(p - m.sketch().begin())),
        "move P50"));
    seen = {};
    REQUIRE(app.sketch().move(std::vector<Vec2d>{{25.05, 0.05}}));
    CHECK(seen == Changes(Change::Sketch));
    CHECK_MESSAGE(fresh(), "a drag frame: the live values");
    REQUIRE(app.sketch().end());
    CHECK_MESSAGE(fresh(), "its release, one commit");
    REQUIRE(doc.undo());
    app.tick(0.0);
    CHECK(fresh());

    const int before = constraints.refreshes + constraints.resets;
    app.changed().emit(Change::Tool);
    CHECK_MESSAGE(
        (fresh() && constraints.refreshes + constraints.resets == before),
        "a tool's change reads no list again");
    CHECK((constraints.stale == 0 && elements.stale == 0 && params.stale == 0));
}

}  // namespace gs::test

namespace gs::test {

TEST_CASE("list adapter: a sketch's body rows, helper toggles and glyph eyes") {
    App app(std::make_unique<FakeBackend>());
    Document & doc = app.document();
    REQUIRE(app.open(fs::path(GS_TEST_DATA_DIR) /
                     "../../docs/instances/sketch-body.json"));
    app.sync();
    app.scene(false);
    Panels & panels = app.panels();
    using K = SelKey::Kind;
    FakeRetainedAdapter elements(app, panels.elements);
    FakeRetainedAdapter constraints(app, panels.constraints);
    auto fresh = [&] {
        return elements.matches_fresh_read() &&
               constraints.matches_fresh_read();
    };
    REQUIRE(fresh());
    CHECK_MESSAGE(
        (misfit(panels.elements).empty() && misfit(panels.constraints).empty()),
        misfit(panels.elements) << misfit(panels.constraints));
    CHECK_MESSAGE(std::ranges::contains(elements.names(), std::string("tv.c")),
                  "the members are read under their body's row");

    REQUIRE(panels.elements.toggle({{K::Element, "wall"}}, 5));
    CHECK_MESSAGE(fresh(), "a helper flag: one commit");
    REQUIRE(panels.elements.toggle({{K::Body, "tv"}}, 0));
    CHECK_MESSAGE(fresh(), "a body's eye: its members' tips too");
    REQUIRE(panels.constraints.toggle({{K::Constraint, "pin"}}, 1));
    CHECK_MESSAGE(fresh(), "a glyph's eye");

    const int resets = elements.resets;
    REQUIRE(panels.host.rename({K::Body, "tv"}, "telly"));
    CHECK_MESSAGE(
        (fresh() && elements.resets == resets + 1 &&
         std::ranges::contains(elements.names(), std::string("telly.c"))),
        "a body renamed: every member's path moves");
    REQUIRE(doc.undo());
    app.sync();
    CHECK(fresh());

    REQUIRE(app.sketch().begin(
        sketch::drag_body(*doc.sketch_model(), 0, {0.0, 0.0}), "move tv"));
    REQUIRE(app.sketch().move(std::vector<Vec2d>{{0.6, 1.25}}));
    CHECK_MESSAGE(fresh(), "a body's drag: its pose and members, live");
    REQUIRE(app.sketch().end());
    app.tick(0.0);
    CHECK(fresh());
    REQUIRE(!doc.edit("params.W", "1.23 +"));
    REQUIRE(doc.sketch_stale());
    CHECK_MESSAGE(fresh(), "a failed compile: the states greyed");
    REQUIRE(doc.undo());
    CHECK(fresh());

    REQUIRE(app.open(tv_file()));
    app.sync();
    CHECK_MESSAGE(fresh(),
                  "a file without a sketch: its sketch columns and "
                  "filter hidden again");
    CHECK((elements.stale == 0 && constraints.stale == 0));
}

TEST_CASE("list adapter: a constraint that moves between Geometry and Design") {
    // A formula edit moves a row to the other header with its RowId, under
    // Content alone: a cache that adds before it removes finds the row
    // present, then drops it with its old parent.
    App app(std::make_unique<FakeBackend>());
    Document & doc = app.document();
    REQUIRE(app.open(fs::path(GS_TEST_DATA_DIR) /
                     "../../docs/instances/sketch-basic.json"));
    app.sync();
    app.scene(false);
    ListPresenter & list = app.panels().constraints;
    FakeRetainedAdapter constraints(app, list);
    auto parent_of = [&](const char * name) {
        const std::optional<ListRow> r =
            list.row({{SelKey::Kind::Constraint, name}});
        REQUIRE((r && r->parent));
        return r->parent->group;
    };
    REQUIRE(parent_of("width") == "geometry");
    REQUIRE(doc.edit("constraints[2].expr", "dist(O, E) >= room_w"));
    CHECK(parent_of("width") == "design");
    CHECK_MESSAGE(constraints.matches_fresh_read(), "Geometry to Design");
    REQUIRE(doc.edit("constraints[2].expr", "dist(O, E) == room_w"));
    CHECK(parent_of("width") == "geometry");
    CHECK_MESSAGE(constraints.matches_fresh_read(), "Design to Geometry");
    CHECK(constraints.stale == 0);
}

}  // namespace gs::test

namespace gs::test {

// A tool that adds the first element of a sketch changes what the lists
// show of a file without one: Constraints' groups, Elements' Sketch group,
// filter and Helper column. Its undo takes them away again.
TEST_CASE("list adapter: a tool adds a sketch section, its undo removes it") {
    App app(std::make_unique<FakeBackend>());
    Document & doc = app.document();
    REQUIRE(app.open(tv_file()));
    app.sync();
    app.scene(false);
    FakeLog log;
    provide_fake(app, log);
    std::vector<std::unique_ptr<FakeRetainedAdapter>> lists;
    for(const PanelId id :
        {PanelId::Elements, PanelId::Variables, PanelId::Quantities,
         PanelId::Params, PanelId::Ranges, PanelId::Constraints,
         PanelId::Criteria})
        lists.push_back(
            std::make_unique<FakeRetainedAdapter>(app, *app.panels().list(id)));
    auto fresh = [&] {
        return std::ranges::all_of(
            lists, [](const auto & l) { return l->matches_fresh_read(); });
    };
    REQUIRE(fresh());

    ToolHost & tools = app.tools();
    REQUIRE(tools.start("tool.segment"));
    int refreshes = 0;
    for(const auto & l : lists) refreshes += l->refreshes + l->resets;
    tools.event({.kind = ToolEvent::Kind::Move, .world = {0.1, 0.1}});
    tools.event({.kind = ToolEvent::Kind::Press, .world = {0.1, 0.1}});
    int after = 0;
    for(const auto & l : lists) after += l->refreshes + l->resets;
    CHECK_MESSAGE((fresh() && after == refreshes),
                  "a tool's move and press, before its commit: Tool only, no "
                  "list reads again");

    tools.event({.kind = ToolEvent::Kind::Press, .world = {0.5, 0.1}});
    REQUIRE((log.commits == 1 && doc.has_sketch()));
    CHECK_MESSAGE(fresh(), "the sketch section added");
    REQUIRE(doc.undo());
    app.sync();
    CHECK_MESSAGE((!doc.has_sketch() && fresh()), "and removed by its undo");
    REQUIRE(doc.redo());
    app.sync();
    CHECK_MESSAGE((doc.has_sketch() && fresh()), "and added by its redo");
    REQUIRE(doc.undo());
    app.sync();
    // An edit that selects nothing: the filters are read again on
    // Structure, not on the Selection a tool's commit brings.
    REQUIRE(doc.edit(
        "sketch",
        Json{{"elements", {{"Q", {{"type", "point"}, {"value", {0, 0}}}}}}}));
    CHECK_MESSAGE((doc.has_sketch() && fresh()),
                  "a section added without a tool");
    for(const auto & l : lists) CHECK(l->stale == 0);
}

}  // namespace gs::test

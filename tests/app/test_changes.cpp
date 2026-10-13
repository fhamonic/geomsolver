#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "gs/app/changes.hpp"
#include "gs/app/document.hpp"
#include "gs/app/selection.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

// Every Changes a signal emitted since the last take().
struct Recorder {
    explicit Recorder(Signal<Changes> & signal)
        : connection(
              signal.connect([this](Changes c) { emits.push_back(c); })) {}
    std::vector<Changes> take() { return std::exchange(emits, {}); }

    std::vector<Changes> emits;
    Signal<Changes>::Connection connection;
};

const Changes kCompiled =
    Change::Content | Change::Model | Change::Values | Change::Sweep;

}  // namespace

TEST_CASE("changes: bit set and signal") {
    Changes c;
    CHECK_FALSE(c.any());
    c |= Change::Values;
    CHECK((c.has(Change::Values) && !c.has(Change::Model) && c.any()));
    CHECK(((Change::Values | Change::Model) == (c | Change::Model)));

    Signal<int> signal;
    std::string calls;
    auto a = signal.connect([&](int v) { calls += "a" + std::to_string(v); });
    Signal<int>::Connection b;
    {
        auto scoped = signal.connect([&](int) { calls += "s"; });
        b = signal.connect([&](int v) { calls += "b" + std::to_string(v); });
        signal.emit(1);
    }
    signal.emit(2);
    CHECK_MESSAGE(calls == "a1sb1a2b2",
                  "slots run in connection order; a destroyed connection "
                  "disconnects its slot");

    calls.clear();
    Signal<int>::Connection later, late;
    auto first = signal.connect([&](int) {
        calls += "f";
        later.disconnect();
        late = signal.connect([&](int) { calls += "l"; });
    });
    later = signal.connect([&](int) { calls += "x"; });
    signal.emit(3);
    CHECK_MESSAGE(calls == "a3b3f",
                  "a slot disconnected during an emit is not called by it, "
                  "and one connected during it waits for the next emit");
    calls.clear();
    first.disconnect();
    signal.emit(4);
    CHECK(calls == "a4b4l");

    Signal<int>::Connection moved = std::move(a);
    calls.clear();
    signal.emit(5);
    CHECK_MESSAGE(calls == "a5b5l", "a moved connection stays connected");
    moved = {};
    calls.clear();
    signal.emit(6);
    CHECK_MESSAGE(calls == "b6l",
                  "assigning over a connection disconnects its slot");

    Signal<int>::Connection outlives;
    {
        Signal<int> gone;
        outlives = gone.connect([](int) {});
    }
    outlives.disconnect();
}

TEST_CASE("changes: what each document call emits") {
    const fs::path instance = tv_file();
    const fs::path dir = scratch_copy(instance, "changes");
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    } cleanup{dir};
    Document doc;
    Recorder rec(doc.changed());

    CHECK_FALSE(doc.load(dir / "missing.json"));
    CHECK(rec.take() == std::vector<Changes>{Change::Log});

    if(!doc.load(dir / instance.filename()) || !doc.model())
        FAIL("changes: the copied instance compiles");
    CHECK_MESSAGE(
        (rec.take() ==
         std::vector<Changes>{Change::Instance | Change::Structure | kCompiled |
                              Change::History | Change::Log}),
        "load: another instance, compiled, a cleared history, a log line");

    CHECK(doc.edit("params.clr", 0.03));
    CHECK_MESSAGE(
        (rec.take() == std::vector<Changes>{kCompiled | Change::History}),
        "edit: one emit for the edit and the apply it runs; the undo step "
        "and dirty() are History");

    CHECK(doc.erase("constraints[4]"));
    CHECK_MESSAGE(
        (rec.take() ==
         std::vector<Changes>{Change::Structure | kCompiled | Change::History}),
        "erasing an array entry shifts the paths after it");

    CHECK_FALSE(doc.apply(EditBatch("bad").erase("params.nope")));
    CHECK_MESSAGE((rec.take() == std::vector<Changes>{Change::Log}),
                  "a refused batch changes nothing but the log");

    const int a = doc.model()->find_var("A");
    const double moved[2] = {0.2, 0.15};
    doc.set_var_value(a, moved);
    CHECK_MESSAGE(
        (rec.take() == std::vector<Changes>{Change::Values | Change::History}),
        "a design move outside a gesture is a step of its own");

    doc.begin_gesture("move A");
    CHECK_MESSAGE(rec.take().empty(), "opening a gesture changes nothing");
    const double again[2] = {0.21, 0.15};
    doc.set_var_value(a, again);
    CHECK_MESSAGE((rec.take() == std::vector<Changes>{Change::Values}),
                  "a move inside a gesture records no step");
    doc.end_gesture();
    CHECK((rec.take() == std::vector<Changes>{Change::History}));

    doc.set_sweep_t(0, 0.5);
    CHECK((rec.take() == std::vector<Changes>{Change::Sweep}));
    doc.set_sweep_t(0, 0.5);
    CHECK_MESSAGE(rec.take().empty(), "the same sweep position emits nothing");

    CHECK(doc.undo());
    CHECK_MESSAGE(
        (rec.take() ==
         std::vector<Changes>{Change::Structure | kCompiled | Change::History}),
        "undo: paths may shift, the snapshot's model and x");

    CHECK(doc.save());
    CHECK_MESSAGE(
        (rec.take() == std::vector<Changes>{Change::History | Change::Log}),
        "save: dirty() turns false");

    doc.log(LogEntry::Level::Info, "note");
    CHECK((rec.take() == std::vector<Changes>{Change::Log}));

    CHECK(doc.reload());
    CHECK((rec.take() ==
           std::vector<Changes>{Change::Instance | Change::Structure |
                                kCompiled | Change::History | Change::Log}));
}

TEST_CASE("changes: selection generation and signal") {
    Document doc;
    if(!doc.load(tv_file())) FAIL("changes: the instance loads");
    Selection sel;
    Recorder rec(sel.changed());
    sel.follow(doc);
    const std::uint64_t g0 = sel.generation();
    CHECK_MESSAGE((rec.take().empty() && g0 == 0),
                  "following a document with nothing selected changes "
                  "nothing");

    const SelKey a{SelKey::Kind::Design, "A"};
    const SelKey couch{SelKey::Kind::Constraint, "couch"};
    sel.set(a);
    sel.set(a);
    sel.add(a);
    CHECK_MESSAGE((sel.generation() == g0 + 1 &&
                   rec.take() == std::vector<Changes>{Change::Selection}),
                  "selecting what is already selected emits nothing");
    sel.toggle(couch);
    sel.reveal(couch);
    sel.reveal(couch);
    CHECK_MESSAGE((sel.generation() == g0 + 4 && rec.take().size() == 3 &&
                   sel.reveal_generation() == 2),
                  "revealing the same entry again asks the lists again");

    sel.follow(doc);
    CHECK_MESSAGE(rec.take().empty(),
                  "follow on an unchanged document emits nothing");
    CHECK(doc.erase("constraints[0]"));
    sel.follow(doc);
    CHECK_MESSAGE(
        (rec.take().size() == 1 && sel.keys() == std::vector<SelKey>{a}),
        "follow drops the deleted constraint and emits once");

    sel.remove(couch);
    sel.clear();
    sel.clear();
    CHECK((rec.take().size() == 1 && sel.empty()));
}

}  // namespace gs::test

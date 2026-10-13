#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "gs/app/app.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

// An instance without includes, which loads from memory too.
fs::path domains_file() {
    return fs::path(GS_TEST_DATA_DIR) /
           "../../docs/instances/guide-a-domains.json";
}

std::string key_of(const fs::path & file) {
    return "visibility." + fs::absolute(file).lexically_normal().string();
}

nlohmann::json stored(const SettingsMap & settings, const fs::path & file) {
    const auto it = settings.values.find(key_of(file));
    return it == settings.values.end() ? nlohmann::json()
                                       : nlohmann::json::parse(it->second);
}

}  // namespace

TEST_CASE("visibility: a name's eye is kept in the settings for its file") {
    const fs::path dir = scratch_copy(tv_file(), "visibility");
    const fs::path tv = dir / tv_file().filename();
    SettingsMap settings;
    {
        App app(nullptr, std::make_unique<MapSettingsStore>(settings));
        Document & doc = app.document();
        Visibility & eyes = app.visibility();
        REQUIRE(app.open(tv));
        int changes = 0;
        auto counter = app.changed().connect(
            [&](Changes c) { changes += c.has(Change::Visibility) ? 1 : 0; });
        eyes.set_hidden("A", true);
        eyes.set_hidden("A", true);
        eyes.set_let_shown("p", true);
        CHECK_MESSAGE((eyes.hidden("A") && eyes.let_shown("p") &&
                       !eyes.hidden("B") && changes == 2),
                      "Visibility is emitted once per eye that changes");
        CHECK_MESSAGE((!doc.dirty() && doc.undo_count() == 0),
                      "an eye is neither an edit nor an undo step");
        CHECK((stored(settings, tv) ==
               nlohmann::json{{"hidden", {"A"}}, {"shown", {"p"}}}));

        REQUIRE(doc.edit("params.clr", 0.06));
        doc.undo();
        REQUIRE(doc.reload());
        app.sync();
        CHECK_MESSAGE((eyes.hidden("A") && eyes.let_shown("p") && changes == 2),
                      "an undo and a reload keep the eyes, unchanged");

        REQUIRE(app.open(domains_file()));
        CHECK_MESSAGE(
            (!eyes.hidden("A") && !eyes.let_shown("p") && changes == 3),
            "another file has eyes of its own");
        REQUIRE(app.open(tv));
        CHECK(eyes.hidden("A"));

        const fs::path copy = dir / "copy.json";
        REQUIRE(doc.save_as(copy));
        app.sync();
        eyes.set_hidden("B", true);
        CHECK_MESSAGE(
            (stored(settings, copy) ==
                 nlohmann::json{{"hidden", {"A", "B"}}, {"shown", {"p"}}} &&
             stored(settings, tv) ==
                 nlohmann::json{{"hidden", {"A"}}, {"shown", {"p"}}}),
            "after a Save As the eyes are kept, and stored for the new file");
    }
    App restarted(nullptr, std::make_unique<MapSettingsStore>(settings));
    REQUIRE(restarted.open(tv));
    CHECK_MESSAGE((restarted.visibility().hidden("A") &&
                   !restarted.visibility().hidden("B")),
                  "the next start reads them back");

    const int writes = settings.writes;
    REQUIRE(restarted.document().load_json(
        nlohmann::ordered_json::parse(read_all(domains_file())), {}));
    restarted.sync();
    restarted.visibility().set_hidden("P", true);
    CHECK_MESSAGE(
        (restarted.visibility().hidden("P") && settings.writes == writes),
        "a document without a file keeps its eyes for the session");
    fs::remove_all(dir);
}

TEST_CASE("visibility: a rename keeps the eye, the selection and the hot key") {
    SettingsMap settings;
    App app(nullptr, std::make_unique<MapSettingsStore>(settings));
    REQUIRE(app.open(tv_file()));
    app.sync();
    app.visibility().set_hidden("A", true);
    app.selection().set({SelKey::Kind::Design, "A"});
    app.set_hot(HotSource::List, SelKey{SelKey::Kind::Design, "A"});
    PanelHost & host = app.panels().host;
    host.renamer().open("design.A", PanelId::Properties);
    REQUIRE(host.rename({SelKey::Kind::Design, "A"}, "A2"));
    app.sync();
    const SelKey a2{SelKey::Kind::Design, "A2"};
    CHECK((app.visibility().hidden("A2") && !app.visibility().hidden("A") &&
           app.selection().keys() == std::vector<SelKey>{a2} &&
           app.hot() == a2));
    CHECK(
        stored(settings, tv_file()) ==
        nlohmann::json{{"hidden", {"A2"}}, {"shown", nlohmann::json::array()}});

    app.selection().reveal(a2);
    REQUIRE(host.rename(app.selection().keys().front(), "pivot_of_the_wall"));
    app.sync();
    const SelKey pivot{SelKey::Kind::Design, "pivot_of_the_wall"};
    CHECK_MESSAGE((app.visibility().hidden("pivot_of_the_wall") &&
                   app.selection().keys() == std::vector<SelKey>{pivot} &&
                   app.selection().to_reveal() == pivot && app.hot() == pivot),
                  "renamed through the selected key itself, which the "
                  "selection rewrites first");

    app.entry_renamed({SelKey::Kind::Constraint, "pivot_of_the_wall"}, "A3");
    CHECK_MESSAGE(app.visibility().hidden("pivot_of_the_wall"),
                  "a constraint's name is not one with an eye");
}

TEST_CASE("visibility: an undone and a redone rename move the eye back") {
    SettingsMap settings;
    App app(nullptr, std::make_unique<MapSettingsStore>(settings));
    REQUIRE(app.open(tv_file()));
    app.sync();
    const SelKey b{SelKey::Kind::Design, "B"}, bee{SelKey::Kind::Design, "Bee"};
    app.visibility().set_hidden("B", true);
    app.selection().set(b);
    app.set_hot(HotSource::List, b);
    PanelHost & host = app.panels().host;
    host.renamer().open("design.B", PanelId::Properties);
    REQUIRE(host.rename(b, "Bee"));
    app.sync();
    REQUIRE(app.document().undo());
    app.sync();
    CHECK_MESSAGE(
        (app.visibility().hidden("B") && !app.visibility().hidden("Bee") &&
         app.selection().keys() == std::vector<SelKey>{b} && app.hot() == b),
        "the undo moves the eye, the selection and the hot key back");
    CHECK(
        stored(settings, tv_file()) ==
        nlohmann::json{{"hidden", {"B"}}, {"shown", nlohmann::json::array()}});
    REQUIRE(app.document().redo());
    app.sync();
    CHECK((app.visibility().hidden("Bee") && !app.visibility().hidden("B") &&
           app.selection().keys() == std::vector<SelKey>{bee} &&
           app.hot() == bee));

    app.visibility().set_hidden("Bee", false);
    REQUIRE(app.document().undo());
    app.sync();
    CHECK_MESSAGE(!app.visibility().hidden("B"),
                  "an eye opened after the rename stays open: it is no step");
    CHECK_MESSAGE(app.selection().keys() == std::vector<SelKey>{b},
                  "the step a redo put back keeps its rename for the next "
                  "undo");
}

TEST_CASE("hot key: the canvas's, else the list's, signalled on a change") {
    App app(nullptr);
    int hovers = 0;
    auto counter = app.changed().connect(
        [&](Changes c) { hovers += c.has(Change::Hover) ? 1 : 0; });
    const SelKey a{SelKey::Kind::Design, "A"}, tv{SelKey::Kind::Let, "tv"};
    app.set_hot(HotSource::List, a);
    app.set_hot(HotSource::List, a);
    CHECK((app.hot() == a && hovers == 1));
    app.set_hot(HotSource::Canvas, tv);
    app.set_hot(HotSource::List, std::nullopt);
    CHECK_MESSAGE((app.hot() == tv && hovers == 2),
                  "the canvas's key wins; the list's going changes nothing");
    app.set_hot(HotSource::Canvas, std::nullopt);
    CHECK((!app.hot() && hovers == 3));

    app.set_hot(HotSource::Canvas, SelKey{SelKey::Kind::Display, "", 5});
    app.set_hot(HotSource::List, SelKey{SelKey::Kind::Display, "", 2});
    app.display_erased(2);
    CHECK_MESSAGE(app.hot() == SelKey(SelKey::Kind::Display, "", 4),
                  "an erased display item moves the later ones up");
    app.display_erased(4);
    CHECK_MESSAGE(!app.hot(), "and drops its own key, from both sources");
}

TEST_CASE("hot key: a deletion of display items moves the later ones up") {
    App app(nullptr);
    REQUIRE(app.open(tv_file()));
    app.sync();
    app.selection().set({SelKey::Kind::Display, "", 1});
    app.selection().add({SelKey::Kind::Display, "", 3});
    app.set_hot(HotSource::Canvas, SelKey{SelKey::Kind::Display, "", 8});
    app.request_delete();
    CHECK((app.document().undo_label() == "delete 2 entries" &&
           app.selection().empty() &&
           app.hot() == SelKey(SelKey::Kind::Display, "", 6)));
}

}  // namespace gs::test

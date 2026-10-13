#include <array>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/solver_presenter.hpp"

#include "helpers.hpp"

namespace gs::test {

// Every section of the TV instance is edited from the panels, Properties, the
// Document panel and the formula pop-up counting as panels, each edit one undo
// step that changes its section; "format" is only read, raised by the program
// and lowered by Use format /1.
TEST_CASE(
    "gate: every section of the TV instance is editable from the panels") {
    const fs::path dir = scratch_copy(tv_file(), "gate");
    App app(std::make_unique<FakeBackend>());
    REQUIRE(app.open(dir / tv_file().filename()));
    app.sync();
    app.scene(false);
    Document & doc = app.document();
    Selection & sel = app.selection();
    Panels & panels = app.panels();
    PropertiesPresenter & properties = panels.properties;
    using K = SelKey::Kind;

    std::set<std::string> edited;
    // `edit` makes one undo step, which changes `section` and no other.
    auto check = [&](const char * section, const std::function<bool()> & edit) {
        const Json before = doc.instance()->doc();
        const std::size_t steps = doc.undo_count();
        const bool done = edit();
        app.sync();
        const Json & after = doc.instance()->doc();
        bool others_kept = true;
        for(const auto & [key, value] : before.items())
            if(key != section && key != "format")
                others_kept &= after.contains(key) && after[key] == value;
        CHECK_MESSAGE((done && doc.undo_count() == steps + 1 &&
                       before[section] != after[section] && others_kept),
                      section << ": edited from its panel in one step");
        edited.insert(section);
    };
    // The formula pop-up on `key`, its formula replaced, then committed.
    auto formula = [&](const SelKey & key, const std::string & text) {
        FormulaDraft & f = app.formula();
        if(!f.edit(key)) return false;
        f.set_text(text, text.size());
        return f.commit();
    };

    check("description", [&] {
        return panels.document.set("description", std::string("A TV."));
    });
    check("include", [&] {
        return panels.document.set("include0",
                                   std::string("./example_room_ref.json"));
    });
    check("params", [&] {
        return properties.set({K::Param, "clr"}, "value", std::string("4cm"));
    });
    check("design", [&] {
        return properties.set({K::Design, "phi0"}, "released", false);
    });
    check("sweeps", [&] {
        return properties.set({K::Sweep, "tau"}, "max", std::string("0.9"));
    });
    check("let",
          [&] { return formula({K::Let, "normal"}, "dir(phi + 89deg)"); });
    check("display",
          [&] { return properties.set({K::Let, "tv"}, "fill", false); });
    check("constraints", [&] {
        return formula({K::Constraint, "chair"},
                       "clearance(tv, chair1) >= 2 * clr");
    });
    check("criteria",
          [&] { return properties.set({K::Criterion, "link_1"}, "role", 5); });
    check("solver", [&] {
        SolverPresenter solver(app);
        solver.begin_frame();
        solver.set_count(&SolverSettings::starts, 32);
        solver.store_settings();
        return true;
    });
    std::set<std::string> sections;
    for(const auto & [key, value] : doc.instance()->doc().items())
        sections.insert(key);
    sections.erase("format");
    CHECK_MESSAGE(edited == sections, "every section but format");

    const Json format = *doc.instance()->get("format");
    CHECK_MESSAGE((format == "geomsolver-instance/1" &&
                   !panels.document.set("format", std::string("x"))),
                  "format: read-only in the Document panel");
    sel.set({K::Criterion, "link_2"});
    REQUIRE(properties.set({K::Criterion, "link_2"}, "enabled", false));
    CHECK_MESSAGE(*doc.instance()->get("format") == "geomsolver-instance/2",
                  "raised by an edit that needs /2");
    REQUIRE(properties.set({K::Criterion, "link_2"}, "enabled", true));
    CHECK(panels.document.press("use_format1"));
    CHECK_MESSAGE(*doc.instance()->get("format") == "geomsolver-instance/1",
                  "lowered by Use format /1 only");
    fs::remove_all(dir);
}

}  // namespace gs::test

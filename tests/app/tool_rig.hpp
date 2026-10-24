#pragma once

#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/picker.hpp"
#include "gs/app/snapper.hpp"

#include "helpers.hpp"

namespace gs::test {

using namespace gs::app;

inline fs::path sketch_instance(const char * name) {
    return fs::path(GS_TEST_DATA_DIR) / "../../docs/instances" / name;
}

// The tool `id` that App provides, on `file`, fed synthetic events at 100
// pixels per metre, as test_segment_tool's rig does; snapped() asks the
// Snapper as the canvas does. sketch-basic.json leaves x in [2.3, 3], y in
// [1, 1.8] empty.
struct ToolRig {
    App app{nullptr};
    std::size_t constraints = 0;

    // `text` written to a file of the temporary directory: a fixture.
    static fs::path write(const char * name, const char * text) {
        const fs::path p = fs::temp_directory_path() / name;
        std::ofstream(p) << text;
        return p;
    }
    explicit ToolRig(
        std::string_view id,
        const fs::path & file = sketch_instance("sketch-basic.json")) {
        REQUIRE(app.open(file));
        app.sync();
        const Json * c = doc().instance()->get("constraints");
        constraints = c ? c->size() : 0;
        REQUIRE(host().start(id));
    }
    ToolHost & host() { return app.tools(); }
    Document & doc() { return app.document(); }
    Tool & tool() {
        REQUIRE(host().active());
        return *host().active();
    }
    template <class T>
    T & as() {
        return static_cast<T &>(tool());
    }
    SnapResult snapped(Vec2d p) {
        const Scene & scene = app.scene(false);
        return Snapper(scene, scene.model.get(), app.canvas().shown(scene))
            .snap(p, 100.0, tool().snap_filter(), false);
    }
    // What the canvas picks at `p` for a constraint tool.
    std::optional<ToolPick> picked(Vec2d p) {
        const Scene & scene = app.scene(false);
        return Picker(scene, app.canvas().shown(scene))
            .pick(p, 100.0, tool().pick_filter());
    }
    void event(ToolEvent::Kind kind, Vec2d p, std::optional<SnapResult> snap,
               Mods mods = Mods::None, int clicks = 1,
               std::optional<ToolPick> pick = {}) {
        SnapResult s = snap ? *snap : SnapResult{.world = p};
        host().event({.kind = kind,
                      .world = p,
                      .px_per_m = 100.0,
                      .clicks = clicks,
                      .mods = mods,
                      .snap = std::move(s),
                      .pick = std::move(pick)});
    }
    // A move and a press of a constraint tool, with the canvas's picks.
    void hover(Vec2d p) {
        event(ToolEvent::Kind::Move, p, {}, Mods::None, 1, picked(p));
    }
    void pick(Vec2d p) {
        hover(p);
        event(ToolEvent::Kind::Press, p, {}, Mods::None, 1, picked(p));
    }
    void move(Vec2d p, std::optional<SnapResult> snap = {},
              Mods mods = Mods::None) {
        event(ToolEvent::Kind::Move, p, snap, mods);
    }
    void press(Vec2d p, std::optional<SnapResult> snap = {},
               Mods mods = Mods::None) {
        move(p, snap, mods);
        event(ToolEvent::Kind::Press, p, snap, mods);
    }
    // A press where the canvas would put it: snapped.
    void click(Vec2d p) { press(p, snapped(p)); }
    const Json * element(const std::string & name) {
        return doc().instance()->get("sketch.elements." + name);
    }
    // The constraints the tool added, "name: formula".
    std::vector<std::string> added() {
        std::vector<std::string> out;
        const Json * c = doc().instance()->get("constraints");
        for(std::size_t i = constraints; c && i < c->size(); ++i)
            out.push_back(std::format("{}: {}",
                                      (*c)[i]["name"].get<std::string>(),
                                      (*c)[i]["expr"].get<std::string>()));
        return out;
    }
    const ValueField * field(std::string_view id) {
        for(const ValueField & f : tool().fields())
            if(f.id == id) return &f;
        return nullptr;
    }
    Vec2d value(const std::string & point) {
        const Json & v = *element(point);
        return {v["value"][0].get<double>(), v["value"][1].get<double>()};
    }
    bool edits(std::string_view id, std::string_view text) {
        const std::optional<ToolHost::Edit> & e = host().editing();
        return e && e->id == id && e->text == text;
    }
    std::vector<std::string> suggestions() {
        std::vector<std::string> out;
        for(const Suggestion & s : tool().preview().suggestions)
            out.push_back(s.formula);
        return out;
    }
};

}  // namespace gs::test

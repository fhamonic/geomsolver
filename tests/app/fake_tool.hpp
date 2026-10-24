#pragma once

#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/shape_draft.hpp"
#include "gs/app/state_tool.hpp"
#include "gs/app/tool.hpp"
#include "gs/app/tool_host.hpp"
#include "gs/engine/instance.hpp"

namespace gs::test {

using namespace gs::app;

// What a FakeTool saw, shared with the test that plugs it in.
struct FakeLog {
    int made = 0, document_changes = 0, commits = 0, finishes = 0, backs = 0;
    std::vector<ToolEvent> events;
};

enum class FakeState { First, Second, End };

// A two-press segment for the framework's tests: First places A, Second
// places B, End commits the points and a segment between them as one batch.
// First has the positional fields x and y, Second the dimensional length
// and angle. Two methods, so that M cycles.
class FakeTool : public StateTool<FakeState> {
public:
    explicit FakeTool(FakeLog & log)
        : StateTool({"two points", "point, length, angle"}), log_(log) {
        ++log_.made;
        add_field(
            FakeState::First,
            {.id = "x", .label = "x", .kind = ValueField::Kind::Positional});
        add_field(
            FakeState::First,
            {.id = "y", .label = "y", .kind = ValueField::Kind::Positional});
        add_field(FakeState::Second, {.id = "length", .label = "Length"});
        add_field(
            FakeState::Second,
            {.id = "angle", .label = "Angle", .unit = ValueField::Unit::Angle});
    }

    std::string_view id() const override { return "tool.segment"; }
    std::string hint() const override {
        return state() == FakeState::First ? "pick the first point"
                                           : "pick the second point";
    }
    SnapFilter snap_filter() const override { return {}; }
    PreviewList preview() const override {
        PreviewList p;
        if(state() == FakeState::Second)
            p.prims.push_back(
                {.kind = PreviewPrim::Kind::Path, .points = {a_, b_}});
        return p;
    }
    void on_event(const ToolEvent & e, ToolContext & ctx) override {
        log_.events.push_back(e);
        StateTool::on_event(e, ctx);
    }
    void on_document_changed(ToolContext & ctx) override {
        ++log_.document_changes;
        StateTool::on_document_changed(ctx);
    }

    Vec2d a() const { return a_; }

protected:
    void track(const ToolEvent & e, ToolContext &) override {
        if(state() == FakeState::First) {
            measure("x", std::format("{}", e.world.x), e.world);
            measure("y", std::format("{}", e.world.y), e.world);
            return;
        }
        b_ = end(e.world);
        measure("length",
                std::format("{:.3f}", std::hypot(b_.x - a_.x, b_.y - a_.y)),
                b_);
        measure("angle",
                std::format("{:.1f}", std::atan2(b_.y - a_.y, b_.x - a_.x) *
                                          180.0 / 3.14159265358979323846),
                b_);
    }
    bool place(const ToolEvent & e, ToolContext &) override {
        if(state() == FakeState::First) {
            a_ = {typed("x").value_or(e.world.x),
                  typed("y").value_or(e.world.y)};
            return true;
        }
        b_ = end(e.world);
        return true;
    }
    bool commit(ToolContext & ctx) override {
        ++log_.commits;
        const Instance & inst = *ctx.document().instance();
        const std::string pa = unused_point_name(inst, "", {});
        const std::string pb =
            unused_point_name(inst, "", std::vector<std::string>{pa});
        EditBatch batch("add segment");
        batch.set("sketch.elements." + pa,
                  Json{{"type", "point"}, {"value", {a_.x, a_.y}}});
        batch.set("sketch.elements." + pb,
                  Json{{"type", "point"}, {"value", {b_.x, b_.y}}});
        const std::string pab[] = {pa, pb};
        const std::string l = ShapeDraft(ctx).next_element_name("L", pab);
        batch.set("sketch.elements." + l,
                  Json{{"type", "segment"}, {"from", pa}, {"to", pb}});
        SketchEdit edit;
        edit.batch = std::move(batch);
        edit.created = {{SelKey::Kind::Element, l}};
        return ctx.commit(std::move(edit));
    }
    void clear() override { a_ = b_ = {}; }
    void finish(ToolContext &) override { ++log_.finishes; }
    void back(ToolContext &) override { ++log_.backs; }

private:
    // B: the pointer, or what the typed length and angle make of it.
    Vec2d end(Vec2d pointer) const {
        const double dx = pointer.x - a_.x, dy = pointer.y - a_.y;
        const double len = typed("length").value_or(std::hypot(dx, dy));
        const double ang = typed("angle").value_or(std::atan2(dy, dx));
        return {a_.x + len * std::cos(ang), a_.y + len * std::sin(ang)};
    }

    FakeLog & log_;
    Vec2d a_, b_;
};

// Plugs a FakeTool in as tool.segment, logging into `log`.
inline void provide_fake(App & app, FakeLog & log) {
    app.tools().provide("tool.segment",
                        [&log] { return std::make_unique<FakeTool>(log); });
}

}  // namespace gs::test

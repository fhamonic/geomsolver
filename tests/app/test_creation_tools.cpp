#include <format>
#include <string>
#include <vector>

#include "gs/app/creation_tools.hpp"

#include "tool_rig.hpp"

namespace gs::test {

using Outcome = ToolHost::Outcome;

TEST_CASE("creation tools: one rule for hints, fields, labels and Esc") {
    struct Case {
        const char * id;
        const char * hint;  // before the first point
        std::vector<Vec2d> clicks;
        bool enter;          // Enter finishes the shape after the clicks
        const char * field;  // the dimensional field a digit opens next
        const char * label;
        const char * element;
    };
    const Case cases[] = {
        {"tool.point",
         "Point: pick its place, or press Tab to type x and y",
         {{2.4, 1.2}},
         false,
         "",
         "add point P1",
         "P1"},
        {"tool.segment",
         "Segment, two points: pick the first point, or press Tab to type x "
         "and y; M: point, length, angle",
         {{2.4, 1.2}, {2.8, 1.5}},
         false,
         "length",
         "add segment L1",
         "L1"},
        {"tool.polyline",
         "Polyline: pick the first point",
         {{2.4, 1.2}, {2.8, 1.5}, {2.5, 1.7}},
         true,
         "length",
         "add polyline PL1",
         "PL1"},
        {"tool.box",
         "Box, two corners: pick the first corner; M: centre and corner",
         {{2.4, 1.2}, {2.8, 1.5}},
         false,
         "width",
         "add box B1",
         "B1"},
        {"tool.circle",
         "Circle: pick the centre",
         {{2.6, 1.4}, {2.9, 1.4}},
         false,
         "radius",
         "add circle K1",
         "K1"},
        {"tool.arc",
         "Arc: pick the centre",
         {{2.6, 1.4}, {2.9, 1.4}, {2.6, 1.7}},
         false,
         "radius",
         "add arc Arc1",
         "Arc1"},
    };
    for(const Case & c : cases) {
        CAPTURE(c.id);
        ToolRig r(c.id);
        const std::size_t steps = r.doc().undo_count();
        CHECK(r.host().hint() == c.hint);
        CHECK_MESSAGE(r.host().hint().starts_with(
                          std::string(r.app.actions().find(c.id)->label())),
                      "the hint starts with the tool's name");

        r.press(c.clicks[0]);
        if(c.clicks.size() > 1) {
            REQUIRE(r.tool().in_progress());
            r.host().typed("-");
            REQUIRE(r.host().editing());
            CHECK(r.host().editing()->id == c.field);
            r.host().set_field_text(c.field, "-1");
            CHECK(r.host().field_key(c.field, Key::Enter) == Outcome::Refused);
            CHECK(r.field(c.field)->refusal ==
                  std::format("a {} is positive", c.field));
            CHECK(r.host().field_key(c.field, Key::Escape) ==
                  Outcome::Reverted);
            r.app.escape();
            CHECK_MESSAGE((r.host().active() && !r.tool().in_progress()),
                          "Esc drops the shape under way, the tool stays");
            r.press(c.clicks[0]);
            REQUIRE(r.host().right_click());
            CHECK_MESSAGE((r.host().active() && !r.tool().in_progress()),
                          "a right click too");
            for(const Vec2d at : c.clicks) r.press(at);
        }
        if(c.enter) r.host().finish();
        CHECK((r.doc().undo_count() == steps + 1 &&
               r.doc().undo_label() == c.label));
        CHECK(r.app.selection().keys() ==
              std::vector<SelKey>{{SelKey::Kind::Element, c.element}});
        CHECK_MESSAGE((r.host().active() && !r.tool().in_progress()),
                      "continuous: the next shape");
        REQUIRE(r.host().right_click());
        CHECK_MESSAGE(!r.host().active(),
                      "nothing in progress: a right click quits the tool");
    }
}

}  // namespace gs::test

namespace gs::test {

// Finding 10: a press that would place nothing (no length, no radius, no
// area) is refused silently otherwise; the hint says why while the pointer
// is there, as a constraint tool's does.
TEST_CASE("creation tools: where a press places nothing, the hint says why") {
    struct Case {
        const char * id;
        Vec2d first, there;
        const char * hint;
    };
    const Case cases[] = {
        {"tool.segment",
         {2.4, 1.2},
         {2.4, 1.2},
         "Segment, two points: a segment needs a length"},
        {"tool.polyline",
         {2.4, 1.2},
         {2.4, 1.21},
         "Polyline: an edge needs a length"},
        {"tool.box",
         {2.4, 1.2},
         {2.4, 1.6},
         "Box, two corners: a box needs a width and a height"},
        {"tool.circle",
         {2.6, 1.4},
         {2.6, 1.4},
         "Circle: a circle needs a radius"},
        {"tool.arc", {2.6, 1.4}, {2.6, 1.4}, "Arc: an arc needs a radius"},
    };
    for(const Case & c : cases) {
        CAPTURE(c.id);
        ToolRig r(c.id);
        r.press(c.first);
        r.move(c.there);
        CHECK_MESSAGE(r.host().hint() != c.hint,
                      "not before a press there: the pointer starts there");
        const std::size_t steps = r.doc().undo_count();
        r.press(c.there);
        CHECK((r.doc().undo_count() == steps && r.host().hint() == c.hint));
        r.move(c.there);
        CHECK(r.host().hint() == c.hint);
        r.move({2.9, 1.7});
        CHECK(r.host().hint() != c.hint);
    }
}

}  // namespace gs::test

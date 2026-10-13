#include "gs/app/view.hpp"

#include "helpers.hpp"

namespace gs::test {

TEST_CASE("view: number_or_string, view maths, grid step and playback") {
    CHECK_MESSAGE((number_or_string("0.5").is_number() &&
                   number_or_string(" 1e-3 ").is_number() &&
                   number_or_string("15deg").is_string() &&
                   number_or_string("W/2").is_string() &&
                   number_or_string("inf").is_string()),
                  "number_or_string: numbers vs expressions");
    View2d v;
    v.ox = 10;
    v.oy = 20;
    v.w = 800;
    v.h = 600;
    v.centre = {1.0, 2.0};
    v.scale = 123.0;
    const Vec2d p{0.3, -0.7};
    const Vec2d r = v.to_world(v.to_screen(p));
    CHECK_MESSAGE((near(r.x, p.x, 1e-12) && near(r.y, p.y, 1e-12)),
                  "view: world -> screen -> world");
    CHECK_MESSAGE((v.to_screen({1.0, 3.0}).y < v.to_screen({1.0, 2.0}).y),
                  "view: world y up is screen up");
    const Vec2d cursor{200.0, 150.0};
    const Vec2d under = v.to_world(cursor);
    v.zoom_at(cursor, 1.7);
    const Vec2d still = v.to_world(cursor);
    CHECK_MESSAGE(
        (near(still.x, under.x, 1e-12) && near(still.y, under.y, 1e-12) &&
         near(v.scale, 123.0 * 1.7, 1e-9)),
        "view: zoom keeps the point under the cursor");
    Box2d b;
    b.add({0, 0});
    b.add({5.6, 5});
    v.fit(b, 30);
    const Vec2d s0 = v.to_screen({0, 0}), s1 = v.to_screen({5.6, 5});
    CHECK_MESSAGE((s0.x >= v.ox + 29.9 && s1.x <= v.ox + v.w - 29.9 &&
                   s1.y >= v.oy + 29.9 && s0.y <= v.oy + v.h - 29.9),
                  "view: fit puts the box inside the margins");
    CHECK_MESSAGE(
        (grid_step(100.0, 70.0) == 1.0 && grid_step(1000.0, 70.0) == 0.1 &&
         grid_step(300.0, 70.0) == 0.5),
        "grid step 1-2-5 sequence");
    SweepPlayer pl;
    pl.playing = true;
    pl.period = 2.0;
    CHECK_MESSAGE((near(pl.advance(0.9, 0.4), 0.1, 1e-12)),
                  "playback: loop wraps");
    pl.mode = SweepPlayer::Mode::Bounce;
    CHECK_MESSAGE((near(pl.advance(0.9, 0.4), 0.9, 1e-12) && pl.direction < 0),
                  "playback: bounce reflects and reverses");
    pl.mode = SweepPlayer::Mode::Once;
    pl.direction = 1.0;
    CHECK_MESSAGE((pl.advance(0.9, 0.4) == 1.0 && !pl.playing),
                  "playback: once stops at 1");
}

}  // namespace gs::test

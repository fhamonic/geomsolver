#include <cmath>
#include <thread>

#include "corpus.hpp"
#include "helpers.hpp"

namespace gs::test {

namespace {

bool same(const std::vector<RowRef> & a, const std::vector<RowRef> & b) {
    return std::ranges::equal(a, b, [](const RowRef & x, const RowRef & y) {
        return x.group == y.group && x.split == y.split;
    });
}

void check_same(const sketch::Diagnosis & a, const sketch::Diagnosis & b) {
    CHECK(a.status == b.status);
    CHECK(a.held == b.held);
    CHECK(a.rank.dof == b.rank.dof);
    CHECK(same(a.rank.near_dependent, b.rank.near_dependent));
    CHECK(same(a.singular, b.singular));
    REQUIRE(a.rank.dependent.size() == b.rank.dependent.size());
    for(std::size_t i = 0; i < a.rank.dependent.size(); ++i) {
        CHECK(same({a.rank.dependent[i].row}, {b.rank.dependent[i].row}));
        CHECK(same(a.rank.dependent[i].group, b.rank.dependent[i].group));
    }
    REQUIRE(a.free.size() == b.free.size());
    for(std::size_t e = 0; e < a.free.size(); ++e) {
        CHECK(a.free[e].count == b.free[e].count);
        CHECK(a.free[e].directions == b.free[e].directions);
    }
}

}  // namespace

TEST_CASE("worker: a diagnosis on another thread, drags on this one") {
    // The worker of plan 3.10 shares the System and the model, nothing
    // else: its result is the synchronous one, to the bit, while drags,
    // their monitored groups and a second diagnosis run beside it.
    const Json d = strip_doc(60);
    const Run r = settle_doc(d);
    const Model & m = *r.settled.model;
    const sketch::System system(r.settled.model);
    const std::vector<double> x = m.initial_x();
    const std::vector<double> q = r.settled.q;
    const std::vector<int> left = sketch::left_out(r.settled);
    const sketch::Diagnosis sync = system.diagnose(x, q, left);
    sketch::Diagnosis first, second;
    {
        std::jthread a([&] { first = system.diagnose(x, q, left); });
        std::jthread b([&] { second = system.diagnose(x, q, left); });
        std::vector<double> live = q;
        const auto it = std::ranges::find(m.sketch(), std::string("P59"),
                                          &SketchInfo::name);
        const sketch::Gesture g =
            sketch::drag_point(m, static_cast<int>(it - m.sketch().begin()));
        for(int k = 0; k < 3; ++k) {
            sketch::Drag drag =
                system.begin_drag(g.handles, g.held, g.fallback, x, live, left);
            const Vec2d s = drag.start()[0];
            for(int f = 1; f <= 20; ++f) {
                drag.move(std::vector<Vec2d>{{s.x - 0.01 * f, s.y + 0.01 * f}},
                          live);
                system.monitor(x, live, 1e-7);
            }
        }
        CHECK(live != q);
    }
    check_same(first, sync);
    check_same(second, sync);
}

}  // namespace gs::test

#include "ui/canvas.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

#include "gs/engine/geometry.hpp"
#include "ui/widgets.hpp"

namespace gs::ui {
namespace {

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

const ImU32 kBackground = rgba(22, 25, 30);
const ImU32 kGridMinor = rgba(255, 255, 255, 10);
const ImU32 kGridMajor = rgba(255, 255, 255, 26);
const ImU32 kAxis = rgba(255, 255, 255, 64);
const ImU32 kGridText = rgba(160, 166, 178, 210);
const ImU32 kGeomFill = rgba(214, 220, 232, 30);
const ImU32 kGeomStroke = rgba(196, 202, 214, 165);
const ImU32 kGeomText = rgba(176, 182, 196, 220);
const ImU32 kDomain = rgba(110, 196, 255, 150);
const ImU32 kHandle = rgba(255, 150, 40, 235);
const ImU32 kHandleFixed = rgba(150, 150, 150, 220);
const ImU32 kViolation = rgba(255, 90, 80, 255);

std::vector<Vec2d> vertices(const GeoValue & g) {
    std::vector<Vec2d> v(uz(g.vertex_count()));
    for(int i = 0; i < g.vertex_count(); ++i) v[uz(i)] = g.vertex(i);
    return v;
}

Vec2d centroid(const GeoValue & g) {
    if(g.type == ValueType::Vec) return g.vec();
    if(g.type != ValueType::Shape) return {};
    if(g.kind == ShapeKind::Circle) return g.circle_center();
    Vec2d c;
    const int n = g.vertex_count();
    for(int i = 0; i < n; ++i) {
        c.x += g.vertex(i).x / n;
        c.y += g.vertex(i).y / n;
    }
    return c;
}

void add_to_box(Box2d & b, const GeoValue & g) {
    if(g.type == ValueType::Vec) b.add(g.vec());
    if(g.type != ValueType::Shape) return;
    if(g.kind == ShapeKind::Circle) {
        const Vec2d c = g.circle_center();
        const double r = g.circle_radius();
        b.add({c.x - r, c.y - r});
        b.add({c.x + r, c.y + r});
        return;
    }
    for(int i = 0; i < g.vertex_count(); ++i) b.add(g.vertex(i));
}

double dist_point_segment(Vec2d p, Vec2d a, Vec2d b) {
    const double ex = b.x - a.x, ey = b.y - a.y;
    const double L2 = ex * ex + ey * ey;
    double t = L2 > 0 ? ((p.x - a.x) * ex + (p.y - a.y) * ey) / L2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(a.x + t * ex - p.x, a.y + t * ey - p.y);
}

// Inside a polygon or circle, or within tol_world of an outline.
bool hit_shape(const GeoValue & g, Vec2d world, double tol_world) {
    if(g.type != ValueType::Shape) return false;
    if(g.kind == ShapeKind::Circle) {
        const Vec2d c = g.circle_center();
        return std::hypot(world.x - c.x, world.y - c.y) <=
               g.circle_radius() + tol_world;
    }
    const std::vector<Vec2d> v = vertices(g);
    if(g.kind == ShapeKind::Polygon && v.size() >= 3 &&
       point_in_polygon(world, v))
        return true;
    for(std::size_t i = 0; i + 1 < v.size(); ++i)
        if(dist_point_segment(world, v[i], v[i + 1]) <= tol_world) return true;
    return false;
}

class Painter {
public:
    Painter(ImDrawList * dl, const View2d & v) : dl_(dl), view_(v) {}

    ImVec2 s(Vec2d p) const { return to_im(view_.to_screen(p)); }

    void polyline(const std::vector<Vec2d> & w, bool closed, ImU32 col,
                  float width, bool dashed) {
        if(w.size() < 2) return;
        to_screen(w);
        if(!dashed) {
            dl_->AddPolyline(pts_.data(), static_cast<int>(pts_.size()), col,
                             closed ? ImDrawFlags_Closed : ImDrawFlags_None,
                             width);
            return;
        }
        const std::size_t n = pts_.size();
        const std::size_t edges = closed ? n : n - 1;
        for(std::size_t i = 0; i < edges; ++i)
            dashed_segment(pts_[i], pts_[(i + 1) % n], col, width);
    }

    void shape(const GeoValue & g, ImU32 stroke, ImU32 fill, float width,
               bool dashed = false) {
        if(g.type == ValueType::Vec) {
            marker(g.vec(), stroke, 3.5f, true);
            return;
        }
        if(g.type != ValueType::Shape) return;
        if(g.kind == ShapeKind::Circle) {
            const ImVec2 c = s(g.circle_center());
            const auto r = static_cast<float>(g.circle_radius() * view_.scale);
            if(!(r > 0.0f) || r > 1e6f) return;
            if((fill >> 24) != 0) dl_->AddCircleFilled(c, r, fill, 0);
            if(dashed) {
                std::vector<Vec2d> ring;
                const Vec2d cc = g.circle_center();
                for(int k = 0; k < 96; ++k) {
                    const double a = 2 * 3.14159265358979323846 * k / 96;
                    ring.push_back({cc.x + g.circle_radius() * std::cos(a),
                                    cc.y + g.circle_radius() * std::sin(a)});
                }
                polyline(ring, true, stroke, width, true);
            } else {
                dl_->AddCircle(c, r, stroke, 0, width);
            }
            return;
        }
        std::vector<Vec2d> v = vertices(g);
        if(g.kind == ShapeKind::Polyline) {
            polyline(v, false, stroke, width, dashed);
            return;
        }
        if(v.size() < 3) {
            polyline(v, true, stroke, width, dashed);
            return;
        }
        if((fill >> 24) != 0) {
            // ImGui fills want clockwise order as seen on screen, which is
            // clockwise in the y-up world too (the flip mirrors both the
            // polygon and the viewer). The concave fill triangulates the
            // other order wrongly, filling area outside the polygon.
            if(signed_area(v) > 0) std::reverse(v.begin(), v.end());
            to_screen(v);
            if(is_convex(v))
                dl_->AddConvexPolyFilled(pts_.data(),
                                         static_cast<int>(pts_.size()), fill);
            else
                dl_->AddConcavePolyFilled(pts_.data(),
                                          static_cast<int>(pts_.size()), fill);
        }
        polyline(v, true, stroke, width, dashed);
    }

    void marker(Vec2d p, ImU32 col, float r, bool filled) {
        const ImVec2 c = s(p);
        if(filled)
            dl_->AddCircleFilled(c, r, col, 12);
        else
            dl_->AddCircle(c, r, col, 12, 1.5f);
    }

    void diamond(Vec2d p, ImU32 col, float r) {
        const ImVec2 c = s(p);
        dl_->AddQuadFilled({c.x, c.y - r}, {c.x + r, c.y}, {c.x, c.y + r},
                           {c.x - r, c.y}, col);
    }

    // A label right of p at `offset`, else mirrored to the left, else above
    // or below: the first spot that overlaps no earlier label or mark.
    void label(Vec2d p, std::string_view text, ImU32 col, ImVec2 offset) {
        if(text.empty()) return;
        const ImVec2 sz =
            ImGui::CalcTextSize(text.data(), text.data() + text.size());
        const float lh = sz.y + 2.0f;
        place(s(p), text, col, sz,
              {offset,
               {-offset.x - sz.x, offset.y},
               {offset.x, offset.y - lh},
               {offset.x, offset.y + lh},
               {-offset.x - sz.x, offset.y - lh},
               {-offset.x - sz.x, offset.y + lh}});
    }

    void centred_label(Vec2d p, std::string_view text, ImU32 col) {
        if(text.empty()) return;
        const ImVec2 sz =
            ImGui::CalcTextSize(text.data(), text.data() + text.size());
        const float lh = sz.y + 2.0f;
        place(s(p), text, col, sz,
              {{-sz.x / 2, -sz.y / 2},
               {-sz.x / 2, -sz.y / 2 - lh},
               {-sz.x / 2, -sz.y / 2 + lh}});
    }

    // Reserves a screen rectangle (a marker) that labels avoid.
    void reserve(ImVec2 a, ImVec2 b) { taken_.push_back({a, b}); }

private:
    void to_screen(const std::vector<Vec2d> & w) {
        pts_.resize(w.size());
        for(std::size_t i = 0; i < w.size(); ++i) pts_[i] = s(w[i]);
    }

    void dashed_segment(ImVec2 a, ImVec2 b, ImU32 col, float width) {
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if(!(len > 0.0f) || len > 1e5f) return;
        const float dash = 7.0f, gap = 5.0f;
        for(float t = 0.0f; t < len; t += dash + gap) {
            const float e = std::min(t + dash, len);
            dl_->AddLine({a.x + dx * t / len, a.y + dy * t / len},
                         {a.x + dx * e / len, a.y + dy * e / len}, col, width);
        }
    }

    void place(ImVec2 anchor, std::string_view text, ImU32 col, ImVec2 sz,
               std::initializer_list<ImVec2> offsets) {
        ImVec2 at{anchor.x + offsets.begin()->x, anchor.y + offsets.begin()->y};
        for(const ImVec2 & o : offsets) {
            const ImVec2 a{anchor.x + o.x, anchor.y + o.y};
            const ImVec2 b{a.x + sz.x, a.y + sz.y};
            const bool clash = std::ranges::any_of(
                taken_, [&](const std::pair<ImVec2, ImVec2> & r) {
                    return a.x < r.second.x && r.first.x < b.x &&
                           a.y < r.second.y && r.first.y < b.y;
                });
            if(!clash) {
                at = a;
                break;
            }
        }
        taken_.push_back({at, {at.x + sz.x, at.y + sz.y}});
        dl_->AddText(at, col, text.data(), text.data() + text.size());
    }

    ImDrawList * dl_;
    const View2d & view_;
    std::vector<ImVec2> pts_;
    std::vector<std::pair<ImVec2, ImVec2>> taken_;
};

std::string grid_label(double v, double step) {
    const int decimals =
        std::max(0, -static_cast<int>(std::floor(std::log10(step) + 1e-9)));
    if(std::fabs(v) < step * 1e-6) v = 0.0;
    return std::format("{:.{}f}", v, decimals);
}

void draw_grid(ImDrawList * dl, const View2d & v) {
    const double major = grid_step(v.scale, 70.0);
    const double minor = major / 5.0;
    const Vec2d lo = v.to_world({v.ox, v.oy + v.h});
    const Vec2d hi = v.to_world({v.ox + v.w, v.oy});
    const auto x0 = static_cast<float>(v.ox), y0 = static_cast<float>(v.oy);
    const auto x1 = static_cast<float>(v.ox + v.w),
               y1 = static_cast<float>(v.oy + v.h);
    auto lines = [&](double step, ImU32 col) {
        if(step * v.scale < 10.0) return;
        for(double gx = std::ceil(lo.x / step) * step; gx <= hi.x; gx += step) {
            const auto sx = static_cast<float>(v.to_screen({gx, 0}).x);
            dl->AddLine({sx, y0}, {sx, y1}, col);
        }
        for(double gy = std::ceil(lo.y / step) * step; gy <= hi.y; gy += step) {
            const auto sy = static_cast<float>(v.to_screen({0, gy}).y);
            dl->AddLine({x0, sy}, {x1, sy}, col);
        }
    };
    lines(minor, kGridMinor);
    lines(major, kGridMajor);
    const ImVec2 origin = to_im(v.to_screen({0, 0}));
    if(origin.x >= x0 && origin.x <= x1)
        dl->AddLine({origin.x, y0}, {origin.x, y1}, kAxis, 1.5f);
    if(origin.y >= y0 && origin.y <= y1)
        dl->AddLine({x0, origin.y}, {x1, origin.y}, kAxis, 1.5f);
    const float th = ImGui::GetTextLineHeight();
    for(double gx = std::ceil(lo.x / major) * major; gx <= hi.x; gx += major) {
        const auto sx = static_cast<float>(v.to_screen({gx, 0}).x);
        const std::string t = grid_label(gx, major);
        dl->AddText({sx + 3, y1 - th - 2}, kGridText, t.c_str());
    }
    for(double gy = std::ceil(lo.y / major) * major; gy <= hi.y; gy += major) {
        const auto sy = static_cast<float>(v.to_screen({0, gy}).y);
        const std::string t = grid_label(gy, major);
        dl->AddText({x0 + 4, sy - th - 1}, kGridText, t.c_str());
    }
    dl->AddText({x1 - ImGui::CalcTextSize("m").x - 6, y1 - th - 2}, kGridText,
                "m");
}

struct Hover {
    std::string title;
    std::vector<std::string> lines;
};

}  // namespace

void Canvas::draw(Document & doc, const Scene & scene) {
    ImGuiIO & io = ImGui::GetIO();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 50.0f);
    size.y = std::max(size.y, 50.0f);
    ImGui::InvisibleButton("##canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    view_.ox = p0.x;
    view_.oy = p0.y;
    view_.w = size.x;
    view_.h = size.y;

    // Handles read x through the document, so they need the scene's model to
    // be the document's (it is, unless an edit happened earlier this frame).
    const Model * model = scene.model ? scene.model.get() : nullptr;
    const bool model_current = model && model == doc.model().get();
    const double tol = doc.feas_tol();
    if(!model_current) drag_var_ = -1;
    const Model * geom_model = model ? model : doc.background().get();
    const bool have_scene =
        model && scene.display.size() == model->display().size();

    struct Handle {
        int var;
        Vec2d p;
        bool fixed;
    };
    std::vector<Handle> handles;
    if(model_current && options.handles) {
        for(std::size_t i = 0; i < model->design().size(); ++i) {
            const DesignVar & dv = model->design()[i];
            if(dv.type != VarType::Point) continue;
            const std::vector<double> val = doc.var_value(static_cast<int>(i));
            if(val.size() == 2)
                handles.push_back(
                    {static_cast<int>(i), {val[0], val[1]}, dv.fixed});
        }
    }

    if(fit_request_ != FitNone && geom_model) {
        Box2d box;
        if(fit_request_ == FitDesign) {
            if(have_scene) {
                for(const GeoValue & g : scene.display) add_to_box(box, g);
                for(const auto & gs : scene.ghosts)
                    for(const GeoValue & g : gs) add_to_box(box, g);
                for(const auto & tr : scene.traces)
                    for(const Vec2d & p : tr) box.add(p);
            }
            for(const Handle & h : handles) box.add(h.p);
        }
        if(fit_request_ == FitAll || box.empty()) {
            for(const GeometryInfo & g : geom_model->geometry())
                add_to_box(box, g.value);
            if(have_scene)
                for(const GeoValue & g : scene.display) add_to_box(box, g);
        }
        if(!box.empty()) view_.fit(box, 40.0);
        fit_request_ = FitNone;
    }

    const Vec2d mouse_world = view_.to_world({io.MousePos.x, io.MousePos.y});
    const double px = 1.0 / view_.scale;  // one pixel in metres

    auto handle_under_mouse = [&]() {
        int best = -1;
        double bd = 9.0;
        for(const Handle & h : handles) {
            if(h.fixed) continue;
            const Vec2d s = view_.to_screen(h.p);
            const double d =
                std::hypot(s.x - static_cast<double>(io.MousePos.x),
                           s.y - static_cast<double>(io.MousePos.y));
            if(d < bd) {
                bd = d;
                best = h.var;
            }
        }
        return best;
    };

    if(hovered && io.MouseWheel != 0.0f)
        view_.zoom_at({io.MousePos.x, io.MousePos.y},
                      std::pow(1.18, static_cast<double>(io.MouseWheel)));
    if(ImGui::IsItemActivated()) {
        const int h = ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                          ? handle_under_mouse()
                          : -1;
        if(h >= 0) {
            drag_var_ = h;
            const std::vector<double> val = doc.var_value(h);
            drag_offset_ = {val[0] - mouse_world.x, val[1] - mouse_world.y};
        } else {
            panning_ = true;
        }
    }
    if(!active) {
        drag_var_ = -1;
        panning_ = false;
    }
    if(drag_var_ >= 0 && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
        const double target[2] = {mouse_world.x + drag_offset_.x,
                                  mouse_world.y + drag_offset_.y};
        doc.set_var_value(drag_var_, target);
    }
    if(panning_)
        view_.pan_pixels(static_cast<double>(io.MouseDelta.x),
                         static_cast<double>(io.MouseDelta.y));
    if(hovered && !io.WantTextInput) {
        if(ImGui::IsKeyPressed(ImGuiKey_F)) fit_request_ = FitAll;
        if(ImGui::IsKeyPressed(ImGuiKey_G)) fit_request_ = FitDesign;
    }

    ImDrawList * dl = ImGui::GetWindowDrawList();
    const ImVec2 p1{p0.x + size.x, p0.y + size.y};
    dl->PushClipRect(p0, p1, true);
    dl->AddRectFilled(p0, p1, kBackground);
    if(options.grid) draw_grid(dl, view_);
    Painter paint(dl, view_);

    if(geom_model && options.geometry) {
        for(const GeometryInfo & g : geom_model->geometry())
            if(g.value.type == ValueType::Vec) {
                const ImVec2 c = paint.s(g.value.vec());
                paint.reserve({c.x - 5, c.y - 5}, {c.x + 5, c.y + 5});
            }
        // A display item with a label names that shape already.
        auto labelled_by_display = [&](const std::string & name) {
            return model && std::ranges::any_of(
                                model->display(), [&](const DisplayInfo & d) {
                                    return d.text == name && !d.label.empty();
                                });
        };
        for(const GeometryInfo & g : geom_model->geometry()) {
            const GeoValue & v = g.value;
            if(v.type == ValueType::Vec) {
                paint.diamond(v.vec(), kGeomStroke, 4.5f);
                if(options.labels)
                    paint.label(v.vec(), g.name, kGeomText, {7, -7});
                continue;
            }
            if(v.type != ValueType::Shape) continue;
            const bool line = v.kind == ShapeKind::Polyline;
            paint.shape(v, kGeomStroke, line ? 0u : kGeomFill,
                        line ? 3.0f : 1.2f);
            if(!options.labels || line || labelled_by_display(g.name)) continue;
            paint.centred_label(centroid(v), g.name, kGeomText);
        }
    }

    // Domains of free point variables (fixed ones have no domain to move in).
    if(model && options.domains) {
        for(const DesignVar & dv : model->design()) {
            if(dv.type != VarType::Point || dv.fixed) continue;
            paint.shape(dv.domain, kDomain, 0u, 1.2f, true);
        }
    }

    if(have_scene) {
        const auto & items = model->display();
        if(options.traces)
            for(std::size_t i = 0; i < items.size(); ++i)
                if(i < scene.traces.size() && !scene.traces[i].empty())
                    paint.polyline(scene.traces[i], false,
                                   engine_color(items[i].color, 0.55f), 1.2f,
                                   false);
        if(options.ghosts)
            for(std::size_t i = 0; i < items.size(); ++i) {
                if(i >= scene.ghosts.size()) break;
                const DisplayInfo & d = items[i];
                for(const GeoValue & g : scene.ghosts[i])
                    paint.shape(g, engine_color(d.color, 0.30f),
                                d.fill ? engine_color(d.color, 0.07f) : 0u,
                                1.0f);
            }
        for(std::size_t i = 0; i < items.size(); ++i) {
            const DisplayInfo & d = items[i];
            const GeoValue & g = scene.display[i];
            const auto width = static_cast<float>(std::max(d.width, 0.5));
            paint.shape(g, engine_color(d.color),
                        d.fill ? engine_color(d.color, 0.38f) : 0u, width);
            if(!options.labels || d.label.empty()) continue;
            if(g.type == ValueType::Vec)
                paint.label(g.vec(), d.label, engine_color(d.color),
                            {7, -ImGui::GetTextLineHeight() - 2});
            else if(g.type == ValueType::Shape)
                paint.centred_label(centroid(g), d.label, rgba(240, 242, 246));
        }
    }

    const int hot =
        drag_var_ >= 0 ? drag_var_ : (hovered ? handle_under_mouse() : -1);
    for(const Handle & h : handles) {
        const DesignVar & dv = model->design()[uz(h.var)];
        const ImVec2 c = paint.s(h.p);
        const float r = h.var == hot ? 7.5f : 5.5f;
        if(h.fixed) {
            dl->AddRectFilled({c.x - 4, c.y - 4}, {c.x + 4, c.y + 4},
                              kHandleFixed);
        } else {
            dl->AddCircleFilled(c, r, kHandle, 16);
            dl->AddCircle(c, r, rgba(255, 255, 255, 230), 16, 1.5f);
        }
        bool labelled = false;
        for(const DisplayInfo & d : model->display())
            if(d.text == dv.name && !d.label.empty()) labelled = true;
        if(options.labels && !labelled)
            paint.label(h.p, dv.name, rgba(255, 196, 120), {8, 2});
    }

    const float pad = 8.0f;
    const float lh = ImGui::GetTextLineHeightWithSpacing();
    bool violated_now = false;
    ImVec2 box_end{p0.x, p0.y};  // lower right of the "Violated here" box
    if(model && scene.verification &&
       scene.violation_now.size() == model->groups().size()) {
        std::vector<std::string> lines;
        int hidden = 0;
        for(std::size_t g = 0; g < model->groups().size(); ++g) {
            const double v = scene.violation_now[g];
            if(!(v > tol) && !std::isnan(v)) continue;
            violated_now = true;
            const RowGroup & rg = model->groups()[g];
            if(lines.size() >= 8) {
                ++hidden;
                continue;
            }
            lines.push_back(
                std::format("{}: {}{}", rg.name, format_number(v, 3),
                            rg.sweep >= 0 ? "" : "  (every position)"));
        }
        if(hidden > 0) lines.push_back(std::format("... {} more", hidden));
        if(!lines.empty()) {
            float wmax = ImGui::CalcTextSize("Violated here (natural units)").x;
            for(const std::string & l : lines)
                wmax = std::max(wmax, ImGui::CalcTextSize(l.c_str()).x);
            const ImVec2 b0{p0.x + pad, p0.y + pad};
            const ImVec2 b1{
                b0.x + wmax + 2 * pad,
                b0.y + lh * static_cast<float>(lines.size() + 1) + pad};
            dl->AddRectFilled(b0, b1, rgba(40, 14, 14, 215), 4.0f);
            dl->AddRect(b0, b1, rgba(255, 90, 80, 200), 4.0f);
            box_end = b1;
            dl->AddText({b0.x + pad, b0.y + pad / 2}, rgba(255, 150, 140),
                        "Violated here (natural units)");
            for(std::size_t i = 0; i < lines.size(); ++i)
                dl->AddText({b0.x + pad,
                             b0.y + pad / 2 + lh * static_cast<float>(i + 1)},
                            rgba(255, 210, 205), lines[i].c_str());
        }
    }
    if(violated_now) dl->AddRect(p0, p1, kViolation, 0.0f, 0, 3.0f);

    {
        std::vector<std::pair<std::string, ImU32>> lines;
        if(!model) {
            lines.push_back({doc.has_instance()
                                 ? "No compiled model: fix the errors listed "
                                   "in the Inspector / Messages"
                                 : "No instance loaded (File > Open)",
                             rgba(255, 150, 140)});
        } else {
            if(doc.stale())
                lines.push_back(
                    {"Showing the last model that compiled "
                     "(the document has errors)",
                     rgba(255, 200, 120)});
            if(scene.verification) {
                const Verification & V = *scene.verification;
                if(V.feasible(tol))
                    lines.push_back({std::format("feasible on {} samples",
                                                 scene.verify_samples),
                                     rgba(130, 220, 140)});
                else
                    lines.push_back(
                        {std::format(
                             "max violation {} ({}) on {} samples",
                             format_number(V.max_violation, 3),
                             V.worst_group >= 0
                                 ? model->groups()[uz(V.worst_group)].name
                                 : std::string("?"),
                             scene.verify_samples),
                         rgba(255, 150, 140)});
                if(V.objective != 0.0 ||
                   std::ranges::any_of(model->criteria(), [](const auto & c) {
                       return c.role == CriterionRole::Minimize;
                   }))
                    lines.push_back({std::format("objective {}",
                                                 format_number(V.objective, 7)),
                                     rgba(220, 224, 232)});
            }
            if(!scene.error.empty())
                lines.push_back(
                    {"evaluation error: " + scene.error, rgba(255, 150, 140)});
            // Scalar display items have no geometry: list them here.
            if(have_scene)
                for(std::size_t i = 0; i < model->display().size(); ++i) {
                    const DisplayInfo & d = model->display()[i];
                    if(scene.display[i].type != ValueType::Scalar) continue;
                    lines.push_back(
                        {std::format("{} = {}",
                                     d.label.empty() ? d.text : d.label,
                                     format_value(scene.display[i])),
                         engine_color(d.color)});
                }
        }
        float y = p0.y + pad;
        for(const auto & [text, col] : lines) {
            const float w = ImGui::CalcTextSize(text.c_str()).x;
            // In a narrow canvas the right-aligned status would run into
            // the violation box: continue below it instead.
            if(p1.x - pad - w - 4 < box_end.x && y < box_end.y)
                y = box_end.y + pad / 2;
            const ImVec2 at{p1.x - pad - w, y};
            dl->AddRectFilled({at.x - 4, at.y - 1},
                              {at.x + w + 4, at.y + lh - 2},
                              rgba(22, 25, 30, 200), 3.0f);
            dl->AddText(at, col, text.c_str());
            y += lh;
        }
    }
    if(hovered) {
        const std::string pos = std::format(
            "x {}  y {} m   |   1 m = {} px", format_number(mouse_world.x, 4),
            format_number(mouse_world.y, 4), format_number(view_.scale, 3));
        const ImVec2 sz = ImGui::CalcTextSize(pos.c_str());
        const ImVec2 at{p1.x - sz.x - pad, p1.y - lh - pad - lh};
        dl->AddRectFilled({at.x - 4, at.y - 1},
                          {at.x + sz.x + 4, at.y + lh - 2},
                          rgba(22, 25, 30, 200), 3.0f);
        dl->AddText(at, kGridText, pos.c_str());
    }
    dl->PopClipRect();

    if(!hovered || panning_) return;
    Hover hv;
    const double pick = 7.0 * px;
    const int h = drag_var_ >= 0 ? drag_var_ : handle_under_mouse();
    if(h >= 0 && model) {
        const DesignVar & dv = model->design()[uz(h)];
        const std::vector<double> val = doc.var_value(h);
        hv.title = std::format("{} (design point)", dv.name);
        hv.lines.push_back(std::format("({}, {}) m", format_number(val[0], 5),
                                       format_number(val[1], 5)));
        if(!dv.note.empty()) hv.lines.push_back(dv.note);
        hv.lines.push_back("drag to move; stays inside its domain");
    }
    if(hv.title.empty() && have_scene) {
        const auto & items = model->display();
        for(std::size_t i = items.size(); i-- > 0;) {
            const GeoValue & g = scene.display[i];
            if(g.type != ValueType::Vec) continue;
            const Vec2d p = g.vec();
            if(std::hypot(p.x - mouse_world.x, p.y - mouse_world.y) > pick)
                continue;
            hv.title = items[i].label.empty() ? items[i].text : items[i].label;
            hv.lines.push_back(std::format("{} = ({}, {}) m", items[i].text,
                                           format_number(p.x, 5),
                                           format_number(p.y, 5)));
            break;
        }
    }
    if(hv.title.empty() && geom_model && options.geometry) {
        for(const GeometryInfo & g : geom_model->geometry()) {
            if(g.value.type != ValueType::Vec) continue;
            const Vec2d p = g.value.vec();
            if(std::hypot(p.x - mouse_world.x, p.y - mouse_world.y) > pick)
                continue;
            hv.title = g.name;
            hv.lines.push_back(std::format("point ({}, {}) m",
                                           format_number(p.x, 5),
                                           format_number(p.y, 5)));
            if(!g.note.empty()) hv.lines.push_back(g.note);
            if(!g.file.empty()) hv.lines.push_back("from " + g.file);
            break;
        }
    }
    if(hv.title.empty() && have_scene) {
        const auto & items = model->display();
        for(std::size_t i = items.size(); i-- > 0;) {
            if(!hit_shape(scene.display[i], mouse_world, 4.0 * px)) continue;
            hv.title = items[i].label.empty() ? items[i].text : items[i].label;
            hv.lines.push_back(std::format("{}: {}", items[i].text,
                                           format_value(scene.display[i])));
            break;
        }
    }
    if(hv.title.empty() && geom_model && options.geometry) {
        const auto & geo = geom_model->geometry();
        for(std::size_t i = geo.size(); i-- > 0;) {
            if(!hit_shape(geo[i].value, mouse_world, 4.0 * px)) continue;
            hv.title = geo[i].name;
            hv.lines.push_back(format_value(geo[i].value));
            if(!geo[i].note.empty()) hv.lines.push_back(geo[i].note);
            if(!geo[i].file.empty()) hv.lines.push_back("from " + geo[i].file);
            break;
        }
    }
    if(hv.title.empty()) return;
    if(ImGui::BeginTooltip()) {
        ImGui::TextUnformatted(hv.title.c_str());
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        for(const std::string & l : hv.lines) ImGui::TextUnformatted(l.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

}  // namespace gs::ui

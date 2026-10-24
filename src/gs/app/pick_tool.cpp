#include "gs/app/pick_tool.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <numeric>
#include <utility>

#include "geometry2d.hpp"
#include "gs/app/document.hpp"
#include "gs/app/shape_draft.hpp"
#include "gs/engine/model.hpp"

namespace gs::app {
namespace {

using PK = ToolPick::Kind;

using geometry::kTurn;

SketchEdit refused(std::string why) {
    SketchEdit out;
    out.refusal = std::move(why);
    return out;
}

// What the preview draws of a pick taken.
void draw(PreviewList & out, const ToolPick & p) {
    switch(p.kind) {
        case PK::Vertex:
            out.prims.push_back(
                {.kind = PreviewPrim::Kind::Point, .points = {p.at}});
            break;
        case PK::Line:
            if(p.ends.size() == 2)
                out.prims.push_back({.kind = PreviewPrim::Kind::Path,
                                     .points = {p.ends[0].at, p.ends[1].at}});
            break;
        case PK::Round: {
            const Vec2d c = p.centre.at;
            if(p.ends.size() != 2) {
                out.prims.push_back({.kind = PreviewPrim::Kind::Circle,
                                     .points = {c},
                                     .radius = p.radius});
                break;
            }
            // An arc from its start, counter-clockwise.
            const double a0 =
                std::atan2(p.ends[0].at.y - c.y, p.ends[0].at.x - c.x);
            double span =
                std::atan2(p.ends[1].at.y - c.y, p.ends[1].at.x - c.x) - a0;
            span = std::fmod(std::fmod(span, kTurn) + kTurn, kTurn);
            if(span == 0.0) span = kTurn;
            out.prims.push_back(
                {.kind = PreviewPrim::Kind::Path,
                 .points = geometry::arc_path(c, p.radius, a0, span)});
            break;
        }
        case PK::Body:
            break;
    }
}

// Whether `p` reads a value of its own that the sketch solver may move: a
// free point element, a line ending at one, a circle or an arc that is not
// fixed (its radius) or centred on one, a body. A derived point or a fixed
// one may still move with its element or its body: false only says that
// the settle must tell.
bool movable(const ToolPick & p, const Model & m) {
    auto free = [&](std::string_view name) {
        const auto e = std::ranges::find(m.sketch(), name, &SketchInfo::name);
        return e != m.sketch().end() && !e->fixed &&
               (e->type == SketchType::Point || e->type == SketchType::Circle ||
                e->type == SketchType::Arc);
    };
    switch(p.kind) {
        case PK::Vertex:
            return free(p.ref);
        case PK::Line:
            return std::ranges::any_of(
                p.ends, [&](const ToolPick::Point & e) { return free(e.ref); });
        case PK::Round:
            return free(p.key.name) || free(p.centre.ref);
        case PK::Body:
            return true;
    }
    return true;
}

}  // namespace

std::optional<std::string> immovable(const ToolContext & ctx,
                                     std::span<const ToolPick> picks,
                                     const std::string & formula) {
    const Document & doc = ctx.document();
    const Model * m = doc.model().get();
    if(!m || !doc.has_instance() ||
       std::ranges::any_of(picks,
                           [&](const ToolPick & p) { return movable(p, *m); }))
        return std::nullopt;
    const Instance & inst = *doc.instance();
    const Json * list = inst.get("constraints");
    const std::string path = std::format(
        "constraints[{}]", list && list->is_array() ? list->size() : 0);
    EditBatch batch;
    batch.set(path, Json{{"name", unused_constraint_name(inst, "probe")},
                         {"expr", formula}});
    const std::string dims[] = {path};
    if(!ctx.try_constrain(batch, dims, {}).stuck.front()) return std::nullopt;
    return std::format("nothing {} reads can move", formula);
}

PickTool::Proposal PickTool::proposed(std::size_t signature,
                                      std::span<const ToolPick> picks,
                                      const ToolContext & ctx) const {
    Proposal p = propose(signature, picks, ctx);
    // Smart Dimension's holds at the drawing: a reference (Tried::fixed).
    if(completion_ == Completion::Pick && p.refusal.empty() &&
       !p.formula.empty())
        if(std::optional<std::string> why = immovable(ctx, picks, p.formula))
            p.refusal = std::move(*why);
    return p;
}

PickTool::PickTool(std::string id, std::string label,
                   std::vector<Signature> signatures,
                   std::vector<std::string> methods, Completion completion)
    : id_(std::move(id))
    , label_(std::move(label))
    , signatures_(std::move(signatures))
    , methods_(std::move(methods))
    , completion_(completion) {}

std::string PickTool::hint() const {
    std::string head = label_;
    if(methods_.size() > 1)
        head += ", " + methods_[static_cast<std::size_t>(method_)];
    if(hover_ && refused_ && !refused_->empty())
        return std::format("{}: {}", head, *refused_);
    if(proposal_) {
        if(!proposal_->refusal.empty())
            return std::format("{}: {}", head, proposal_->refusal);
        std::string out = std::format("{}: a click {}", head, proposal_->does);
        if(!proposal_->note.empty()) out += "; " + proposal_->note;
        return out;
    }
    if(!picks_.empty()) return std::format("{}: {}", head, prompt(picks_));
    std::string out = std::format("{}: pick {}", head, wants());
    if(methods_.size() > 1)
        out +=
            "; M: " +
            methods_[static_cast<std::size_t>(method_ + 1) % methods_.size()];
    return out;
}

void PickTool::set_method(int method) {
    const int n = std::max<int>(1, static_cast<int>(methods_.size()));
    method_ = ((method % n) + n) % n;
    on_method_changed();
    // Proposed with the method before: the next move proposes again.
    hover_.reset();
    refused_.reset();
    proposal_.reset();
}

void PickTool::set_methods(std::vector<std::string> methods) {
    methods_ = std::move(methods);
    method_ = 0;
}

SnapFilter PickTool::snap_filter() const {
    return {1u << static_cast<unsigned>(SnapResult::Kind::None)};
}

PickFilter PickTool::pick_filter() const {
    PickFilter out;
    for(const Signature & s : signatures_)
        if(s.size() > picks_.size() && prefix_of(s, picks_))
            out = out | s[picks_.size()];
    return out;
}

bool PickTool::prefix_of(const Signature & s, std::span<const ToolPick> picks) {
    if(picks.size() > s.size()) return false;
    for(std::size_t i = 0; i < picks.size(); ++i)
        if(!s[i].accepts(picks[i].kind)) return false;
    return true;
}

std::optional<std::size_t> PickTool::completed(
    std::span<const ToolPick> picks) const {
    for(std::size_t i = 0; i < signatures_.size(); ++i)
        if(signatures_[i].size() == picks.size() &&
           prefix_of(signatures_[i], picks))
            return i;
    return std::nullopt;
}

bool PickTool::prefix(std::span<const ToolPick> picks) const {
    return std::ranges::any_of(signatures_, [&](const Signature & s) {
        return s.size() > picks.size() && prefix_of(s, picks);
    });
}

std::optional<std::string> PickTool::refuse(
    const ToolPick & next, std::span<const ToolPick> picks) const {
    if(std::ranges::contains(picks, next.key, &ToolPick::key))
        return std::string();
    return std::nullopt;
}

void PickTool::on_event(const ToolEvent & e, ToolContext & ctx) {
    switch(e.kind) {
        case ToolEvent::Kind::Move:
            hover(e.pick, ctx);
            break;
        case ToolEvent::Kind::Press: {
            if(e.button != Button::Left) break;
            if(e.pick) {
                hover(e.pick, ctx);
                if(hover_ && !refused_) {
                    take(*e.pick, ctx);
                    break;
                }
            }
            if(completion_ != Completion::Place) break;
            if(const std::optional<std::size_t> s = completed(picks_)) {
                // A copy: place() drops the picks.
                const std::vector<ToolPick> picks = picks_;
                place(*s, picks, e, ctx);
            }
            break;
        }
        case ToolEvent::Kind::Key:
            if(e.key == Key::Backspace && !picks_.empty()) {
                picks_.pop_back();
                on_picks_changed();
                const std::optional<ToolPick> over = hover_;
                proposal_.reset();
                hover(over, ctx);
            }
            break;
        case ToolEvent::Kind::Release:
        case ToolEvent::Kind::Cancel:
            break;
    }
}

void PickTool::hover(std::optional<ToolPick> pick, const ToolContext & ctx) {
    const std::optional<ToolPick> was = std::exchange(hover_, std::nullopt);
    const bool same = was && pick && was->key == pick->key && proposal_;
    refused_.reset();
    if(!pick || !pick_filter().accepts(pick->kind)) {
        proposal_.reset();
        return;
    }
    hover_ = std::move(pick);
    refused_ = refuse(*hover_, picks_);
    if(refused_) {
        proposal_.reset();
        return;
    }
    // Proposed once per pick: a move along a segment changes no formula.
    if(same) return;
    proposal_.reset();
    std::vector<ToolPick> next(picks_.begin(), picks_.end());
    next.push_back(*hover_);
    if(const std::optional<std::size_t> s = completed(next))
        proposal_ = proposed(*s, next, ctx);
}

void PickTool::take(const ToolPick & pick, ToolContext & ctx) {
    std::vector<ToolPick> next(picks_.begin(), picks_.end());
    next.push_back(pick);
    const std::optional<std::size_t> s = completed(next);
    if(s && completion_ == Completion::Pick) {
        const Proposal p = proposed(*s, next, ctx);
        SketchEdit edit = apply(*s, next, p, ctx);
        forget();
        ctx.commit(std::move(edit));
        committed();
        return;
    }
    if(!s && !prefix(next)) return;
    picks_ = std::move(next);
    hover_.reset();
    refused_.reset();
    proposal_.reset();
    on_picks_changed();
}

void PickTool::on_start(ToolContext & ctx) {
    std::vector<SelKey> keys;
    for(const SelKey & k : ctx.selection().keys())
        if(k.kind == SelKey::Kind::Element || k.kind == SelKey::Kind::Body)
            keys.push_back(k);
    // No sketch entry selected (the constraint the last commit selected), or
    // the entries a tool's commit just made (the Steps 9-10 ruling: drawn,
    // then constrained): tool first.
    if(keys.empty() || ctx.selection_from_tool()) return;
    std::size_t longest = 0;
    for(const Signature & s : signatures_)
        longest = std::max(longest, s.size());
    // A selection no signature takes starts it tool first too.
    if(keys.size() > longest) return;
    std::vector<ToolPick> picked;
    for(std::optional<ToolPick> & p : ctx.picks_of(keys)) {
        if(!p) return;
        picked.push_back(std::move(*p));
    }
    std::optional<std::vector<ToolPick>> start;
    // Why refuse() refused a pick, said in the notice if no order fits.
    std::optional<std::string> why;
    // The selection's order first, then each other one in turn.
    std::vector<std::size_t> order(picked.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    do {
        std::vector<ToolPick> picks;
        bool fits = true;
        for(const std::size_t i : order) {
            if(std::optional<std::string> r = refuse(picked[i], picks)) {
                if(!r->empty() && !why) why = std::move(*r);
                fits = false;
                break;
            }
            picks.push_back(picked[i]);
        }
        if(!fits) continue;
        if(const std::optional<std::size_t> s = completed(picks)) {
            // Its label is still to place: the picks wait for it.
            if(completion_ == Completion::Place) {
                picks_ = std::move(picks);
                on_picks_changed();
                return;
            }
            const Proposal p = proposed(*s, picks, ctx);
            ctx.commit(apply(*s, picks, p, ctx));
            done_ = true;
            return;
        }
        if(!start && prefix(picks)) start = std::move(picks);
    } while(std::ranges::next_permutation(order).found);
    if(start) {
        picks_ = std::move(*start);
        on_picks_changed();
        return;
    }
    if(why) ctx.commit(refused(std::format("{}: {}", label_, *why)));
}

PreviewList PickTool::preview() const {
    PreviewList out;
    for(const ToolPick & p : picks_) draw(out, p);
    if(proposal_ && proposal_->refusal.empty() && !proposal_->glyph.empty())
        out.suggestions.push_back(
            {proposal_->formula, proposal_->glyph, proposal_->anchor});
    return out;
}

void PickTool::on_document_changed(ToolContext &) { forget(); }

bool PickTool::escape(ToolContext &) {
    if(picks_.empty()) return false;
    forget();
    return true;
}

void PickTool::forget() {
    const bool had = !picks_.empty();
    picks_.clear();
    hover_.reset();
    refused_.reset();
    proposal_.reset();
    if(had) on_picks_changed();
}

SketchEdit PickTool::apply(std::size_t, std::span<const ToolPick>,
                           const Proposal & p, ToolContext & ctx) {
    if(!p.refusal.empty()) return refused(p.refusal);
    std::string what = label_;
    std::ranges::transform(what, what.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return constraint_edit(ctx, p.prefix, p.formula, what, p.note, p.keys);
}

SketchEdit constraint_edit(ToolContext & ctx, std::string_view prefix,
                           std::string formula, std::string_view what,
                           std::string_view note, Json keys) {
    if(!ctx.document().has_instance()) return SketchEdit();
    ShapeDraft draft(ctx);
    const std::string name =
        draft.constraint(prefix, std::move(formula), std::move(keys));
    SketchEdit out = draft.edit(ctx, std::format("add {} {}", what, name));
    if(!out.refusal.empty()) return out;
    out.created = {{SelKey::Kind::Constraint, name}};
    if(!note.empty())
        out.report = std::format("{}{}{}: {}", out.report,
                                 out.report.empty() ? "" : "; ", name, note);
    return out;
}

}  // namespace gs::app

#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "gs/app/tool.hpp"

namespace gs::app {

// FreeCAD's DrawSketchDefaultHandler conventions (plan 4.2), for a tool
// that places its shape state by state. `State` is an enum whose
// enumerators run from 0, the first state, to End, the last; `Method`
// numbers the construction methods from 0 (an enum, or int).
// - A move updates the preview and the measured fields (track()); a left
//   press takes the state's point (place()) and advances.
// - Reaching End commits one batch (commit()), then restarts in continuous
//   mode, on by default, or asks to be quit (done()).
// - M cycles the method, which restarts the shape.
// - A field applied is locked: track() reads its value (typed()) instead
//   of the pointer. A field of an earlier state, which fields() still
//   gives, ValueField::placed, returns to that state when applied.
//   Once every field of the current state is set, it advances as a press
//   at the last pointer position would.
// - Esc and a right click drop the shape (drop()).
template <class State, class Method = int>
class StateTool : public Tool {
    static_assert(std::is_enum_v<State>, "StateTool: State is an enum");

public:
    std::span<const std::string> methods() const override { return methods_; }
    int method() const override { return method_; }
    // Out of range wraps around, so that M may pass method() + 1.
    void set_method(int method) override {
        const int n = std::max<int>(1, static_cast<int>(methods_.size()));
        method_ = ((method % n) + n) % n;
        restart();
    }
    // The fields of the states up to the current one, in state order.
    std::span<const ValueField> fields() const override {
        std::size_t n = 0;
        while(n < owners_.size() && owners_[n] <= state_) ++n;
        return std::span<const ValueField>(fields_.data(), n);
    }
    void on_event(const ToolEvent & e, ToolContext & ctx) override {
        switch(e.kind) {
            case ToolEvent::Kind::Move:
                last_ = e;
                track(e, ctx);
                break;
            case ToolEvent::Kind::Press:
                if(e.button != Button::Left) break;
                last_ = e;
                if(place(e, ctx)) advance(ctx);
                break;
            case ToolEvent::Kind::Key:
                if(e.key == Key::Enter) finish(ctx);
                if(e.key == Key::Backspace) back(ctx);
                break;
            // A press places at once: one that ends without its release
            // (Cancel) leaves nothing half done.
            case ToolEvent::Kind::Release:
            case ToolEvent::Kind::Cancel:
                break;
        }
    }
    void on_field(std::string_view id, std::string_view text,
                  ToolContext & ctx) override {
        const std::size_t i = index(id);
        if(i >= fields_.size() || owners_[i] > state_) return;
        ValueField & f = fields_[i];
        ToolContext::Value v = parse(f, text, ctx);
        if(!v.value) {
            f.refusal =
                v.refusal.empty() ? std::string("not a value") : v.refusal;
            return;
        }
        f.refusal.reset();
        f.set = true;
        f.text = std::move(v.written);
        values_[i] = v.value;
        enter(owners_[i]);
        if(complete(state_)) {
            if(place(last_, ctx)) advance(ctx);
        } else {
            track(last_, ctx);
        }
    }
    // Drops the shape: what it read may be gone.
    void on_document_changed(ToolContext &) override { restart(); }
    bool in_progress() const override {
        return state_ != State{} ||
               std::ranges::any_of(fields_, &ValueField::set);
    }
    bool escape(ToolContext & ctx) override {
        if(!in_progress()) return false;
        drop(ctx);
        return true;
    }
    bool done() const override { return done_; }

    // Continuous mode: after a commit the tool starts the next shape, else
    // it asks to be quit.
    bool continuous() const { return continuous_; }
    void set_continuous(bool on) { continuous_ = on; }

protected:
    explicit StateTool(std::vector<std::string> methods = {})
        : methods_(std::move(methods)) {}

    State state() const { return state_; }
    Method current_method() const { return static_cast<Method>(method_); }
    // The last Move or Press: where a state completed by typing places.
    const ToolEvent & last_event() const { return last_; }

    // The pointer moved in state(): the preview, and the measured values of
    // the fields not set (measure()).
    virtual void track(const ToolEvent & move, ToolContext & ctx) = 0;
    // A left press, or the last pointer position once typed fields complete
    // the state: takes what the state places, reading typed() for the set
    // fields. False keeps the state.
    virtual bool place(const ToolEvent & press, ToolContext & ctx) = 0;
    // End is reached: one batch for the whole shape, through ctx.commit().
    // False keeps the shape at the state before End, its refusal in the
    // notice.
    virtual bool commit(ToolContext & ctx) = 0;
    // Forgets the shape placed so far.
    virtual void clear() = 0;
    // The value of `text` typed into `field` and the text the field then
    // shows, which a dimension's constant side is (typed_text()); no value,
    // with a refusal, when the tool refuses it.
    virtual ToolContext::Value parse(const ValueField & field,
                                     std::string_view text, ToolContext & ctx) {
        return ctx.evaluate(text, field.unit);
    }
    // Enter (tool.finish) and Backspace (tool.back): nothing by default.
    virtual void finish(ToolContext &) {}
    virtual void back(ToolContext &) {}
    // Esc or a right click with a shape in progress (FreeCAD's
    // rightButtonOrEsc): the shape is dropped. A polyline with two vertices
    // or more finishes open instead.
    virtual void drop(ToolContext &) { restart(); }
    // Whether the fields set complete state `s`: all its Dimensional ones,
    // or, with none, all its Positional ones. A state without fields is
    // complete by a press only.
    virtual bool complete(State s) const {
        bool any = false, all = true, dimensional = false;
        for(std::size_t i = 0; i < fields_.size(); ++i)
            dimensional = dimensional ||
                          (owners_[i] == s &&
                           fields_[i].kind == ValueField::Kind::Dimensional);
        for(std::size_t i = 0; i < fields_.size(); ++i) {
            if(owners_[i] != s ||
               (dimensional &&
                fields_[i].kind != ValueField::Kind::Dimensional))
                continue;
            any = true;
            all = all && fields_[i].set;
        }
        return any && all;
    }

    // Declares a field of state `owner`, in state order: a field of an
    // earlier state after one of a later state would hide in fields().
    void add_field(State owner, ValueField field) {
        const auto at = std::ranges::upper_bound(owners_, owner);
        const auto k = at - owners_.begin();
        owners_.insert(at, owner);
        fields_.insert(fields_.begin() + k, std::move(field));
        values_.insert(values_.begin() + k, std::nullopt);
    }
    // The value applied to field `id`; nullopt while the pointer drives it.
    std::optional<double> typed(std::string_view id) const {
        const std::size_t i = index(id);
        return i < values_.size() ? values_[i] : std::nullopt;
    }
    // The text applied to field `id`, as the document is given it ("30deg"
    // for 30 typed in an angle); "" while the pointer drives it.
    std::string typed_text(std::string_view id) const {
        const std::size_t i = index(id);
        return i < values_.size() && values_[i] ? fields_[i].text
                                                : std::string();
    }
    // Shows `text` in field `id`, at `anchor`, unless a value is applied.
    void measure(std::string_view id, std::string text, Vec2d anchor) {
        const std::size_t i = index(id);
        if(i >= fields_.size()) return;
        fields_[i].anchor = anchor;
        if(!fields_[i].set) fields_[i].text = std::move(text);
    }
    // Unsets the fields of state `s`, with their values and refusals, for a
    // tool that places that state again, a polyline's next vertex: kept, a
    // length typed for one edge would lock the next one too.
    void release(State s) {
        for(std::size_t i = 0; i < fields_.size(); ++i) {
            if(owners_[i] != s) continue;
            fields_[i].set = false;
            fields_[i].refusal.reset();
            values_[i].reset();
        }
    }
    // Forgets the shape and every value applied: the first state.
    void restart() {
        clear();
        enter(State{});
        for(std::size_t i = 0; i < fields_.size(); ++i) {
            fields_[i].set = false;
            fields_[i].refusal.reset();
            values_[i].reset();
        }
    }
    // To the next state; at End, the commit.
    void advance(ToolContext & ctx) {
        enter(static_cast<State>(static_cast<int>(state_) + 1));
        if(state_ != State::End) {
            track(last_, ctx);
            return;
        }
        if(!commit(ctx)) {
            enter(static_cast<State>(static_cast<int>(State::End) - 1));
            return;
        }
        restart();
        done_ = !continuous_;
        track(last_, ctx);
    }

private:
    // The only way state_ changes: ValueField::placed follows it.
    void enter(State s) {
        state_ = s;
        for(std::size_t i = 0; i < fields_.size(); ++i)
            fields_[i].placed = owners_[i] < s;
    }
    std::size_t index(std::string_view id) const {
        return static_cast<std::size_t>(
            std::ranges::find(fields_, id, &ValueField::id) - fields_.begin());
    }

    std::vector<std::string> methods_;
    int method_ = 0;
    State state_{};
    bool continuous_ = true;
    bool done_ = false;
    ToolEvent last_;
    std::vector<ValueField> fields_;
    std::vector<State> owners_;
    std::vector<std::optional<double>> values_;
};

}  // namespace gs::app

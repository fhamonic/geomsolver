#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace gs::app {

// What a call may have changed, for a front-end that redraws only what it
// must (a retained toolkit); an immediate-mode one can compare the generation
// counters instead. A bit follows its counter, not the content: a recompile
// sets Sweep even when every sweep position stayed.
enum class Change : std::uint32_t {
    // Another document (load, reload, Open). Every other part may differ
    // too; Structure is always set with it.
    Instance = 1 << 0,
    // A JSON path may now name another entry (Document::path_generation).
    Structure = 1 << 1,
    // A compile attempt: content, diagnostics and excluded items.
    Content = 1 << 2,
    Model = 1 << 3,   // model() replaced
    Values = 1 << 4,  // x moved
    Sweep = 1 << 5,   // a sweep position
    // The undo or redo stack (their labels too), or what dirty() answers.
    History = 1 << 6,
    Log = 1 << 7,
    Selection = 1 << 8,  // the selected keys or the key to reveal
    Solver = 1 << 9,     // a job's progress or results
    View = 1 << 10,      // the canvas: its options, a fit, a pan or a zoom
    Panels = 1 << 11,    // a panel opened or closed (App::panel_open)
    Hover = 1 << 12,     // the hot key (App::hot), not every pointer move
    // An eye: a name hidden or shown on the canvas (App::visibility). A
    // display item's eye is its "visible" key, a document edit.
    Visibility = 1 << 13,
    // A list's filters (ListPresenter::set_filter(), clear_filters()), on
    // that list's own changed(): App::changed() does not carry it, so that
    // a filter of one list resets no other.
    Filters = 1 << 14,
    // The frame's scene computed again (App::scene()), after the change of
    // x, a sweep position or the model it follows: a value a list or a
    // sheet reads from App::scene_now() is new only now.
    Scene = 1 << 15,
    // A sweep player played or paused (App::toggle_play()), which the sweep
    // bar and a range's sheet both show.
    Player = 1 << 16,
};

class Changes {
public:
    constexpr Changes() = default;
    constexpr Changes(Change c) : bits_(static_cast<std::uint32_t>(c)) {}

    constexpr Changes & operator|=(Changes other) {
        bits_ |= other.bits_;
        return *this;
    }
    friend constexpr Changes operator|(Changes a, Changes b) { return a |= b; }
    constexpr bool operator==(const Changes &) const = default;

    constexpr bool has(Change c) const {
        return (bits_ & static_cast<std::uint32_t>(c)) != 0;
    }
    constexpr bool has_any(Changes other) const {
        return (bits_ & other.bits_) != 0;
    }
    constexpr bool any() const { return bits_ != 0; }

private:
    std::uint32_t bits_ = 0;
};

constexpr Changes operator|(Change a, Change b) { return Changes(a) | b; }

// Synchronous observer list. A slot must not change the object that emits
// to it: the emitter is in the middle of its own call, and the slot's change
// would reach the other slots inside this emit, before the first change. A
// slot may disconnect itself or another slot during the emit; a slot
// disconnected there is not called.
template <class... Args>
class Signal {
    struct Slot {
        std::function<void(Args...)> fn;
        bool connected = true;
    };
    using Slots = std::vector<std::shared_ptr<Slot>>;

public:
    // Disconnects in its destructor; it may outlive the Signal.
    class Connection {
    public:
        Connection() = default;
        Connection(Connection && other) noexcept
            : slots_(std::move(other.slots_)), slot_(std::move(other.slot_)) {}
        Connection & operator=(Connection && other) noexcept {
            if(this != &other) {
                disconnect();
                slots_ = std::move(other.slots_);
                slot_ = std::move(other.slot_);
            }
            return *this;
        }
        ~Connection() { disconnect(); }

        void disconnect() {
            if(const std::shared_ptr<Slot> slot = slot_.lock()) {
                slot->connected = false;
                if(const std::shared_ptr<Slots> slots = slots_.lock())
                    std::erase(*slots, slot);
            }
            slots_.reset();
            slot_.reset();
        }

    private:
        friend class Signal;
        Connection(const std::shared_ptr<Slots> & slots,
                   const std::shared_ptr<Slot> & slot)
            : slots_(slots), slot_(slot) {}

        std::weak_ptr<Slots> slots_;
        std::weak_ptr<Slot> slot_;
    };

    Signal() = default;
    // A copy would share the slot list: emitting on either would call the
    // other's slots.
    Signal(const Signal &) = delete;
    Signal & operator=(const Signal &) = delete;

    [[nodiscard]] Connection connect(std::function<void(Args...)> fn) {
        auto slot = std::make_shared<Slot>(Slot{std::move(fn)});
        slots_->push_back(slot);
        return Connection(slots_, slot);
    }

    // Calls the slots in connection order.
    void emit(Args... args) const {
        if(slots_->empty()) return;
        // A copy: a slot that disconnects would erase from the list being
        // iterated.
        const Slots slots = *slots_;
        for(const std::shared_ptr<Slot> & slot : slots)
            if(slot->connected) slot->fn(args...);
    }

private:
    std::shared_ptr<Slots> slots_ = std::make_shared<Slots>();
};

}  // namespace gs::app

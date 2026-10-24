#include "gs/app/tool_host.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <utility>

#include "gs/app/app.hpp"

namespace gs::app {
namespace {

constexpr Mods kShift = Mods::Shift;

// Plan 4.3's chords: Shift+letter creates, FreeCAD's single letters
// constrain.
constexpr ToolSpec kTools[] = {
    {"tool.point", "Point", "", {Key::P, kShift}, ToolGroup::Create},
    {"tool.segment", "Segment", "", {Key::L, kShift}, ToolGroup::Create},
    {"tool.polyline", "Polyline", "", {Key::M, kShift}, ToolGroup::Create},
    {"tool.box", "Box", "", {Key::R, kShift}, ToolGroup::Create},
    {"tool.circle", "Circle", "", {Key::C, kShift}, ToolGroup::Create},
    {"tool.arc", "Arc", "", {Key::A, kShift}, ToolGroup::Create},
    {"tool.coincident", "Coincident", "", {Key::C}, ToolGroup::Constrain},
    {"tool.point_on_object",
     "Point on object",
     "On object",
     {Key::O},
     ToolGroup::Constrain},
    {"tool.horizontal", "Horizontal", "", {Key::H}, ToolGroup::Constrain},
    {"tool.vertical", "Vertical", "", {Key::V}, ToolGroup::Constrain},
    {"tool.parallel", "Parallel", "", {Key::P}, ToolGroup::Constrain},
    {"tool.perpendicular", "Perpendicular", "", {Key::N}, ToolGroup::Constrain},
    {"tool.tangent", "Tangent", "", {Key::T}, ToolGroup::Constrain},
    {"tool.equal", "Equal", "", {Key::E}, ToolGroup::Constrain},
    {"tool.symmetric", "Symmetric", "", {Key::S}, ToolGroup::Constrain},
    {"tool.dimension",
     "Smart dimension",
     "Dimension",
     {Key::D},
     ToolGroup::Constrain},
    {"tool.fix", "Fix", "", {Key::K}, ToolGroup::Constrain},
    {"tool.new_body", "New body", "", {Key::B, kShift}, ToolGroup::Body},
    {"tool.body_from_selection",
     "Body from selection",
     "From selection",
     {Key::F, kShift},
     ToolGroup::Body},
};

// The field after `id` among those Tab moves through, the current state's
// (not placed), wrapping around; "" when `id` is not one of them.
std::string next_field(std::span<const ValueField> fields,
                       std::string_view id) {
    std::vector<const ValueField *> cycle;
    for(const ValueField & f : fields)
        if(!f.placed) cycle.push_back(&f);
    const auto at = std::ranges::find(cycle, id, &ValueField::id);
    if(at == cycle.end()) return {};
    return (std::next(at) == cycle.end() ? cycle.front() : *std::next(at))->id;
}

// The field Tab and '=' open with none open: the current state's first
// unset one, else its first.
const ValueField * first_open(std::span<const ValueField> fields) {
    const ValueField * first = nullptr;
    for(const ValueField & f : fields) {
        if(f.placed) continue;
        if(!f.set) return &f;
        if(!first) first = &f;
    }
    return first;
}

std::size_t spec_index(std::string_view id) {
    return static_cast<std::size_t>(
        std::ranges::find(kTools, id, &ToolSpec::id) - std::begin(kTools));
}

// What a view of the host shows: Tool is emitted when it changes.
struct Look {
    std::string id, hint;
    int method = 0;
    bool progress = false;
    std::vector<ValueField> fields;
    PreviewList preview;
    std::optional<ToolHost::Edit> edit;
    // A request raised again (a refused Enter asks the keyboard back) is a
    // change too: a retained view takes the requests on Tool.
    std::optional<std::string> focus;
    std::optional<ToolHost::TextUpdate> update;
    ToolOptions options;

    bool operator==(const Look &) const = default;
};

}  // namespace

std::span<const ToolSpec> tool_specs() { return kTools; }

class ToolHost::Notify {
public:
    explicit Notify(ToolHost & host) : host_(host) {
        if(host_.notify_depth_++ == 0) before_ = look();
    }
    ~Notify() {
        if(--host_.notify_depth_ > 0) return;
        if(look() != before_) host_.changed_.emit(Change::Tool);
    }
    Notify(const Notify &) = delete;
    Notify & operator=(const Notify &) = delete;

private:
    Look look() const {
        Look l;
        l.edit = host_.edit_;
        l.focus = host_.focus_;
        l.update = host_.update_;
        l.options = host_.options_;
        if(const Tool * t = host_.tool_.get()) {
            l.id = std::string(t->id());
            l.hint = t->hint();
            l.method = t->method();
            l.progress = t->in_progress();
            l.fields.assign(t->fields().begin(), t->fields().end());
            l.preview = t->preview();
        }
        return l;
    }

    ToolHost & host_;
    Look before_;
};

ToolHost::ToolHost(App & app) : app_(app), factories_(std::size(kTools)) {}

ToolHost::~ToolHost() = default;

void ToolHost::provide(std::string_view id, Factory make) {
    const std::size_t i = spec_index(id);
    if(i >= factories_.size())
        throw std::invalid_argument("no tool " + std::string(id));
    factories_[i] = std::move(make);
}

bool ToolHost::provided(std::string_view id) const {
    const std::size_t i = spec_index(id);
    return i < factories_.size() && factories_[i] != nullptr;
}

ToolHost::Seen ToolHost::seen_now() const {
    const Document & doc = app_.document();
    return {doc.instance(),
            doc.has_instance() ? doc.path() : std::filesystem::path(),
            doc.path_generation(), doc.compile_generation(),
            doc.restore_generation()};
}

bool ToolHost::drawable() const {
    const Document & doc = app_.document();
    return doc.model() != nullptr && !doc.stale();
}

bool ToolHost::start(std::string_view id) {
    if(!provided(id) || !drawable()) return false;
    const Notify notify(*this);
    cancel();
    tool_ = factories_[spec_index(id)]();
    ToolContext ctx(app_);
    tool_->on_start(ctx);
    seen_ = seen_now();
    drop_closed_edit();
    quit_if_done();
    return true;
}

void ToolHost::cancel() {
    const Notify notify(*this);
    tool_.reset();
    close_edit();
}

bool ToolHost::escape() {
    const Notify notify(*this);
    follow();
    if(!tool_) return false;
    // A text left in a field that lost the keyboard goes first: one Esc,
    // one step.
    if(edit_) {
        const std::string id = edit_->id;
        close_edit();
        closed(id);
        return true;
    }
    drop_or_quit();
    return true;
}

bool ToolHost::right_click() {
    const Notify notify(*this);
    follow();
    if(!tool_) return false;
    close_edit();
    drop_or_quit();
    return true;
}

void ToolHost::drop_or_quit() {
    ToolContext ctx(app_);
    if(tool_->in_progress() && tool_->escape(ctx)) {
        seen_ = seen_now();
        drop_closed_edit();
        quit_if_done();
        return;
    }
    cancel();
}

void ToolHost::event(const ToolEvent & e) {
    const Notify notify(*this);
    follow();
    if(!tool_) return;
    // A text typed and left unapplied in a field counts for the press that
    // places what it measures; a refused one keeps the press from placing
    // a value the field does not show.
    if(e.kind == ToolEvent::Kind::Press && edit_ && !apply_pending()) return;
    // The text applied may have completed the last shape of a tool out of
    // continuous mode, which then quit.
    if(!tool_) return;
    ToolContext ctx(app_);
    tool_->on_event(e, ctx);
    seen_ = seen_now();
    drop_closed_edit();
    quit_if_done();
}

void ToolHost::typed(std::string_view text) {
    const Notify notify(*this);
    follow();
    if(!tool_ || text.empty()) return;
    const char c = text.front();
    const bool seed =
        (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '(';
    if(edit_) {
        // A text left in a field that had the keyboard takes only a seed, or
        // the blank of Space would give it the keyboard back and the Space
        // drag would place a point.
        if(!waiting_ && !seed) return;
        if(std::exchange(selected_, false))
            edit_->text = text;
        else
            edit_->text += text;
        update_ = TextUpdate{edit_->id, edit_->text, edit_->text.size()};
        if(!waiting_) ask_keyboard();
        return;
    }
    const std::span<const ValueField> fields = tool_->fields();
    if(c == '=') {
        if(const ValueField * f = first_open(fields))
            open(f->id, std::string(text.substr(1)));
        return;
    }
    if(!seed) return;
    const auto f = std::ranges::find_if(fields, [](const ValueField & v) {
        return !v.placed && v.kind == ValueField::Kind::Dimensional && !v.set;
    });
    if(f != fields.end()) open(f->id, std::string(text));
}

void ToolHost::finish() {
    const Notify notify(*this);
    follow();
    if(!tool_) return;
    // A field seeded by a typed character is open before the view gives it
    // the keyboard (two frames in ImGui), while these keys still run the
    // actions: they act on the field as they would once it has it, or Enter
    // would place the shape without the value typed.
    if(edit_) {
        field_key(edit_->id, Key::Enter);
        return;
    }
    ToolContext ctx(app_);
    tool_->on_event({.kind = ToolEvent::Kind::Key, .key = Key::Enter}, ctx);
    seen_ = seen_now();
    drop_closed_edit();
    quit_if_done();
}

void ToolHost::back() {
    const Notify notify(*this);
    follow();
    if(!tool_) return;
    if(edit_) {
        // A character, not a byte: half a ° would leave the text unparsable.
        std::string & t = edit_->text;
        if(std::exchange(selected_, false)) t.clear();
        while(!t.empty() &&
              (static_cast<unsigned char>(t.back()) & 0xc0) == 0x80)
            t.pop_back();
        if(!t.empty()) t.pop_back();
        update_ = TextUpdate{edit_->id, t, t.size()};
        if(!waiting_) ask_keyboard();
        return;
    }
    ToolContext ctx(app_);
    tool_->on_event({.kind = ToolEvent::Kind::Key, .key = Key::Backspace}, ctx);
    seen_ = seen_now();
    drop_closed_edit();
    quit_if_done();
}

void ToolHost::next_method() {
    const Notify notify(*this);
    follow();
    // M typed fast after a digit ("5m") must not restart the shape under
    // the field it opened.
    if(!tool_ || edit_ || tool_->methods().size() < 2) return;
    tool_->set_method(tool_->method() + 1);
    drop_closed_edit();
}

void ToolHost::open_field() {
    const Notify notify(*this);
    follow();
    if(!tool_) return;
    if(edit_) {
        field_key(edit_->id, Key::Tab);
        return;
    }
    if(const ValueField * f = first_open(tool_->fields())) open(f->id, {});
}

void ToolHost::tick() {
    const Notify notify(*this);
    follow();
    quit_if_done();
}

void ToolHost::follow() {
    const Seen now = seen_now();
    if(now == seen_) return;
    const bool other = now.instance != seen_.instance && now.file != seen_.file;
    seen_ = now;
    if(!tool_) return;
    if(other || !drawable()) {
        cancel();
        return;
    }
    ToolContext ctx(app_);
    tool_->on_document_changed(ctx);
    seen_ = seen_now();
    drop_closed_edit();
}

void ToolHost::quit_if_done() {
    if(tool_ && tool_->done()) cancel();
}

void ToolHost::open(const std::string & id, std::string text, bool select) {
    update_ = TextUpdate{id, text, text.size(), select};
    edit_ = Edit{id, std::move(text)};
    ask_keyboard();
    selected_ = select;
}

void ToolHost::ask_keyboard() {
    focus_ = edit_->id;
    waiting_ = true;
}

void ToolHost::field_focused(std::string_view id) {
    if(!edit_ || edit_->id != id) return;
    waiting_ = false;
    // The view's field holds the selection now.
    selected_ = false;
}

const ValueField * ToolHost::field(std::string_view id) const {
    if(!tool_) return nullptr;
    const std::span<const ValueField> fields = tool_->fields();
    const auto f = std::ranges::find(fields, id, &ValueField::id);
    return f == fields.end() ? nullptr : &*f;
}

bool ToolHost::typed_into(const ValueField & f) const {
    return edit_ && edit_->id == f.id && !edit_->text.empty() &&
           edit_->text != f.text;
}

void ToolHost::close_edit() {
    edit_.reset();
    focus_.reset();
    waiting_ = false;
    selected_ = false;
    update_.reset();
}

bool ToolHost::apply_pending() {
    if(const ValueField * f = field(edit_->id); f && typed_into(*f))
        return apply();
    const std::string id = edit_->id;
    close_edit();
    closed(id);
    return tool_ != nullptr;
}

void ToolHost::closed(const std::string & id) {
    ToolContext ctx(app_);
    tool_->on_field_closed(id, ctx);
    seen_ = seen_now();
    drop_closed_edit();
    quit_if_done();
}

bool ToolHost::apply() {
    const std::string id = edit_->id;
    ToolContext ctx(app_);
    tool_->on_field(id, edit_->text, ctx);
    seen_ = seen_now();
    const std::span<const ValueField> fields = tool_->fields();
    const auto f = std::ranges::find(fields, id, &ValueField::id);
    if(f != fields.end() && f->refusal) return false;
    edit_.reset();
    waiting_ = false;
    drop_closed_edit();
    quit_if_done();
    return true;
}

void ToolHost::drop_closed_edit() {
    if(!edit_) return;
    if(tool_ &&
       std::ranges::contains(tool_->fields(), edit_->id, &ValueField::id))
        return;
    close_edit();
}

std::optional<std::string> ToolHost::take_field_focus() {
    return std::exchange(focus_, std::nullopt);
}

std::optional<ToolHost::TextUpdate> ToolHost::take_text_update() {
    return std::exchange(update_, std::nullopt);
}

void ToolHost::set_field_text(std::string_view id, std::string text) {
    const Notify notify(*this);
    if(!tool_ || !std::ranges::contains(tool_->fields(), id, &ValueField::id))
        return;
    if(edit_ && edit_->id != id) update_.reset();
    edit_ = Edit{std::string(id), std::move(text)};
    // Only a field with the keyboard reports an edit.
    waiting_ = false;
    selected_ = false;
}

ToolHost::Outcome ToolHost::field_key(std::string_view key_of, Key key) {
    // A copy: finish() and open_field() pass edit_->id, which apply() frees.
    const std::string id(key_of);
    const Notify notify(*this);
    follow();
    if(!tool_) return Outcome::None;
    const std::span<const ValueField> fields = tool_->fields();
    const auto f = std::ranges::find(fields, id, &ValueField::id);
    if(f == fields.end()) return Outcome::None;
    const bool opened = edit_ && edit_->id == id;
    switch(key) {
        case Key::Enter:
            if(!opened) return Outcome::None;
            if(apply()) return Outcome::Applied;
            break;
        case Key::Escape: {
            // The view claims Esc from its toolkit's field: None here would
            // leave a field clicked into with the keyboard, and Esc dead.
            if(!opened) return Outcome::Reverted;
            const std::string shown = f->text;
            close_edit();
            update_ = TextUpdate{id, shown, shown.size()};
            closed(id);
            return Outcome::Reverted;
        }
        case Key::Tab: {
            const bool touched = typed_into(*f);
            if(touched && !apply()) break;
            // After the apply: a field of an earlier state returns to it, and
            // the last of a state moves on to the next state, whose fields
            // Tab does not jump into.
            const std::string next =
                tool_ ? next_field(tool_->fields(), id) : std::string();
            // The state's only field, untouched: opened again empty, Smart
            // Dimension's editor would lose its value, and Enter then apply
            // nothing.
            if(next == id && opened && !touched) return Outcome::None;
            if(!next.empty()) {
                open(next, {});
                return Outcome::Moved;
            }
            if(opened && !touched) close_edit();
            return touched ? Outcome::Applied : Outcome::Reverted;
        }
        default:
            return Outcome::None;
    }
    // Refused: the field keeps the keyboard and the text typed.
    ask_keyboard();
    update_ = TextUpdate{edit_->id, edit_->text, edit_->text.size()};
    return Outcome::Refused;
}

void ToolHost::set_options(const ToolOptions & options) {
    const Notify notify(*this);
    options_ = options;
}

std::string ToolHost::hint() const { return tool_ ? tool_->hint() : ""; }

}  // namespace gs::app

#include "gs/app/dialogs.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <functional>
#include <system_error>

#include "gs/app/app.hpp"

namespace gs::app {

void PathDialog::start(Kind kind) {
    kind_ = kind;
    path_ = app_.document().path().string();
    error_.clear();
    overwrite_ok_.clear();
    open_ = true;
}

bool PathDialog::submit(Replace replace) {
    Document & doc = app_.document();
    if(kind_ == Kind::Open) {
        if(app_.open(path_)) return true;
        error_ =
            doc.log().empty() ? "cannot open the file" : doc.log().back().text;
        return false;
    }
    std::error_code ec;
    const std::filesystem::path target(path_);
    const bool same =
        std::filesystem::equivalent(target, doc.path(), ec) && !ec;
    if(replace == Replace::Ask && !same &&
       std::filesystem::exists(target, ec) && overwrite_ok_ != path_) {
        overwrite_ok_ = path_;
        error_ =
            std::format("'{}' exists: press Save again to replace it", path_);
        return false;
    }
    std::string err;
    if(!doc.save_as(path_, &err)) {
        error_ = err;
        return false;
    }
    overwrite_ok_.clear();
    return true;
}

void UnsavedChanges::ask(std::function<void()> then) {
    if(!doc_.dirty()) {
        then();
        return;
    }
    then_ = std::move(then);
    open_ = true;
}

bool UnsavedChanges::save() {
    if(!doc_.save()) return false;
    discard();
    return true;
}

void UnsavedChanges::discard() {
    if(const std::function<void()> then = std::exchange(then_, nullptr)) then();
}

DeleteRequest::DeleteRequest(App & app)
    : app_(app), doc_(app.document()), selection_(app.selection()) {}

void DeleteRequest::request() {
    if(!doc_.has_instance() || selection_.empty()) return;
    DeletePlan plan = plan_delete(*doc_.instance(), selection_);
    if(plan.broken.empty() && plan.unparsed.empty() && plan.taken.empty() &&
       plan.check.empty()) {
        perform(std::move(plan));
        return;
    }
    plan_ = std::move(plan);
    plan_compile_ = doc_.compile_generation();
    open_ = true;
}

bool DeleteRequest::valid() {
    if(plan_ && doc_.compile_generation() != plan_compile_) plan_.reset();
    return plan_.has_value();
}

std::string DeleteRequest::question() const {
    std::string names;
    for(const SelKey & k : plan_->deleted)
        names += (names.empty() ? "" : ", ") + deleted_name(k);
    return std::format("Delete {}?", names);
}

std::vector<DeleteRequest::Group> DeleteRequest::groups() const {
    std::vector<Group> out;
    auto group = [&](const char * intro,
                     const std::vector<std::string> & paths) {
        if(paths.empty()) return;
        Group & g = out.emplace_back(Group{intro, {}});
        for(const std::string & path : paths) {
            const Json * j = doc_.instance()->get(path);
            g.lines.push_back(
                std::format("  {}: {}", path, j ? json_text(*j) : ""));
        }
    };
    group(plan_->fails_compile
              ? "These expressions use a deleted name. The document will not "
                "compile until you change them, and the last model that "
                "compiled stays active (undo brings everything back):"
              : "These expressions use a deleted name. Their items will be "
                "excluded from the model, in red, until you change them (undo "
                "brings everything back):",
          plan_->broken);
    group("These expressions do not parse, so they may use a deleted name too:",
          plan_->unparsed);
    auto listed = [&](const char * intro,
                      const std::vector<std::string> & lines) {
        if(lines.empty()) return;
        Group & g = out.emplace_back(Group{intro, {}});
        for(const std::string & line : lines) g.lines.push_back("  " + line);
    };
    listed("These elements use what is deleted, and are deleted with it:",
           plan_->taken);
    listed("These vertices are removed from their polylines and polygons:",
           plan_->vertices);
    listed("These points were used only by what is deleted, and go too:",
           plan_->orphans);
    listed("These constraints are deleted with what they constrain:",
           plan_->removed);
    listed(
        "These formulas read an edge or a vertex that moved or went: check "
        "them:",
        plan_->check);
    return out;
}

std::vector<std::string> DeleteRequest::refused() const {
    std::vector<std::string> out;
    for(const std::string & why : plan_->refused)
        out.push_back("Not deleted: " + why);
    return out;
}

void DeleteRequest::confirm() {
    perform(std::move(*plan_));
    plan_.reset();
}

void DeleteRequest::perform(DeletePlan plan) {
    for(const std::string & why : plan.refused)
        doc_.log(LogEntry::Level::Warning, "not deleted: " + why);
    if(plan.batch.ops.empty()) return;
    // An erased param or let moves no path: its drafts must go by hand.
    for(const EditBatch::Op & op : plan.batch.ops)
        app_.panels().host.fields().forget(op.path);
    const std::string label = plan.batch.label;
    doc_.apply(std::move(plan.batch));
    std::vector<int> displays;
    for(const SelKey & k : plan.deleted)
        if(k.kind == SelKey::Kind::Display)
            displays.push_back(k.index);
        else
            selection_.remove(k);
    std::ranges::sort(displays, std::greater<>());
    for(const int i : displays) app_.display_erased(i);
    // After the selection moved, which would hide it at once.
    app_.sync();
    if(!plan.removed.empty()) {
        std::string names;
        for(const std::string & r : plan.removed)
            names += (names.empty() ? "" : ", ") + r.substr(0, r.find(": "));
        app_.notify(Tone::Normal, std::format("{}: deleted {}", label, names));
    }
}

const char * PathDialog::title() const {
    return kind_ == Kind::Open ? "Open instance" : "Save instance as";
}

const char * PathDialog::submit_label() const {
    return kind_ == Kind::Open ? "Open" : "Save";
}

std::string_view ControlsHelp::text() {
    return "Canvas: click a handle, a point, a line or a filled shape to "
           "select it (Shift or Ctrl+click toggles), click empty space to "
           "clear;\n"
           "drag from anywhere but an orange handle or a sketch element to "
           "select what lies inside the box (Shift or Ctrl adds); "
           "double-click shows the entry in its list and in Properties.\n"
           "Drag the orange handles to move design points (they stay in "
           "their domain), and a sketch's elements (their constraints hold). "
           "Middle, right or Space+drag pans, the wheel zooms at the "
           "cursor,\n"
           "F fits all, G fits the design. Right-click for the actions on "
           "the selection: hide and show, enable, fix, select the "
           "associated constraints...\n"
           "Esc drops the shape a tool draws, then quits the tool, then "
           "clears the selection; Del deletes it, after listing the "
           "expressions that use a deleted name; F2 renames the selected "
           "entry.\n"
           "Tools: Shift+P, Shift+L, Shift+M, Shift+R, Shift+C, Shift+A "
           "draw, single letters constrain (see the toolbar's tooltips); a "
           "digit types a value, Tab opens the next field, M takes the next "
           "method, Enter finishes, a right click is Esc; hold Ctrl not to "
           "snap.\n"
           "Lists: click a row to select it; double-click its name, or F2, "
           "to rename it and every use of it; double-click elsewhere to edit "
           "it in Properties.\n"
           "Properties: Enter applies a field and recompiles; Esc cancels. A "
           "let, constraint, criterion or display item with an error\n"
           "is excluded from the model and shown in red, with what uses it; "
           "any other error keeps the last good model active.\n"
           "Margins: drag the vertical line to move the sweep.\n"
           "Ctrl+Z undo, Ctrl+Shift+Z or Ctrl+Y redo (a whole drag is one "
           "step), outside text fields.\n"
           "Ctrl+S save, Ctrl+O open, Ctrl+R reload, Ctrl+Q quit.";
}

}  // namespace gs::app

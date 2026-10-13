#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gs/app/document.hpp"
#include "gs/app/selection.hpp"

namespace gs::app {

class App;

// The questions the application asks. Each raises an open request that the
// front-end takes once and answers by showing its dialog; the dialog then
// calls back the answer.

// The path of the instance to open, or to save the document as.
class PathDialog {
public:
    enum class Kind : std::uint8_t { Open, SaveAs };

    explicit PathDialog(App & app) : app_(app) {}

    // Asks again, starting from the document's path.
    void start(Kind kind);
    bool take_open_request() { return std::exchange(open_, false); }
    Kind kind() const { return kind_; }
    // "Open instance", "Save instance as": the dialog's title.
    const char * title() const;
    // "Open", "Save": the button that submits.
    const char * submit_label() const;
    static constexpr const char * kPrompt =
        "Path of the instance file (.json):";
    static constexpr const char * kCancel = "Cancel";
    // The edited path.
    std::string & path() { return path_; }
    // Why the last submit() failed, or a question about it.
    const std::string & error() const { return error_; }
    // Ask: this dialog asks before Save As replaces another file, and
    // submitting the same path again replaces it. A native save dialog has
    // asked already and passes Confirmed: with Ask its one submit() only
    // gets the question back, since start() forgets the agreement.
    enum class Replace : std::uint8_t { Ask, Confirmed };
    // Opens the instance (and fits the view to it) or saves the document
    // there; true when done, so the dialog closes.
    bool submit(Replace replace = Replace::Ask);

private:
    App & app_;
    Kind kind_ = Kind::Open;
    bool open_ = false;
    std::string path_;
    std::string error_;
    // Save As target the user agreed to replace.
    std::string overwrite_ok_;
};

// Save, discard or cancel, before an action that drops unsaved changes.
class UnsavedChanges {
public:
    explicit UnsavedChanges(Document & doc) : doc_(doc) {}

    static constexpr const char * kTitle = "Unsaved changes";
    static constexpr const char * kQuestion =
        "The document has unsaved changes.";
    static constexpr const char * kSave = "Save";
    static constexpr const char * kDiscard = "Discard";
    static constexpr const char * kCancel = "Cancel";

    // Runs `then` now on a clean document; otherwise keeps it, in place of
    // the one kept before, until the answer.
    void ask(std::function<void()> then);
    bool take_open_request() { return std::exchange(open_, false); }
    // Saves, then runs the kept action. False when the save failed: the
    // question stays.
    bool save();
    void discard();
    void cancel() { then_ = nullptr; }

private:
    Document & doc_;
    std::function<void()> then_;
    bool open_ = false;
};

// Deleting the selection, confirmed first when expressions use a deleted
// name.
class DeleteRequest {
public:
    explicit DeleteRequest(App & app);

    static constexpr const char * kTitle = "Delete";
    static constexpr const char * kDelete = "Delete";
    static constexpr const char * kCancel = "Cancel";

    // Deletes the selection as one undo step, unless expressions use a
    // deleted name: then the plan waits for confirm().
    void request();
    bool take_open_request() { return std::exchange(open_, false); }
    // False once the plan is gone: confirmed, cancelled, or dropped because
    // the document recompiled since. Ctrl+Z works under the dialog, and an
    // undo shifts the paths the plan erases. The dialog then closes.
    // question(), groups(), refused() and confirm() need it true.
    bool valid();
    std::string question() const;  // "Delete A, display[1]?"
    struct Group {
        std::string intro;
        std::vector<std::string> lines;  // "  constraints[1].expr: A.x > 0"
    };
    std::vector<Group> groups() const;
    std::vector<std::string> refused() const;  // "Not deleted: ..."
    void confirm();
    void cancel() { plan_.reset(); }

private:
    void perform(DeletePlan plan);

    App & app_;
    Document & doc_;
    Selection & selection_;
    std::optional<DeletePlan> plan_;
    std::uint64_t plan_compile_ = 0;
    bool open_ = false;
};

class ControlsHelp {
public:
    static constexpr const char * kTitle = "Controls";
    static constexpr const char * kClose = "Close";

    void request() { open_ = true; }
    bool take_open_request() { return std::exchange(open_, false); }
    static std::string_view text();

private:
    bool open_ = false;
};

}  // namespace gs::app

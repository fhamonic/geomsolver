#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "ui/document.hpp"
#include "ui/scene_cache.hpp"

namespace gs::ui {

// Editors for every part of the instance. Texts come from the document (so a
// file that does not compile can still be fixed here); values and status
// come from the last good model and the scene.
class Inspector {
public:
    void draw(Document & doc, const Scene & scene);
    void expand_all() { expand_all_ = true; }

private:
    // RawJson: any JSON text (a plain word is taken as a string); the fallback
    // editor of keys that have no editor of their own.
    enum class Kind { Expression, NumberOrExpression, Text, RawJson };
    // Returns true when the field committed an edit (Enter).
    bool field(Document & doc, const std::string & path, Kind kind, float width,
               const char * hint = nullptr);
    void field_diagnostics(Document & doc, const std::string & path);

    void header(Document & doc, const Scene & scene);
    void params(Document & doc);
    void design(Document & doc);
    void sweeps(Document & doc);
    void lets(Document & doc, const Scene & scene);
    void constraints(Document & doc, const Scene & scene);
    void criteria(Document & doc, const Scene & scene);
    void display(Document & doc);
    void geometry(Document & doc);

    // Structural edits shift array indices: drop every buffered text.
    void forget_edits() {
        edits_.clear();
        held_.clear();
    }

    struct Edit {
        std::string text;
        bool pending = false;  // edited, not yet applied with Enter
        bool active = false;
    };
    std::unordered_map<std::string, Edit> edits_;
    // Value of a fixed variable's slider / drag while it is active. The
    // value reaches the document only when the edit ends, and the editor is
    // refilled from the document every frame: without this the release frame
    // would commit the old value.
    std::unordered_map<std::string, std::array<double, 2>> held_;
    // Paths drawn with field() this frame: an error at any other path gets a
    // raw JSON editor so that it can be fixed here.
    std::unordered_set<std::string> drawn_;
    const Instance * seen_instance_ = nullptr;
    std::uint64_t seen_compile_ = ~0ull;
    std::string new_name_;
    bool expand_all_ = false;
};

}  // namespace gs::ui

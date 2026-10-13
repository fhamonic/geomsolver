#pragma once

#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>

#include "gs/app/changes.hpp"
#include "gs/app/document.hpp"
#include "gs/app/selection.hpp"
#include "gs/app/settings.hpp"

namespace gs::app {

// The eyes of the names, kept for each instance file in the settings, under
// "visibility.<absolute path of the file>": hiding a design point, a geometry
// entry or a let is neither a document edit nor an undo step, and it lasts
// through undo, reload and the next start. An anonymous display item's eye is
// its "visible" key instead, an edit of the document; a display item that
// draws a name has the name's eye.
//
// The sets are not pruned: a name that no longer exists is harmless, and
// undoing its deletion brings its eye back. Only App::entry_renamed() moves a
// name from one entry to another, also when App::sync() follows an undone or
// redone rename.
class Visibility {
public:
    explicit Visibility(SettingsStore & settings) : settings_(settings) {}
    Visibility(const Visibility &) = delete;
    Visibility & operator=(const Visibility &) = delete;

    // A design point or a geometry entry is shown unless hidden, and so is a
    // let that a display item draws.
    bool hidden(std::string_view name) const { return hidden_.contains(name); }
    // A let that no display item draws is hidden unless shown.
    bool let_shown(std::string_view name) const {
        return shown_.contains(name);
    }
    void set_hidden(std::string_view name, bool hidden);
    void set_let_shown(std::string_view name, bool shown);
    void renamed(std::string_view from, std::string_view to);
    // Brings the eyes up to date with `doc`: another instance (Open, Reload)
    // reads those of its file, nothing for a document without a file; after
    // a Save As the eyes stay and are stored for the new file.
    void follow(const Document & doc);

    // Visibility, when an eye changes.
    Signal<Changes> & changed() { return changed_; }

private:
    using Names = std::set<std::string, std::less<>>;
    void save() const;

    SettingsStore & settings_;
    // "" while the document has no file: the eyes last for the session.
    std::string key_;
    // A new instance is made while the old one is alive, so another
    // instance has another address.
    const Instance * source_ = nullptr;
    Names hidden_, shown_;
    Signal<Changes> changed_;
};

// The entry whose eye `key` has: a design point, a geometry entry, a let or
// an anonymous display item, itself; a display item that draws a name
// (display_keys()), that name. Nullopt for an entry without an eye (a design
// scalar, a param, a sweep, a constraint, a criterion) or that neither the
// model nor the document has.
std::optional<SelKey> eye_of(const Document & doc, const SelKey & key);
// Whether the eye of `eye`, an eye_of() result, shows it.
bool eye_open(const Document & doc, const Visibility & eyes,
              const SelKey & eye);
// Where the eye of `eye`, an eye_of() result, is kept, open or not: a
// display item's in the file, as an undo step; a name's in the settings.
std::string eye_tip(const Document & doc, const SelKey & eye, bool open);
// Opens or closes the eyes of `keys`. A name's changes `eyes`; those of the
// anonymous display items change their "visible" key, together in one undo
// step ("hide display[2]"), which shows them again by erasing it.
void set_eyes(Document & doc, Visibility & eyes, std::span<const SelKey> keys,
              bool open);

}  // namespace gs::app

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "gs/app/selection.hpp"
#include "gs/app/tone.hpp"

namespace gs::app {

class App;

// The Messages panel: the document's problems, then its log. A problem is a
// link to what holds it: activate() shows the entry in its home list and in
// Properties, with the keyboard in the field of the problem's key.
class MessagesPresenter {
public:
    explicit MessagesPresenter(App & app) : app_(app) {}
    MessagesPresenter(const MessagesPresenter &) = delete;
    MessagesPresenter & operator=(const MessagesPresenter &) = delete;

    struct Line {
        Tone tone = Tone::Dim;
        std::string text;
        // The entry that holds the problem, none for the document's own
        // fields ("description", "include[0]").
        std::optional<SelKey> key;
        // The key the problem is at, "" for a line that is no link: a log
        // line, or a problem outside the entries and the Document panel's
        // fields.
        std::string path;
        std::string tip;  // what a click on it shows, for a link
    };
    struct View {
        std::vector<Line> problems;  // diagnostics, then excluded items
        std::vector<Line> log;
    };
    View view() const;
    // "Messages (2 errors)"
    std::string tab_label() const;
    // A click on problems[i] of view(): selects its entry and reveals it
    // (App::reveal()), then gives the keyboard to the field of its key when
    // the sheet has one, a raw JSON one included, or opens the formula
    // pop-up on a problem of a formula; or brings the Document panel and its
    // field. False for a line that is no link.
    bool activate(std::size_t i);

private:
    App & app_;
};

}  // namespace gs::app

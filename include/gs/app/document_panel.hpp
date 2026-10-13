#pragma once

#include <memory>
#include <string_view>

#include "gs/app/sheet.hpp"

namespace gs::app {

class App;

// The Document panel: what the instance file holds besides its entries, as
// a sheet of no entry. The file's path and compile status; "description",
// a MultilineText field (Ctrl+Enter applies); "include", each spec a text
// field with its resolved path and status, and Remove, then a field whose
// Enter adds a file; "format", read-only, the keys that need /2, and "Use
// format /1" once none is left: the program raises the format by itself,
// and lowers it only there, as one undo step.
class DocumentPresenter {
public:
    explicit DocumentPresenter(App & app);
    ~DocumentPresenter();
    DocumentPresenter(const DocumentPresenter &) = delete;
    DocumentPresenter & operator=(const DocumentPresenter &) = delete;

    // A hint without an instance.
    DetailSheet sheet();
    // SheetBuilder's operations on this sheet's items: a field's text, as
    // Enter would apply it, and the buttons.
    bool set(std::string_view item, const DetailValue & value);
    bool press(std::string_view item);

private:
    class Sheet;
    std::unique_ptr<Sheet> sheet_;
};

}  // namespace gs::app

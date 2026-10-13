#include "gs/app/document_panel.hpp"

#include <format>
#include <string>
#include <utility>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/status.hpp"

namespace gs::app {
namespace {

using DK = DetailItem::Kind;

constexpr std::string_view kFormat1 = "geomsolver-instance/1";
constexpr std::string_view kFormat2 = "geomsolver-instance/2";

// The errors of include[i], whose file is `spec`: those of its place in the
// list, and those of the file itself, which carry its spec.
int include_errors(const Document & doc, std::size_t i,
                   const std::string & spec) {
    const std::string at = std::format("include[{}]", i);
    int n = 0;
    for(const Diagnostic & d : doc.diagnostics())
        n += d.severity == Diagnostic::Severity::Error &&
             (path_is_under(d.path, at) ||
              (!spec.empty() &&
               (d.path == spec || d.path.starts_with(spec + ":"))));
    return n;
}

}  // namespace

class DocumentPresenter::Sheet final : public SheetBuilder {
public:
    using SheetBuilder::SheetBuilder;

    // The document's sheet belongs to no entry: its key names none, and
    // build() takes no other.
    static inline const SelKey kKey{};

protected:
    std::optional<DetailSheet> build(const SelKey & key) override {
        if(key != kKey) return std::nullopt;
        const Document & doc = app().document();
        const Instance & inst = *doc.instance();
        DetailSheet s;
        auto add = [&](DetailItem item) { s.items.push_back(std::move(item)); };
        add({.kind = DK::Section, .id = "section.file", .label = "File"});
        add({.kind = DK::Value,
             .id = "path",
             .label = "Path",
             .text = doc.path().string(),
             .tip = "File > Save writes this file; the included files are "
                    "never written"});
        const Status compile = compile_status(doc);
        const std::string warnings = warning_count(doc);
        add({.kind = DK::Value,
             .id = "status",
             .label = "Status",
             .text = warnings.empty()
                         ? compile.text
                         : std::format("{}, {}", compile.text, warnings),
             .tone = compile.tone,
             .tip = "Messages lists the errors and the warnings"});
        add({.kind = DK::Field,
             .id = "description",
             .label = "Description",
             .field = {"description", FieldKind::MultilineText}});

        add({.kind = DK::Section,
             .id = "section.includes",
             .label = "Includes"});
        const Json * list = inst.get("include");
        const std::size_t n = list && list->is_array() ? list->size() : 0;
        for(std::size_t i = 0; i < n; ++i) {
            const Json & spec = (*list)[i];
            const std::string text =
                spec.is_string() ? spec.get<std::string>() : std::string();
            const std::filesystem::path file = inst.path().parent_path() / text;
            const int errors = include_errors(doc, i, text);
            add({.kind = DK::Field,
                 .id = std::format("include{}", i),
                 .label = std::format("Include {}", i + 1),
                 .field = {std::format("include[{}]", i), FieldKind::Text},
                 .tip = "A geometry file, relative to this file's folder"});
            add({.kind = DK::Value,
                 .id = std::format("include{}.status", i),
                 .text = errors > 0
                             ? std::format("{} error(s): see Messages", errors)
                             : std::format("ok: {}", file.string()),
                 .tone = errors > 0 ? Tone::Bad : Tone::Ok,
                 .tip = file.string()});
            add({.kind = DK::Button,
                 .id = std::format("remove{}", i),
                 .label = std::format("Remove include {}", i + 1),
                 .tip = "Takes the file out of the list; the file stays"});
        }
        add({.kind = DK::Field,
             .id = "add_include",
             .label = "Add",
             .field = {std::format("include[{}]", n), FieldKind::Text},
             .tip = "A geometry file, relative to this file's folder: Enter "
                    "adds it to the list"});

        add({.kind = DK::Section, .id = "section.format", .label = "Format"});
        const Json * format = inst.get("format");
        const std::vector<std::string> keys = format2_keys(inst.doc());
        add({.kind = DK::Value,
             .id = "format",
             .label = "Format",
             .text = format ? json_text(*format) : std::string("?"),
             .tip = "Raised to /2 by the program when a key needs it"});
        std::string needs;
        for(const std::string & k : keys)
            needs += (needs.empty() ? "" : ", ") + k;
        add({.kind = DK::Value,
             .id = "needs",
             .label = "Needs /2",
             .text = keys.empty() ? std::string("no key") : needs,
             .tone = Tone::Dim,
             .tip = "The keys that a binary reading only /1 would ignore "
                    "and solve another problem without"});
        if(format && *format == kFormat2 && keys.empty())
            add({.kind = DK::Button,
                 .id = "use_format1",
                 .label = "Use format /1",
                 .tip = "No key needs /2 any more: /1 lets older binaries "
                        "read the file"});
        return s;
    }

    bool run(const SelKey &, const DetailItem & item) override {
        Document & doc = app().document();
        std::string err;
        if(item.id == "use_format1") {
            if(!format2_keys(doc.instance()->doc()).empty()) return false;
            doc.apply(
                EditBatch("use format /1").set("format", std::string(kFormat1)),
                &err);
            return err.empty();
        }
        if(!item.id.starts_with("remove")) return false;
        const std::string i = item.id.substr(6);
        doc.apply(EditBatch(std::format("remove include {}", std::stoi(i) + 1))
                      .erase(std::format("include[{}]", i)),
                  &err);
        return err.empty();
    }
};

DocumentPresenter::DocumentPresenter(App & app)
    : sheet_(std::make_unique<Sheet>(app)) {}

DocumentPresenter::~DocumentPresenter() = default;

DetailSheet DocumentPresenter::sheet() {
    std::optional<DetailSheet> s = sheet_->sheet(Sheet::kKey);
    if(s) return std::move(*s);
    DetailSheet hint;
    hint.items.push_back(
        {.kind = DK::Note, .id = "none", .text = "No instance loaded."});
    return hint;
}

bool DocumentPresenter::set(std::string_view item, const DetailValue & value) {
    return sheet_->set(Sheet::kKey, item, value);
}

bool DocumentPresenter::press(std::string_view item) {
    return sheet_->press(Sheet::kKey, item);
}

}  // namespace gs::app

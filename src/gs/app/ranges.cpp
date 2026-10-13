#include "gs/app/ranges.hpp"

#include <format>
#include <utility>

#include "gs/app/app.hpp"
#include "gs/app/format.hpp"
#include "gs/engine/evaluator.hpp"
#include "gs/solve/settings.hpp"

namespace gs::app {
namespace {

using DK = DetailItem::Kind;

const Column kColumns[] = {{"Sampling", 4.5f},
                           {"Name", 6.5f, ListCell::Kind::Name},
                           {"Interval", 0.0f},
                           {"Position", 4.5f}};

constexpr const char * kSamplingTip =
    "adaptive: the solver starts from the initial samples and adds the "
    "positions where a requirement fails in between; fixed: the samples are "
    "the problem, and what happens between them is only reported";

// The instance's solver settings, also while it does not compile.
gs::SolverSettings settings_of(const Document & doc) {
    const Json * solver = doc.instance()->get("solver");
    return gs::settings_from_json(solver ? *solver : Json::object());
}

std::string text_at(const Document & doc, const std::string & path) {
    const Json * j = doc.instance()->get(path);
    return j ? json_text(*j) : std::string("?");
}

// The model's sweep and its index, nullopt when the model lacks it.
std::optional<std::pair<int, SweepInfo>> model_sweep(const Document & doc,
                                                     const std::string & name) {
    const Model * m = doc.model().get();
    const int k = m ? m->find_sweep(name) : -1;
    if(k < 0) return std::nullopt;
    return std::pair{k, m->sweeps()[static_cast<std::size_t>(k)]};
}

}  // namespace

std::span<const Column> RangesPresenter::columns() const { return kColumns; }

std::vector<RowId> RangesPresenter::children(
    const std::optional<RowId> & parent) {
    std::vector<SelKey> keys;
    const Document & doc = app_.document();
    const Json * sweeps =
        doc.has_instance() ? doc.instance()->get("sweeps") : nullptr;
    if(!parent && sweeps && sweeps->is_object())
        for(const auto & [name, value] : sweeps->items())
            keys.emplace_back(SelKey::Kind::Sweep, name);
    return row_ids(keys);
}

std::optional<ListRow> RangesPresenter::row(const RowId & id) {
    const Document & doc = app_.document();
    if(id.key.kind != SelKey::Kind::Sweep || id.ordinal != 0 ||
       !doc.has_instance())
        return std::nullopt;
    const std::string path = "sweeps." + id.key.name;
    const Json * entry = doc.instance()->get(path);
    if(!entry || !entry->is_object()) return std::nullopt;
    ListRow r{.id = id, .path = path};
    const Json * sampling = doc.instance()->get(path + ".sampling");
    r.cells.push_back({ListCell::Kind::Text,
                       sampling ? json_text(*sampling) : "adaptive",
                       Tone::Normal, kSamplingTip});
    r.cells.push_back({ListCell::Kind::Name, id.key.name, Tone::Normal,
                       app_.panels().host.name(path, {}).tip});
    const Json * samples = doc.instance()->get(path + ".samples");
    const std::string count =
        samples ? json_text(*samples)
                : std::to_string(settings_of(doc).initial_samples);
    const std::string interval =
        std::format("[{}, {}], {} samples", text_at(doc, path + ".min"),
                    text_at(doc, path + ".max"), count);
    r.cells.push_back(
        {ListCell::Kind::Text, interval, Tone::Normal,
         samples ? std::format("{}\nIts own initial samples", interval)
                 : std::format("{}\nThe solver's initial_samples", interval)});
    const auto sweep = model_sweep(doc, id.key.name);
    const std::span<const double> t = doc.sweep_t();
    if(sweep && static_cast<std::size_t>(sweep->first) < t.size())
        r.cells.push_back(
            {ListCell::Kind::Text,
             std::format("{} = {}", id.key.name,
                         format_number(sweep->second.value(
                             t[static_cast<std::size_t>(sweep->first)]))),
             Tone::Normal,
             "Its position now: the canvas shows the design there"});
    else
        r.cells.emplace_back();
    return r;
}

std::vector<std::string> RangesPresenter::toolbar() const {
    return {"range.add", "edit.delete"};
}

Changes RangesPresenter::resets_on() const {
    return Change::Instance | Change::Structure;
}

std::vector<ListCell> RangesPresenter::footer() {
    const Document & doc = app_.document();
    const Model * m = doc.has_instance() ? doc.model().get() : nullptr;
    if(!m)
        return {{ListCell::Kind::Text, "no model to count the rows of",
                 Tone::Dim, "The instance has not compiled yet"}};
    const gs::SolverSettings s = settings_of(doc);
    const int rows =
        m->nlp_layout(SampleSets::initial(*m, s.initial_samples)).m();
    return {{ListCell::Kind::Text,
             std::format("\u2248 {} rows with the initial samples", rows),
             Tone::Normal,
             "The rows of the problem a solve starts from: each requirement's "
             "rows at the initial samples of its sweep. An adaptive sweep adds "
             "the positions where a requirement fails, so a solve ends with "
             "more."},
            {ListCell::Kind::Text,
             std::format("fine check: {} samples (solver settings)",
                         s.verify_samples),
             Tone::Dim,
             "The grid that verifies a design, the solver's verify_samples"}};
}

std::optional<DetailSheet> RangeSheet::build(const SelKey & key) {
    const Document & doc = app().document();
    const std::string path = "sweeps." + key.name;
    const Json * entry = doc.instance()->get(path);
    if(key.kind != SelKey::Kind::Sweep || !entry || !entry->is_object())
        return std::nullopt;
    DetailSheet s;
    s.key = key;
    s.title = "range " + key.name;
    s.path = path;
    auto add = [&](DetailItem item) { s.items.push_back(std::move(item)); };
    add({.kind = DK::Name,
         .id = "name",
         .label = "Name",
         .field = {path, FieldKind::Text},
         .text = key.name,
         .tip = app().panels().host.name(path, {}).tip});
    add({.kind = DK::Field,
         .id = "min",
         .label = "Min",
         .field = {path + ".min", FieldKind::NumberOrExpression},
         .tip = "A number in SI units, or a constant expression: \"-30deg\""});
    add({.kind = DK::Field,
         .id = "max",
         .label = "Max",
         .field = {path + ".max", FieldKind::NumberOrExpression},
         .tip = "A number in SI units, or a constant expression: \"30deg\""});
    // RawJson, not NumberOrExpression: number_or_string("17") is the double
    // 17.0, which the schema's integer type refuses.
    add({.kind = DK::Field,
         .id = "samples",
         .label = "Samples",
         .field = {path + ".samples", FieldKind::RawJson},
         .tip = std::format("An integer: the initial samples of this sweep, "
                            "instead of the solver's initial_samples ({}). "
                            "The key raises the format to /2.",
                            settings_of(doc).initial_samples)});
    const Json * sampling = doc.instance()->get(path + ".sampling");
    add({.kind = DK::Choice,
         .id = "sampling",
         .label = "Sampling",
         .field = {path + ".sampling"},
         .tip = std::format("{}. Fixed raises the format to /2.", kSamplingTip),
         .value = !sampling || *sampling == "adaptive" ? 0
                  : *sampling == "fixed"               ? 1
                                                       : -1,
         .choices = {"adaptive", "fixed"}});
    if(const auto sweep = model_sweep(doc, key.name)) {
        const auto k = static_cast<std::size_t>(sweep->first);
        const std::vector<SweepPlayer> & players = app().players();
        const bool playing = k < players.size() && players[k].playing;
        add({.kind = DK::Slider,
             .id = "position",
             .label = "Position",
             .tip = "The position the canvas and the lists show: no undo "
                    "step",
             .value = k < doc.sweep_t().size()
                          ? sweep->second.value(doc.sweep_t()[k])
                          : sweep->second.min,
             .min = sweep->second.min,
             .max = sweep->second.max});
        add({.kind = DK::Button,
             .id = "play",
             .label = playing ? "Pause" : "Play",
             .tip = "Plays the motion, as the sweep bar does"});
    }
    return s;
}

bool RangeSheet::write(const SelKey & key, const DetailItem & item,
                       const DetailValue & value) {
    Document & doc = app().document();
    if(item.id == "sampling") {
        if(std::get<int>(value) == 1)
            doc.edit(item.field.path, "fixed");
        else if(doc.instance()->get(item.field.path))
            doc.erase(item.field.path);
        else
            return false;
        return true;
    }
    if(item.id != "position") return SheetBuilder::write(key, item, value);
    const auto sweep = model_sweep(doc, key.name);
    if(!sweep || sweep->second.max == sweep->second.min) return false;
    const SweepInfo & sw = sweep->second;
    doc.set_sweep_t(sweep->first,
                    (std::get<double>(value) - sw.min) / (sw.max - sw.min));
    return true;
}

bool RangeSheet::run(const SelKey & key, const DetailItem & item) {
    const auto sweep = model_sweep(app().document(), key.name);
    if(item.id != "play" || !sweep) return false;
    app().toggle_play(static_cast<std::size_t>(sweep->first));
    return true;
}

void RangeSheet::dragged(const SelKey & key, const DetailItem & item,
                         const DetailValue & value) {
    if(item.id == "position") write(key, item, value);
}

}  // namespace gs::app

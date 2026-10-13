#include "gs/app/settings.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace gs::app {

namespace fs = std::filesystem;

FileSettingsStore::FileSettingsStore(fs::path dir)
    : file_(std::move(dir) / "settings.json") {
    load();
}

void FileSettingsStore::load() {
    // A file that cannot be opened keeps what the run wrote: in a directory
    // that cannot be written, every write but the last would be lost.
    std::ifstream in(file_, std::ios::binary);
    if(!in) return;
    const std::string text{std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>()};
    const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    // Cleared only once the file parses: a file another process left half
    // written must not drop the keys this run read or wrote, which the next
    // write saves back.
    if(!doc.is_object()) return;
    values_.clear();
    for(const auto & [key, value] : doc.items())
        if(value.is_string()) values_[key] = value.get<std::string>();
}

std::optional<std::string> FileSettingsStore::read(std::string_view key) const {
    const auto it = values_.find(key);
    if(it == values_.end()) return std::nullopt;
    return it->second;
}

void FileSettingsStore::write(std::string_view key, std::string_view value) {
    load();
    if(const auto it = values_.find(key);
       it != values_.end() && it->second == value)
        return;
    values_.insert_or_assign(std::string(key), std::string(value));
    nlohmann::json doc = nlohmann::json::object();
    for(const auto & [k, v] : values_) doc[k] = v;
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    fs::path tmp = file_;
    tmp += ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << doc.dump(2) << '\n';
    out.close();
    if(out) fs::rename(tmp, file_, ec);
    if(!out || ec) fs::remove(tmp, ec);
}

fs::path default_settings_dir() {
    if(const char * xdg = std::getenv("XDG_CONFIG_HOME")) {
        const fs::path dir(xdg);
        if(dir.is_absolute()) return dir / "geomsolver";
    }
    if(const char * home = std::getenv("HOME")) {
        const fs::path dir(home);
        if(dir.is_absolute()) return dir / ".config" / "geomsolver";
    }
    return {};
}

}  // namespace gs::app

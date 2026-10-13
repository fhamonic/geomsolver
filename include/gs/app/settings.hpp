#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace gs::app {

// The user's settings: what the GUI keeps between runs that is not part of a
// document, such as the panels' layout. Keys and values are text; a missing
// key reads as nullopt.
class SettingsStore {
public:
    virtual ~SettingsStore() = default;
    virtual std::optional<std::string> read(std::string_view key) const = 0;
    virtual void write(std::string_view key, std::string_view value) = 0;
};

// Reads nothing and writes nothing: scripted runs and the tests, which must
// neither depend on nor change the user's settings.
class NullSettingsStore final : public SettingsStore {
public:
    std::optional<std::string> read(std::string_view) const override {
        return std::nullopt;
    }
    void write(std::string_view, std::string_view) override {}
};

// Every key in one JSON object, `dir`/settings.json. A file that cannot be
// read or parsed reads as empty, and leaves the keys read or written before
// as they were. A write re-reads the file before it replaces
// it, so a key another process wrote meanwhile is kept, then writes a
// temporary file and renames it over the old one: an interrupted write leaves
// the previous settings. A write that cannot reach the disk is kept for the
// run only.
class FileSettingsStore final : public SettingsStore {
public:
    explicit FileSettingsStore(std::filesystem::path dir);

    std::optional<std::string> read(std::string_view key) const override;
    void write(std::string_view key, std::string_view value) override;
    const std::filesystem::path & file() const { return file_; }

private:
    void load();

    std::filesystem::path file_;
    std::map<std::string, std::string, std::less<>> values_;
};

// $XDG_CONFIG_HOME/geomsolver, else ~/.config/geomsolver; empty when neither
// variable gives an absolute directory (the XDG specification ignores a
// relative XDG_CONFIG_HOME).
std::filesystem::path default_settings_dir();

}  // namespace gs::app

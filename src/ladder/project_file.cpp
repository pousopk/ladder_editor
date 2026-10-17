#include "ladder/project_file.hpp"
#include "ladder/notation.hpp"
#include <fmt/format.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace ladder {

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

constexpr int kFormat = 1;

// Thrown while reading; loadProject() turns it into the error.
struct LoadError {
    std::string message;
};

std::string readText(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw LoadError{fmt::format("can't read {}", path.filename().string())};
    }
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

json readJson(const fs::path& path) {
    try {
        return json::parse(readText(path));
    } catch (const json::parse_error& error) {
        throw LoadError{fmt::format("{} isn't valid JSON: {}", path.filename().string(), error.what())};
    }
}

// A string field, with a default when it's missing.
std::string text(const json& object, const char* key, const std::string& fallback = {}) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return fallback;
    }
    if (!it->is_string()) {
        throw LoadError{fmt::format("'{}' must be a string", key)};
    }
    return it->get<std::string>();
}

std::string required(const json& object, const char* key, const std::string& where) {
    if (!object.contains(key)) {
        throw LoadError{fmt::format("{}: '{}' is missing", where, key)};
    }
    return text(object, key);
}

void readKopBlock(Block& block, const fs::path& path) {
    const auto file = path.filename().string();
    const auto data = readJson(path);
    block.returnType = text(data, "returnType");
    for (const auto& entry : data.value("interface", json::array())) {
        Variable variable;
        variable.name = required(entry, "name", file);
        variable.type = required(entry, "type", file);
        const auto section = text(entry, "section", "VAR");
        const auto parsed = sectionFromName(section);
        if (!parsed) {
            throw LoadError{fmt::format("{}: '{}' has an unknown section '{}'", file, variable.name, section)};
        }
        variable.section = *parsed;
        variable.initial = text(entry, "initial");
        variable.address = text(entry, "address");
        variable.comment = text(entry, "comment");
        block.variables.push_back(std::move(variable));
    }
    std::size_t number = 0;
    for (const auto& entry : data.value("networks", json::array())) {
        ++number;
        auto rung = parseRung(required(entry, "rung", file));
        if (!rung) {
            throw LoadError{fmt::format("{}, network {}: {}", file, number, rung.error())};
        }
        rung->comment = text(entry, "comment");
        block.networks.push_back(std::move(*rung));
    }
}

json kopBlockJson(const Block& block) {
    json data = json::object();
    if (block.kind == BlockKind::Function) {
        data["returnType"] = block.returnType;
    }
    data["interface"] = json::array();
    for (const auto& variable : block.variables) {
        json entry = {{"name", variable.name}, {"type", variable.type}, {"section", sectionName(variable.section)}};
        // Only what's set, so the file stays short.
        if (!variable.initial.empty())
            entry["initial"] = variable.initial;
        if (!variable.address.empty())
            entry["address"] = variable.address;
        if (!variable.comment.empty())
            entry["comment"] = variable.comment;
        data["interface"].push_back(std::move(entry));
    }
    data["networks"] = json::array();
    for (const auto& rung : block.networks) {
        json entry = json::object();
        if (!rung.comment.empty())
            entry["comment"] = rung.comment;
        entry["rung"] = formatRung(rung);
        data["networks"].push_back(std::move(entry));
    }
    return data;
}

// Writes next to the target, then renames: the old file is replaced in one
// step, so it's never half-written.
void writeAtomically(const fs::path& path, const std::string& contents) {
    const auto temporary = fs::path(path).concat(".tmp");
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << contents;
        if (!file) {
            throw LoadError{fmt::format("can't write {}", temporary.string())};
        }
    }
    std::error_code error;
    fs::rename(temporary, path, error);
    if (error) {
        throw LoadError{fmt::format("can't replace {}: {}", path.string(), error.message())};
    }
}

// The block files listed in an existing project.json, if there is one.
std::set<std::string> listedFiles(const fs::path& folder) {
    std::set<std::string> files;
    std::error_code error;
    if (!fs::exists(folder / "project.json", error)) {
        return files;
    }
    try {
        for (const auto& entry : readJson(folder / "project.json").value("blocks", json::array())) {
            files.insert(text(entry, "file"));
        }
    } catch (const LoadError&) {
        // An unreadable old project.json: just don't remove anything.
    }
    return files;
}

} // namespace

std::string blockFileName(const Block& block) {
    return block.name + (block.language == Language::Kop ? ".kop.json" : ".st");
}

tl::expected<Project, std::string> loadProject(const std::filesystem::path& folder) {
    try {
        const auto data = readJson(folder / "project.json");
        if (data.value("format", 0) != kFormat) {
            throw LoadError{fmt::format("project.json: unknown format {} (this editor reads format {})",
                                        data.value("format", 0), kFormat)};
        }
        Project project;
        project.name = text(data, "name", folder.filename().string());
        for (const auto& entry : data.value("blocks", json::array())) {
            Block block;
            block.name = required(entry, "name", "project.json");
            const auto kind = kindFromName(required(entry, "kind", "project.json"));
            const auto language = languageFromName(required(entry, "language", "project.json"));
            if (!kind || !language) {
                throw LoadError{fmt::format("project.json: block '{}' has an unknown kind or language", block.name)};
            }
            block.kind = *kind;
            block.language = *language;
            const auto file = folder / required(entry, "file", "project.json");
            if (block.language == Language::Kop) {
                readKopBlock(block, file);
            } else {
                block.source = readText(file);
            }
            project.blocks.push_back(std::move(block));
        }
        for (const auto& entry : data.value("watch", json::array())) {
            project.watch.push_back(WatchEntry{required(entry, "path", "project.json, watch table"),
                                               text(entry, "modify", ""), text(entry, "comment", "")});
        }
        return project;
    } catch (const LoadError& error) {
        return tl::unexpected(error.message);
    } catch (const json::exception& error) {
        return tl::unexpected(fmt::format("project files don't have the expected shape: {}", error.what()));
    }
}

tl::expected<void, std::string> saveProject(const Project& project, const std::filesystem::path& folder) {
    try {
        std::error_code error;
        fs::create_directories(folder, error);
        if (error) {
            throw LoadError{fmt::format("can't make the folder {}: {}", folder.string(), error.message())};
        }
        const auto previous = listedFiles(folder);

        json data = {{"format", kFormat}, {"name", project.name}, {"blocks", json::array()}};
        std::set<std::string> current;
        for (const auto& block : project.blocks) {
            const auto file = blockFileName(block);
            current.insert(file);
            data["blocks"].push_back({{"name", block.name},
                                      {"kind", kindName(block.kind)},
                                      {"language", languageName(block.language)},
                                      {"file", file}});
            writeAtomically(folder / file,
                            block.language == Language::Kop ? kopBlockJson(block).dump(2) + "\n" : block.source);
        }
        if (!project.watch.empty()) { // no key at all for an empty table
            data["watch"] = json::array();
            for (const auto& entry : project.watch) {
                json line = {{"path", entry.path}};
                if (!entry.modify.empty()) {
                    line["modify"] = entry.modify;
                }
                if (!entry.comment.empty()) {
                    line["comment"] = entry.comment;
                }
                data["watch"].push_back(std::move(line));
            }
        }
        writeAtomically(folder / "project.json", data.dump(2) + "\n");

        for (const auto& file : previous) {
            if (current.count(file) == 0) {
                fs::remove(folder / file, error); // a block that was deleted or renamed
            }
        }
        return {};
    } catch (const LoadError& error) {
        return tl::unexpected(error.message);
    }
}

} // namespace ladder

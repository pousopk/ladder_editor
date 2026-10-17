#include "online/session.hpp"
#include "monitor/monitor_client.hpp"
#include <algorithm>
#include <cctype>

namespace online {

namespace {

std::string upper(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
    return result;
}

std::vector<std::string> words(std::string_view text) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start < text.size()) {
        const auto end = std::min(text.find(' ', start), text.size());
        if (end > start) {
            result.emplace_back(text.substr(start, end - start));
        }
        start = end + 1;
    }
    return result;
}

bool ok(std::string_view reply) {
    return reply == "OK" || reply.rfind("OK ", 0) == 0;
}

constexpr std::size_t kPathsPerRead = 64; // keeps each request line reasonably short

} // namespace

struct Session::Connection {
    softplc::monitor::MonitorClient client;
};

tl::expected<std::unique_ptr<Session>, std::string> Session::connect(std::uint16_t port, const std::string& host,
                                                                     std::chrono::milliseconds period) {
    auto client = softplc::monitor::MonitorClient::connect(host, port);
    if (!client) {
        return tl::unexpected(client.error());
    }
    auto connection = std::make_unique<Connection>(Connection{std::move(*client)});
    return std::unique_ptr<Session>(new Session(std::move(connection), period));
}

Session::Session(std::unique_ptr<Connection> connection, std::chrono::milliseconds period)
    : connection_(std::move(connection)), period_(period) {
    status_.connected = true;
    thread_ = std::thread([this] { run(); });
}

Session::~Session() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    thread_.join();
}

void Session::watch(std::vector<std::string> paths) {
    std::lock_guard lock(mutex_);
    watched_ = std::move(paths);
}

std::optional<std::string> Session::value(std::string_view path) const {
    std::lock_guard lock(mutex_);
    const auto it = values_.find(upper(path));
    if (it == values_.end() || it->second == "?") {
        return std::nullopt;
    }
    return it->second;
}

std::optional<bool> Session::boolean(std::string_view path) const {
    const auto text = value(path);
    if (text == "TRUE") {
        return true;
    }
    if (text == "FALSE") {
        return false;
    }
    return std::nullopt;
}

void Session::send(std::string command) {
    {
        std::lock_guard lock(mutex_);
        commands_.push_back(std::move(command));
    }
    wake_.notify_all(); // don't wait for the next round
}

std::optional<Session::Variable> Session::describe(std::string_view path) const {
    std::lock_guard lock(mutex_);
    if (!variables_) {
        return std::nullopt;
    }
    const auto it = variables_->find(upper(path));
    return it == variables_->end() ? std::nullopt : std::optional(it->second);
}

bool Session::variablesKnown() const {
    std::lock_guard lock(mutex_);
    return variables_.has_value();
}

void Session::load(std::string_view source) {
    std::string command = "LOAD ";
    for (const char c : source) { // one line: \n for line ends, \\ for backslashes
        if (c == '\n') {
            command += "\\n";
        } else if (c == '\\') {
            command += "\\\\";
        } else if (c != '\r') {
            command += c;
        }
    }
    send(std::move(command));
}

std::optional<Session::LoadResult> Session::lastLoad() const {
    std::lock_guard lock(mutex_);
    return lastLoad_;
}

PlcStatus Session::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

std::map<std::string, std::string> Session::forces() const {
    std::lock_guard lock(mutex_);
    return forces_;
}

void Session::run() {
    for (;;) {
        if (!exchange()) {
            return;
        }
        std::unique_lock lock(mutex_);
        wake_.wait_for(lock, period_, [this] { return stop_ || !commands_.empty(); });
        if (stop_) {
            return;
        }
    }
}

bool Session::exchange() {
    std::deque<std::string> commands;
    std::vector<std::string> watched;
    {
        std::lock_guard lock(mutex_);
        commands.swap(commands_);
        watched = watched_;
    }
    auto& client = connection_->client;
    const auto lost = [&](const std::string& why) {
        std::lock_guard lock(mutex_);
        status_.connected = false;
        status_.running = false;
        status_.lastError = "connection lost: " + why;
        return false;
    };

    // The program's variables don't change while it runs: listed once.
    if (!listed_) {
        auto reply = client.request("LIST");
        if (!reply) {
            return lost(reply.error());
        }
        std::map<std::string, Variable> variables;
        for (const auto& word : words(*reply)) { // OK count:INT lamp:BOOL t1.ET:TIME ...
            if (const auto colon = word.rfind(':'); colon != std::string::npos) {
                const auto path = word.substr(0, colon);
                variables[upper(path)] = Variable{path, word.substr(colon + 1)};
            }
        }
        listed_ = true;
        std::lock_guard lock(mutex_);
        variables_ = std::move(variables);
    }

    std::optional<std::string> refused;
    for (const auto& command : commands) {
        auto reply = client.request(command);
        if (!reply) {
            return lost(reply.error());
        }
        if (command.rfind("LOAD ", 0) == 0) {
            // OK kept=25 initialized=2 boxes newFlag
            LoadResult result;
            result.ok = ok(*reply);
            result.message = *reply;
            if (result.ok) {
                const auto parts = words(*reply);
                for (std::size_t i = 1; i < parts.size(); ++i) {
                    if (parts[i].rfind("kept=", 0) == 0) {
                        result.kept = std::stoul(parts[i].substr(5));
                    } else if (parts[i].rfind("initialized=", 0) != 0) {
                        result.initialized.push_back(parts[i]);
                    }
                }
                listed_ = false; // the program's variables changed: list them again
            }
            std::lock_guard lock(mutex_);
            result.number = lastLoad_ ? lastLoad_->number + 1 : 1;
            lastLoad_ = std::move(result);
            continue;
        }
        if (!ok(*reply)) {
            refused = command + ": " + *reply;
        }
    }

    // The network calls happen without the mutex: the UI never waits on them.
    std::map<std::string, std::string> values;
    for (std::size_t first = 0; first < watched.size(); first += kPathsPerRead) {
        const auto last = std::min(first + kPathsPerRead, watched.size());
        std::string request = "READ";
        for (std::size_t i = first; i < last; ++i) {
            request += ' ' + watched[i];
        }
        auto reply = client.request(request);
        if (!reply) {
            return lost(reply.error());
        }
        const auto answer = words(*reply);
        if (answer.empty() || answer[0] != "OK" || answer.size() != last - first + 1) {
            continue; // a path soft_plc couldn't parse: these stay unknown
        }
        for (std::size_t i = first; i < last; ++i) {
            values[upper(watched[i])] = answer[i - first + 1];
        }
    }
    auto status = client.request("STATUS");
    if (!status) {
        return lost(status.error());
    }
    auto forced = client.request("FORCES");
    if (!forced) {
        return lost(forced.error());
    }
    std::map<std::string, std::string> forces;
    for (const auto& word : words(*forced)) {
        if (const auto equals = word.find('='); equals != std::string::npos) {
            forces[word.substr(0, equals)] = word.substr(equals + 1);
        }
    }

    std::lock_guard lock(mutex_);
    values_ = std::move(values);
    forces_ = std::move(forces);
    status_.running = status->rfind("OK RUN", 0) == 0;
    status_.details = ok(*status) ? status->substr(3) : *status;
    if (refused) {
        status_.lastError = *refused;
    }
    ++status_.updates;
    return true;
}

} // namespace online

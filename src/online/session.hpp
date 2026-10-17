#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tl/expected.hpp>
#include <vector>

// Online mode's connection to a running soft_plc, through its monitoring
// protocol (one line of text per request, see soft_plc's monitor_service.hpp).
//
// The protocol is blocking, and a GUI must never wait on the network, so a
// background thread does all the talking: about ten times a second it sends
// the queued commands (WRITE, FORCE, ...), then reads the watched variables,
// the PLC's status and its forces. The UI thread only swaps lists under a
// mutex: it says what to watch, and takes the latest values.
//
//   UI thread                         session thread
//   ─────────                         ──────────────
//   watch({"start", "t1.Q"}) ──►  READ start t1.Q  ──► soft_plc
//   value("t1.Q")            ◄──  values_ (under the mutex) ◄── OK TRUE FALSE
namespace online {

struct PlcStatus {
    bool connected = false;
    bool running = false;      // RUN; STOP after a runtime error
    std::string details;       // what STATUS said: cycles, times, the error
    std::string lastError;     // the last request soft_plc refused, or why the connection ended
    std::uint64_t updates = 0; // how many times the values were read
};

class Session {
public:
    static tl::expected<std::unique_ptr<Session>, std::string>
    connect(std::uint16_t port, const std::string& host = "127.0.0.1",
            std::chrono::milliseconds period = std::chrono::milliseconds(100));

    ~Session(); // stops the thread and disconnects

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // The variables to read from now on, by path: "start", "t1.Q", "p1.timer.ET".
    void watch(std::vector<std::string> paths);

    // The last value read, as soft_plc writes it (TRUE, 42, T#1s200ms);
    // nothing if it isn't watched, isn't read yet, or soft_plc doesn't know it.
    std::optional<std::string> value(std::string_view path) const;
    std::optional<bool> boolean(std::string_view path) const;

    // A command for soft_plc: "WRITE start TRUE", "FORCE stop FALSE", "UNFORCE stop".
    void send(std::string command);

    // A variable of the PLC: its path as declared and its type (BOOL, INT,
    // TIME, ...). Nothing if the PLC has no such variable, or until the
    // variables are known (they're listed once, when the session starts).
    struct Variable {
        std::string path;
        std::string type;
    };
    std::optional<Variable> describe(std::string_view path) const;
    bool variablesKnown() const;

    PlcStatus status() const;
    // The forced variables and their values, as soft_plc reports them.
    std::map<std::string, std::string> forces() const;

private:
    struct Connection;
    Session(std::unique_ptr<Connection> connection, std::chrono::milliseconds period);
    void run();
    bool exchange(); // one round: commands, values, status; false when the connection is gone

    std::unique_ptr<Connection> connection_;
    std::chrono::milliseconds period_;
    bool listed_ = false; // only the session thread uses it

    mutable std::mutex mutex_; // guards everything below
    std::condition_variable wake_;
    bool stop_ = false;
    std::vector<std::string> watched_;
    std::deque<std::string> commands_;
    std::map<std::string, std::string> values_; // by path in upper case
    std::map<std::string, std::string> forces_;
    std::optional<std::map<std::string, Variable>> variables_; // by path in upper case, once listed
    PlcStatus status_;

    std::thread thread_; // last: started once everything else exists
};

} // namespace online

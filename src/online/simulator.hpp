#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <tl/expected.hpp>

// A soft_plc inside the editor, like PLCSIM in TIA Portal: the project's ST
// runs on its own thread with a 10 ms scan cycle, and answers monitoring
// requests on 127.0.0.1, exactly like `soft_plc --monitor program.st`.
// The editor talks to it through a Session, the same way it talks to a
// soft_plc running on its own.
//
// It has no I/O: nothing overwrites the inputs, so writing an input
// variable (start AT %IX0.0) holds until it's written again, as if someone
// pressed the button. A real soft_plc reads its inputs every cycle, so
// there an input has to be forced.
namespace online {

class Simulator {
public:
    // Compiles `source` and starts running it. The monitoring port is any free one.
    static tl::expected<std::unique_ptr<Simulator>, std::string>
    start(std::string_view source, std::chrono::milliseconds cycleTime = std::chrono::milliseconds(10));

    ~Simulator(); // stops the scan cycle and the server

    Simulator(const Simulator&) = delete;
    Simulator& operator=(const Simulator&) = delete;

    std::uint16_t port() const;

private:
    struct State;
    explicit Simulator(std::unique_ptr<State> state);
    std::unique_ptr<State> state_; // soft_plc's objects, kept out of this header
};

} // namespace online

#include "online/simulator.hpp"
#include "core/io_driver.hpp"
#include "core/scan_cycle.hpp"
#include "monitor/monitor_server.hpp"
#include "monitor/monitor_service.hpp"
#include "st/analyzer.hpp"
#include "st/diagnostic.hpp"
#include "st/st_program.hpp"
#include <atomic>
#include <thread>

namespace online {

namespace {

// No hardware: inputs keep whatever was written to them, outputs go nowhere.
class NoIo : public softplc::IoDriver {
public:
    void readInputs(softplc::ProcessImage&) override {}
    void writeOutputs(const softplc::ProcessImage&) override {}
};

} // namespace

struct Simulator::State {
    softplc::ProcessImage image{16, 16, 256};
    NoIo io;
    std::unique_ptr<softplc::st::StProgram> program;
    std::unique_ptr<softplc::ScanCycle> cycle;
    std::unique_ptr<softplc::monitor::MonitorServer> server;
    std::unique_ptr<softplc::monitor::MonitorService> service;
    std::atomic<bool> stop{false};
    std::thread thread;
};

tl::expected<std::unique_ptr<Simulator>, std::string> Simulator::start(std::string_view source,
                                                                       std::chrono::milliseconds cycleTime) {
    auto compiled = softplc::st::compileProgram(source);
    if (!compiled) {
        return tl::unexpected(softplc::st::formatDiagnostic(compiled.error(), source));
    }
    auto state = std::make_unique<State>();
    for (const auto& [number, size] : softplc::st::dataBlocksUsedBy(*compiled)) {
        state->image.addDataBlock(number, size);
    }
    // On a runtime error the program goes to STOP and stays there: STATUS
    // tells the editor why.
    state->program = std::make_unique<softplc::st::StProgram>(std::move(*compiled));
    state->cycle = std::make_unique<softplc::ScanCycle>(state->image, state->io, *state->program, cycleTime);
    try {
        state->server = std::make_unique<softplc::monitor::MonitorServer>(0);
    } catch (const std::system_error& error) {
        return tl::unexpected(std::string("can't start monitoring: ") + error.what());
    }
    state->service = std::make_unique<softplc::monitor::MonitorService>(*state->program, state->image, *state->cycle,
                                                                     std::string(source));
    auto* s = state.get();
    s->cycle->setIdleTask(
        [s] { s->server->processPending([s](std::string_view request) { return s->service->handle(request); }); });
    s->thread = std::thread([s] { s->cycle->run(s->stop); });
    return std::unique_ptr<Simulator>(new Simulator(std::move(state)));
}

Simulator::Simulator(std::unique_ptr<State> state) : state_(std::move(state)) {}

Simulator::~Simulator() {
    state_->stop = true;
    state_->thread.join();
    state_->server.reset(); // its clients wait on requests only this thread's cycle answered
}

std::uint16_t Simulator::port() const {
    return state_->server->port();
}

} // namespace online

#include "online/session.hpp"
#include "online/simulator.hpp"
#include <chrono>
#include <doctest/doctest.h>
#include <functional>
#include <thread>

using namespace std::chrono_literals;

// The simulator runs a program; a session watches and changes it, over
// TCP, as the editor does.

namespace {

const char* kProgram = "PROGRAM Main\n"
                       "VAR\n"
                       "    start AT %IX0.0 : BOOL;\n"
                       "    stop : BOOL := TRUE;\n"
                       "    motor AT %QX0.0 : BOOL;\n"
                       "    count : INT;\n"
                       "    t1 : TON;\n"
                       "END_VAR\n"
                       "    motor := (start OR motor) AND stop;\n"
                       "    t1(IN := motor, PT := T#50ms);\n"
                       "    IF count < 30000 THEN count := count + 1; END_IF;\n"
                       "END_PROGRAM\n";

// Waits (up to 3 s) until `condition` holds.
bool eventually(const std::function<bool()>& condition) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return false;
}

struct Online {
    std::unique_ptr<online::Simulator> plc;
    std::unique_ptr<online::Session> session;

    explicit Online(const char* program) {
        auto started = online::Simulator::start(program);
        REQUIRE_MESSAGE(started.has_value(), (started ? "" : started.error()));
        plc = std::move(*started);
        auto connected = online::Session::connect(plc->port(), "127.0.0.1", 10ms);
        REQUIRE_MESSAGE(connected.has_value(), (connected ? "" : connected.error()));
        session = std::move(*connected);
    }
};

} // namespace

TEST_CASE("The simulator runs; the session reads what it watches") {
    Online online(kProgram);
    online.session->watch({"count", "motor", "t1.ET", "nothing"});
    REQUIRE(eventually([&] { return online.session->value("count").has_value(); }));
    const auto first = std::stoi(*online.session->value("count"));
    CHECK(eventually([&] { return std::stoi(*online.session->value("count")) > first; })); // it's running
    CHECK(online.session->boolean("MOTOR") == false);                                      // paths in any case
    CHECK(online.session->value("t1.ET") == "T#0ms");
    CHECK_FALSE(online.session->value("nothing").has_value()); // unknown to soft_plc
    CHECK_FALSE(online.session->value("start").has_value());   // not watched
    const auto status = online.session->status();
    CHECK(status.connected);
    CHECK(status.running);
    CHECK(status.details.rfind("RUN cycles=", 0) == 0);
}

TEST_CASE("Writing an input in the simulator holds it, like a button kept pressed") {
    Online online(kProgram);
    online.session->watch({"start", "motor", "t1.Q"});
    online.session->send("WRITE start TRUE");
    CHECK(eventually([&] { return online.session->boolean("motor") == true; }));
    online.session->send("WRITE start FALSE");
    CHECK(eventually([&] { return online.session->boolean("start") == false; }));
    CHECK(online.session->boolean("motor") == true); // holds itself
    CHECK(eventually([&] { return online.session->boolean("t1.Q") == true; }));
}

TEST_CASE("Forcing, and the forces list") {
    Online online(kProgram);
    online.session->watch({"stop", "motor"});
    online.session->send("WRITE start TRUE");
    CHECK(eventually([&] { return online.session->boolean("motor") == true; }));
    online.session->send("FORCE stop FALSE");
    CHECK(eventually([&] { return online.session->boolean("motor") == false; }));
    CHECK(eventually([&] { return online.session->forces().count("stop") == 1; }));
    online.session->send("UNFORCE stop");
    CHECK(eventually([&] { return online.session->forces().empty(); }));
}

TEST_CASE("Refused commands and runtime errors are reported") {
    Online online("PROGRAM Main\nVAR a : INT; b : INT; END_VAR\n    a := 10 / b;\nEND_PROGRAM\n");
    online.session->send("WRITE nothing 1");
    CHECK(eventually([&] { return !online.session->status().lastError.empty(); }));
    CHECK(online.session->status().lastError.find("unknown variable") != std::string::npos);
    CHECK(eventually([&] { return !online.session->status().running && online.session->status().updates > 0; }));
    CHECK(online.session->status().details.find("division by zero") != std::string::npos);
}

TEST_CASE("A program that doesn't compile doesn't start; the session notices a PLC that goes away") {
    CHECK_FALSE(online::Simulator::start("PROGRAM Main\n    x := 1;\nEND_PROGRAM\n").has_value());

    Online online(kProgram);
    REQUIRE(eventually([&] { return online.session->status().updates > 0; }));
    online.plc.reset();
    CHECK(eventually([&] { return !online.session->status().connected; }));
    CHECK(online.session->status().lastError.rfind("connection lost", 0) == 0);
}

#include "online/paths.hpp"

TEST_CASE("Monitor paths of operands") {
    using online::monitorPath;
    CHECK(monitorPath("start", "") == "start");
    CHECK(monitorPath("t1.Q", "") == "t1.Q");
    CHECK(monitorPath("demand", "line.p1") == "line.p1.demand");
    CHECK(monitorPath("%IX0.0", "") == "_kop_IX0_0");
    CHECK_FALSE(monitorPath("%IX0.0", "p1").has_value());
    for (const char* literal : {"TRUE", "false", "30", "-5", "T#5s", "16#FF", "2.5", ""}) {
        CAPTURE(literal);
        CHECK_FALSE(monitorPath(literal, "").has_value());
    }
    CHECK(online::boolLiteral("True") == true);
    CHECK_FALSE(online::boolLiteral("x").has_value());
}

TEST_CASE("The session knows the PLC's variables and their types") {
    Online online(kProgram);
    REQUIRE(eventually([&] { return online.session->variablesKnown(); }));
    const auto count = online.session->describe("COUNT");
    REQUIRE(count.has_value());
    CHECK(count->path == "count"); // as declared
    CHECK(count->type == "INT");
    CHECK(online.session->describe("t1.ET")->type == "TIME");
    CHECK_FALSE(online.session->describe("nothing").has_value());
}

TEST_CASE("Online change through the session: values are kept, new variables listed") {
    Online online(kProgram);
    online.session->watch({"count"});
    REQUIRE(eventually([&] { return online.session->value("count").has_value(); }));
    REQUIRE(eventually([&] { return std::stoi(*online.session->value("count")) > 20; }));

    std::string changed = kProgram;
    changed.replace(changed.find("    t1 : TON;\n"), 14, "    t1 : TON;\n    added : INT := 7;\n");
    online.session->load(changed);
    REQUIRE(eventually([&] { return online.session->lastLoad().has_value(); }));
    const auto result = *online.session->lastLoad();
    CHECK(result.ok);
    CHECK(result.number == 1);
    CHECK(result.initialized == std::vector<std::string>{"added"});
    CHECK(result.kept > 0);
    CHECK(std::stoi(*online.session->value("count")) > 20);                           // not back to 0
    CHECK(eventually([&] { return online.session->describe("added").has_value(); })); // listed again

    online.session->load("PROGRAM Main\n    nothing := 1;\nEND_PROGRAM\n");
    REQUIRE(eventually([&] { return online.session->lastLoad()->number == 2; }));
    CHECK_FALSE(online.session->lastLoad()->ok);
    CHECK(online.session->lastLoad()->message.find("nothing") != std::string::npos);
    CHECK(online.session->status().running); // the old program goes on
}

TEST_CASE("Upload through the session: the running program's source, after a load too") {
    Online online(kProgram);
    online.session->requestSource();
    REQUIRE(eventually([&] { return online.session->lastSource().has_value(); }));
    CHECK(online.session->lastSource()->ok);
    CHECK(online.session->lastSource()->number == 1);
    CHECK(online.session->lastSource()->source == kProgram);

    std::string changed = std::string("// a \\ backslash\n") + kProgram;
    online.session->load(changed);
    REQUIRE(eventually([&] { return online.session->lastLoad().has_value(); }));
    online.session->requestSource();
    REQUIRE(eventually([&] { return online.session->lastSource()->number == 2; }));
    CHECK(online.session->lastSource()->source == changed);
}

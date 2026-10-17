#include "ladder/notation.hpp"
#include "ladder/project_file.hpp"
#include "ladder/translate.hpp"
#include "st/analyzer.hpp"
#include "st/interpreter.hpp"
#include "st_support/diagnostics.hpp"
#include <chrono>
#include <doctest/doctest.h>
#include <string>

using namespace std::chrono_literals;
using ladder::Section;
using ladder::Variable;

// The translated rungs, compiled and run by soft_plc: they must do what the
// ladder does.

namespace {

Variable boolean(const char* name) {
    return Variable{name, "BOOL", Section::Local, "", "", ""};
}
Variable typed(const char* name, const char* type) {
    return Variable{name, type, Section::Local, "", "", ""};
}

// A PROGRAM made of the given rungs, compiled and run with a clock that
// advances 10 ms per cycle.
class Rungs {
public:
    Rungs(std::vector<std::string> rungs, std::vector<Variable> variables)
        : image_(4, 4, 8), interpreter_(compile(rungs, std::move(variables))) {}

    Rungs& set(std::string_view name, softplc::st::Value value) {
        REQUIRE_MESSAGE(interpreter_.setValue(name, value, image_), "no variable " << std::string(name));
        return *this;
    }

    Rungs& cycles(int count = 1) {
        for (int i = 0; i < count; ++i) {
            interpreter_.runCycle(image_, now_);
            now_ += 10ms;
        }
        return *this;
    }

    bool get(std::string_view name) const { return std::get<bool>(value(name)); }
    std::int64_t integer(std::string_view name) const { return std::get<std::int64_t>(value(name)); }
    softplc::ProcessImage& image() { return image_; }

private:
    softplc::st::Value value(std::string_view name) const {
        auto v = interpreter_.valueOf(name, image_);
        REQUIRE_MESSAGE(v.has_value(), "no variable " << std::string(name));
        return *v;
    }

    static softplc::st::CompilationUnit compile(const std::vector<std::string>& rungs,
                                                std::vector<Variable> variables) {
        ladder::Block block;
        block.name = "Main";
        block.variables = std::move(variables);
        for (const auto& text : rungs) {
            auto rung = ladder::parseRung(text);
            REQUIRE_MESSAGE(rung.has_value(), text);
            block.networks.push_back(std::move(*rung));
        }
        const auto translated = ladder::translateBlock(block);
        REQUIRE(translated.problems.empty());
        auto unit = softplc::st::compileProgram(translated.text);
        if (!unit) {
            FAIL("compile error: " << unit.error().message << "\n" << translated.text);
        }
        return std::move(*unit);
    }

    softplc::ProcessImage image_;
    softplc::st::Interpreter interpreter_;
    softplc::TimePoint now_ = softplc::TimePoint{} + 1h;
};

} // namespace

TEST_CASE("Start/stop: start holds itself, stop wins") {
    Rungs plc({"(start | motor) stop => motor"}, {boolean("start"), boolean("stop"), boolean("motor")});
    plc.set("stop", true).cycles();
    CHECK_FALSE(plc.get("motor"));
    plc.set("start", true).cycles();
    CHECK(plc.get("motor"));
    plc.set("start", false).cycles();
    CHECK(plc.get("motor")); // holds itself
    plc.set("stop", false).set("start", true).cycles();
    CHECK_FALSE(plc.get("motor")); // stop wins, even with start pressed
}

TEST_CASE("Set and reset coils latch") {
    Rungs plc({"alarm => S:latched", "ack => R:latched"}, {boolean("alarm"), boolean("ack"), boolean("latched")});
    plc.set("alarm", true).cycles();
    CHECK(plc.get("latched"));
    plc.set("alarm", false).cycles();
    CHECK(plc.get("latched"));
    plc.set("ack", true).cycles();
    CHECK_FALSE(plc.get("latched"));
}

TEST_CASE("A TON in a rung delays its power") {
    Rungs plc({"a {TON t1, PT := T#100ms, ET => elapsed} => lamp"},
              {boolean("a"), boolean("lamp"), typed("t1", "TON"), typed("elapsed", "TIME")});
    plc.set("a", true).cycles(5);
    CHECK_FALSE(plc.get("lamp"));
    CHECK(plc.integer("elapsed") == 40);
    plc.cycles(10);
    CHECK(plc.get("lamp"));
    plc.set("a", false).cycles();
    CHECK_FALSE(plc.get("lamp"));
}

TEST_CASE("A CTU counts rising edges of its power") {
    Rungs plc({"pulse {CTU c, R := clear, PV := 3, CV => count} => done"},
              {boolean("pulse"), boolean("clear"), boolean("done"), typed("c", "CTU"), typed("count", "INT")});
    for (int i = 0; i < 3; ++i) {
        CHECK_FALSE(plc.get("done"));
        plc.set("pulse", true).cycles().set("pulse", false).cycles();
    }
    CHECK(plc.integer("count") == 3);
    CHECK(plc.get("done"));
    plc.set("clear", true).cycles();
    CHECK(plc.integer("count") == 0);
}

TEST_CASE("A box on a branch sees the power that enters the branch") {
    // lamp: on at once while `quick`, or after 50 ms of `slow`; both need `enable`.
    Rungs plc({"enable ({TON t1, PT := T#50ms} | quick) => lamp"},
              {boolean("enable"), boolean("quick"), boolean("lamp"), typed("t1", "TON")});
    plc.set("enable", true).set("quick", true).cycles();
    CHECK(plc.get("lamp"));
    plc.set("quick", false).cycles();
    CHECK_FALSE(plc.get("lamp")); // the timer has run for 10 ms only
    plc.cycles(6);
    CHECK(plc.get("lamp"));
    plc.set("enable", false).set("quick", true).cycles();
    CHECK_FALSE(plc.get("lamp"));
}

TEST_CASE("Direct addresses read inputs and write outputs") {
    Rungs plc({"%IX0.0 /%IX0.1 => %QX0.0, /%QX0.1"}, {});
    plc.image().writeByte(softplc::Area::Input, 0, 0b01);
    plc.cycles();
    CHECK(plc.image().readByte(softplc::Area::Output, 0) == 0b01);
    plc.image().writeByte(softplc::Area::Input, 0, 0b11);
    plc.cycles();
    CHECK(plc.image().readByte(softplc::Area::Output, 0) == 0b10);
}

TEST_CASE("The example project compiles as one ST program, and runs") {
    auto project = ladder::loadProject(LADDER_EDITOR_SOURCE_DIR "/examples/conveyor");
    REQUIRE(project.has_value());
    const auto check = st_support::checkCode(*project);
    CHECK_FALSE(check.error.has_value());
    for (const auto& part : check.translation.parts) {
        CHECK(part.translation.problems.empty());
    }
    auto unit = softplc::st::compileProgram(check.translation.text);
    REQUIRE_MESSAGE(unit.has_value(), (unit ? std::string() : unit.error().message));

    // Start the belt: start, with stop and the emergency stop (wired normally closed) not pressed.
    softplc::ProcessImage image(4, 4, 8);
    softplc::st::Interpreter plc(std::move(*unit));
    auto now = softplc::TimePoint{} + 1h;
    image.writeByte(softplc::Area::Input, 0, 0b0111); // start, stop, eStop
    plc.runCycle(image, now);
    CHECK(image.readBit(softplc::Area::Output, 0, 0)); // the motor runs
    image.writeByte(softplc::Area::Input, 0, 0b0110);  // start released: it holds itself
    plc.runCycle(image, now += 10ms);
    CHECK(image.readBit(softplc::Area::Output, 0, 0));
    image.writeByte(softplc::Area::Input, 0, 0b0100); // stop pressed
    plc.runCycle(image, now += 10ms);
    CHECK_FALSE(image.readBit(softplc::Area::Output, 0, 0));
}

TEST_CASE("A KOP function block used as a box in the KOP program") {
    // Pump: runs while `demand`, unless `fault`; counts its starts.
    ladder::Project project;
    project.name = "pumps";
    auto pump = ladder::newBlock("Pump", ladder::BlockKind::FunctionBlock, ladder::Language::Kop);
    pump.variables = {
        {"demand", "BOOL", Section::Input, "", "", ""},      {"fault", "BOOL", Section::Input, "", "", ""},
        {"running", "BOOL", Section::Output, "", "", ""},    {"starts", "INT", Section::Output, "", "", ""},
        {"edge", "R_TRIG", Section::Local, "", "", ""},      {"counter", "CTU", Section::Local, "", "", ""},
        {"limitReached", "BOOL", Section::Local, "", "", ""}};
    pump.networks = {
        *ladder::parseRung("demand /fault => running"),
        *ladder::parseRung("running {R_TRIG edge} {CTU counter, PV := 1000, CV => starts} => limitReached")};

    auto main = ladder::newBlock("Main", ladder::BlockKind::Program, ladder::Language::Kop);
    main.variables = {{"call", "BOOL", Section::Local, "", "", ""},
                      {"trip", "BOOL", Section::Local, "", "", ""},
                      {"lamp", "BOOL", Section::Local, "", "", ""},
                      {"count", "INT", Section::Local, "", "", ""},
                      {"p1", "Pump", Section::Local, "", "", ""}};
    main.networks = {*ladder::parseRung("call {Pump p1, fault := trip, starts => count} => lamp")};
    project.blocks = {main, pump};

    const auto check = st_support::checkCode(project);
    REQUIRE_MESSAGE(!check.error, (check.error ? check.error->message : "") << "\n" << check.translation.text);
    auto unit = softplc::st::compileProgram(check.translation.text);
    REQUIRE(unit.has_value());
    softplc::ProcessImage image(4, 4, 8);
    softplc::st::Interpreter plc(std::move(*unit));
    auto now = softplc::TimePoint{} + 1h;
    const auto cycle = [&] { plc.runCycle(image, now += 10ms); };
    const auto value = [&](const char* name) { return *plc.valueOf(name, image); };

    plc.setValue("call", true, image);
    cycle();
    CHECK(std::get<bool>(value("lamp")));
    plc.setValue("trip", true, image);
    cycle();
    CHECK_FALSE(std::get<bool>(value("lamp")));
    plc.setValue("trip", false, image);
    cycle();
    CHECK(std::get<bool>(value("lamp")));
    CHECK(std::get<std::int64_t>(value("count")) == 2); // started twice
}

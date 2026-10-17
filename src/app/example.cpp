#include "app/example.hpp"
#include "ladder/rung_file.hpp"

namespace app {

namespace {

constexpr const char* kExampleNetworks =
    R"(// Motor starter: start holds itself, stop (wired normally closed) always wins
(start | motor) stop => motor

// The lamp comes on 2 s after the motor starts
motor {TON lampDelay, PT := T#2s, ET => elapsed} => lamp

// Count boxes on the conveyor, reset by the operator
boxSensor {R_TRIG boxEdge} {CTU boxes, R := resetCount, PV := 10, CV => count} => batchDone
)";

} // namespace

ladder::Project exampleProject() {
    using namespace ladder;
    Project project;
    project.name = "example";
    auto main = newBlock("Main", BlockKind::Program, Language::Kop);
    main.networks = parseRungFile(kExampleNetworks).value();
    main.variables = {
        Variable{"start", "BOOL", Section::Local, "", "%IX0.0", "start button"},
        Variable{"stop", "BOOL", Section::Local, "", "%IX0.1", "stop button, wired normally closed"},
        Variable{"boxSensor", "BOOL", Section::Local, "", "%IX0.2", "a box passes"},
        Variable{"resetCount", "BOOL", Section::Local, "", "%IX0.3", ""},
        Variable{"motor", "BOOL", Section::Local, "", "%QX0.0", ""},
        Variable{"lamp", "BOOL", Section::Local, "", "%QX0.1", ""},
        Variable{"batchDone", "BOOL", Section::Local, "", "%QX0.2", "10 boxes counted"},
        Variable{"lampDelay", "TON", Section::Local, "", "", ""},
        Variable{"elapsed", "TIME", Section::Local, "", "", ""},
        Variable{"boxEdge", "R_TRIG", Section::Local, "", "", ""},
        Variable{"boxes", "CTU", Section::Local, "", "", ""},
        Variable{"count", "INT", Section::Local, "", "", ""},
    };
    project.blocks.push_back(std::move(main));
    project.blocks.push_back(newBlock("Mixer", BlockKind::FunctionBlock, Language::St));
    return project;
}

} // namespace app

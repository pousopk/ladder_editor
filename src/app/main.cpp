// ladder_editor: edits a project of KOP and ST blocks.
//
//   ladder_editor                       the built-in example (can't be saved)
//   ladder_editor examples/conveyor     open a project folder; Ctrl+S saves it
//   ladder_editor myproject             a folder without a project yet: start a new one
#include "app/project_window.hpp"
#include "ladder/block.hpp"
#include "ladder/project_file.hpp"
#include "ladder/rung_file.hpp"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <string>

namespace {

constexpr const char* kExampleNetworks =
    R"(// Motor starter: start holds itself, stop (wired normally closed) always wins
(start | motor) stop => motor

// The lamp comes on 2 s after the motor starts
motor {TON lampDelay, PT := T#2s, ET => elapsed} => lamp

// Count boxes on the conveyor, reset by the operator
{R_TRIG boxEdge} {CTU boxes, R := resetCount, PV := 10, CV => count} => batchDone
)";

ladder::Project exampleProject() {
    using namespace ladder;
    Project project;
    project.name = "example";
    auto main = newBlock("Main", BlockKind::Program, Language::Kop);
    main.networks = parseRungFile(kExampleNetworks).value();
    main.variables = {
        Variable{"start", "BOOL", Section::Local, "", "%IX0.0", "start button"},
        Variable{"stop", "BOOL", Section::Local, "", "%IX0.1", "stop button, wired normally closed"},
        Variable{"motor", "BOOL", Section::Local, "", "%QX0.0", ""},
        Variable{"lamp", "BOOL", Section::Local, "", "%QX0.1", ""},
        Variable{"lampDelay", "TON", Section::Local, "", "", ""},
        Variable{"elapsed", "TIME", Section::Local, "", "", ""},
    };
    project.blocks.push_back(std::move(main));
    project.blocks.push_back(newBlock("Mixer", BlockKind::FunctionBlock, Language::St));
    return project;
}

} // namespace

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    fs::path folder; // empty: the built-in example
    ladder::Project project;
    if (argc == 2) {
        folder = argv[1];
        if (fs::exists(folder / "project.json")) {
            auto loaded = ladder::loadProject(folder);
            if (!loaded) {
                std::fprintf(stderr, "%s: %s\n", folder.string().c_str(), loaded.error().c_str());
                return 1;
            }
            project = std::move(*loaded);
        } else {
            // A new project: one KOP PROGRAM to start with. Nothing is
            // written until it's saved.
            project.name = folder.filename().string();
            project.blocks.push_back(ladder::newBlock("Main", ladder::BlockKind::Program, ladder::Language::Kop));
        }
    } else if (argc == 1) {
        project = exampleProject();
    } else {
        std::fprintf(stderr, "usage: ladder_editor [project folder]\n");
        return 2;
    }

    glfwSetErrorCallback(
        [](int error, const char* description) { std::fprintf(stderr, "GLFW error %d: %s\n", error, description); });
    if (!glfwInit()) {
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(1400, 900, "ladder_editor", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    {
        app::ProjectWindow editor(std::move(project), folder);
        for (;;) {
            // Closing the window with unsaved changes asks first.
            if (glfwWindowShouldClose(window)) {
                if (!editor.modified() || editor.closeConfirmed()) {
                    break;
                }
                glfwSetWindowShouldClose(window, GLFW_FALSE);
                editor.askToClose();
            }
            if (editor.closeConfirmed()) {
                break;
            }
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            editor.draw();
            ImGui::Render();
            int width = 0;
            int height = 0;
            glfwGetFramebufferSize(window, &width, &height);
            glViewport(0, 0, width, height);
            glClearColor(0.08F, 0.08F, 0.09F, 1.0F);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

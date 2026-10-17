// ladder_editor: shows the rungs of a rung file (one rung per line in the
// text notation, see src/ladder/rung_file.hpp).
//
//   ladder_editor                         the built-in example (can't be saved)
//   ladder_editor examples/conveyor.lad   open a file; Ctrl+S saves it
//   ladder_editor new.lad                 a file that doesn't exist yet: start empty
#include "app/editor_window.hpp"
#include "ladder/rung_file.hpp"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <sstream>
#include <string>

namespace {

constexpr const char* kExample = R"(// Motor starter: start holds itself, stop (wired normally closed) always wins
(start | motor) stop => motor

// The lamp comes on 2 s after the motor starts
motor {TON lampDelay, PT := T#2s, ET => elapsed} => lamp

// Count boxes on the conveyor, reset by the operator
{R_TRIG boxEdge} {CTU boxes, R := resetCount, PV := 10, CV => count} => batchDone

// Alarm: latched by a fault, cleared by acknowledge when the fault is gone
fault => S:alarm
acknowledge /fault => R:alarm
)";

bool readFile(const std::string& path, std::string& text) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    text = contents.str();
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::string path; // empty: the built-in example
    std::string text = kExample;
    if (argc == 2) {
        path = argv[1];
        if (!std::filesystem::exists(path)) {
            text.clear(); // a new file
        } else if (!readFile(path, text)) {
            std::fprintf(stderr, "cannot open '%s'\n", argv[1]);
            return 1;
        }
    } else if (argc > 2) {
        std::fprintf(stderr, "usage: ladder_editor [rungs.lad]\n");
        return 2;
    }
    auto rungs = ladder::parseRungFile(text);
    if (!rungs) {
        std::fprintf(stderr, "%s: %s\n", path.empty() ? "built-in example" : path.c_str(), rungs.error().c_str());
        return 1;
    }

    glfwSetErrorCallback(
        [](int error, const char* description) { std::fprintf(stderr, "GLFW error %d: %s\n", error, description); });
    if (!glfwInit()) {
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(1280, 860, "ladder_editor", nullptr, nullptr);
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
        app::EditorWindow editor(path, std::move(*rungs));
        while (!glfwWindowShouldClose(window)) {
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

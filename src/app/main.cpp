// ladder_editor: edits a project of KOP and ST blocks.
//
//   ladder_editor                       the built-in example (can't be saved)
//   ladder_editor examples/conveyor     open a project folder; Ctrl+S saves it
//   ladder_editor myproject             a folder without a project yet: start a new one
//   ladder_editor --simulate ...        and go online at once, in the simulator
//   ladder_editor --connect=5020 ...    and go online with the soft_plc monitoring on that port
#include "app/example.hpp"
#include "app/project_window.hpp"
#include "ladder/block.hpp"
#include "ladder/project_file.hpp"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    fs::path folder; // empty: the built-in example
    ladder::Project project;
    bool simulate = false;
    int connectPort = 0;
    while (argc > 1 && std::string_view(argv[1]).rfind("--", 0) == 0) {
        const std::string_view option = argv[1];
        if (option == "--simulate") {
            simulate = true;
        } else if (option.rfind("--connect=", 0) == 0) {
            connectPort = std::atoi(argv[1] + 10);
        } else {
            argc = 0; // unknown: show the usage
            break;
        }
        --argc;
        ++argv;
    }
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
        project = app::exampleProject();
    } else {
        std::fprintf(stderr, "usage: ladder_editor [--simulate | --connect=PORT] [project folder]\n");
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
        // Going online at start; if it can't, the window says why.
        if (simulate) {
            editor.simulate();
        } else if (connectPort > 0 && connectPort < 65536) {
            editor.connect(static_cast<std::uint16_t>(connectPort));
        }
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

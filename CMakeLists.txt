cmake_minimum_required(VERSION 3.21)
project(ladder_editor CXX)

# Some fetched dependencies (e.g. doctest 2.4.x) declare a
# cmake_minimum_required below 3.5, which CMake 4.x refuses. This is CMake's
# documented workaround for configuring them anyway.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(LADDER_EDITOR_SANITIZERS "Build Debug builds with AddressSanitizer and UBSan" ON)

include(FetchContent)

# --- {fmt}: text formatting (GCC 11 has no <format>) ---
FetchContent_Declare(fmt GIT_REPOSITORY https://github.com/fmtlib/fmt.git GIT_TAG 11.1.4)
FetchContent_MakeAvailable(fmt)

# --- tl::expected: value-or-error return type (std::expected is C++23) ---
FetchContent_Declare(tl-expected GIT_REPOSITORY https://github.com/TartanLlama/expected.git GIT_TAG v1.1.0)
set(EXPECTED_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(EXPECTED_BUILD_PACKAGE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tl-expected)

# Treat third-party headers as system headers so our strict warnings apply
# only to our own code.
function(ladder_editor_mark_system target)
  get_target_property(dirs ${target} INTERFACE_INCLUDE_DIRECTORIES)
  set_target_properties(${target} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${dirs}")
endfunction()
ladder_editor_mark_system(fmt)
ladder_editor_mark_system(expected)

# --- Shared compile settings for our own targets ---
add_library(ladder_editor_options INTERFACE)
target_compile_options(ladder_editor_options INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
)
if(LADDER_EDITOR_SANITIZERS)
  target_compile_options(ladder_editor_options INTERFACE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined -fno-omit-frame-pointer>
  )
  target_link_options(ladder_editor_options INTERFACE $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
endif()

# --- The editor's model: rungs, blocks, projects (no GUI) ---
add_library(ladder_core STATIC
  src/ladder/rung.cpp
  src/ladder/boxes.cpp
  src/ladder/check.cpp
  src/ladder/notation.cpp
  src/ladder/layout.cpp
  src/ladder/rung_file.cpp
  src/ladder/edit.cpp
  src/ladder/history.cpp
)
target_include_directories(ladder_core PUBLIC src)
target_link_libraries(ladder_core
  PUBLIC  ladder_editor_options tl::expected
  PRIVATE fmt::fmt
)

# --- Dear ImGui, without a window: enough for the editor's UI and its tests ---
# Dear ImGui has no CMake support upstream: built as plain static libraries.
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.92.6)
FetchContent_MakeAvailable(imgui)
add_library(imgui_core STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(imgui_core PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/misc/cpp)
ladder_editor_mark_system(imgui_core)

# The editor's user interface, drawn with ImGui but not tied to a window, so
# the UI tests can drive it with simulated mouse and keys.
add_library(ladder_ui STATIC
  src/app/editor_window.cpp
  src/view/rung_view.cpp
)
target_include_directories(ladder_ui PUBLIC src)
target_link_libraries(ladder_ui PUBLIC ladder_core imgui_core PRIVATE fmt::fmt)

# --- The editor application (optional: needs GLFW and OpenGL) ---
option(LADDER_EDITOR_BUILD_APP "Build the ladder_editor application (needs GLFW and OpenGL)" ON)
if(LADDER_EDITOR_BUILD_APP)
  find_package(glfw3 QUIET)
  find_package(OpenGL QUIET)
endif()
if(LADDER_EDITOR_BUILD_APP AND glfw3_FOUND AND OpenGL_FOUND)
  add_library(imgui_backends STATIC
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  )
  target_include_directories(imgui_backends PUBLIC ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(imgui_backends PUBLIC imgui_core glfw OpenGL::GL)
  ladder_editor_mark_system(imgui_backends)

  add_executable(ladder_editor src/app/main.cpp)
  target_link_libraries(ladder_editor PRIVATE ladder_ui imgui_backends fmt::fmt)
elseif(LADDER_EDITOR_BUILD_APP)
  message(STATUS "ladder_editor application not built: GLFW or OpenGL development files not found")
endif()

enable_testing()
add_subdirectory(tests)

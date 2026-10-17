FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git GIT_TAG v2.4.11)
FetchContent_MakeAvailable(doctest)
ladder_editor_mark_system(doctest)

add_executable(ladder_tests
  test_main.cpp
  ladder/rung_test.cpp
  ladder/notation_test.cpp
  ladder/check_test.cpp
  ladder/layout_test.cpp
  ladder/rung_file_test.cpp
  ladder/edit_test.cpp
  ladder/history_test.cpp
)
target_link_libraries(ladder_tests PRIVATE ladder_core doctest::doctest fmt::fmt)
add_test(NAME ladder_tests COMMAND ladder_tests)

# The editor's UI, driven headless: no window, simulated mouse and keyboard.
add_executable(ladder_ui_tests
  test_main.cpp
  ui/editor_ui_test.cpp
)
target_link_libraries(ladder_ui_tests PRIVATE ladder_ui doctest::doctest fmt::fmt)
target_compile_definitions(ladder_ui_tests PRIVATE LADDER_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME ladder_ui_tests COMMAND ladder_ui_tests)

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
  ladder/project_test.cpp
  ladder/translate_test.cpp
  ladder/st_split_test.cpp
  ladder/power_test.cpp
  text/text_document_test.cpp
)
target_link_libraries(ladder_tests PRIVATE ladder_core doctest::doctest fmt::fmt)
target_compile_definitions(ladder_tests PRIVATE LADDER_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME ladder_tests COMMAND ladder_tests)

# Colours and errors for ST, from soft_plc.
add_executable(ladder_st_tests
  test_main.cpp
  st/st_support_test.cpp
  st/translate_run_test.cpp
)
target_link_libraries(ladder_st_tests PRIVATE ladder_st soft_plc_language doctest::doctest)
target_compile_definitions(ladder_st_tests PRIVATE LADDER_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME ladder_st_tests COMMAND ladder_st_tests)

# Online mode: the simulator and the session, talking over TCP on 127.0.0.1.
add_executable(ladder_online_tests
  test_main.cpp
  online/online_test.cpp
)
target_link_libraries(ladder_online_tests PRIVATE ladder_online ladder_st doctest::doctest)
add_test(NAME ladder_online_tests COMMAND ladder_online_tests)

# The editor's UI, driven headless: no window, simulated mouse and keyboard.
add_executable(ladder_ui_tests
  test_main.cpp
  ui/editor_ui_test.cpp
  ui/project_ui_test.cpp
  ui/online_ui_test.cpp
)
target_link_libraries(ladder_ui_tests PRIVATE ladder_ui doctest::doctest fmt::fmt)
target_compile_definitions(ladder_ui_tests PRIVATE LADDER_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME ladder_ui_tests COMMAND ladder_ui_tests)

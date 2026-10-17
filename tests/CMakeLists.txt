FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git GIT_TAG v2.4.11)
FetchContent_MakeAvailable(doctest)
ladder_editor_mark_system(doctest)

add_executable(ladder_tests
  test_main.cpp
  ladder/rung_test.cpp
  ladder/notation_test.cpp
  ladder/check_test.cpp
)
target_link_libraries(ladder_tests PRIVATE ladder_core doctest::doctest fmt::fmt)
add_test(NAME ladder_tests COMMAND ladder_tests)

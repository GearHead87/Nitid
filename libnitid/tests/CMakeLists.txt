# Unit tests for libnitid's pure logic (no GPU needed). Internal code is compiled in directly
# because the shared library only exports the public API.
add_executable(nitid-unit-tests
  test_main.cpp
  ${PROJECT_SOURCE_DIR}/src/input/scene_cut.cpp
)
target_include_directories(nitid-unit-tests PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/include)
target_compile_features(nitid-unit-tests PRIVATE cxx_std_23)
target_compile_options(nitid-unit-tests PRIVATE -Wall -Wextra)
target_link_libraries(nitid-unit-tests PRIVATE Threads::Threads)
add_test(NAME nitid-unit-tests COMMAND nitid-unit-tests)

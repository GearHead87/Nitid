find_package(SDL3 REQUIRED CONFIG)
add_executable(s7-present-timing main.cpp)
target_link_libraries(s7-present-timing PRIVATE spike_common SDL3::SDL3)

set(CMAKE_CXX_STANDARD 20)

add_executable(unit_test main.cpp oneshot_test.cpp)

target_compile_options(unit_test PRIVATE -Wall -Wfatal-errors -Wextra -Wnon-virtual-dtor -pedantic)

find_package(Boost 1.80 COMPONENTS unit_test_framework REQUIRED)
target_link_libraries(unit_test oneshot Boost::headers Boost::unit_test_framework)

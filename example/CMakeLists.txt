set(CMAKE_CXX_STANDARD 20)

add_executable(oneshot_example oneshot_example.cpp)

target_compile_options(oneshot_example PRIVATE -Wall -Wfatal-errors -Wextra -Wnon-virtual-dtor -pedantic)

find_package(Boost 1.80 REQUIRED)
find_package(fmt REQUIRED)
target_link_libraries(oneshot_example oneshot fmt::fmt Boost::headers)

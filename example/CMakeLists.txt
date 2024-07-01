set(CMAKE_CXX_STANDARD 20)

add_executable(oneshot_example oneshot_example.cpp)

target_compile_options(oneshot_example PRIVATE -Wall -Wfatal-errors -Wextra -Wnon-virtual-dtor -pedantic)

find_package(Boost REQUIRED)

target_link_libraries(oneshot_example oneshot Boost::headers)

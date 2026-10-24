cmake_minimum_required(VERSION 3.14)
project(SafeRoute CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include third_party)

set(SOURCES
    src/graph.cpp
    src/dijkstra.cpp
    src/knapsack.cpp
)

# Main executable
add_executable(saferoute src/main.cpp src/server.cpp ${SOURCES})

find_package(Threads REQUIRED)
target_link_libraries(saferoute PRIVATE Threads::Threads)

if(WIN32)
    target_link_libraries(saferoute PRIVATE ws2_32)
endif()

# Test runner executable
add_executable(test_runner tests/test_main.cpp ${SOURCES})
target_link_libraries(test_runner PRIVATE Threads::Threads)

enable_testing()
add_test(NAME AllTests COMMAND test_runner)

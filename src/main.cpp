// main.cpp — Entry point for SafeRoute
//
// What this file does:
//   Serves as the main program entry point. It parses command-line arguments and
//   runs SafeRoute in one of two modes:
//     1. Web server mode (default): starts the embedded HTTP server on port 8080
//        and serves the web application to browser clients.
//     2. CLI mode (--cli flag): launches an interactive terminal menu allowing a
//        student or operator to test shortest paths, detours, road blockages,
//        scenarios, and shelter coverage directly without opening a browser.
//
// What is inside:
//   - runCli: interactive console loop offering 11 disaster management actions.
//   - main: argument parsing (--cli, --port, --data, --web), data loading, and mode dispatch.
//
// Other files that use this file:
//   - Compiled directly as the main application binary: saferoute.

#include "../include/graph.h"
#include "../include/dijkstra.h"
#include "../include/server.h"

// C++ feature note:
// <iostream> provides std::cout, std::cin, and std::cerr for terminal input/output.
// <iomanip> provides formatting manipulators like std::setw and std::setprecision.
// <cstdlib> provides std::atoi for converting text strings to integers.
#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <algorithm>
#include <cstdlib>

// ─── CLI Mode ────────────────────────────────────────────────────────────

// Runs an interactive text-based console interface for testing graph operations.
//
// How it works:
//   Displays a numbered menu in an infinite while-loop until the user enters 0.
//   Each choice prompts for needed parameters (e.g. source and destination node IDs)
//   and executes the corresponding C++ graph or Dijkstra function.
//
// Input:  Graph& graph (active network loaded from network.json).
// Output: Prints results directly to standard output (std::cout).
static void runCli(Graph& graph) {
    std::cout << "\n======================================" << std::endl;
    std::cout << "  SafeRoute — CLI Mode" << std::endl;
    std::cout << "  Disaster Evacuation Route Planner" << std::endl;
    std::cout << "======================================\n" << std::endl;

    std::cout << "Loaded " << graph.nodeCount() << " nodes and "
              << graph.roadCount() << " roads.\n" << std::endl;

    bool running = true;
    while (running) {
        // Display interactive menu
        std::cout << "\n--- SafeRoute Menu ---" << std::endl;
        std::cout << "1. List all nodes & facilities" << std::endl;
        std::cout << "2. List all roads & blockage reasons" << std::endl;
        std::cout << "3. Find shortest route" << std::endl;
        std::cout << "4. Block a road (with reason)" << std::endl;
        std::cout << "5. Restore a road" << std::endl;
        std::cout << "6. Reset all roads" << std::endl;
        std::cout << "7. Compare routes (original vs revised)" << std::endl;
        std::cout << "8. Rank shelters from a location" << std::endl;
        std::cout << "9. Apply disaster scenario (Light, Moderate, Severe)" << std::endl;
        std::cout << "10. View multi-source village coverage" << std::endl;
        std::cout << "11. View nearest services by category" << std::endl;
        std::cout << "0. Exit" << std::endl;
        std::cout << "Choice: ";

        std::string choice;
        std::getline(std::cin, choice);

        // Choice 1: Print all facilities and demo contact numbers
        if (choice == "1") {
            std::cout << "\n--- Facilities & Nodes ---" << std::endl;
            for (const auto& [id, node] : graph.getNodes()) {
                std::cout << "  " << std::setw(6) << node.id
                          << "  " << std::setw(14) << nodeTypeToString(node.type)
                          << "  " << std::setw(28) << node.name
                          << "  Demo: " << node.contact << std::endl;
            }

        // Choice 2: Print all roads, lengths in km, and active disaster blockage reasons
        } else if (choice == "2") {
            std::cout << "\n--- Roads ---" << std::endl;
            for (const auto& [id, road] : graph.getRoads()) {
                std::cout << "  " << std::setw(5) << road.id
                          << "  " << road.from << " <-> " << road.to
                          << "  " << road.distance << " km";
                if (road.blocked) {
                    std::cout << "  [BLOCKED: " << road.reason << "]";
                }
                std::cout << std::endl;
            }

        // Choice 3: Compute and display shortest route between two user-entered nodes
        } else if (choice == "3") {
            std::cout << "Source node ID: ";
            std::string src;
            std::getline(std::cin, src);
            std::cout << "Destination node ID: ";
            std::string dst;
            std::getline(std::cin, dst);

            try {
                PathResult result = dijkstraShortestPath(graph, src, dst);
                if (result.reachable) {
                    std::cout << "Distance: " << result.distance << " km" << std::endl;
                    std::cout << "Path: ";
                    for (size_t i = 0; i < result.path.size(); ++i) {
                        if (i > 0) std::cout << " -> ";
                        std::cout << result.path[i] << " (" << graph.getNode(result.path[i]).name << ")";
                    }
                    std::cout << std::endl;
                } else {
                    std::cout << "No safe route available. Destination is isolated." << std::endl;
                }
            } catch (const std::exception& e) {
                std::cout << "Error: " << e.what() << std::endl;
            }

        // Choice 4: Block a specific road segment with a hazard reason
        } else if (choice == "4") {
            std::cout << "Road ID to block: ";
            std::string rid;
            std::getline(std::cin, rid);
            std::cout << "Obstruction reason (leave empty for default): ";
            std::string reason;
            std::getline(std::cin, reason);
            if (reason.empty()) reason = "Flooded road";

            try {
                graph.blockRoad(rid, reason);
                std::cout << "Road " << rid << " is now BLOCKED (" << reason << ")." << std::endl;
            } catch (const std::exception& e) {
                std::cout << "Error: " << e.what() << std::endl;
            }

        // Choice 5: Reopen a previously closed road segment
        } else if (choice == "5") {
            std::cout << "Road ID to restore: ";
            std::string rid;
            std::getline(std::cin, rid);
            try {
                graph.restoreRoad(rid);
                std::cout << "Road " << rid << " is now OPEN." << std::endl;
            } catch (const std::exception& e) {
                std::cout << "Error: " << e.what() << std::endl;
            }

        // Choice 6: Clear all obstructions across the entire network
        } else if (choice == "6") {
            graph.resetAllRoads();
            std::cout << "All roads restored to open status." << std::endl;

        // Choice 7: Compare original baseline corridor vs revised detour corridor
        } else if (choice == "7") {
            std::cout << "Source node ID: ";
            std::string src;
            std::getline(std::cin, src);
            std::cout << "Destination node ID: ";
            std::string dst;
            std::getline(std::cin, dst);

            try {
                // Compute route with current road blocks
                PathResult revised = dijkstraShortestPath(graph, src, dst);

                // Temporarily reset roads to calculate baseline route
                std::vector<std::pair<std::string, std::string>> savedBlocks;
                for (const auto& [id, road] : graph.getRoads()) {
                    if (road.blocked) savedBlocks.push_back({id, road.reason});
                }
                graph.resetAllRoads();
                PathResult original = dijkstraShortestPath(graph, src, dst);

                // Restore previous blockages
                for (const auto& [rid, rsn] : savedBlocks) {
                    graph.blockRoad(rid, rsn);
                }

                // Output comparison metrics
                std::cout << "\n--- Route Comparison ---" << std::endl;
                std::cout << "Original route: ";
                if (original.reachable) {
                    std::cout << original.distance << " km  [";
                    for (size_t i = 0; i < original.path.size(); ++i) {
                        if (i > 0) std::cout << " -> ";
                        std::cout << graph.getNode(original.path[i]).name;
                    }
                    std::cout << "]" << std::endl;
                } else {
                    std::cout << "unreachable" << std::endl;
                }

                std::cout << "Revised route:  ";
                if (revised.reachable) {
                    std::cout << revised.distance << " km  [";
                    for (size_t i = 0; i < revised.path.size(); ++i) {
                        if (i > 0) std::cout << " -> ";
                        std::cout << graph.getNode(revised.path[i]).name;
                    }
                    std::cout << "]" << std::endl;
                } else {
                    std::cout << "unreachable (no safe route available)" << std::endl;
                }

                if (original.reachable && revised.reachable) {
                    double extra = revised.distance - original.distance;
                    double pct = (original.distance > 0) ? (extra / original.distance) * 100.0 : 0.0;
                    std::cout << "Detour: +" << extra << " km (+"
                              << std::fixed << std::setprecision(1) << pct << "%)" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cout << "Error: " << e.what() << std::endl;
            }

        // Choice 8: Rank all evacuation shelters in order of travel distance
        } else if (choice == "8") {
            std::cout << "Source node ID: ";
            std::string src;
            std::getline(std::cin, src);

            try {
                auto distances = dijkstraAllDistances(graph, src);
                auto shelters = graph.getShelters();

                // Collect and sort shelters by travel distance
                struct SItem {
                    std::string id;
                    std::string name;
                    double dist;
                };
                std::vector<SItem> list;
                for (const auto* s : shelters) {
                    list.push_back({s->id, s->name, distances[s->id]});
                }
                std::sort(list.begin(), list.end(), [](const SItem& a, const SItem& b) {
                    return a.dist < b.dist;
                });

                std::cout << "\n--- Ranked Shelters ---" << std::endl;
                for (size_t i = 0; i < list.size(); ++i) {
                    std::cout << "  " << (i + 1) << ". " << list[i].name << " (" << list[i].id << "): ";
                    if (list[i].dist == UNREACHABLE) {
                        std::cout << "CUT OFF" << std::endl;
                    } else {
                        std::cout << list[i].dist << " km" << std::endl;
                    }
                }
            } catch (const std::exception& e) {
                std::cout << "Error: " << e.what() << std::endl;
            }

        // Choice 9: Generate reproducible disaster scenarios using PRNG seed
        } else if (choice == "9") {
            std::cout << "Severity (light / moderate / severe) [moderate]: ";
            std::string sev;
            std::getline(std::cin, sev);
            if (sev.empty()) sev = "moderate";

            std::cout << "Seed (integer) [2026]: ";
            std::string seedStr;
            std::getline(std::cin, seedStr);
            int seed = seedStr.empty() ? 2026 : std::atoi(seedStr.c_str());

            auto sc = graph.applyScenario(sev, seed);
            std::cout << "\nApplied " << sc.severity << " scenario (seed " << sc.seed
                      << "): " << sc.blockedCount << " roads blocked." << std::endl;
            for (const auto& [rid, rsn] : sc.blockedRoads) {
                const auto& r = graph.getRoad(rid);
                std::cout << "  " << rid << ": " << graph.getNode(r.from).name
                          << " <-> " << graph.getNode(r.to).name << " (" << rsn << ")" << std::endl;
            }

        // Choice 10: Multi-source shelter coverage overview for all villages
        } else if (choice == "10") {
            auto cov = graph.computeCoverage();
            std::cout << "\n--- Village Shelter Coverage ---" << std::endl;
            std::cout << "Total Villages: " << cov.totalVillages
                      << " | Covered: " << cov.coveredVillages
                      << " | Cut Off: " << cov.cutoffVillages << std::endl;

            for (const auto& cv : cov.villageCoverages) {
                std::cout << "  " << std::setw(18) << cv.villageName << " -> ";
                if (cv.covered) {
                    std::cout << cv.assignedShelterName << " (" << cv.distance << " km)";
                } else {
                    std::cout << "[CUT OFF]";
                }
                std::cout << std::endl;
            }

        // Choice 11: Display nearest relief services grouped by facility type
        } else if (choice == "11") {
            std::cout << "Source node ID: ";
            std::string src;
            std::getline(std::cin, src);

            try {
                auto services = graph.getNearestServices(src);
                std::cout << "\n--- Nearest Services ---" << std::endl;
                for (const auto& [cat, list] : services) {
                    std::cout << "[" << cat << "]:" << std::endl;
                    for (const auto& item : list) {
                        std::cout << "  " << item.name << " (" << item.id << ") - ";
                        if (item.reachable) {
                            std::cout << item.distance << " km | Demo: " << item.contact;
                        } else {
                            std::cout << "CUT OFF";
                        }
                        std::cout << std::endl;
                    }
                }
            } catch (const std::exception& e) {
                std::cout << "Error: " << e.what() << std::endl;
            }

        // Choice 0: Exit the CLI loop
        } else if (choice == "0") {
            running = false;
            std::cout << "Goodbye!" << std::endl;

        } else {
            std::cout << "Invalid choice. Please enter 0-11." << std::endl;
        }
    }
}

// ─── main ────────────────────────────────────────────────────────────────

// Application startup entry point.
//
// How it works:
//   1. Parses command-line flags (--cli, --port, --data, --web, --help).
//   2. Loads the road network from JSON (default "data/network.json").
//   3. Dispatches execution to runCli if --cli was provided, or starts the web server.
//
// Input:  Command-line arguments (argc, argv).
// Output: Integer exit status (0 on success, 1 on initialization failure).
int main(int argc, char* argv[]) {
    bool cliMode = false;
    int port = 8080;
    std::string dataPath = "data/network.json";
    std::string webRoot = "web";

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--cli") {
            cliMode = true;
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "--data" && i + 1 < argc) {
            dataPath = argv[++i];
        } else if (arg == "--web" && i + 1 < argc) {
            webRoot = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: saferoute [options]" << std::endl;
            std::cout << "  --cli         Run in command-line mode" << std::endl;
            std::cout << "  --port N      Set server port (default: 8080)" << std::endl;
            std::cout << "  --data PATH   Path to network.json (default: data/network.json)" << std::endl;
            std::cout << "  --web PATH    Path to web/ folder (default: web)" << std::endl;
            std::cout << "  --help        Show this help" << std::endl;
            return 0;
        }
    }

    // Load road network from data file
    Graph graph;
    try {
        graph.loadFromFile(dataPath);
        std::cout << "Loaded network: " << graph.nodeCount() << " nodes, "
                  << graph.roadCount() << " roads." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error loading network data: " << e.what() << std::endl;
        return 1;
    }

    // Branch to CLI or Web server mode
    if (cliMode) {
        runCli(graph);
    } else {
        startServer(graph, port, webRoot);
    }

    return 0;
}

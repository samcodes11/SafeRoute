// test_main.cpp — Automated assert-based test runner for SafeRoute
//
// What this file does:
//   Contains the standalone automated test suite for the SafeRoute system.
//   It verifies graph loading, Dijkstra routing, detour recalculation, cut-off isolation,
//   hazard reason tracking, PRNG scenario reproducibility, multi-source coverage equivalence,
//   trace visualizer fidelity, exception handling, and 0/1 Knapsack relief loading guarantees.
//
// What is inside:
//   - Test counting and reporting harness (runTest, approxEqual).
//   - Hand-crafted 7-node, 8-road test graph (buildSmallTestGraph) for isolated unit tests.
//   - Tests 1-14: Graph routing, scenarios, nearest services, coverage, and error handling.
//   - Tests 15-23: 0/1 Knapsack DP, brute-force benchmark, and limits.
//   - main: runs all 23 tests sequentially and returns exit code 0 on complete pass.
//
// Other files that use this file:
//   - Compiled directly as the test_runner executable.

#include "../include/graph.h"
#include "../include/dijkstra.h"
#include "../include/knapsack.h"
#include "../third_party/json.hpp"

// C++ feature note:
// <functional> provides std::function to pass test functions as callable arguments.
// <cmath> provides std::fabs for floating-point absolute difference comparison.
#include <iostream>
#include <fstream>
#include <string>
#include <cmath>
#include <vector>
#include <functional>
#include <algorithm>
#include <set>
#include <random>

// ─── Test Counters ───────────────────────────────────────────────────────
// Global counters tracking test execution statistics
static int totalTests = 0;
static int passedTests = 0;
static int failedTests = 0;

// Test execution harness: catches any unexpected exceptions and prints [PASS] or [FAIL].
// Input:  test name string, callable boolean test function.
static void runTest(const std::string& name, std::function<bool()> testFn) {
    totalTests++;
    try {
        bool ok = testFn();
        if (ok) {
            passedTests++;
            std::cout << "  [PASS] " << name << std::endl;
        } else {
            failedTests++;
            std::cout << "  [FAIL] " << name << std::endl;
        }
    } catch (const std::exception& e) {
        failedTests++;
        std::cout << "  [FAIL] " << name << " (threw: " << e.what() << ")" << std::endl;
    } catch (...) {
        failedTests++;
        std::cout << "  [FAIL] " << name << " (threw unknown exception)" << std::endl;
    }
}

// Floating-point equality comparison within an epsilon tolerance (default 1e-9).
// Necessary because double precision calculations may have microscopic rounding artifacts.
static bool approxEqual(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) < eps;
}

// ─── Hand-Crafted Small Graph for Targeted Unit Tests ────────────────────

// Builds a predictable 7-node, 8-road test topology with known ground-truth distances.
// Layout:
//   v1 (0,0) ----(3km:r1)----> v2 (100,0) ----(4km:r2)----> s1 (200,0)
//    |                          |                            |
//  (2km:r3)                   (5km:r4)                     (6km:r7)
//    |                          |                            |
//    v                          v                            v
//   h1 (0,100) --(4km:r5)----> j1 (100,100) --(2km:r6)----> s2 (200,100)
//    |
//  (3km:r8)
//    v
//   w1 (0,200)
static Graph buildSmallTestGraph() {
    Graph g;
    g.addNode({"v1", "Village 1", NodeType::Village, 0, 0, "00000 00001", "Note 1"});
    g.addNode({"v2", "Village 2", NodeType::Village, 100, 0, "00000 00002", "Note 2"});
    g.addNode({"s1", "Shelter 1", NodeType::Shelter, 200, 0, "00000 00003", "Note 3"});
    g.addNode({"h1", "Hospital 1", NodeType::Hospital, 0, 100, "00000 00004", "Note 4"});
    g.addNode({"j1", "Junction 1", NodeType::Junction, 100, 100, "00000 00005", "Note 5"});
    g.addNode({"s2", "Shelter 2", NodeType::Shelter, 200, 100, "00000 00006", "Note 6"});
    g.addNode({"w1", "Water 1", NodeType::Water, 0, 200, "00000 00007", "Note 7"});

    g.addRoad({"r1", "v1", "v2", 3.0, false, ""});
    g.addRoad({"r2", "v2", "s1", 4.0, false, ""});
    g.addRoad({"r3", "v1", "h1", 2.0, false, ""});
    g.addRoad({"r4", "v2", "j1", 5.0, false, ""});
    g.addRoad({"r5", "h1", "j1", 4.0, false, ""});
    g.addRoad({"r6", "j1", "s2", 2.0, false, ""});
    g.addRoad({"r7", "s1", "s2", 6.0, false, ""});
    g.addRoad({"r8", "h1", "w1", 3.0, false, ""});

    return g;
}

// ─── Tests 1-14: Routing, Scenarios & Coverage ───────────────────────────

// Test 1: Checks standard shortest path calculation on an unobstructed network.
// Why it matters: Proves Dijkstra finds the true optimal corridor (v1 -> v2 -> s1 = 7.0 km).
static bool testNormalRoute() {
    Graph g = buildSmallTestGraph();
    PathResult res = dijkstraShortestPath(g, "v1", "s1");
    if (!res.reachable) return false;
    if (!approxEqual(res.distance, 7.0)) return false;
    if (res.path != std::vector<std::string>{"v1", "v2", "s1"}) return false;
    return true;
}

// Test 2: Checks that blocking the primary bridge forces the algorithm to find a longer detour.
// Why it matters: Validates that SafeRoute routes traffic around hazards (7.0 km -> 14.0 km).
static bool testBlockedRoadDetour() {
    Graph g = buildSmallTestGraph();
    g.blockRoad("r2", "Flooded road");
    PathResult res = dijkstraShortestPath(g, "v1", "s1");
    if (!res.reachable) return false;
    if (!approxEqual(res.distance, 14.0)) return false;
    if (res.path != std::vector<std::string>{"v1", "h1", "j1", "s2", "s1"}) return false;
    return true;
}

// Test 3: Checks that total ingress block correctly marks a node as unreachable.
// Why it matters: Ensures responders are alerted when a community is completely isolated.
static bool testUnreachable() {
    Graph g = buildSmallTestGraph();
    g.blockRoad("r1", "Bridge damaged");
    g.blockRoad("r3", "Landslide debris");
    PathResult res = dijkstraShortestPath(g, "v1", "s1");
    if (res.reachable) return false;
    if (res.distance != UNREACHABLE) return false;
    if (!res.path.empty()) return false;
    return true;
}

// Test 4: Checks the edge case where source equals destination.
// Why it matters: Distance must be exactly 0 km and path must contain only the source node.
static bool testSourceEqualsDestination() {
    Graph g = buildSmallTestGraph();
    PathResult res = dijkstraShortestPath(g, "v1", "v1");
    if (!res.reachable) return false;
    if (!approxEqual(res.distance, 0.0)) return false;
    if (res.path != std::vector<std::string>{"v1"}) return false;
    return true;
}

// Test 5: Checks that hazard reason strings are stored on road block and cleared on restore.
// Why it matters: Verifies that responders see accurate operational reasons for closures.
static bool testReasonStorage() {
    Graph g = buildSmallTestGraph();
    g.blockRoad("r1", "Waterlogging");
    const Road& r = g.getRoad("r1");
    if (!r.blocked) return false;
    if (r.reason != "Waterlogging") return false;

    // Default reason fallback
    g.blockRoad("r2", "");
    if (g.getRoad("r2").reason.empty()) return false;

    // Restoration clears reason
    g.restoreRoad("r1");
    if (g.getRoad("r1").blocked) return false;
    if (!g.getRoad("r1").reason.empty()) return false;
    return true;
}

// Test 6: Checks that the same scenario seed produces the exact same road blockages.
// Why it matters: PRNG reproducibility is essential for repeatable simulation drills and grading.
static bool testScenarioSeedReproducibility() {
    Graph g;
    g.loadFromFile("data/network.json");

    auto sc1 = g.applyScenario("moderate", 2026);
    auto blocked1 = sc1.blockedRoads;

    auto sc2 = g.applyScenario("moderate", 2026);
    auto blocked2 = sc2.blockedRoads;

    if (sc1.blockedCount != sc2.blockedCount) return false;
    if (blocked1 != blocked2) return false;
    return true;
}

// Test 7: Checks that Light and Moderate disaster scenarios preserve shelter connectivity.
// Why it matters: Training scenarios must keep all communities connected unless Severe is chosen.
static bool testLightAndModerateShelterConnectivity() {
    Graph g;
    g.loadFromFile("data/network.json");

    g.applyScenario("light", 42);
    if (!g.checkAllVillagesHaveShelter()) return false;

    g.applyScenario("moderate", 2026);
    if (!g.checkAllVillagesHaveShelter()) return false;

    g.applyScenario("moderate", 999);
    if (!g.checkAllVillagesHaveShelter()) return false;

    return true;
}

// Test 8: Checks nearest relief services grouping and distance sorting.
// Why it matters: Proves closest hospital (h1: 2km), water (w1: 5km), and shelter (s1: 7km) match.
static bool testNearestServiceCorrectness() {
    Graph g = buildSmallTestGraph();
    auto services = g.getNearestServices("v1");

    if (services["shelter"].empty()) return false;
    if (services["shelter"][0].id != "s1" || !approxEqual(services["shelter"][0].distance, 7.0)) return false;

    if (services["hospital"].empty()) return false;
    if (services["hospital"][0].id != "h1" || !approxEqual(services["hospital"][0].distance, 2.0)) return false;

    if (services["water"].empty()) return false;
    if (services["water"][0].id != "w1" || !approxEqual(services["water"][0].distance, 5.0)) return false;

    return true;
}

// Test 9: Checks that multi-source Dijkstra matches the minimum of individual Dijkstra runs.
// Why it matters: Proves that the single-pass multi-source optimization is mathematically sound.
static bool testMultiSourceCoverageEquivalence() {
    Graph g;
    g.loadFromFile("data/network.json");

    auto coverage = g.computeCoverage();
    auto shelters = g.getShelters();
    std::vector<std::unordered_map<std::string, double>> shelterDists;
    for (const auto* s : shelters) {
        shelterDists.push_back(dijkstraAllDistances(g, s->id));
    }

    for (const auto& cv : coverage.villageCoverages) {
        if (!cv.covered) {
            for (const auto& dMap : shelterDists) {
                if (dMap.at(cv.villageId) != UNREACHABLE) return false;
            }
        } else {
            double minDist = UNREACHABLE;
            for (size_t i = 0; i < shelters.size(); i++) {
                double d = shelterDists[i].at(cv.villageId);
                if (d < minDist) {
                    minDist = d;
                }
            }
            if (!approxEqual(cv.distance, minDist)) return false;
        }
    }
    return true;
}

// Test 10: Checks that the stepper trace recorder produces results identical to standard Dijkstra.
// Why it matters: Proves the educational stepper displays the real routing path without bugs.
static bool testTraceParity() {
    Graph g;
    g.loadFromFile("data/network.json");

    PathResult standard = dijkstraShortestPath(g, "v1", "s2");
    TraceRecorder recorder;
    PathResult traced = dijkstraShortestPath(g, "v1", "s2", &recorder);

    if (standard.reachable != traced.reachable) return false;
    if (!approxEqual(standard.distance, traced.distance)) return false;
    if (standard.path != traced.path) return false;
    if (recorder.steps.empty()) return false;

    // Verify trace recorded the destination settlement event
    bool hasSettle = false;
    for (const auto& step : recorder.steps) {
        if (step.action == "settle_destination" && step.currentNode == "s2") {
            hasSettle = true;
            break;
        }
    }
    return hasSettle;
}

// Test 11: Checks that disrupt-route only selects roads that actually lie on the active path.
// Why it matters: Artificial disruptions must force a detour on the active route, not elsewhere.
static bool testDisruptRouteRoadCheck() {
    Graph g;
    g.loadFromFile("data/network.json");

    PathResult original = dijkstraShortestPath(g, "v1", "s2");
    std::set<std::string> pathNodes(original.path.begin(), original.path.end());

    auto disrupt = g.disruptRoute("v1", "s2", 12345);
    if (!disrupt.disrupted) return false;

    for (const auto& blockedId : disrupt.blockedRoadIds) {
        const Road& road = g.getRoad(blockedId);
        if (pathNodes.count(road.from) == 0 || pathNodes.count(road.to) == 0) {
            return false;
        }
    }
    return true;
}

// Test 12: Checks robust exception handling for invalid IDs and negative road distances.
// Why it matters: Ensures the engine throws clean, catchable errors rather than crashing.
static bool testErrorHandling() {
    Graph g = buildSmallTestGraph();

    // Unknown source node
    try {
        dijkstraShortestPath(g, "bad_src", "s1");
        return false;
    } catch (const std::invalid_argument&) {}

    // Unknown destination node
    try {
        dijkstraShortestPath(g, "v1", "bad_dst");
        return false;
    } catch (const std::invalid_argument&) {}

    // Unknown road ID
    try {
        g.blockRoad("bad_road");
        return false;
    } catch (const std::invalid_argument&) {}

    // Negative road distance rejected
    try {
        g.addRoad({"rx", "v1", "v2", -4.0, false, ""});
        return false;
    } catch (const std::invalid_argument&) {}

    return true;
}

// Test 13: Checks that reopening a road restores the original optimal shortest route.
// Why it matters: Verifies dynamic road restoration during disaster recovery operations.
static bool testRestoreRoad() {
    Graph g = buildSmallTestGraph();
    PathResult orig = dijkstraShortestPath(g, "v1", "s1");

    g.blockRoad("r2", "Flooded road");
    PathResult blocked = dijkstraShortestPath(g, "v1", "s1");
    if (approxEqual(orig.distance, blocked.distance)) return false;

    g.restoreRoad("r2");
    PathResult restored = dijkstraShortestPath(g, "v1", "s1");
    if (!approxEqual(orig.distance, restored.distance)) return false;
    if (orig.path != restored.path) return false;

    return true;
}

// Test 14: Validates parsing and node/road thresholds on the full network.json data file.
// Why it matters: Guarantees that the real production dataset loads cleanly with all facilities.
static bool testLoadNetworkJson() {
    Graph g;
    g.loadFromFile("data/network.json");
    if (g.nodeCount() < 38) return false;
    if (g.roadCount() < 70) return false;
    if (g.getShelters().empty()) return false;
    if (g.getNodesByType(NodeType::Hospital).empty()) return false;
    return true;
}

// ─── Tests 15-23: Knapsack Relief Truck Loading ──────────────────────────

// Test 15: Checks that 0/1 Knapsack DP matches the brute-force exact optimum on 50 random cases.
// Why it matters: Formally proves that solveKnapsackDP finds the absolute mathematical maximum.
static bool testKnapsackDPEqualsBruteForce() {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> nDist(3, 11);
    std::uniform_int_distribution<int> weightDist(1, 25);
    std::uniform_int_distribution<int> urgDist(5, 100);
    std::uniform_int_distribution<int> capDist(10, 80);

    for (int iter = 0; iter < 50; ++iter) {
        int n = nDist(rng);
        int cap = capDist(rng);
        std::vector<SupplyItem> items;
        for (int i = 0; i < n; ++i) {
            items.push_back({"Item_" + std::to_string(i), weightDist(rng), urgDist(rng)});
        }

        KnapsackResult dpRes = solveKnapsackDP(items, cap);
        KnapsackResult bfRes = solveKnapsackBruteForce(items, cap);

        // DP result must exactly match exhaustive brute-force search
        if (dpRes.totalUrgency != bfRes.totalUrgency) {
            std::cout << "Mismatch at iter " << iter << ": DP=" << dpRes.totalUrgency
                      << " BF=" << bfRes.totalUrgency << " cap=" << cap << std::endl;
            return false;
        }
        if (dpRes.totalWeightKg > cap) {
            return false;
        }
    }
    return true;
}

// Test 16: Verifies the 50 kg DP worked example (DP = 220 urgency, 50 kg, table 4x51).
// Why it matters: Confirms DP optimal choice and full table dimensions for pedagogical demonstration.
static bool testKnapsackDemoDP() {
    int cap = 50;
    std::vector<SupplyItem> items = {
        {"Medical kit", 10, 60},
        {"Blanket bundle", 20, 100},
        {"Water crate", 30, 120}
    };

    KnapsackResult dpRes = solveKnapsackDP(items, cap, true);

    // DP must pick Blankets + Water: urgency = 100 + 120 = 220, weight = 50 kg (100% capacity)
    if (dpRes.totalUrgency != 220) return false;
    if (dpRes.totalWeightKg != 50) return false;

    // Verify chosen items in DP
    if (dpRes.chosenItems.size() != 2) return false;
    if (dpRes.chosenItems[0].name != "Blanket bundle" || dpRes.chosenItems[1].name != "Water crate") return false;

    // Verify DP table dimensions: (3+1) rows by (50+1) columns
    if (dpRes.dpTable.size() != 4) return false;
    if (dpRes.dpTable[0].size() != 51) return false;

    // Bottom-right corner must hold the maximum urgency value 220
    if (dpRes.dpTable[3][50] != 220) return false;

    return true;
}

// Test 17: Checks edge case where truck capacity is 0 kg.
// Why it matters: Verifies that a zero-capacity vehicle correctly loads 0 items with 0 urgency.
static bool testKnapsackCapacityZero() {
    std::vector<SupplyItem> items = {
        {"Water crate", 12, 90},
        {"Food packet box", 10, 80}
    };

    KnapsackResult res = solveKnapsackDP(items, 0);
    if (!res.chosenItems.empty()) return false;
    if (res.totalWeightKg != 0) return false;
    if (res.totalUrgency != 0) return false;

    return true;
}

// Test 18: Checks that items heavier than total capacity are never loaded.
// Why it matters: Ensures overweight items are cleanly rejected without memory issues.
static bool testKnapsackItemsHeavierThanCapacityNeverChosen() {
    std::vector<SupplyItem> items = {
        {"Heavy Generator", 60, 200},
        {"Heavy Water Tank", 70, 250}
    };
    int cap = 50;

    KnapsackResult res = solveKnapsackDP(items, cap);
    if (!res.chosenItems.empty()) return false;
    if (res.totalWeightKg != 0) return false;
    if (res.totalUrgency != 0) return false;

    return true;
}

// Test 19: Checks that if all candidate items fit on the truck, all are chosen.
// Why it matters: Verifies optimal packing when capacity is sufficiently abundant.
static bool testKnapsackAllItemsFit() {
    std::vector<SupplyItem> items = {
        {"Med Kit", 8, 100},
        {"Water", 12, 90},
        {"Food", 10, 80}
    };
    int totalW = 8 + 12 + 10; // 30 kg
    int cap = 100;

    KnapsackResult res = solveKnapsackDP(items, cap);
    if (res.chosenItems.size() != items.size()) return false;
    if (res.totalWeightKg != totalW) return false;
    if (res.totalUrgency != (100 + 90 + 80)) return false;

    return true;
}

// Test 20: Checks that chosen cargo weight never exceeds truck capacity across 50 random runs.
// Why it matters: Confirms that physical payload safety constraints are strictly respected.
static bool testKnapsackChosenWeightNeverExceedsCapacity() {
    std::mt19937 rng(777);
    std::uniform_int_distribution<int> nDist(10, 50);
    std::uniform_int_distribution<int> weightDist(3, 40);
    std::uniform_int_distribution<int> urgDist(10, 150);
    std::uniform_int_distribution<int> capDist(40, 250);

    for (int iter = 0; iter < 50; ++iter) {
        int n = nDist(rng);
        int cap = capDist(rng);
        std::vector<SupplyItem> items;
        for (int i = 0; i < n; ++i) {
            items.push_back({"Item_" + std::to_string(i), weightDist(rng), urgDist(rng)});
        }

        KnapsackResult res = solveKnapsackDP(items, cap);
        if (res.totalWeightKg > cap) return false;

        // Verify that sum of weights of chosen items matches totalWeightKg
        int sumW = 0;
        int sumU = 0;
        for (const auto& item : res.chosenItems) {
            sumW += item.weightKg;
            sumU += item.urgency;
        }
        if (sumW != res.totalWeightKg) return false;
        if (sumU != res.totalUrgency) return false;
    }
    return true;
}

// Test 21: Checks that invalid inputs (negative weights, capacity > 5000, etc.) throw exceptions.
// Why it matters: Validates that error conditions are caught early before invalid memory access.
static bool testKnapsackInvalidInputs() {
    std::vector<SupplyItem> validItems = {{"Item 1", 10, 50}};

    // Negative capacity
    try {
        solveKnapsackDP(validItems, -5);
        return false;
    } catch (const std::invalid_argument&) {}

    // Capacity > 5000 kg
    try {
        solveKnapsackDP(validItems, 5001);
        return false;
    } catch (const std::invalid_argument&) {}

    // Non-positive item weight (0 kg)
    try {
        std::vector<SupplyItem> badWeight = {{"Bad item", 0, 50}};
        solveKnapsackDP(badWeight, 100);
        return false;
    } catch (const std::invalid_argument&) {}

    // Negative item weight (-2 kg)
    try {
        std::vector<SupplyItem> badWeight2 = {{"Bad item", -2, 50}};
        solveKnapsackDP(badWeight2, 100);
        return false;
    } catch (const std::invalid_argument&) {}

    // Negative urgency
    try {
        std::vector<SupplyItem> badUrgency = {{"Bad urgency", 10, -1}};
        solveKnapsackDP(badUrgency, 100);
        return false;
    } catch (const std::invalid_argument&) {}

    // Too many items (>200)
    try {
        std::vector<SupplyItem> tooMany(201, {"Item", 5, 10});
        solveKnapsackDP(tooMany, 100);
        return false;
    } catch (const std::invalid_argument&) {}

    return true;
}

// Test 22: Checks that an unreachable destination returns reachable: false without crashing.
// Why it matters: Proves that relief load planning handles severed road connections safely.
static bool testKnapsackUnreachableVillageRoute() {
    Graph g = buildSmallTestGraph();
    // Block r1 and r3 so v1 is isolated
    g.blockRoad("r1", "Flooded road");
    g.blockRoad("r3", "Landslide debris");

    // Try routing from shelter/depot s1 to isolated village v1
    PathResult route = dijkstraShortestPath(g, "s1", "v1");
    if (route.reachable) return false;
    if (route.distance != UNREACHABLE) return false;

    return true;
}

// Test 23: Checks that manifest rows aggregate correctly and totals match reported values.
// Why it matters: Guarantees that grouped cargo item rows never deviate from total weight/urgency
//                 and that capacity limits are strictly obeyed across multiple village scenarios.
static bool testManifestBreakdownIntegrity() {
    std::ifstream needFile("data/village_needs.json");
    if (!needFile.is_open()) return false;
    nlohmann::json needsJson = nlohmann::json::parse(needFile);

    std::vector<std::string> sampleVillages = {"v1", "v2", "v3", "v5", "v10"};
    std::vector<int> sampleCapacities = {50, 100, 200, 350, 500};

    for (const auto& vid : sampleVillages) {
        if (!needsJson["villages"].contains(vid)) continue;
        const auto& vProfile = needsJson["villages"][vid];

        std::vector<SupplyItem> items;
        for (const auto& itemDemand : vProfile["needs"]) {
            std::string name = itemDemand["name"].get<std::string>();
            int w = itemDemand["weightKg"].get<int>();
            int urg = itemDemand["urgency"].get<int>();
            int units = itemDemand["unitsNeeded"].get<int>();
            for (int u = 0; u < units; ++u) {
                items.push_back({name, w, urg});
            }
        }

        for (int cap : sampleCapacities) {
            KnapsackResult dp = solveKnapsackDP(items, cap);

            auto verifyManifest = [cap](const KnapsackResult& kr) -> bool {
                std::unordered_map<std::string, int> counts;
                std::unordered_map<std::string, int> unitWeights;
                std::unordered_map<std::string, int> unitUrgencies;

                for (const auto& item : kr.chosenItems) {
                    counts[item.name]++;
                    unitWeights[item.name] = item.weightKg;
                    unitUrgencies[item.name] = item.urgency;
                }

                int sumRowWeight = 0;
                int sumRowUrgency = 0;
                int sumRowCount = 0;

                for (const auto& [name, cnt] : counts) {
                    int rowWt = cnt * unitWeights[name];
                    int rowUrg = cnt * unitUrgencies[name];
                    sumRowCount += cnt;
                    sumRowWeight += rowWt;
                    sumRowUrgency += rowUrg;
                }

                if (sumRowCount != static_cast<int>(kr.chosenItems.size())) return false;
                if (sumRowWeight != kr.totalWeightKg) return false;
                if (sumRowWeight > cap) return false;
                if (sumRowUrgency != kr.totalUrgency) return false;

                return true;
            };

            if (!verifyManifest(dp)) return false;
        }
    }
    return true;
}

// ─── Main ────────────────────────────────────────────────────────────────

// Test suite runner: runs all 23 unit tests and reports aggregated pass/fail statistics.
// Output: 0 if all tests pass; 1 if any test fails.
int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  SafeRoute — Automated Test Suite" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Routing & Network Tests (14)
    runTest("Normal shortest path (v1 -> s1 = 7.0 km)", testNormalRoute);
    runTest("Road block forces longer detour", testBlockedRoadDetour);
    runTest("Total ingress block produces unreachable", testUnreachable);
    runTest("Source equals destination -> distance 0", testSourceEqualsDestination);
    runTest("Obstruction reasons are stored and returned", testReasonStorage);
    runTest("Same seed gives identical scenario", testScenarioSeedReproducibility);
    runTest("Light & Moderate scenarios preserve village-shelter connectivity", testLightAndModerateShelterConnectivity);
    runTest("Nearest services grouping per facility type", testNearestServiceCorrectness);
    runTest("Multi-source coverage matches min of per-shelter Dijkstra", testMultiSourceCoverageEquivalence);
    runTest("Dijkstra trace parity with standard routing", testTraceParity);
    runTest("Disrupt-route only blocks roads on active path", testDisruptRouteRoadCheck);
    runTest("Exception handling on invalid inputs and negative distances", testErrorHandling);
    runTest("Restoring a road restores original optimal route", testRestoreRoad);
    runTest("Load full network.json (>=38 nodes, >=70 roads)", testLoadNetworkJson);

    // Relief Truck Loading Knapsack Tests (9)
    runTest("Knapsack DP equals brute-force on 50 random small instances (n<=11)", testKnapsackDPEqualsBruteForce);
    runTest("Knapsack demo verification (DP = 220 urgency, 50 kg, table 4x51)", testKnapsackDemoDP);
    runTest("Knapsack capacity 0 returns empty load", testKnapsackCapacityZero);
    runTest("Knapsack items heavier than capacity are never chosen", testKnapsackItemsHeavierThanCapacityNeverChosen);
    runTest("Knapsack all items fit means all are taken", testKnapsackAllItemsFit);
    runTest("Knapsack chosen weight never exceeds truck capacity (50 runs)", testKnapsackChosenWeightNeverExceedsCapacity);
    runTest("Knapsack invalid inputs return clean errors", testKnapsackInvalidInputs);
    runTest("Knapsack route to unreachable village returns reachable:false", testKnapsackUnreachableVillageRoute);
    runTest("Manifest breakdown rows sum to total weight & urgency across plans", testManifestBreakdownIntegrity);

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Results: " << passedTests << " passed, "
              << failedTests << " failed, "
              << totalTests << " total" << std::endl;
    std::cout << "========================================" << std::endl;

    return (failedTests == 0) ? 0 : 1;
}

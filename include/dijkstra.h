// dijkstra.h — Dijkstra shortest-path algorithm and stepper trace recorder for SafeRoute
//
// What this file does:
//   Declares shortest-path algorithms used across the SafeRoute evacuation engine:
//   1. dijkstraShortestPath: Single-pair shortest path between two specific nodes.
//      Includes an optional trace recorder for step-by-step visual demonstration.
//   2. dijkstraAllDistances: Finds shortest distances from one source to every node.
//   3. dijkstraMultiSource: Finds the nearest evacuation shelter for all villages
//      in one single pass by expanding outward from all open shelters at once.
//
// What is inside:
//   - UNREACHABLE: numerical constant denoting infinite distance.
//   - PathResult: struct holding distance, node waypoint path, and reachability flag.
//   - TraceStep & TraceRecorder: structs capturing heap snapshots for the visualizer.
//   - MultiSourceDistance: struct holding distance and nearest shelter origin ID.
//   - Function declarations for the three algorithm variations.
//
// Other files that use this header:
//   - src/dijkstra.cpp: contains the complete implementations of these functions.
//   - src/graph.cpp: calls dijkstraAllDistances and dijkstraMultiSource for services.
//   - src/server.cpp: calls these functions to serve /api/compare and /api/trace.
//   - src/main.cpp: calls dijkstraShortestPath in interactive CLI mode.
//   - tests/test_main.cpp: validates routing correctness, detours, and trace parity.

// Include guard: ensures the compiler reads this file only once per compilation unit.
#ifndef SAFEROUTE_DIJKSTRA_H
#define SAFEROUTE_DIJKSTRA_H

#include "graph.h"

// C++ feature note:
// <limits> provides std::numeric_limits to represent infinity (double::max).
#include <vector>
#include <string>
#include <unordered_map>
#include <limits>
#include <utility>

// Large numerical sentinel denoting an unreachable node (represents infinity).
// If a village is cut off by flood water, its settled distance remains UNREACHABLE.
constexpr double UNREACHABLE = std::numeric_limits<double>::max();

// ─── PathResult ──────────────────────────────────────────────────────────
// Holds the outcome of a single-pair shortest path query.
struct PathResult {
    double distance;                // Total travel distance in km (or UNREACHABLE if cut off)
    std::vector<std::string> path;  // Ordered list of node IDs from source to destination
    bool reachable;                 // True if an open-road corridor exists; false if cut off
};

// ─── TraceStep & TraceRecorder ───────────────────────────────────────────
// Captures internal algorithmic states for the step-by-step visualizer.
// viva note: This allows a student or reviewer to inspect each heap pop,
// edge relaxation, and distance table update exactly as the algorithm runs.
struct TraceStep {
    int stepIndex;                  // 1-based sequential step counter
    // Action type: "pop", "relax", "settle_destination", "skip_stale"
    std::string action;
    std::string currentNode;        // Node ID currently popped from the min-heap
    double currentDistance;         // Settled distance in km for the popped node
    std::string targetNode;         // Neighbor node ID being checked during relaxation
    double edgeDistance;            // Length in km of the road connecting current to target
    double oldDistance;             // Target's tentative distance in km before relaxation
    double newDistance;             // Proposed new distance in km via currentNode
    bool relaxed;                   // True if tentative distance improved and was updated
    // Snapshot of active items in the min-heap priority queue: pairs of (nodeId, tentativeDist)
    std::vector<std::pair<std::string, double>> pqSnapshot;
    // Snapshot of the entire distance table at this step: maps nodeId to current distance
    std::unordered_map<std::string, double> distancesSnapshot;
};

// Collector passed into dijkstraShortestPath to record execution history.
struct TraceRecorder {
    std::vector<TraceStep> steps;   // List of recorded execution steps
    int maxSteps = 500;             // Upper safety bound to prevent excessive memory/JSON size
};

// ─── MultiSourceDistance ─────────────────────────────────────────────────
// Result entry for multi-source shelter coverage.
struct MultiSourceDistance {
    double distance;                // Shortest distance in km to the nearest shelter
    std::string sourceOrigin;       // ID of the specific shelter that reached this node first
};

// ─── Algorithm Declarations ──────────────────────────────────────────────

// Computes the shortest path from source to destination using Dijkstra's algorithm.
//
// How it works:
//   1. Maintains a min-heap priority queue of candidate nodes ordered by tentative distance.
//   2. Repeatedly pops the closest unvisited node.
//   3. Relaxes all outgoing open road edges to find shorter paths to neighboring nodes.
//   4. Stops early the instant destination is popped (greedy choice guarantees optimality).
//   5. Rebuilds the final path by walking backwards from destination via parent pointers.
//   6. If recorder is provided, snapshots heap and distance states at each step.
//
// Input:  const Graph& graph, source node ID, destination node ID, optional recorder pointer.
// Output: PathResult containing distance, path vector, and reachable flag.
// Errors: Throws std::invalid_argument if source or destination does not exist in graph.
// Time:   O((V + E) log V) with binary min-heap; stops even faster when destination is reached.
// Space:  O(V + E) for priority queue, distance map, visited set, and parent pointers.
PathResult dijkstraShortestPath(const Graph& graph,
                                const std::string& source,
                                const std::string& destination,
                                TraceRecorder* recorder = nullptr);

// Computes shortest distances from a single source node to every other node in the graph.
// Does not stop early; runs until the min-heap is completely empty.
//
// Input:  const Graph& graph, source node ID.
// Output: Hash map mapping every node ID in the graph to its shortest distance in km.
// Errors: Throws std::invalid_argument if source node does not exist.
// Time:   O((V + E) log V).
// Space:  O(V) for distance table and priority queue.
std::unordered_map<std::string, double> dijkstraAllDistances(
    const Graph& graph,
    const std::string& source);

// Multi-source Dijkstra: finds which evacuation shelter reaches each village fastest.
//
// How it works:
//   Instead of starting with one node at distance 0, ALL open shelters are inserted
//   into the priority queue simultaneously with distance 0.0 and their own ID as origin.
//   The search wavefront expands outward like ripples across the open road network.
//   The first shelter wavefront to reach any village is mathematically guaranteed to be
//   its closest evacuation shelter.
//
// Input:  const Graph& graph, vector of shelter node IDs.
// Output: Hash map mapping every node ID to a MultiSourceDistance (distance and origin ID).
// Time:   O((V + E) log V) — solves territory-wide shelter assignment in a single search pass!
// Space:  O(V) for the multi-source distance table and priority queue.
std::unordered_map<std::string, MultiSourceDistance> dijkstraMultiSource(
    const Graph& graph,
    const std::vector<std::string>& sources);

#endif // SAFEROUTE_DIJKSTRA_H

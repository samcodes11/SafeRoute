// This header defines Dijkstra results, trace data, and function declarations.

// Prevent this header from being included more than once.
#ifndef SAFEROUTE_DIJKSTRA_H
#define SAFEROUTE_DIJKSTRA_H

#include "graph.h"

// These headers provide the result containers and infinity value.
#include <vector>
#include <string>
#include <unordered_map>
#include <limits>
#include <utility>

// Use the largest double value to mark an unreachable node.
constexpr double UNREACHABLE = std::numeric_limits<double>::max();

// Result of a route search between two nodes.
struct PathResult {
    double distance;                // Total travel distance in km (or UNREACHABLE if cut off)
    std::vector<std::string> path;  // Ordered list of node IDs from source to destination
    bool reachable;                 // True if an open-road corridor exists; false if cut off
};

// Data recorded for one visualizer step.
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

// Collects trace steps during route search.
struct TraceRecorder {
    std::vector<TraceStep> steps;   // List of recorded execution steps
    int maxSteps = 500;             // Upper safety bound to prevent excessive memory/JSON size
};

// Distance and source shelter for one node.
struct MultiSourceDistance {
    double distance;                // Shortest distance in km to the nearest shelter
    std::string sourceOrigin;       // ID of the specific shelter that reached this node first
};

// Find a shortest route; optionally record each algorithm step.
PathResult dijkstraShortestPath(const Graph& graph,
                                const std::string& source,
                                const std::string& destination,
                                TraceRecorder* recorder = nullptr);

// Find shortest distances from one node to all nodes.
std::unordered_map<std::string, double> dijkstraAllDistances(
    const Graph& graph,
    const std::string& source);

// Find the closest source for each node, starting from all sources together.
std::unordered_map<std::string, MultiSourceDistance> dijkstraMultiSource(
    const Graph& graph,
    const std::vector<std::string>& sources);

#endif // SAFEROUTE_DIJKSTRA_H

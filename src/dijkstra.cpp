// dijkstra.cpp — Implementation of Dijkstra algorithms and stepper trace recorder
//
// What this file does:
//   Implements the shortest-path algorithms declared in include/dijkstra.h.
//   It provides:
//     1. Single-pair shortest path with early exit and path reconstruction.
//     2. Single-source distance map to all network nodes.
//     3. Multi-source Dijkstra expanding from all shelters simultaneously.
//   It also records step-by-step trace events directly from the main algorithm loop
//   so the web visualizer can demonstrate the exact execution path without code divergence.
//
// What is inside:
//   - PqElement: type alias for (distance, nodeId) heap elements.
//   - snapshotPq: helper function to serialize priority queue contents for trace steps.
//   - dijkstraShortestPath: min-heap single-pair shortest path with early stopping.
//   - dijkstraAllDistances: full single-source run exploring all reachable nodes.
//   - dijkstraMultiSource: multi-source wave expansion assigning villages to shelters.
//
// Other files that use this file:
//   - Linked directly into the saferoute server executable and test_runner.

#include "../include/dijkstra.h"

// C++ feature note:
// <queue> provides std::priority_queue (heap structure).
// <algorithm> provides std::reverse to reverse the reconstructed path vector.
// <set> provides std::set to track settled nodes and eliminate stale entries.
// <stdexcept> provides std::invalid_argument for input error reporting.
#include <queue>
#include <algorithm>
#include <set>
#include <stdexcept>

// ─── Priority Queue Element for Single-Source Dijkstra ───────────────────
// C++ feature: 'using' defines a convenient type alias.
// A std::pair holds:
//   .first  = double (tentative distance from source in km)
//   .second = std::string (node ID string)
using PqElement = std::pair<double, std::string>;

// Helper to copy and snapshot priority queue contents for the step-by-step trace.
// Takes pqCopy by value (creating an independent copy) and pops elements in order.
// Input:  Copy of active priority queue.
// Output: Vector of pairs (nodeId, tentativeDistance) sorted by distance ascending.
static std::vector<std::pair<std::string, double>> snapshotPq(
    std::priority_queue<PqElement, std::vector<PqElement>, std::greater<PqElement>> pqCopy) {
    std::vector<std::pair<std::string, double>> list;
    while (!pqCopy.empty()) {
        auto [d, id] = pqCopy.top();
        pqCopy.pop();
        list.push_back({id, d});
    }
    return list;
}

// ─── dijkstraShortestPath ─────────────────────────────────────────────────

// Finds the shortest path between source and destination on currently open roads.
//
// Algorithm explanation (for viva):
//   1. Distance Map (dist): Stores the best-known distance from source to each node.
//      Initialized to UNREACHABLE (infinity) for all nodes, with dist[source] = 0.
//   2. Min-Heap Priority Queue: By default, std::priority_queue is a max-heap.
//      Passing std::greater<PqElement> makes it a MIN-heap, so top() always returns
//      the unvisited node with the smallest tentative distance in O(log V) time.
//   3. Stale Entry Skipping: Standard binary heaps lack a decrease-key operation.
//      When edge relaxation finds a shorter path, a new entry is pushed to the heap.
//      When an older, longer entry pops later, visited.count(currentNode) detects that
//      the node was already settled, so we skip it in O(1) time.
//   4. Early Exit: As soon as currentNode == destination is popped, we stop immediately.
//      Because all edge distances are positive and the heap always pops the minimum,
//      no remaining path can ever reach the destination with a shorter distance.
//   5. Relaxation Step: For each open neighbor v of u with road length w:
//      if dist[u] + w < dist[v], update dist[v] = dist[u] + w, record parent[v] = u,
//      and push {dist[v], v} into the min-heap.
//   6. Path Reconstruction: Walks backwards from destination to source using parent[],
//      then reverses the vector to give the corridor in forward order [source -> ... -> dest].
//
// Input:  const Graph& graph, source ID, destination ID, optional recorder pointer.
// Output: PathResult struct containing total distance, path vector, and reachable flag.
// Errors: Throws std::invalid_argument if source or destination does not exist.
// Time:   O((V + E) log V); stops even earlier when destination is reached.
// Space:  O(V + E) for priority queue, visited set, distance table, and parent pointers.
PathResult dijkstraShortestPath(const Graph& graph,
                                const std::string& source,
                                const std::string& destination,
                                TraceRecorder* recorder) {
    // Validate that both source and destination exist in the graph
    if (!graph.hasNode(source)) {
        throw std::invalid_argument("Source node does not exist: " + source);
    }
    if (!graph.hasNode(destination)) {
        throw std::invalid_argument("Destination node does not exist: " + destination);
    }

    // Trivial base case: traveler is already at the destination (distance is 0 km)
    if (source == destination) {
        if (recorder) {
            TraceStep step;
            step.stepIndex = 1;
            step.action = "settle_destination";
            step.currentNode = source;
            step.currentDistance = 0.0;
            step.targetNode = source;
            step.edgeDistance = 0.0;
            step.oldDistance = 0.0;
            step.newDistance = 0.0;
            step.relaxed = false;
            step.pqSnapshot = {{source, 0.0}};
            step.distancesSnapshot = {{source, 0.0}};
            recorder->steps.push_back(step);
        }
        return {0.0, {source}, true};
    }

    // Step 1: Initialize distance table to infinity (UNREACHABLE) and source to 0.0
    std::unordered_map<std::string, double> dist;
    for (const auto& [id, node] : graph.getNodes()) {
        dist[id] = UNREACHABLE;
    }
    dist[source] = 0.0;

    // Step 2: Parent pointer map to reconstruct the final route backwards
    std::unordered_map<std::string, std::string> parent;

    // Step 3: Set of finalized nodes whose shortest path is fully settled
    std::set<std::string> visited;

    // Step 4: Min-heap priority queue storing {tentativeDistance, nodeId}
    // C++ feature: std::greater comparator inverts the heap to pop smallest distance first
    std::priority_queue<PqElement, std::vector<PqElement>, std::greater<PqElement>> pq;
    pq.push({0.0, source});

    int stepCounter = 0;

    // Step 5: Main Dijkstra settlement loop
    while (!pq.empty()) {
        auto [currentDist, currentNode] = pq.top();
        pq.pop();

        // Stale entry check: ignore if an earlier shorter path already settled this node
        if (visited.count(currentNode)) {
            if (recorder && static_cast<int>(recorder->steps.size()) < recorder->maxSteps) {
                TraceStep step;
                step.stepIndex = ++stepCounter;
                step.action = "skip_stale";
                step.currentNode = currentNode;
                step.currentDistance = currentDist;
                step.targetNode = "";
                step.edgeDistance = 0.0;
                step.oldDistance = dist[currentNode];
                step.newDistance = currentDist;
                step.relaxed = false;
                step.pqSnapshot = snapshotPq(pq);
                step.distancesSnapshot = dist;
                recorder->steps.push_back(step);
            }
            continue;
        }

        // Early stopping check: if destination popped, its shortest path is guaranteed optimal
        if (currentNode == destination) {
            visited.insert(currentNode);
            if (recorder && static_cast<int>(recorder->steps.size()) < recorder->maxSteps) {
                TraceStep step;
                step.stepIndex = ++stepCounter;
                step.action = "settle_destination";
                step.currentNode = currentNode;
                step.currentDistance = currentDist;
                step.targetNode = destination;
                step.edgeDistance = 0.0;
                step.oldDistance = currentDist;
                step.newDistance = currentDist;
                step.relaxed = false;
                step.pqSnapshot = snapshotPq(pq);
                step.distancesSnapshot = dist;
                recorder->steps.push_back(step);
            }
            break;
        }

        visited.insert(currentNode);

        // Record pop step for visualizer trace
        if (recorder && static_cast<int>(recorder->steps.size()) < recorder->maxSteps) {
            TraceStep step;
            step.stepIndex = ++stepCounter;
            step.action = "pop";
            step.currentNode = currentNode;
            step.currentDistance = currentDist;
            step.targetNode = "";
            step.edgeDistance = 0.0;
            step.oldDistance = currentDist;
            step.newDistance = currentDist;
            step.relaxed = false;
            step.pqSnapshot = snapshotPq(pq);
            step.distancesSnapshot = dist;
            recorder->steps.push_back(step);
        }

        // Step 6: Relax open incident road edges
        for (const auto& edge : graph.getNeighbours(currentNode)) {
            double oldDist = dist[edge.neighbour];
            double newDist = currentDist + edge.distance;
            bool didRelax = false;

            // Relaxation condition: is path through currentNode shorter than known distance?
            if (newDist < oldDist) {
                dist[edge.neighbour] = newDist;
                parent[edge.neighbour] = currentNode;
                pq.push({newDist, edge.neighbour});
                didRelax = true;
            }

            // Record edge relaxation step for visualizer trace
            if (recorder && static_cast<int>(recorder->steps.size()) < recorder->maxSteps) {
                TraceStep step;
                step.stepIndex = ++stepCounter;
                step.action = "relax";
                step.currentNode = currentNode;
                step.currentDistance = currentDist;
                step.targetNode = edge.neighbour;
                step.edgeDistance = edge.distance;
                step.oldDistance = oldDist;
                step.newDistance = newDist;
                step.relaxed = didRelax;
                step.pqSnapshot = snapshotPq(pq);
                step.distancesSnapshot = dist;
                recorder->steps.push_back(step);
            }
        }
    }

    // Step 7: Check if destination was reached or is cut off by flood obstructions
    if (dist[destination] == UNREACHABLE) {
        return {UNREACHABLE, {}, false};
    }

    // Step 8: Reconstruct path from parent pointers by walking backwards from destination
    std::vector<std::string> path;
    std::string curr = destination;
    while (curr != source) {
        path.push_back(curr);
        auto it = parent.find(curr);
        if (it == parent.end()) {
            return {UNREACHABLE, {}, false};
        }
        curr = it->second;
    }
    path.push_back(source);

    // Reverse path so it runs in forward order: source -> waypoints -> destination
    std::reverse(path.begin(), path.end());

    return {dist[destination], path, true};
}

// ─── dijkstraAllDistances ─────────────────────────────────────────────────

// Computes shortest distances from a single source node to every node in the graph.
// Used by nearest services search and shelter ranking.
//
// Input:  const Graph& graph, source node ID string.
// Output: Map of nodeId -> shortest distance in km (or UNREACHABLE if disconnected).
// Errors: Throws std::invalid_argument if source node does not exist.
// Time:   O((V + E) log V).
// Space:  O(V) for distance table and priority queue.
std::unordered_map<std::string, double> dijkstraAllDistances(
    const Graph& graph,
    const std::string& source) {
    if (!graph.hasNode(source)) {
        throw std::invalid_argument("Source node does not exist: " + source);
    }

    // Initialize all distances to UNREACHABLE and source to 0.0
    std::unordered_map<std::string, double> dist;
    for (const auto& [id, node] : graph.getNodes()) {
        dist[id] = UNREACHABLE;
    }
    dist[source] = 0.0;

    std::set<std::string> visited;
    std::priority_queue<PqElement, std::vector<PqElement>, std::greater<PqElement>> pq;
    pq.push({0.0, source});

    // Run Dijkstra to exhaustion (explore all reachable components)
    while (!pq.empty()) {
        auto [currentDist, currentNode] = pq.top();
        pq.pop();

        // Skip stale heap entries
        if (visited.count(currentNode)) {
            continue;
        }
        visited.insert(currentNode);

        // Relax open edges
        for (const auto& edge : graph.getNeighbours(currentNode)) {
            double newDist = currentDist + edge.distance;
            if (newDist < dist[edge.neighbour]) {
                dist[edge.neighbour] = newDist;
                pq.push({newDist, edge.neighbour});
            }
        }
    }

    return dist;
}

// ─── dijkstraMultiSource ──────────────────────────────────────────────────

// Solves territory-wide shelter coverage in a single search pass.
//
// How it works:
//   1. All open shelters are seeded into the min-heap at distance 0 with their own ID as origin.
//   2. The search expands simultaneously across the network like multiple circular wave fronts.
//   3. Whichever shelter wave front reaches a village first settles that village's distance
//      and assigns the closest shelter ID.
//
// Input:  const Graph& graph, vector of source shelter node IDs.
// Output: Map of nodeId -> MultiSourceDistance {distance, sourceOrigin}.
// Time:   O((V + E) log V).
// Space:  O(V) for result map and min-heap.
std::unordered_map<std::string, MultiSourceDistance> dijkstraMultiSource(
    const Graph& graph,
    const std::vector<std::string>& sources) {

    std::unordered_map<std::string, MultiSourceDistance> result;
    for (const auto& [id, node] : graph.getNodes()) {
        result[id] = {UNREACHABLE, ""};
    }

    if (sources.empty()) {
        return result;
    }

    // Custom priority queue element storing: (distance, node, originShelter)
    struct MultiItem {
        double dist;
        std::string node;
        std::string origin;

        // Inverted operator> for min-heap comparison
        bool operator>(const MultiItem& other) const {
            return dist > other.dist;
        }
    };

    std::priority_queue<MultiItem, std::vector<MultiItem>, std::greater<MultiItem>> pq;

    // Push all starting shelters simultaneously at distance 0
    for (const auto& src : sources) {
        if (graph.hasNode(src)) {
            result[src] = {0.0, src};
            pq.push({0.0, src, src});
        }
    }

    std::set<std::string> visited;

    // Multi-source Dijkstra loop
    while (!pq.empty()) {
        auto top = pq.top();
        pq.pop();

        // Skip stale entries
        if (visited.count(top.node)) {
            continue;
        }
        visited.insert(top.node);

        // Relax open edges carrying the origin shelter tag forward
        for (const auto& edge : graph.getNeighbours(top.node)) {
            double newDist = top.dist + edge.distance;
            if (newDist < result[edge.neighbour].distance) {
                result[edge.neighbour] = {newDist, top.origin};
                pq.push({newDist, edge.neighbour, top.origin});
            }
        }
    }

    return result;
}

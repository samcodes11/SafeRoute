// This file contains the Dijkstra routing algorithms.

#include "../include/dijkstra.h"

// These headers provide the heap, path reversal, visited set, and input errors.
#include <queue>
#include <algorithm>
#include <set>
#include <stdexcept>

// Heap entries store a distance and its node ID.
using PqElement = std::pair<double, std::string>;

// Copy the heap into a list for one visualizer step.
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

// Find the shortest route, starting at source and stopping at destination.
PathResult dijkstraShortestPath(const Graph& graph,
                                const std::string& source,
                                const std::string& destination,
                                TraceRecorder* recorder) {
    // Reject IDs that are not in the graph.
    if (!graph.hasNode(source)) {
        throw std::invalid_argument("Source node does not exist: " + source);
    }
    if (!graph.hasNode(destination)) {
        throw std::invalid_argument("Destination node does not exist: " + destination);
    }

    // A route from a node to itself has no distance.
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

    // Start with every distance unknown, except the source at zero.
    std::unordered_map<std::string, double> dist;
    for (const auto& [id, node] : graph.getNodes()) {
        dist[id] = UNREACHABLE;
    }
    dist[source] = 0.0;

    // Remember the previous node for each best route.
    std::unordered_map<std::string, std::string> parent;

    // Track nodes whose shortest distance is final.
    std::set<std::string> visited;

    // The min-heap returns the closest known node first.
    std::priority_queue<PqElement, std::vector<PqElement>, std::greater<PqElement>> pq;
    pq.push({0.0, source});

    int stepCounter = 0;

    // Process nodes until the heap is empty or destination is settled.
    while (!pq.empty()) {
        auto [currentDist, currentNode] = pq.top();
        pq.pop();

        // Ignore old heap entries for nodes already settled.
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

        // The destination is final when it is the closest heap entry.
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

        // Save this heap removal for the visualizer.
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

        // Check each road leaving the current node.
        for (const auto& edge : graph.getNeighbours(currentNode)) {
            double oldDist = dist[edge.neighbour];
            double newDist = currentDist + edge.distance;
            bool didRelax = false;

            // Keep the new route only if it is shorter.
            if (newDist < oldDist) {
                dist[edge.neighbour] = newDist;
                parent[edge.neighbour] = currentNode;
                pq.push({newDist, edge.neighbour});
                didRelax = true;
            }

            // Save this road check for the visualizer.
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

    // No finite distance means no open route exists.
    if (dist[destination] == UNREACHABLE) {
        return {UNREACHABLE, {}, false};
    }

    // Follow parent links backward to rebuild the route.
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

    // Put the route in source-to-destination order.
    std::reverse(path.begin(), path.end());

    return {dist[destination], path, true};
}

// ─── dijkstraAllDistances ─────────────────────────────────────────────────

// Find shortest distances from one source to every reachable node.
std::unordered_map<std::string, double> dijkstraAllDistances(
    const Graph& graph,
    const std::string& source) {
    if (!graph.hasNode(source)) {
        throw std::invalid_argument("Source node does not exist: " + source);
    }

    // Set all distances to infinity, then start the source at zero.
    std::unordered_map<std::string, double> dist;
    for (const auto& [id, node] : graph.getNodes()) {
        dist[id] = UNREACHABLE;
    }
    dist[source] = 0.0;

    std::set<std::string> visited;
    std::priority_queue<PqElement, std::vector<PqElement>, std::greater<PqElement>> pq;
    pq.push({0.0, source});

    // Keep processing until every reachable node is settled.
    while (!pq.empty()) {
        auto [currentDist, currentNode] = pq.top();
        pq.pop();

        // Ignore nodes that were already settled.
        if (visited.count(currentNode)) {
            continue;
        }
        visited.insert(currentNode);

        // Update a neighbor when this route is shorter.
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

// Find the nearest source for every node in one search.
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

        // Order heap entries by distance.
        bool operator>(const MultiItem& other) const {
            return dist > other.dist;
        }
    };

    std::priority_queue<MultiItem, std::vector<MultiItem>, std::greater<MultiItem>> pq;

    // Start every valid shelter at distance zero.
    for (const auto& src : sources) {
        if (graph.hasNode(src)) {
            result[src] = {0.0, src};
            pq.push({0.0, src, src});
        }
    }

    std::set<std::string> visited;

    // Expand the closest route from any shelter.
    while (!pq.empty()) {
        auto top = pq.top();
        pq.pop();

        // Ignore nodes that have already been settled.
        if (visited.count(top.node)) {
            continue;
        }
        visited.insert(top.node);

        // Pass the shelter ID along whenever a route improves.
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

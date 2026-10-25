// Stores the road network and provides its supporting graph operations.

#include "../include/graph.h"
#include "../include/dijkstra.h"
#include "../third_party/json.hpp"

#include <fstream>
#include <stdexcept>
#include <random>
#include <algorithm>
#include <queue>
#include <cmath>
#include <set>

// Type alias: makes nlohmann::json easier to write throughout the file
using json = nlohmann::json;

// A fixed list of realistic hazards used when roads are blocked during simulations.
// C++ feature: 'static' means this variable is visible only within this .cpp file.
static const std::vector<std::string> STANDARD_REASONS = {
    "Fallen trees detected",
    "Flooded road",
    "Landslide debris",
    "Bridge damaged",
    "Waterlogging",
    "Power lines down",
    "Road cracked after tremor",
    "Vehicle accident"
};

// Returns a reference to the static list of standard hazard reasons.
const std::vector<std::string>& Graph::getStandardReasons() {
    return STANDARD_REASONS;
}


// Converts a NodeType enum value into a lowercase wire string for JSON output.
std::string nodeTypeToString(NodeType type) {
    switch (type) {
        case NodeType::Village:     return "village";
        case NodeType::Junction:    return "junction";
        case NodeType::Hospital:    return "hospital";
        case NodeType::Shelter:     return "shelter";
        case NodeType::Ngo:         return "ngo";
        case NodeType::Food:        return "food";
        case NodeType::Water:       return "water";
        case NodeType::FireStation: return "fire_station";
        case NodeType::Police:      return "police";
    }
    return "unknown";
}

// Parses a lowercase wire string from JSON into a strongly typed NodeType enum.
NodeType stringToNodeType(const std::string& str) {
    if (str == "village")      return NodeType::Village;
    if (str == "junction")     return NodeType::Junction;
    if (str == "hospital")     return NodeType::Hospital;
    if (str == "shelter")      return NodeType::Shelter;
    if (str == "ngo")          return NodeType::Ngo;
    if (str == "food")         return NodeType::Food;
    if (str == "water")        return NodeType::Water;
    if (str == "fire_station") return NodeType::FireStation;
    if (str == "police")       return NodeType::Police;
    throw std::invalid_argument("Unknown node type: " + str);
}

// Returns a user-friendly display title suitable for labels in the web UI.
std::string nodeTypeToDisplayName(NodeType type) {
    switch (type) {
        case NodeType::Village:     return "Village";
        case NodeType::Junction:    return "Transit Junction";
        case NodeType::Hospital:    return "Hospital & Medical Post";
        case NodeType::Shelter:     return "Evacuation Shelter";
        case NodeType::Ngo:         return "NGO Relief Camp";
        case NodeType::Food:        return "Food Distribution Centre";
        case NodeType::Water:       return "Drinking Water Point";
        case NodeType::FireStation: return "Fire & Rescue Station";
        case NodeType::Police:      return "Police Station";
    }
    return "Facility";
}


// Loads network nodes and roads from a JSON file into internal memory structures.
void Graph::loadFromFile(const std::string& filepath) {
    // Open file stream from disk
    std::ifstream inFile(filepath);
    if (!inFile.is_open()) {
        throw std::runtime_error("Cannot open network data file: " + filepath);
    }

    // Parse the JSON document
    json data;
    try {
        data = json::parse(inFile);
    } catch (const json::parse_error& e) {
        throw std::runtime_error("Failed to parse JSON file: " + filepath + " - " + e.what());
    }

    // Verify top-level structure contains the required arrays
    if (!data.contains("nodes") || !data["nodes"].is_array()) {
        throw std::runtime_error("JSON must contain a 'nodes' array.");
    }
    if (!data.contains("roads") || !data["roads"].is_array()) {
        throw std::runtime_error("JSON must contain a 'roads' array.");
    }

    // Clear any previous network data before loading the new one
    clear();

    // 1. Ingest Nodes: loop through each JSON node element
    for (const auto& jNode : data["nodes"]) {
        // Validate required node fields exist
        if (!jNode.contains("id") || !jNode.contains("name") ||
            !jNode.contains("type") || !jNode.contains("x") ||
            !jNode.contains("y")) {
            throw std::runtime_error("Each node must contain id, name, type, x, and y.");
        }

        // Construct Node struct and extract fields
        Node node;
        node.id   = jNode.at("id").get<std::string>();
        node.name = jNode.at("name").get<std::string>();
        node.type = stringToNodeType(jNode.at("type").get<std::string>());
        node.x    = jNode.at("x").get<double>();
        node.y    = jNode.at("y").get<double>();

        // Optional contact number with fallback default
        node.contact = jNode.contains("contact")
            ? jNode.at("contact").get<std::string>()
            : "00000 00000";

        // Optional operational note with fallback default
        node.note = jNode.contains("note")
            ? jNode.at("note").get<std::string>()
            : "Operational relief facility";

        // Prevent duplicate node IDs
        if (hasNode(node.id)) {
            throw std::runtime_error("Duplicate node id: " + node.id);
        }
        addNode(node);
    }

    // 2. Ingest Roads: loop through each JSON road element
    for (const auto& jRoad : data["roads"]) {
        // Validate required road fields exist
        if (!jRoad.contains("id") || !jRoad.contains("from") ||
            !jRoad.contains("to") || !jRoad.contains("distance")) {
            throw std::runtime_error("Each road must have id, from, to, and distance fields.");
        }

        // Construct Road struct and extract fields
        Road road;
        road.id       = jRoad.at("id").get<std::string>();
        road.from     = jRoad.at("from").get<std::string>();
        road.to       = jRoad.at("to").get<std::string>();
        road.distance = jRoad.at("distance").get<double>();
        road.blocked  = false;
        road.reason   = "";

        // Prevent duplicate road IDs
        if (hasRoad(road.id)) {
            throw std::runtime_error("Duplicate road id: " + road.id);
        }

        addRoad(road);
    }
}


// Finds a node by ID and returns a read-only reference.
const Node& Graph::getNode(const std::string& id) const {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        throw std::invalid_argument("Node not found: " + id);
    }
    return it->second;
}

// Checks if a node ID exists in the network.
bool Graph::hasNode(const std::string& id) const {
    return nodes_.find(id) != nodes_.end();
}

// Returns a read-only reference to the internal hash map of all nodes.
const std::unordered_map<std::string, Node>& Graph::getNodes() const {
    return nodes_;
}

// Collects pointers to all nodes categorized as evacuation shelters.
std::vector<const Node*> Graph::getShelters() const {
    return getNodesByType(NodeType::Shelter);
}

// Collects pointers to all nodes matching a specific NodeType.
std::vector<const Node*> Graph::getNodesByType(NodeType type) const {
    std::vector<const Node*> list;
    for (const auto& [id, node] : nodes_) {
        if (node.type == type) {
            list.push_back(&node);
        }
    }
    return list;
}


// Finds a road by ID and returns a read-only reference.
const Road& Graph::getRoad(const std::string& id) const {
    auto it = roads_.find(id);
    if (it == roads_.end()) {
        throw std::invalid_argument("Road not found: " + id);
    }
    return it->second;
}

// Checks if a road ID exists in the network.
bool Graph::hasRoad(const std::string& id) const {
    return roads_.find(id) != roads_.end();
}

// Returns a read-only reference to the internal hash map of all roads.
const std::unordered_map<std::string, Road>& Graph::getRoads() const {
    return roads_;
}


// Returns only the passable (non-blocked) edges incident to a given node.
// Algorithms like Dijkstra call this so blocked roads are never traversed.
std::vector<Graph::Edge> Graph::getNeighbours(const std::string& nodeId) const {
    if (!hasNode(nodeId)) {
        throw std::invalid_argument("Node not found: " + nodeId);
    }

    std::vector<Edge> openEdges;
    auto it = adjList_.find(nodeId);
    if (it == adjList_.end()) {
        return openEdges;
    }

    // Filter edges: check each road's blocked status and keep only open ones
    for (const auto& edge : it->second) {
        const Road& road = roads_.at(edge.roadId);
        if (!road.blocked) {
            openEdges.push_back(edge);
        }
    }
    return openEdges;
}


// Marks a road as blocked and records the reason for the closure.
void Graph::blockRoad(const std::string& roadId, const std::string& reason) {
    auto it = roads_.find(roadId);
    if (it == roads_.end()) {
        throw std::invalid_argument("Road not found: " + roadId);
    }
    it->second.blocked = true;
    it->second.reason = reason.empty() ? "Fallen trees detected" : reason;
}

// Reopens a blocked road and clears its reason string.
void Graph::restoreRoad(const std::string& roadId) {
    auto it = roads_.find(roadId);
    if (it == roads_.end()) {
        throw std::invalid_argument("Road not found: " + roadId);
    }
    it->second.blocked = false;
    it->second.reason = "";
}

// Reopens all roads across the entire graph and clears all blockage reasons.
void Graph::resetAllRoads() {
    for (auto& [id, road] : roads_) {
        road.blocked = false;
        road.reason = "";
    }
}


// Simulates a disaster scenario by blocking a fraction of roads based on severity.
ScenarioResult Graph::applyScenario(const std::string& severity, int seed) {
    resetAllRoads();

    // Determine target percentage of blocked roads
    double pct = 0.15; // default moderate
    if (severity == "light") {
        pct = 0.08;
    } else if (severity == "severe") {
        pct = 0.25;
    }

    int totalRoads = static_cast<int>(roads_.size());
    int targetBlocks = std::max(1, static_cast<int>(std::round(totalRoads * pct)));

    // Collect all road IDs in deterministic sorted order
    std::vector<std::string> roadIds;
    for (const auto& [id, r] : roads_) {
        roadIds.push_back(id);
    }
    std::sort(roadIds.begin(), roadIds.end());

    int attempt = 0;
    const int maxAttempts = 200;
    std::vector<std::pair<std::string, std::string>> finalBlocked;

    // Retry loop: ensures connectivity requirements are met
    while (attempt < maxAttempts) {
        attempt++;
        resetAllRoads();

        // C++ feature: std::mt19937 is a high-quality Mersenne Twister pseudo-random generator.
        // Giving it the exact same seed integer produces the exact same sequence every time.
        std::mt19937 rng(static_cast<uint32_t>(seed + attempt * 17));
        std::vector<std::string> shuffled = roadIds;
        std::shuffle(shuffled.begin(), shuffled.end(), rng);

        std::uniform_int_distribution<size_t> reasonDist(0, STANDARD_REASONS.size() - 1);

        finalBlocked.clear();
        // Block the target number of roads from the shuffled list
        for (int i = 0; i < targetBlocks && i < static_cast<int>(shuffled.size()); i++) {
            const std::string& rid = shuffled[i];
            const std::string& reason = STANDARD_REASONS[reasonDist(rng)];
            blockRoad(rid, reason);
            finalBlocked.push_back({rid, reason});
        }

        // For severe scenario, cut-offs are explicitly permitted
        if (severity == "severe") {
            break;
        }

        // For light and moderate scenarios, ensure all villages have a reachable shelter
        if (checkAllVillagesHaveShelter()) {
            break;
        }
    }

    // Build and return the final scenario result
    ScenarioResult res;
    res.severity = severity;
    res.seed = seed;
    res.blockedCount = static_cast<int>(finalBlocked.size());
    res.blockedRoads = finalBlocked;
    res.attempts = attempt;
    return res;
}


// BFS checks whether any open path connects the two nodes.
bool Graph::hasPath(const std::string& from, const std::string& to) const {
    if (!hasNode(from) || !hasNode(to)) return false;
    if (from == to) return true;

    // Set of visited nodes to prevent cycling in loops
    std::set<std::string> visited;
    // FIFO queue storing nodes waiting to have their neighbors explored
    std::queue<std::string> q;

    q.push(from);
    visited.insert(from);

    // BFS loop: pop front node and visit all unvisited open neighbors
    while (!q.empty()) {
        std::string curr = q.front();
        q.pop();

        if (curr == to) return true;

        for (const auto& edge : getNeighbours(curr)) {
            if (!visited.count(edge.neighbour)) {
                visited.insert(edge.neighbour);
                q.push(edge.neighbour);
            }
        }
    }
    return false;
}

// Verifies whether every village in the network has an open path to at least one shelter.
// Used by applyScenario to reject random road blockage combinations that isolate a village.
bool Graph::checkAllVillagesHaveShelter() const {
    auto villages = getNodesByType(NodeType::Village);
    auto shelters = getShelters();

    if (villages.empty() || shelters.empty()) {
        return true;
    }

    // Check each village one by one
    for (const auto* v : villages) {
        bool canReachAny = false;
        // Test reachability to each shelter until an open corridor is found
        for (const auto* s : shelters) {
            if (hasPath(v->id, s->id)) {
                canReachAny = true;
                break;
            }
        }
        if (!canReachAny) {
            return false;
        }
    }
    return true;
}


// Finds the closest relief facilities (hospitals, water points, etc.) from a source node.
std::unordered_map<std::string, std::vector<ServiceEntry>> Graph::getNearestServices(
    const std::string& sourceId) const {

    if (!hasNode(sourceId)) {
        throw std::invalid_argument("Source node not found: " + sourceId);
    }

    // Step 1: Run Dijkstra once to compute distances to all network nodes
    auto distances = dijkstraAllDistances(*this, sourceId);

    std::unordered_map<std::string, std::vector<ServiceEntry>> grouped;

    // Step 2: Categorize each facility and record its distance
    for (const auto& [nodeId, node] : nodes_) {
        // Exclude the source node itself
        if (nodeId == sourceId) continue;

        std::string typeKey = nodeTypeToString(node.type);
        double d = distances[nodeId];
        bool reachable = (d != UNREACHABLE);

        grouped[typeKey].push_back({
            node.id,
            node.name,
            typeKey,
            d,
            reachable,
            node.contact,
            node.note
        });
    }

    // Step 3: Sort each facility group by reachability and distance
    for (auto& [typeKey, list] : grouped) {
        std::sort(list.begin(), list.end(), [](const ServiceEntry& a, const ServiceEntry& b) {
            if (a.reachable != b.reachable) {
                return a.reachable > b.reachable;
            }
            return a.distance < b.distance;
        });

        // Step 4: Limit to nearest 3 per category to keep the UI concise
        if (list.size() > 3) {
            list.resize(3);
        }
    }

    return grouped;
}


// Computes territory-wide shelter assignment for every village in a single search pass.
CoverageResult Graph::computeCoverage() const {
    auto shelterNodes = getShelters();
    std::vector<std::string> shelterIds;
    for (const auto* s : shelterNodes) {
        shelterIds.push_back(s->id);
    }

    // Step 1: Run multi-source Dijkstra from all open shelters
    auto coverageMap = dijkstraMultiSource(*this, shelterIds);

    CoverageResult result;
    result.totalVillages = 0;
    result.coveredVillages = 0;
    result.cutoffVillages = 0;

    auto villageNodes = getNodesByType(NodeType::Village);
    // Sort villages alphabetically by name for a stable, clean UI presentation
    std::vector<const Node*> sortedVillages = villageNodes;
    std::sort(sortedVillages.begin(), sortedVillages.end(), [](const Node* a, const Node* b) {
        return a->name < b->name;
    });

    // Step 2: Assemble coverage details for each village
    for (const auto* v : sortedVillages) {
        result.totalVillages++;
        const auto& entry = coverageMap[v->id];

        CoverageVillage cv;
        cv.villageId = v->id;
        cv.villageName = v->name;

        // If distance is not infinity, an open path connects the village to an evacuation shelter
        if (entry.distance != UNREACHABLE && !entry.sourceOrigin.empty()) {
            cv.covered = true;
            cv.distance = entry.distance;
            cv.assignedShelterId = entry.sourceOrigin;
            cv.assignedShelterName = getNode(entry.sourceOrigin).name;
            result.coveredVillages++;
        } else {
            // No open path exists: this village is completely isolated by flood water
            cv.covered = false;
            cv.distance = UNREACHABLE;
            cv.assignedShelterId = "";
            cv.assignedShelterName = "None (Cut off)";
            result.cutoffVillages++;
            result.cutoffVillageIds.push_back(v->id);
        }
        result.villageCoverages.push_back(cv);
    }

    return result;
}


// Blocks roads on the active route to demonstrate a detour.
DisruptResult Graph::disruptRoute(const std::string& sourceId,
                                  const std::string& destinationId,
                                  int seed) {
    DisruptResult res;
    res.disrupted = false;

    // Step 1: Calculate the baseline route to identify its constituent roads
    PathResult baseline = dijkstraShortestPath(*this, sourceId, destinationId);
    if (!baseline.reachable || baseline.path.size() < 2) {
        res.message = "Destination is already unreachable or source equals destination.";
        return res;
    }

    // Step 2: Find all road segments currently traversed along the baseline path
    std::vector<std::string> routeRoads;
    for (size_t i = 0; i < baseline.path.size() - 1; i++) {
        const std::string& u = baseline.path[i];
        const std::string& v = baseline.path[i + 1];

        // Find the road connecting consecutive path nodes u and v
        for (const auto& [rid, r] : roads_) {
            if (!r.blocked && ((r.from == u && r.to == v) || (r.from == v && r.to == u))) {
                routeRoads.push_back(rid);
                break;
            }
        }
    }

    if (routeRoads.empty()) {
        res.message = "No traversable roads on active route.";
        return res;
    }

    // Step 3: Shuffle candidate roads along the route
    std::mt19937 rng(static_cast<uint32_t>(seed));
    std::shuffle(routeRoads.begin(), routeRoads.end(), rng);
    std::uniform_int_distribution<size_t> reasonDist(0, STANDARD_REASONS.size() - 1);

    // Step 4: Try blocking 1 or 2 roads, prioritizing options that leave an alternative path
    std::vector<std::string> chosenRoads;
    std::vector<std::string> chosenReasons;

    for (const auto& candidateRoad : routeRoads) {
        const std::string& reason = STANDARD_REASONS[reasonDist(rng)];
        blockRoad(candidateRoad, reason);

        // Check if an alternative route still exists on the remaining open roads
        PathResult alt = dijkstraShortestPath(*this, sourceId, destinationId);
        if (alt.reachable) {
            chosenRoads.push_back(candidateRoad);
            chosenReasons.push_back(reason);
            if (chosenRoads.size() >= 2) {
                break;
            }
        } else {
            // Revert candidate if it completely severs access to the destination
            restoreRoad(candidateRoad);
        }
    }

    // Fallback: if every road is a single point of failure, block the first road to demo cut-off
    if (chosenRoads.empty() && !routeRoads.empty()) {
        const std::string& candidateRoad = routeRoads[0];
        const std::string& reason = STANDARD_REASONS[reasonDist(rng)];
        blockRoad(candidateRoad, reason);
        chosenRoads.push_back(candidateRoad);
        chosenReasons.push_back(reason);
        res.disrupted = true;
        res.blockedRoadIds = chosenRoads;
        res.blockedReasons = chosenReasons;
        res.message = "Critical bottleneck road " + candidateRoad + " (" + reason +
                      ") blocked. No alternative route exists.";
        return res;
    }

    // Step 5: Format human-readable response message
    if (!chosenRoads.empty()) {
        res.disrupted = true;
        res.blockedRoadIds = chosenRoads;
        res.blockedReasons = chosenReasons;
        std::string listStr;
        for (size_t i = 0; i < chosenRoads.size(); i++) {
            if (i > 0) listStr += ", ";
            listStr += chosenRoads[i] + " (" + chosenReasons[i] + ")";
        }
        res.message = "Simulated disruption added on your route: " + listStr + ". Recalculating detour.";
    } else {
        res.message = "Could not apply disruptions on route.";
    }

    return res;
}


// Returns the total number of nodes stored in the network.
size_t Graph::nodeCount() const {
    return nodes_.size();
}

// Returns the total number of physical roads stored in the network.
size_t Graph::roadCount() const {
    return roads_.size();
}

// Registers a Node struct in the nodes_ hash map.
void Graph::addNode(const Node& node) {
    nodes_[node.id] = node;
}

// Adds an undirected road and updates the adjacency lists of both endpoints.
void Graph::addRoad(const Road& road) {
    if (!hasNode(road.from)) {
        throw std::invalid_argument("Road endpoint node not found: " + road.from);
    }
    if (!hasNode(road.to)) {
        throw std::invalid_argument("Road endpoint node not found: " + road.to);
    }
    if (road.distance <= 0.0) {
        throw std::invalid_argument("Road distance must be positive, got: " + std::to_string(road.distance));
    }

    roads_[road.id] = road;

    // Undirected edge: add to both endpoints' adjacency lists
    adjList_[road.from].push_back({road.to, road.distance, road.id});
    adjList_[road.to].push_back({road.from, road.distance, road.id});
}

// Empties all node, road, and adjacency data from memory.
void Graph::clear() {
    nodes_.clear();
    roads_.clear();
    adjList_.clear();
}

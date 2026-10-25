// graph.h — Road network graph structure for SafeRoute
//
// What this file does:
//   Defines the core data structures for modeling a disaster road network.
//   It stores nodes (villages, hospitals, shelters) and roads as a graph.
//   It also declares operations like loading network files, blocking roads,
//   generating disaster scenarios, and finding nearest relief services.
//
// What is inside:
//   - NodeType enum: all facility categories.
//   - Node struct: a single map location with coordinates and details.
//   - Road struct: an undirected connection between two nodes with length in km.
//   - Result structs: ScenarioResult, ServiceEntry, CoverageResult, DisruptResult.
//   - Graph class: manages nodes, roads, adjacency list, and graph queries.
//
// Other files that use this header:
//   - src/graph.cpp: implements all the member functions declared here.
//   - include/dijkstra.h & src/dijkstra.cpp: queries graph nodes, roads, and neighbors.
//   - include/server.h & src/server.cpp: exposes graph operations as HTTP endpoints.
//   - src/main.cpp: runs the interactive terminal CLI and web server startup.
//   - tests/test_main.cpp: runs automated unit tests on graph operations.

// Include guard: prevents compiler errors if this header is included more than once.
#ifndef SAFEROUTE_GRAPH_H
#define SAFEROUTE_GRAPH_H

// C++ feature note:
// <string> provides text strings.
// <vector> is a dynamic array that grows automatically as items are added.
// <unordered_map> is a hash table giving average O(1) fast key-value lookups.
// <stdexcept> provides standard exception types like std::invalid_argument.
// <utility> provides std::pair to hold two values together.
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <utility>

// ─── Node Types ──────────────────────────────────────────────────────────
// C++ feature: 'enum class' creates a type-safe list of named choices.
// It stops accidental conversion to numbers and keeps names scoped inside NodeType.
enum class NodeType {
    Village,      // Community needing evacuation or relief supplies delivery
    Junction,     // Road intersection with no resident population
    Hospital,     // Medical trauma center for injured victims
    Shelter,      // Safe evacuation destination with beds and relief staff
    Ngo,          // Humanitarian NGO relief base camp storing relief supplies
    Food,         // Government food distribution warehouse
    Water,        // Potable drinking water purification and tanker station
    FireStation,  // Fire brigade, search, and water-rescue boat outpost
    Police        // Police station for law enforcement and perimeter security
};

// Converts an enum value to a lowercase wire string, e.g. NodeType::Village -> "village"
// Input:  NodeType enum value.
// Output: Short lowercase std::string.
std::string nodeTypeToString(NodeType type);

// Parses a lowercase wire string back into a NodeType enum.
// Input:  String such as "village" or "hospital".
// Output: Corresponding NodeType enum value.
// Errors: Throws std::invalid_argument if the string is unrecognized.
NodeType stringToNodeType(const std::string& s);

// Converts an enum value to a clean title for display in the user interface.
// Input:  NodeType enum value.
// Output: Human-friendly string such as "Evacuation Shelter".
std::string nodeTypeToDisplayName(NodeType type);

// ─── Node ────────────────────────────────────────────────────────────────
// Represents a physical facility or place on the disaster map.
struct Node {
    std::string id;       // Unique short ID, e.g. "v1", "s2", "h1"
    std::string name;     // Full name, e.g. "Rampur"
    NodeType type;        // Category of facility (village, shelter, hospital, etc.)
    double x;             // Horizontal map position in SVG coordinate units
    double y;             // Vertical map position in SVG coordinate units
    std::string contact;  // Contact telephone number for relief dispatch
    std::string note;     // Operational details, e.g. "Open 24 hours, 40 beds"
};

// ─── Road ────────────────────────────────────────────────────────────────
// An undirected physical road segment connecting two nodes.
struct Road {
    std::string id;       // Unique road identifier, e.g. "r12"
    std::string from;     // ID of the first endpoint node
    std::string to;       // ID of the second endpoint node
    double distance;      // Road length in kilometers (km); must be strictly positive
    bool blocked;         // True if the road is closed due to hazard; false if open
    std::string reason;   // Human-readable hazard description, e.g. "Flooded road"
};

// ─── Scenario Generation Result ──────────────────────────────────────────
// Holds the outcome of applying a randomized disaster scenario.
struct ScenarioResult {
    std::string severity; // Calamity intensity: "light", "moderate", or "severe"
    int seed;             // Random number seed used; same seed repeats the exact scenario
    int blockedCount;     // Total number of road segments closed by this scenario
    // List of pairs storing (road ID, blockage reason) for every closed road
    std::vector<std::pair<std::string, std::string>> blockedRoads;
    int attempts;         // Number of random retries needed to keep all villages connected
};

// ─── Nearest Service Result ──────────────────────────────────────────────
// Information about one nearby facility found by the nearest-services search.
struct ServiceEntry {
    std::string id;       // Facility node ID
    std::string name;     // Facility display name
    std::string type;     // Facility category string (e.g. "hospital", "water")
    double distance;      // Shortest travel distance in km from the starting village
    bool reachable;       // True if reachable along open roads; false if blocked
    std::string contact;  // Facility phone contact
    std::string note;     // Operational notes (e.g. operating capacity)
};

// ─── Multi-Source Coverage Result ────────────────────────────────────────
// Coverage assignment for a single village under territory-wide shelter routing.
struct CoverageVillage {
    std::string villageId;            // Village node ID
    std::string villageName;          // Village display name
    std::string assignedShelterId;    // ID of the closest open shelter
    std::string assignedShelterName;  // Name of the closest open shelter
    double distance;                  // Shortest distance in km to that assigned shelter
    bool covered;                     // True if at least one open shelter can reach it
};

// Summary report of shelter coverage across all villages in the region.
struct CoverageResult {
    int totalVillages;                                // Total village count
    int coveredVillages;                              // Count of villages with open paths
    int cutoffVillages;                               // Count of completely isolated villages
    std::vector<std::string> cutoffVillageIds;        // List of isolated village IDs
    std::vector<CoverageVillage> villageCoverages;    // Detailed report per village
};

// ─── Route Disruption Result ─────────────────────────────────────────────
// Outcome of intentionally placing simulated hazards along an active route.
struct DisruptResult {
    bool disrupted;                             // True if at least one road was closed
    std::string message;                        // Explanation shown to the user
    std::vector<std::string> blockedRoadIds;    // IDs of roads closed by disruption
    std::vector<std::string> blockedReasons;    // Reasons assigned to closed roads
};

// ─── Graph Class ─────────────────────────────────────────────────────────
// Represents the complete road network using an adjacency list.
class Graph {
public:
    // Loads nodes and roads from a JSON network file.
    // Input:  Path to the JSON file on disk.
    // Output: None (populates internal graph structures).
    // Errors: Throws std::runtime_error on missing file, parse error, or invalid data.
    // Time:   O(V + E) where V is node count and E is road count.
    void loadFromFile(const std::string& filepath);

    // ── Node look-ups ──

    // Returns a read-only reference to a node by its ID.
    // Input:  Node ID string (e.g. "v1").
    // Output: Reference to matching Node struct.
    // Errors: Throws std::invalid_argument if node does not exist.
    // Time:   O(1) average hash table lookup.
    const Node& getNode(const std::string& id) const;

    // Checks whether a node exists in the network.
    // Input:  Node ID string.
    // Output: True if present, false otherwise.
    // Time:   O(1) average.
    bool hasNode(const std::string& id) const;

    // Returns all nodes in the network as an ID-to-Node map.
    // Output: Reference to the internal hash map of nodes.
    const std::unordered_map<std::string, Node>& getNodes() const;

    // Collects pointers to all nodes categorized as shelters.
    // Output: std::vector containing pointers to matching Node objects.
    // Time:   O(V) where V is the total node count.
    std::vector<const Node*> getShelters() const;

    // Collects pointers to all nodes of a specific facility type.
    // Input:  NodeType category.
    // Output: Vector of pointers to matching nodes.
    // Time:   O(V).
    std::vector<const Node*> getNodesByType(NodeType type) const;

    // ── Road look-ups ──

    // Returns a read-only reference to a road by its ID.
    // Input:  Road ID string (e.g. "r1").
    // Output: Reference to matching Road struct.
    // Errors: Throws std::invalid_argument if road does not exist.
    // Time:   O(1) average.
    const Road& getRoad(const std::string& id) const;

    // Checks whether a road exists in the network.
    // Input:  Road ID string.
    // Output: True if present, false otherwise.
    // Time:   O(1) average.
    bool hasRoad(const std::string& id) const;

    // Returns all roads in the network as an ID-to-Road map.
    // Output: Reference to the internal hash map of roads.
    const std::unordered_map<std::string, Road>& getRoads() const;

    // ── Adjacency list edge ──
    // Stores one outgoing connection from a node to a neighbor along an open road.
    struct Edge {
        std::string neighbour;  // ID of the connected neighbor node
        double distance;        // Road distance in km to reach that neighbor
        std::string roadId;     // ID of the underlying Road struct
    };

    // Returns all open (non-blocked) edges incident to a given node.
    // Blocked roads are filtered out so algorithms only explore passable paths.
    // Input:  Node ID string.
    // Output: Vector of Edge structs representing open connections.
    // Errors: Throws std::invalid_argument if node does not exist.
    // Time:   O(deg(v)) where deg(v) is the degree (neighbor count) of the node.
    std::vector<Edge> getNeighbours(const std::string& nodeId) const;

    // ── Road status management ──

    // Marks a road as blocked by hazard and attaches an explanatory reason.
    // Input:  Road ID and optional reason string.
    // Errors: Throws std::invalid_argument if road ID is unknown.
    // Time:   O(1) average.
    void blockRoad(const std::string& roadId, const std::string& reason = "Fallen trees detected");

    // Reopens a previously blocked road and clears its reason string.
    // Input:  Road ID.
    // Errors: Throws std::invalid_argument if road ID is unknown.
    // Time:   O(1) average.
    void restoreRoad(const std::string& roadId);

    // Reopens every road in the network and clears all blockage reasons.
    // Time:   O(E) where E is road count.
    void resetAllRoads();

    // Returns the standard predefined list of flood and disaster blockage reasons.
    static const std::vector<std::string>& getStandardReasons();

    // ── Seeded scenario generator ──

    // Simulates randomized disaster damage across the network.
    // Severity levels:
    //   - "light": ~8% of roads blocked. Guarantees every village can reach a shelter.
    //   - "moderate": ~15% of roads blocked. Guarantees village connectivity.
    //   - "severe": ~25% of roads blocked. Pockets may be completely cut off.
    // Input:  Severity string and integer seed for repeatability.
    // Output: ScenarioResult details.
    // Time:   O(attempts * (E + V)) where attempts is usually 1-5.
    ScenarioResult applyScenario(const std::string& severity, int seed);

    // ── Nearest services grouping ──

    // Runs Dijkstra from sourceId and finds the closest 3 facilities of each type.
    // Input:  Source node ID.
    // Output: Map from facility type string to sorted vector of ServiceEntry objects.
    // Errors: Throws std::invalid_argument if source node is unknown.
    // Time:   O((V + E) log V) dominated by the Dijkstra run.
    std::unordered_map<std::string, std::vector<ServiceEntry>> getNearestServices(const std::string& sourceId) const;

    // ── Multi-source coverage ──

    // Runs multi-source Dijkstra from all shelters simultaneously to assign each
    // village to its closest open evacuation shelter in a single pass.
    // Output: CoverageResult with stats and village-by-village assignments.
    // Time:   O((V + E) log V).
    CoverageResult computeCoverage() const;

    // ── Route disruption ──

    // Artificially blocks 1-2 open roads along an active evacuation path so the
    // user can immediately see an alternative detour calculated and drawn.
    // Input:  Source ID, destination ID, and random seed.
    // Output: DisruptResult showing which roads were closed.
    // Time:   O((V + E) log V).
    DisruptResult disruptRoute(const std::string& sourceId, const std::string& destinationId, int seed = 42);

    // ── Connectivity helpers ──

    // Uses Breadth-First Search (BFS) to test if an open path exists between two nodes.
    // Input:  Starting node ID and destination node ID.
    // Output: True if reachable, false if cut off.
    // Time:   O(V + E).
    bool hasPath(const std::string& from, const std::string& to) const;

    // Verifies whether every village in the network can reach at least one shelter.
    // Output: True if all villages have a viable evacuation corridor, false otherwise.
    // Time:   O(V_villages * (V + E)).
    bool checkAllVillagesHaveShelter() const;

    // ── Counts and programmatic builders ──

    // Returns total count of nodes registered in the network.
    size_t nodeCount() const;

    // Returns total count of roads registered in the network.
    size_t roadCount() const;

    // Adds a node directly to the graph (used during JSON load or unit tests).
    void addNode(const Node& node);

    // Adds an undirected road and updates the adjacency lists of both endpoints.
    // Errors: Throws std::invalid_argument if endpoints do not exist or distance <= 0.
    void addRoad(const Road& road);

    // Clears all nodes, roads, and adjacency connections from memory.
    void clear();

private:
    // C++ feature: private member variables have a trailing underscore by convention.
    // Hash map from node ID string to Node data structure
    std::unordered_map<std::string, Node> nodes_;

    // Hash map from road ID string to Road data structure
    std::unordered_map<std::string, Road> roads_;

    // Adjacency list: maps each node ID to a list of its outgoing open edges.
    // This allows graph search algorithms like Dijkstra to quickly find neighbors.
    std::unordered_map<std::string, std::vector<Edge>> adjList_;
};

#endif // SAFEROUTE_GRAPH_H

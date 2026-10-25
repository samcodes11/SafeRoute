// Serves the SafeRoute web app and its JSON API.

#include "../include/server.h"
#include "../include/graph.h"
#include "../include/dijkstra.h"
#include "../include/knapsack.h"

// Third-party single-header libraries stored offline in third_party/
#include "../third_party/httplib.h"
#include "../third_party/json.hpp"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>

// Type alias: makes nlohmann::json easier to write throughout the file
using json = nlohmann::json;


// Helper function to send uniform JSON error responses to the HTTP client.
static void sendError(httplib::Response& res, int status, const std::string& msg) {
    res.status = status;
    json j;
    j["error"] = msg;
    res.set_content(j.dump(), "application/json");
}

// Converts a PathResult struct into a structured JSON object with node details.
static json pathResultToJson(const PathResult& pr, const Graph& graph) {
    json j;
    j["reachable"] = pr.reachable;
    if (pr.reachable) {
        j["distance"] = pr.distance;
        json pathArr = json::array();
        // Enrich each waypoint node ID with name, facility type, contact, and note
        for (const auto& nodeId : pr.path) {
            json step;
            step["id"] = nodeId;
            step["name"] = graph.getNode(nodeId).name;
            step["type"] = nodeTypeToString(graph.getNode(nodeId).type);
            step["contact"] = graph.getNode(nodeId).contact;
            step["note"] = graph.getNode(nodeId).note;
            pathArr.push_back(step);
        }
        j["path"] = pathArr;
    } else {
        // Destination is isolated: distance is null and path is empty
        j["distance"] = nullptr;
        j["path"] = json::array();
    }
    return j;
}


// Formats a KnapsackResult into JSON with item breakdowns and grouped manifest rows.
static json formatKnapsackResult(const KnapsackResult& kr) {
    json kJson;
    kJson["totalWeightKg"] = kr.totalWeightKg;
    kJson["totalUrgency"] = kr.totalUrgency;
    kJson["itemsCount"] = static_cast<int>(kr.chosenItems.size());

    // Serialize discrete chosen items
    json chosenArr = json::array();
    for (const auto& item : kr.chosenItems) {
        json it;
        it["name"] = item.name;
        it["weightKg"] = item.weightKg;
        it["urgency"] = item.urgency;
        chosenArr.push_back(it);
    }
    kJson["chosenItems"] = chosenArr;

    // Group chosen items by name
    std::unordered_map<std::string, int> counts;
    std::unordered_map<std::string, int> unitWeights;
    std::unordered_map<std::string, int> unitUrgencies;

    for (const auto& item : kr.chosenItems) {
        counts[item.name]++;
        unitWeights[item.name] = item.weightKg;
        unitUrgencies[item.name] = item.urgency;
    }

    struct GroupedRow {
        std::string name;
        int count;
        int unitWeight;
        int unitUrgency;
        int totalWeight;
        int totalUrgency;
    };
    std::vector<GroupedRow> rows;
    for (const auto& [name, cnt] : counts) {
        rows.push_back({
            name,
            cnt,
            unitWeights[name],
            unitUrgencies[name],
            unitWeights[name] * cnt,
            unitUrgencies[name] * cnt
        });
    }

    // Sort rows by row urgency descending, ties broken alphabetically
    std::sort(rows.begin(), rows.end(), [](const GroupedRow& a, const GroupedRow& b) {
        if (a.totalUrgency != b.totalUrgency) {
            return a.totalUrgency > b.totalUrgency;
        }
        return a.name < b.name;
    });

    json grouped = json::array();
    for (const auto& r : rows) {
        json g;
        g["name"] = r.name;
        g["qty"] = r.count;
        g["count"] = r.count;
        g["unitWeightKg"] = r.unitWeight;
        g["weightPerUnit"] = r.unitWeight;
        g["unitUrgency"] = r.unitUrgency;
        g["urgencyPerUnit"] = r.unitUrgency;
        g["totalWeightKg"] = r.totalWeight;
        g["subtotalWeightKg"] = r.totalWeight;
        g["totalUrgency"] = r.totalUrgency;
        g["subtotalUrgency"] = r.totalUrgency;
        grouped.push_back(g);
    }
    kJson["grouped"] = grouped;
    return kJson;
}


// Sets up HTTP routes, mounts static file hosting, and enters the server loop.
void startServer(Graph& graph, int port, const std::string& webRoot) {
    // Create the httplib server instance
    httplib::Server svr;

    // Static file serving: maps GET requests to files located in webRoot (e.g. "web/")
    svr.set_mount_point("/", webRoot);

    // Prevent caching of static assets so edits are always reflected immediately
    svr.set_file_request_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store, no-cache, must-revalidate");
        res.set_header("Pragma", "no-cache");
    });

    // Returns the entire road network topology, nodes, roads, and hazard reasons.
    svr.Get("/api/network", [&graph](const httplib::Request&, httplib::Response& res) {
        try {
            json j;

            // Serialize nodes array
            json nodesArr = json::array();
            for (const auto& [id, node] : graph.getNodes()) {
                json n;
                n["id"] = node.id;
                n["name"] = node.name;
                n["type"] = nodeTypeToString(node.type);
                n["displayName"] = nodeTypeToDisplayName(node.type);
                n["x"] = node.x;
                n["y"] = node.y;
                n["contact"] = node.contact;
                n["note"] = node.note;
                nodesArr.push_back(n);
            }
            j["nodes"] = nodesArr;

            // Serialize roads array
            json roadsArr = json::array();
            for (const auto& [id, road] : graph.getRoads()) {
                json r;
                r["id"] = road.id;
                r["from"] = road.from;
                r["to"] = road.to;
                r["distance"] = road.distance;
                r["blocked"] = road.blocked;
                r["reason"] = road.reason;
                roadsArr.push_back(r);
            }
            j["roads"] = roadsArr;

            // Include standard list of disaster obstruction reasons
            j["reasons"] = Graph::getStandardReasons();

            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            sendError(res, 500, std::string("Internal error: ") + e.what());
        }
    });

    // Compares routes with all roads open and with current road closures.
    svr.Post("/api/compare", [&graph](const httplib::Request& req, httplib::Response& res) {
        try {
            // Parse JSON request body
            json body;
            try {
                body = json::parse(req.body);
            } catch (...) {
                sendError(res, 400, "Request body must be valid JSON.");
                return;
            }

            // Validate required fields
            if (!body.contains("source") || !body.contains("destination")) {
                sendError(res, 400, "Missing 'source' or 'destination' field.");
                return;
            }

            std::string source = body["source"].get<std::string>();
            std::string destination = body["destination"].get<std::string>();

            // Validate node existence
            if (!graph.hasNode(source)) {
                sendError(res, 400, "Unknown source node: " + source);
                return;
            }
            if (!graph.hasNode(destination)) {
                sendError(res, 400, "Unknown destination node: " + destination);
                return;
            }

            // Step 1: Calculate revised route using only currently open roads
            PathResult revised = dijkstraShortestPath(graph, source, destination);

            // Step 2: Temporarily store active blockages and compute baseline (all roads open)
            std::vector<std::pair<std::string, std::string>> savedBlocks;
            for (const auto& [id, road] : graph.getRoads()) {
                if (road.blocked) {
                    savedBlocks.push_back({id, road.reason});
                }
            }

            // Reset all roads to open to measure original baseline
            graph.resetAllRoads();
            PathResult original = dijkstraShortestPath(graph, source, destination);

            // Restore active disaster road blocks and reasons
            for (const auto& [rid, reason] : savedBlocks) {
                graph.blockRoad(rid, reason);
            }

            // Step 3: Identify active road blocks lying on the original baseline route
            json blockedOnOriginal = json::array();
            if (original.reachable && original.path.size() >= 2) {
                for (size_t i = 0; i < original.path.size() - 1; i++) {
                    const std::string& u = original.path[i];
                    const std::string& v = original.path[i + 1];

                    // Find road connecting consecutive waypoints u and v
                    for (const auto& [rid, r] : graph.getRoads()) {
                        if (r.blocked && ((r.from == u && r.to == v) || (r.from == v && r.to == u))) {
                            json blk;
                            blk["id"] = r.id;
                            blk["from"] = r.from;
                            blk["fromName"] = graph.getNode(r.from).name;
                            blk["to"] = r.to;
                            blk["toName"] = graph.getNode(r.to).name;
                            blk["reason"] = r.reason;
                            blk["distance"] = r.distance;
                            blockedOnOriginal.push_back(blk);
                        }
                    }
                }
            }

            // Step 4: Assemble comparison metrics (extra km and percentage increase)
            json j;
            j["source"] = source;
            j["sourceName"] = graph.getNode(source).name;
            j["destination"] = destination;
            j["destinationName"] = graph.getNode(destination).name;
            j["original"] = pathResultToJson(original, graph);
            j["revised"] = pathResultToJson(revised, graph);
            j["blockedOnOriginalRoute"] = blockedOnOriginal;
            j["totalBlockedRoads"] = savedBlocks.size();

            // Calculate detour overhead if both paths are reachable
            if (original.reachable && revised.reachable) {
                double extra = revised.distance - original.distance;
                double pct = (original.distance > 0.0)
                    ? (extra / original.distance) * 100.0
                    : 0.0;
                j["extraKm"] = extra;
                j["percentIncrease"] = pct;
            } else {
                j["extraKm"] = nullptr;
                j["percentIncrease"] = nullptr;
            }

            res.set_content(j.dump(), "application/json");
        } catch (const std::invalid_argument& e) {
            sendError(res, 400, e.what());
        } catch (const std::exception& e) {
            sendError(res, 500, std::string("Internal error: ") + e.what());
        }
    });

    // Ranks all evacuation shelters in order of shortest road distance
    svr.Post("/api/shelters", [&graph](const httplib::Request& req, httplib::Response& res) {
        try {
            json body;
            try {
                body = json::parse(req.body);
            } catch (...) {
                sendError(res, 400, "Request body must be valid JSON.");
                return;
            }

            if (!body.contains("source")) {
                sendError(res, 400, "Missing 'source' field.");
                return;
            }

            std::string source = body["source"].get<std::string>();
            if (!graph.hasNode(source)) {
                sendError(res, 400, "Unknown source node: " + source);
                return;
            }

            // Run Dijkstra to find shortest distances to all nodes
            auto distances = dijkstraAllDistances(graph, source);

            // Collect and sort shelters by distance
            struct ShelterSortItem {
                std::string id;
                std::string name;
                double distance;
                bool reachable;
                std::string contact;
                std::string note;
            };

            std::vector<ShelterSortItem> list;
            for (const auto* s : graph.getShelters()) {
                double d = distances[s->id];
                list.push_back({s->id, s->name, d, (d != UNREACHABLE), s->contact, s->note});
            }

            // Sort reachable shelters first by distance ascending
            std::sort(list.begin(), list.end(), [](const ShelterSortItem& a, const ShelterSortItem& b) {
                if (a.reachable != b.reachable) return a.reachable > b.reachable;
                return a.distance < b.distance;
            });

            // Format JSON response
            json j;
            j["source"] = source;
            j["sourceName"] = graph.getNode(source).name;
            json arr = json::array();
            for (const auto& item : list) {
                json s;
                s["id"] = item.id;
                s["name"] = item.name;
                s["reachable"] = item.reachable;
                s["distance"] = item.reachable ? json(item.distance) : json(nullptr);
                s["contact"] = item.contact;
                s["note"] = item.note;
                arr.push_back(s);
            }
            j["shelters"] = arr;

            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            sendError(res, 500, std::string("Internal error: ") + e.what());
        }
    });

    // Finds nearby relief facilities grouped by category.
    svr.Post("/api/services", [&graph](const httplib::Request& req, httplib::Response& res) {
        try {
            json body;
            try {
                body = json::parse(req.body);
            } catch (...) {
                sendError(res, 400, "Request body must be valid JSON.");
                return;
            }

            if (!body.contains("source")) {
                sendError(res, 400, "Missing 'source' field.");
                return;
            }

            std::string source = body["source"].get<std::string>();
            if (!graph.hasNode(source)) {
                sendError(res, 400, "Unknown source node: " + source);
                return;
            }

            // Query nearest facilities grouped by facility type
            auto grouped = graph.getNearestServices(source);

            json j;
            j["source"] = source;
            j["sourceName"] = graph.getNode(source).name;

            json servicesJson = json::object();
            for (const auto& [typeKey, list] : grouped) {
                json arr = json::array();
                for (const auto& s : list) {
                    json item;
                    item["id"] = s.id;
                    item["name"] = s.name;
                    item["type"] = s.type;
                    item["reachable"] = s.reachable;
                    item["distance"] = s.reachable ? json(s.distance) : json(nullptr);
                    item["contact"] = s.contact;
                    item["note"] = s.note;
                    arr.push_back(item);
                }
                servicesJson[typeKey] = arr;
            }
            j["services"] = servicesJson;

            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            sendError(res, 500, std::string("Services error: ") + e.what());
        }
    });

    // Closes a specific road segment and attaches a hazard reason.
    svr.Post("/api/block", [&graph](const httplib::Request& req, httplib::Response& res) {
        try {
            json body;
            try {
                body = json::parse(req.body);
            } catch (...) {
                sendError(res, 400, "Request body must be valid JSON.");
                return;
            }

            if (!body.contains("road")) {
                sendError(res, 400, "Missing 'road' field.");
                return;
            }

            std::string roadId = body["road"].get<std::string>();
            if (!graph.hasRoad(roadId)) {
                sendError(res, 400, "Unknown road id: " + roadId);
                return;
            }

            // Use provided reason or fall back to default
            std::string reason = "Fallen trees detected";
            if (body.contains("reason") && !body["reason"].get<std::string>().empty()) {
                reason = body["reason"].get<std::string>();
            }

            graph.blockRoad(roadId, reason);

            json j;
            j["status"] = "ok";
            j["road"] = roadId;
            j["blocked"] = true;
            j["reason"] = reason;
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            sendError(res, 500, std::string("Internal error: ") + e.what());
        }
    });

    // Reopens a blocked road segment and clears its reason string.
    svr.Post("/api/restore", [&graph](const httplib::Request& req, httplib::Response& res) {
        try {
            json body;
            try {
                body = json::parse(req.body);
            } catch (...) {
                sendError(res, 400, "Request body must be valid JSON.");
                return;
            }

            if (!body.contains("road")) {
                sendError(res, 400, "Missing 'road' field.");
                return;
            }

            std::string roadId = body["road"].get<std::string>();
            if (!graph.hasRoad(roadId)) {
                sendError(res, 400, "Unknown road id: " + roadId);
                return;
            }

            graph.restoreRoad(roadId);

            json j;
            j["status"] = "ok";
            j["road"] = roadId;
            j["blocked"] = false;
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            sendError(res, 500, std::string("Internal error: ") + e.what());
        }
    });

    // Restores every road in the network to open and clears all reasons.
    svr.Post("/api/reset", [&graph](const httplib::Request&, httplib::Response& res) {
        try {
            graph.resetAllRoads();
            json j;
            j["status"] = "ok";
            j["message"] = "All road obstructions cleared.";
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            sendError(res, 500, std::string("Internal error: ") + e.what());
        }
    });

    // Applies a seeded disaster scenario (Light ~8%, Moderate ~15%, Severe ~25%).
    svr.Post("/api/scenario", [&graph](const httplib::Request& req, httplib::Response& res) {
        try {
            json body;
            try {
                body = json::parse(req.body);
            } catch (...) {
                sendError(res, 400, "Request body must be valid JSON.");
                return;
            }

            // Defaults: moderate severity and seed 2026
            std::string severity = "moderate";
            if (body.contains("severity")) {
                severity = body["severity"].get<std::string>();
            }

            int seed = 2026;
            if (body.contains("seed")) {
                seed = body["seed"].get<int>();
            }

            // Apply scenario in the graph
            auto sc = graph.applyScenario(severity, seed);

            json j;
            j["severity"] = sc.severity;
            j["seed"] = sc.seed;
            j["blockedCount"] = sc.blockedCount;
            j["attempts"] = sc.attempts;

            json blockedArr = json::array();
            for (const auto& [rid, reason] : sc.blockedRoads) {
                const auto& r = graph.getRoad(rid);
                json item;
                item["road"] = rid;
                item["from"] = r.from;
                item["fromName"] = graph.getNode(r.from).name;
                item["to"] = r.to;
                item["toName"] = graph.getNode(r.to).name;
                item["distance"] = r.distance;
                item["reason"] = reason;
                blockedArr.push_back(item);
            }
            j["blockedRoads"] = blockedArr;

            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            sendError(res, 500, std::string("Scenario error: ") + e.what());
        }
    });

    // Artificially places simulated hazards on the active evacuation path
    svr.Post("/api/disrupt-route", [&graph](const httplib::Request& req, httplib::Response& res) {
        try {
            json body;
            try {
                body = json::parse(req.body);
            } catch (...) {
                sendError(res, 400, "Request body must be valid JSON.");
                return;
            }

            if (!body.contains("source") || !body.contains("destination")) {
                sendError(res, 400, "Missing 'source' or 'destination' field.");
                return;
            }

            std::string source = body["source"].get<std::string>();
            std::string destination = body["destination"].get<std::string>();
            int seed = body.contains("seed") ? body["seed"].get<int>() : 42;

            auto dis = graph.disruptRoute(source, destination, seed);

            json j;
            j["disrupted"] = dis.disrupted;
            j["message"] = dis.message;
            j["blockedRoadIds"] = dis.blockedRoadIds;
            j["blockedReasons"] = dis.blockedReasons;

            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            sendError(res, 500, std::string("Disrupt route error: ") + e.what());
        }
    });

    // Executes Dijkstra's algorithm step by step and returns snapshots
    svr.Post("/api/trace", [&graph](const httplib::Request& req, httplib::Response& res) {
        try {
            json body;
            try {
                body = json::parse(req.body);
            } catch (...) {
                sendError(res, 400, "Request body must be valid JSON.");
                return;
            }

            if (!body.contains("source") || !body.contains("destination")) {
                sendError(res, 400, "Missing 'source' or 'destination' field.");
                return;
            }

            std::string source = body["source"].get<std::string>();
            std::string destination = body["destination"].get<std::string>();

            // Run Dijkstra with the trace recorder active
            TraceRecorder recorder;
            PathResult pathRes = dijkstraShortestPath(graph, source, destination, &recorder);

            json j;
            j["source"] = source;
            j["destination"] = destination;
            j["reachable"] = pathRes.reachable;
            j["distance"] = pathRes.reachable ? json(pathRes.distance) : json(nullptr);

            // Serialize recorded execution steps
            json stepsArr = json::array();
            for (const auto& step : recorder.steps) {
                json s;
                s["stepIndex"] = step.stepIndex;
                s["action"] = step.action;
                s["currentNode"] = step.currentNode;
                s["currentDistance"] = step.currentDistance;
                s["targetNode"] = step.targetNode;
                s["edgeDistance"] = step.edgeDistance;
                s["oldDistance"] = (step.oldDistance == UNREACHABLE) ? json(nullptr) : json(step.oldDistance);
                s["newDistance"] = (step.newDistance == UNREACHABLE) ? json(nullptr) : json(step.newDistance);
                s["relaxed"] = step.relaxed;

                // Priority queue snapshot
                json pqArr = json::array();
                for (const auto& [nodeId, d] : step.pqSnapshot) {
                    json item;
                    item["node"] = nodeId;
                    item["dist"] = d;
                    pqArr.push_back(item);
                }
                s["pqSnapshot"] = pqArr;

                // Distance table snapshot (filter out infinite distances to save bandwidth)
                json distMap = json::object();
                for (const auto& [nodeId, d] : step.distancesSnapshot) {
                    if (d != UNREACHABLE) {
                        distMap[nodeId] = d;
                    }
                }
                s["distancesSnapshot"] = distMap;

                stepsArr.push_back(s);
            }
            j["steps"] = stepsArr;
            j["totalSteps"] = stepsArr.size();

            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            sendError(res, 500, std::string("Trace error: ") + e.what());
        }
    });

    // Uses all shelters as starting points to find village coverage.
    svr.Get("/api/coverage", [&graph](const httplib::Request&, httplib::Response& res) {
        try {
            auto cov = graph.computeCoverage();

            json j;
            j["totalVillages"] = cov.totalVillages;
            j["coveredVillages"] = cov.coveredVillages;
            j["cutoffVillages"] = cov.cutoffVillages;
            j["cutoffVillageIds"] = cov.cutoffVillageIds;

            json villagesArr = json::array();
            for (const auto& cv : cov.villageCoverages) {
                json item;
                item["villageId"] = cv.villageId;
                item["villageName"] = cv.villageName;
                item["assignedShelterId"] = cv.assignedShelterId;
                item["assignedShelterName"] = cv.assignedShelterName;
                item["distance"] = cv.covered ? json(cv.distance) : json(nullptr);
                item["covered"] = cv.covered;
                villagesArr.push_back(item);
            }
            j["villageCoverages"] = villagesArr;

            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            sendError(res, 500, std::string("Coverage error: ") + e.what());
        }
    });

    // Returns relief items, village needs, and eligible depots.
    svr.Get("/api/supplies", [&graph](const httplib::Request&, httplib::Response& res) {
        try {
            json suppliesData;
            json villageNeedsData;

            // Load commodity catalogue
            std::ifstream supFile("data/supplies.json");
            if (supFile.is_open()) {
                suppliesData = json::parse(supFile);
            } else {
                sendError(res, 500, "Missing data/supplies.json");
                return;
            }

            // Load surveyed relief needs per village
            std::ifstream needFile("data/village_needs.json");
            if (needFile.is_open()) {
                villageNeedsData = json::parse(needFile);
            } else {
                sendError(res, 500, "Missing data/village_needs.json");
                return;
            }

            // Eligible dispatch depots: NGO relief camps and Food distribution centres
            json depotsArr = json::array();
            for (const auto& [id, node] : graph.getNodes()) {
                if (node.type == NodeType::Ngo || node.type == NodeType::Food) {
                    json d;
                    d["id"] = node.id;
                    d["name"] = node.name;
                    d["type"] = nodeTypeToString(node.type);
                    d["contact"] = node.contact;
                    d["note"] = node.note;
                    depotsArr.push_back(d);
                }
            }

            // Target villages array
            json villagesArr = json::array();
            for (const auto& [id, node] : graph.getNodes()) {
                if (node.type == NodeType::Village) {
                    json v;
                    v["id"] = node.id;
                    v["name"] = node.name;
                    villagesArr.push_back(v);
                }
            }

            json j;
            j["catalogue"] = suppliesData["supplies"];
            j["villageNeeds"] = villageNeedsData["villages"];
            j["depots"] = depotsArr;
            j["villages"] = villagesArr;

            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            sendError(res, 500, std::string("Supplies error: ") + e.what());
        }
    });

    // Finds the delivery route and chooses relief items with knapsack DP.
    svr.Post("/api/load-plan", [&graph](const httplib::Request& req, httplib::Response& res) {
        try {
            json body;
            try {
                body = json::parse(req.body);
            } catch (...) {
                sendError(res, 400, "Request body must be valid JSON.");
                return;
            }

            if (!body.contains("depotId") || !body.contains("villageId") || !body.contains("capacityKg")) {
                sendError(res, 400, "Missing depotId, villageId, or capacityKg.");
                return;
            }

            std::string depotId = body["depotId"].get<std::string>();
            std::string villageId = body["villageId"].get<std::string>();
            int capacityKg = body["capacityKg"].get<int>();

            // Validate nodes and payload limits
            if (!graph.hasNode(depotId)) {
                sendError(res, 400, "Unknown depot node: " + depotId);
                return;
            }
            if (!graph.hasNode(villageId)) {
                sendError(res, 400, "Unknown village node: " + villageId);
                return;
            }
            if (capacityKg < 0 || capacityKg > 5000) {
                sendError(res, 400, "Capacity must be between 0 and 5000 kg.");
                return;
            }

            // Step 1: Check road reachability via Dijkstra on current open network
            PathResult route = dijkstraShortestPath(graph, depotId, villageId);

            json j;
            j["depotId"] = depotId;
            j["depotName"] = graph.getNode(depotId).name;
            j["villageId"] = villageId;
            j["villageName"] = graph.getNode(villageId).name;
            j["capacityKg"] = capacityKg;
            j["reachable"] = route.reachable;

            // If destination is isolated by floods, return immediately with clear notification
            if (!route.reachable) {
                j["distance"] = nullptr;
                j["routeDistanceKm"] = nullptr;
                j["path"] = json::array();
                j["routePath"] = json::array();
                j["message"] = "Destination village is cut off by road obstructions. Relief truck cannot reach by road.";
                res.set_content(j.dump(), "application/json");
                return;
            }

            j["distance"] = route.distance;
            j["routeDistanceKm"] = route.distance;
            j["routePath"] = route.path;
            json pathArr = json::array();
            for (const auto& nid : route.path) {
                json step;
                step["id"] = nid;
                step["name"] = graph.getNode(nid).name;
                step["type"] = nodeTypeToString(graph.getNode(nid).type);
                pathArr.push_back(step);
            }
            j["path"] = pathArr;

            // Step 2: Ingest village relief demand profile
            std::ifstream needFile("data/village_needs.json");
            if (!needFile.is_open()) {
                sendError(res, 500, "Cannot open data/village_needs.json");
                return;
            }
            json needsJson = json::parse(needFile);

            if (!needsJson["villages"].contains(villageId)) {
                sendError(res, 400, "No demand profile found for village: " + villageId);
                return;
            }

            const auto& vProfile = needsJson["villages"][villageId];

            // Expand multi-unit requirements into discrete 0/1 SupplyItem instances
            std::vector<SupplyItem> items;
            int totalDemandWeight = 0;
            int totalDemandUrgency = 0;

            for (const auto& itemDemand : vProfile["needs"]) {
                std::string name = itemDemand["name"].get<std::string>();
                int w = itemDemand["weightKg"].get<int>();
                int urg = itemDemand["urgency"].get<int>();
                int units = itemDemand["unitsNeeded"].get<int>();

                // Each unit becomes one 0/1 knapsack choice
                for (int u = 0; u < units; ++u) {
                    items.push_back({name, w, urg});
                    totalDemandWeight += w;
                    totalDemandUrgency += urg;
                }
            }

            j["totalItemsNeeded"] = items.size();
            j["totalWeightNeeded"] = totalDemandWeight;
            j["totalUrgencyNeeded"] = totalDemandUrgency;

            // Step 3: Run DP knapsack solver
            KnapsackResult dpResult = solveKnapsackDP(items, capacityKg, false);

            j["dp"] = formatKnapsackResult(dpResult);

            res.set_content(j.dump(), "application/json");
        } catch (const std::invalid_argument& e) {
            sendError(res, 400, e.what());
        } catch (const std::exception& e) {
            sendError(res, 500, std::string("Load plan error: ") + e.what());
        }
    });

    // Returns a fixed pedagogical worked example for 0/1 Knapsack DP
    svr.Get("/api/load-demo", [](const httplib::Request&, httplib::Response& res) {
        try {
            int capacity = 50;
            std::vector<SupplyItem> items = {
                {"Medical kit", 10, 60},
                {"Blanket bundle", 20, 100},
                {"Water crate", 30, 120}
            };

            // Solve DP with full table and backtracking path recording
            KnapsackResult dp = solveKnapsackDP(items, capacity, true);

            json j;
            j["capacityKg"] = capacity;

            // Serialize candidate items
            json itemsArr = json::array();
            for (const auto& item : items) {
                json it;
                it["name"] = item.name;
                it["weightKg"] = item.weightKg;
                it["urgency"] = item.urgency;
                itemsArr.push_back(it);
            }
            j["items"] = itemsArr;

            // Serialize DP result with grouped breakdown and full 2D table
            json dpJson = formatKnapsackResult(dp);
            dpJson["table"] = dp.dpTable;
            dpJson["dpTable"] = dp.dpTable;

            // Backtracking cell coordinates for table highlighting (both i/w and row/col)
            json backtrackArr = json::array();
            for (const auto& [r, c] : dp.backtrackCells) {
                json cell;
                cell["i"] = r;
                cell["w"] = c;
                cell["row"] = r;
                cell["col"] = c;
                backtrackArr.push_back(cell);
            }
            dpJson["backtrackCells"] = backtrackArr;
            j["dp"] = dpJson;
            j["dpTable"] = dp.dpTable;
            j["backtrackCells"] = backtrackArr;

            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            sendError(res, 500, std::string("Load demo error: ") + e.what());
        }
    });

    std::cout << "SafeRoute server online at http://localhost:" << port << std::endl;
    std::cout << "Serving static assets from " << webRoot << std::endl;

    // Start blocking event loop on 0.0.0.0
    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "Fatal: Could not bind SafeRoute server to port " << port << std::endl;
    }
}

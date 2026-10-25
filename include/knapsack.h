// This header defines the item, result, validation, and knapsack functions.

// Prevent this header from being included more than once.
#ifndef SAFEROUTE_KNAPSACK_H
#define SAFEROUTE_KNAPSACK_H

#include <string>
#include <vector>
#include <utility>
#include <stdexcept>

// One indivisible cargo item.
struct SupplyItem {
    std::string name;       // Human-readable commodity name, e.g. "Medical trauma kit"
    int weightKg;           // Weight in whole kilograms (kg); must be strictly positive (> 0)
    int urgency;            // Priority urgency score (e.g. 10 to 100); higher means more vital
};

// Selected cargo and the totals calculated by a solver.
struct KnapsackResult {
    std::vector<SupplyItem> chosenItems;             // List of items loaded onto the truck
    int totalWeightKg;                               // Sum of weights of chosen items in kg
    int totalUrgency;                                // Sum of urgency scores of chosen items
    std::vector<std::vector<int>> dpTable;           // Full 2D DP matrix (rows: 0..n, cols: 0..W)
    std::vector<std::pair<int, int>> backtrackCells; // List of (i, w) table cells on optimal path
};

// Check that capacity and every item value are allowed.
void validateKnapsackInputs(const std::vector<SupplyItem>& items, int capacityKg);

// Find the best cargo set using a table of items and capacities.
KnapsackResult solveKnapsackDP(const std::vector<SupplyItem>& items,
                              int capacityKg,
                              bool recordTable = false);

// Check every possible item combination; mainly used to verify DP.
KnapsackResult solveKnapsackBruteForce(const std::vector<SupplyItem>& items,
                                       int capacityKg);

#endif // SAFEROUTE_KNAPSACK_H

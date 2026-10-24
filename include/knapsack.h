// knapsack.h — Relief truck loading algorithms for SafeRoute
//
// What this file does:
//   Declares algorithms for packing disaster supplies onto an emergency dispatch truck:
//   1. solveKnapsackDP: Uses 0/1 Dynamic Programming to find the exact optimal load
//      that maximizes delivered humanitarian urgency without exceeding the truck's weight limit.
//   2. solveKnapsackGreedy: Uses an urgency-to-weight ratio heuristic to approximate the load,
//      serving as an educational baseline to show why greedy fails on indivisible items.
//   3. solveKnapsackBruteForce: An exhaustive search checking all 2^n combinations,
//      used in automated unit tests to formally verify DP correctness on small instances.
//
// What is inside:
//   - SupplyItem struct: name, integer weight in kg, and priority urgency score.
//   - KnapsackResult struct: chosen items, total weight, total urgency, and optional DP table.
//   - Input validation function to guarantee safe capacity and item constraints.
//   - Function declarations for DP, Greedy, and Brute-force solvers.
//
// Other files that use this header:
//   - src/knapsack.cpp: implements the DP matrix, backtracking, greedy sort, and brute force.
//   - src/server.cpp: exposes /api/load-plan and /api/load-demo for the web frontend.
//   - tests/test_main.cpp: runs 9 automated tests comparing DP against greedy and brute-force.

// Include guard: prevents multiple inclusion errors during compilation.
#ifndef SAFEROUTE_KNAPSACK_H
#define SAFEROUTE_KNAPSACK_H

#include <string>
#include <vector>
#include <utility>
#include <stdexcept>

// ─── SupplyItem ──────────────────────────────────────────────────────────
// Represents an indivisible unit of emergency relief cargo.
struct SupplyItem {
    std::string name;       // Human-readable commodity name, e.g. "Medical trauma kit"
    int weightKg;           // Weight in whole kilograms (kg); must be strictly positive (> 0)
    int urgency;            // Priority urgency score (e.g. 10 to 100); higher means more vital
};

// ─── KnapsackResult ──────────────────────────────────────────────────────
// Complete manifest and metrics produced by either the DP or Greedy packing solver.
struct KnapsackResult {
    std::vector<SupplyItem> chosenItems;             // List of items loaded onto the truck
    int totalWeightKg;                               // Sum of weights of chosen items in kg
    int totalUrgency;                                // Sum of urgency scores of chosen items
    std::vector<std::vector<int>> dpTable;           // Full 2D DP matrix (rows: 0..n, cols: 0..W)
    std::vector<std::pair<int, int>> backtrackCells; // List of (i, w) table cells on optimal path
};

// ─── Input Validation ────────────────────────────────────────────────────
// Validates all inputs before algorithm execution to guarantee program safety.
// Checks:
//   - capacityKg must be between 0 and 5000 kg.
//   - item count must not exceed 200 items (bounds memory and response size).
//   - each item's weight must be strictly positive (> 0 kg).
//   - each item's urgency must be non-negative (>= 0).
//
// Input:  const std::vector<SupplyItem>& items, int capacityKg.
// Errors: Throws std::invalid_argument with a descriptive message if any check fails.
void validateKnapsackInputs(const std::vector<SupplyItem>& items, int capacityKg);

// ─── Dynamic Programming 0/1 Knapsack Solver ─────────────────────────────
// Finds the mathematically optimal cargo combination using 2D Dynamic Programming.
//
// How it works:
//   1. Builds table dp[i][w], where dp[i][w] is the maximum urgency possible using
//      a subset of the first i items under a weight limit of w kg.
//   2. For each item i and capacity w, evaluates two choices:
//        Option A (Skip): dp[i-1][w]
//        Option B (Take): dp[i-1][w - weight_i] + urgency_i (if weight_i <= w)
//      Takes max(Option A, Option B).
//   3. Backtracks from cell (n, capacityKg) to recover the exact set of chosen items.
//   4. If recordTable is true, preserves the full matrix and path for interactive display.
//
// Input:  const std::vector<SupplyItem>& items, int capacityKg, bool recordTable.
// Output: KnapsackResult containing optimal chosen items, weight, and urgency.
// Errors: Throws std::invalid_argument on invalid capacity or item properties.
// Time:   Theta(n * W) where n is item count and W is capacity in kg.
// Space:  Theta(n * W) for the 2D matrix used to reconstruct the manifest.
KnapsackResult solveKnapsackDP(const std::vector<SupplyItem>& items,
                              int capacityKg,
                              bool recordTable = false);

// ─── Greedy Knapsack Approximation ───────────────────────────────────────
// Fast heuristic that sorts items by urgency density (urgency / weightKg) descending.
//
// How it works:
//   1. Calculates density ratio r_i = urgency / weightKg for every item.
//   2. Sorts items in descending order of ratio.
//   3. Greedily loads items one by one as long as remaining truck capacity allows.
//
// Why it can fail on 0/1 knapsack:
//   Greedy is optimal for fractional items (like liquids or grains). But for indivisible
//   items, picking a small high-ratio item can leave awkward leftover capacity that
//   cannot fit larger high-value items (e.g. 50 kg truck: Greedy achieves 160 urgency,
//   while DP finds the optimal 220 urgency).
//
// Input:  const std::vector<SupplyItem>& items, int capacityKg.
// Output: KnapsackResult containing heuristic chosen items, weight, and urgency.
// Errors: Throws std::invalid_argument on invalid inputs.
// Time:   O(n log n) for sorting by ratio.
// Space:  O(n) for the sorted item array.
KnapsackResult solveKnapsackGreedy(const std::vector<SupplyItem>& items,
                                  int capacityKg);

// ─── Brute-Force Knapsack (for Test Verification) ────────────────────────
// Exhaustive recursive search testing all 2^n subsets to find the absolute maximum.
// Exclusively used in unit tests to formally verify that solveKnapsackDP produces
// the exact global optimum across random problem instances.
//
// Input:  const std::vector<SupplyItem>& items, int capacityKg.
// Output: KnapsackResult containing verified optimal manifest.
// Errors: Throws std::invalid_argument on invalid inputs.
// Time:   O(2^n) exponential — practical only for small test inputs (n <= 16).
// Space:  O(n) call stack recursion depth.
KnapsackResult solveKnapsackBruteForce(const std::vector<SupplyItem>& items,
                                       int capacityKg);

#endif // SAFEROUTE_KNAPSACK_H

// knapsack.cpp — Implementation of 0/1 knapsack algorithms for relief truck loading
//
// What this file does:
//   Implements cargo allocation algorithms to pack emergency supplies onto relief trucks:
//     1. validateKnapsackInputs: Enforces payload limits and item validity before execution.
//     2. solveKnapsackDP: Uses 0/1 Dynamic Programming to find the exact optimal combination
//        of supplies that maximizes urgency under the truck's weight limit.
//     3. solveKnapsackGreedy: Sorts items by urgency density (urgency / weight) to approximate
//        the load, demonstrating why greedy heuristics fail on indivisible 0/1 items.
//     4. solveKnapsackBruteForce: Exhaustively checks all 2^n subsets in unit tests to formally
//        prove that the DP algorithm achieves the true mathematical optimum.
//
// What is inside:
//   - validateKnapsackInputs: input safety checks.
//   - solveKnapsackDP: 2D table construction and backtracking path recovery.
//   - solveKnapsackGreedy: ratio sort and greedy selection loop.
//   - bruteForceHelper and solveKnapsackBruteForce: recursive test benchmark.
//
// Other files that use this file:
//   - Linked directly into the saferoute server executable and test_runner.

#include "../include/knapsack.h"

// C++ feature note:
// <algorithm> provides std::max, std::reverse, and std::sort.
// <stdexcept> provides std::invalid_argument for input error reporting.
#include <algorithm>
#include <stdexcept>

// ─── Input Validation ────────────────────────────────────────────────────

// Checks that truck payload and item parameters are valid before solving.
// Input:  const std::vector<SupplyItem>& items, int capacityKg.
// Errors: Throws std::invalid_argument on capacity out of range, too many items,
//         non-positive weights, or negative urgencies.
void validateKnapsackInputs(const std::vector<SupplyItem>& items, int capacityKg) {
    // Check truck capacity is within reasonable operational bounds (0 to 5000 kg)
    if (capacityKg < 0 || capacityKg > 5000) {
        throw std::invalid_argument("Truck capacity must be between 0 and 5000 kg, got: " +
                                    std::to_string(capacityKg));
    }

    // Limit item count to bound memory usage and JSON response size (max 200 items)
    if (items.size() > 200) {
        throw std::invalid_argument("Too many items to load: maximum is 200, got: " +
                                    std::to_string(items.size()));
    }

    // Verify properties of each individual supply item
    for (size_t i = 0; i < items.size(); ++i) {
        // Physical weight must be strictly positive
        if (items[i].weightKg <= 0) {
            throw std::invalid_argument("Item '" + items[i].name + "' has invalid weight: " +
                                        std::to_string(items[i].weightKg) + " kg. Weight must be positive.");
        }
        // Urgency score cannot be negative
        if (items[i].urgency < 0) {
            throw std::invalid_argument("Item '" + items[i].name + "' has negative urgency: " +
                                        std::to_string(items[i].urgency) + ". Urgency must be non-negative.");
        }
    }
}

// ─── Dynamic Programming 0/1 Knapsack Solver ─────────────────────────────

// Finds the optimal subset of supplies that maximizes total urgency without exceeding capacityKg.
//
// Algorithm explanation (for viva):
//   1. Meaning of dp[i][w]: The maximum urgency points achievable using a subset of the
//      first i items under a truck payload limit of w kilograms.
//   2. Table Size: (n + 1) rows by (capacityKg + 1) columns.
//      - Row 0 represents having 0 items available (all 0 urgency).
//      - Column 0 represents having 0 kg capacity available (all 0 urgency).
//   3. Recurrence Relation at cell (i, w):
//        Option A (Skip item i): dp[i-1][w]
//        Option B (Take item i): dp[i-1][w - weight_i] + urgency_i (only valid if weight_i <= w)
//      Formula: dp[i][w] = max(Option A, Option B)
//   4. Backtracking: Starting from the bottom-right corner (n, capacityKg):
//      - If dp[currI][currW] != dp[currI - 1][currW], item currI was selected!
//        We add it to the chosen manifest, deduct its weight from currW, and move to currI - 1.
//      - Otherwise, item currI was skipped; we simply move to currI - 1.
//   5. Complexity:
//      - Time: Theta(n * capacityKg) — each cell takes O(1) arithmetic operations.
//      - Space: Theta(n * capacityKg) — needed to store the table for backtracking.
//      - This is "pseudo-polynomial" time: polynomial in the numeric value of capacityKg,
//        but exponential in the number of bits required to write capacityKg.
//
// Input:  items vector, capacityKg, recordTable flag (true if table is needed by UI).
// Output: KnapsackResult struct containing chosen items, total weight, urgency, and table.
// Errors: Throws std::invalid_argument if inputs violate constraints.
KnapsackResult solveKnapsackDP(const std::vector<SupplyItem>& items,
                              int capacityKg,
                              bool recordTable) {
    validateKnapsackInputs(items, capacityKg);

    KnapsackResult result;
    result.totalWeightKg = 0;
    result.totalUrgency = 0;

    int n = static_cast<int>(items.size());

    // Edge case: truck has 0 kg capacity or there are 0 items in catalogue
    if (capacityKg == 0 || n == 0) {
        if (recordTable) {
            result.dpTable = std::vector<std::vector<int>>(n + 1, std::vector<int>(capacityKg + 1, 0));
            result.backtrackCells = {{n, 0}};
        }
        return result;
    }

    // Step 1: Allocate the 2D DP matrix of size (n + 1) x (capacityKg + 1) filled with zeros
    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(capacityKg + 1, 0));

    // Step 2: Fill the table row by row (item by item)
    for (int i = 1; i <= n; ++i) {
        const auto& item = items[i - 1]; // items vector is 0-indexed, so item i is items[i-1]
        for (int w = 0; w <= capacityKg; ++w) {
            // Option A: Skip this item, keeping the best result using previous (i-1) items
            int skipUrgency = dp[i - 1][w];

            // Option B: Take this item if it weighs less than or equal to current capacity w
            int takeUrgency = 0;
            if (item.weightKg <= w) {
                takeUrgency = dp[i - 1][w - item.weightKg] + item.urgency;
            }

            // Greedy choice property: take whichever decision yields higher urgency
            dp[i][w] = std::max(skipUrgency, takeUrgency);
        }
    }

    result.totalUrgency = dp[n][capacityKg];

    // Step 3: Backtrack from (n, capacityKg) to recover the exact chosen items
    int currW = capacityKg;
    int currI = n;
    std::vector<std::pair<int, int>> path;
    path.push_back({currI, currW});

    while (currI > 0 && currW > 0) {
        // If current cell differs from the cell above it, item currI was included in optimal load
        if (dp[currI][currW] != dp[currI - 1][currW]) {
            const auto& chosen = items[currI - 1];
            result.chosenItems.push_back(chosen);
            result.totalWeightKg += chosen.weightKg;
            currW -= chosen.weightKg; // Deduct weight to find remaining capacity
        }
        currI--;
        path.push_back({currI, currW});
    }

    // Reverse chosen items so they match the order of the original catalogue
    std::reverse(result.chosenItems.begin(), result.chosenItems.end());

    // Step 4: If requested, store the full table and backtrack path for educational UI display
    if (recordTable) {
        result.dpTable = std::move(dp);
        result.backtrackCells = std::move(path);
    }

    return result;
}

// ─── Greedy Knapsack Approximation ───────────────────────────────────────

// Approximates the knapsack problem by picking items with the highest urgency per kilogram.
//
// Why Greedy fails on 0/1 knapsack (viva explanation):
//   In the fractional knapsack problem, you can cut items (like pouring 2.5 kg of flour).
//   In 0/1 knapsack, each item is indivisible. A greedy approach chooses items with high
//   ratio first, but may leave large leftover space that cannot fit remaining items.
//   Canonical Example:
//     Truck capacity = 50 kg.
//     - Medical kit:    10 kg, Urgency 60  -> ratio = 6.0 pt/kg
//     - Blanket bundle: 20 kg, Urgency 100 -> ratio = 5.0 pt/kg
//     - Water crate:    30 kg, Urgency 120 -> ratio = 4.0 pt/kg
//     Greedy picks Med kit (10 kg, 60), leaving 40 kg. Next picks Blankets (20 kg, 100),
//     leaving 20 kg. Water crate (30 kg) cannot fit!
//     Total Greedy: 30 kg, Urgency 160.
//     DP picks Blankets (20 kg) + Water (30 kg) = 50 kg exact, Urgency 220 (+60 gain!).
//
// Input:  const std::vector<SupplyItem>& items, int capacityKg.
// Output: KnapsackResult struct containing greedy chosen items, weight, and urgency.
// Time:   O(n log n) for sorting.
// Space:  O(n) for the sorted item list.
KnapsackResult solveKnapsackGreedy(const std::vector<SupplyItem>& items,
                                  int capacityKg) {
    validateKnapsackInputs(items, capacityKg);

    KnapsackResult result;
    result.totalWeightKg = 0;
    result.totalUrgency = 0;

    if (capacityKg == 0 || items.empty()) {
        return result;
    }

    // Temporary struct tagging each item with its efficiency ratio and original index
    struct GreedyItem {
        SupplyItem item;
        double ratio;
        int originalIndex;
    };

    // Calculate urgency-to-weight ratio for each item
    std::vector<GreedyItem> sortedList;
    sortedList.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        double r = static_cast<double>(items[i].urgency) / static_cast<double>(items[i].weightKg);
        sortedList.push_back({items[i], r, static_cast<int>(i)});
    }

    // Sort descending by ratio. Break ties by higher urgency, then lower weight
    std::sort(sortedList.begin(), sortedList.end(), [](const GreedyItem& a, const GreedyItem& b) {
        if (std::abs(a.ratio - b.ratio) > 1e-9) {
            return a.ratio > b.ratio;
        }
        if (a.item.urgency != b.item.urgency) {
            return a.item.urgency > b.item.urgency;
        }
        return a.item.weightKg < b.item.weightKg;
    });

    // Greedy loading loop: take items in ratio order while they fit in remaining capacity
    int remainingCapacity = capacityKg;
    for (const auto& g : sortedList) {
        if (g.item.weightKg <= remainingCapacity) {
            result.chosenItems.push_back(g.item);
            result.totalWeightKg += g.item.weightKg;
            result.totalUrgency += g.item.urgency;
            remainingCapacity -= g.item.weightKg;
        }
    }

    return result;
}

// ─── Brute-Force Knapsack (for Test Verification) ────────────────────────

// Recursive helper for brute-force knapsack. Tests both decisions (skip or take)
// at each item index, exploring all 2^n branches of the decision tree.
static void bruteForceHelper(const std::vector<SupplyItem>& items,
                             int index,
                             int currentWeight,
                             int currentUrgency,
                             int capacity,
                             std::vector<SupplyItem>& currentChosen,
                             int& maxUrgency,
                             std::vector<SupplyItem>& bestChosen,
                             int& bestWeight) {
    // Base case: all items evaluated
    if (index == static_cast<int>(items.size())) {
        if (currentUrgency > maxUrgency) {
            maxUrgency = currentUrgency;
            bestChosen = currentChosen;
            bestWeight = currentWeight;
        }
        return;
    }

    // Branch 1: Skip item at index
    bruteForceHelper(items, index + 1, currentWeight, currentUrgency, capacity,
                     currentChosen, maxUrgency, bestChosen, bestWeight);

    // Branch 2: Take item at index if it fits in remaining capacity
    if (currentWeight + items[index].weightKg <= capacity) {
        currentChosen.push_back(items[index]);
        bruteForceHelper(items, index + 1, currentWeight + items[index].weightKg,
                         currentUrgency + items[index].urgency, capacity,
                         currentChosen, maxUrgency, bestChosen, bestWeight);
        currentChosen.pop_back(); // Backtrack to explore other branches
    }
}

// Exhaustive 2^n solver used exclusively in test_main.cpp to prove DP optimality.
// Input:  const std::vector<SupplyItem>& items, int capacityKg.
// Output: KnapsackResult with exact verified optimum.
// Time:   O(2^n) exponential time.
// Space:  O(n) recursion call stack depth.
KnapsackResult solveKnapsackBruteForce(const std::vector<SupplyItem>& items,
                                       int capacityKg) {
    validateKnapsackInputs(items, capacityKg);

    KnapsackResult result;
    result.totalWeightKg = 0;
    result.totalUrgency = 0;

    int maxUrgency = -1;
    std::vector<SupplyItem> currentChosen;
    std::vector<SupplyItem> bestChosen;
    int bestWeight = 0;

    bruteForceHelper(items, 0, 0, 0, capacityKg, currentChosen, maxUrgency, bestChosen, bestWeight);

    result.chosenItems = bestChosen;
    result.totalWeightKg = bestWeight;
    result.totalUrgency = std::max(0, maxUrgency);
    return result;
}

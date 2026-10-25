// This file validates loading inputs and solves 0/1 Knapsack.

#include "../include/knapsack.h"

// These headers provide common algorithms and input errors.
#include <algorithm>
#include <stdexcept>

// ─── Input Validation ────────────────────────────────────────────────────

// Stop invalid capacities and item values before solving.
void validateKnapsackInputs(const std::vector<SupplyItem>& items, int capacityKg) {
    // Keep the capacity within the supported range.
    if (capacityKg < 0 || capacityKg > 5000) {
        throw std::invalid_argument("Truck capacity must be between 0 and 5000 kg, got: " +
                                    std::to_string(capacityKg));
    }

    // Limit the number of items.
    if (items.size() > 200) {
        throw std::invalid_argument("Too many items to load: maximum is 200, got: " +
                                    std::to_string(items.size()));
    }

    // Check each item's weight and urgency.
    for (size_t i = 0; i < items.size(); ++i) {
        // Every item must have positive weight.
        if (items[i].weightKg <= 0) {
            throw std::invalid_argument("Item '" + items[i].name + "' has invalid weight: " +
                                        std::to_string(items[i].weightKg) + " kg. Weight must be positive.");
        }
        // Urgency cannot be negative.
        if (items[i].urgency < 0) {
            throw std::invalid_argument("Item '" + items[i].name + "' has negative urgency: " +
                                        std::to_string(items[i].urgency) + ". Urgency must be non-negative.");
        }
    }
}

// ─── Dynamic Programming 0/1 Knapsack Solver ─────────────────────────────

// Use a table to find the highest urgency that fits the truck.
KnapsackResult solveKnapsackDP(const std::vector<SupplyItem>& items,
                              int capacityKg,
                              bool recordTable) {
    validateKnapsackInputs(items, capacityKg);

    KnapsackResult result;
    result.totalWeightKg = 0;
    result.totalUrgency = 0;

    int n = static_cast<int>(items.size());

    // Return an empty load when nothing can be packed.
    if (capacityKg == 0 || n == 0) {
        if (recordTable) {
            result.dpTable = std::vector<std::vector<int>>(n + 1, std::vector<int>(capacityKg + 1, 0));
            result.backtrackCells = {{n, 0}};
        }
        return result;
    }

    // Each row adds one item; each column represents a capacity.
    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(capacityKg + 1, 0));

    // Compute the best urgency for each item count and capacity.
    for (int i = 1; i <= n; ++i) {
        const auto& item = items[i - 1]; // Convert table row to item index.
        for (int w = 0; w <= capacityKg; ++w) {
            // Option one: leave this item out.
            int skipUrgency = dp[i - 1][w];

            // Option two: add this item when it fits.
            int takeUrgency = 0;
            if (item.weightKg <= w) {
                takeUrgency = dp[i - 1][w - item.weightKg] + item.urgency;
            }

            // Store the better of taking or skipping the item.
            dp[i][w] = std::max(skipUrgency, takeUrgency);
        }
    }

    result.totalUrgency = dp[n][capacityKg];

    // Walk backward through the table to find selected items.
    int currW = capacityKg;
    int currI = n;
    std::vector<std::pair<int, int>> path;
    path.push_back({currI, currW});

    while (currI > 0 && currW > 0) {
        // A changed value means this item was selected.
        if (dp[currI][currW] != dp[currI - 1][currW]) {
            const auto& chosen = items[currI - 1];
            result.chosenItems.push_back(chosen);
            result.totalWeightKg += chosen.weightKg;
            currW -= chosen.weightKg; // Remove its weight from remaining capacity.
        }
        currI--;
        path.push_back({currI, currW});
    }

    // Restore the original item order.
    std::reverse(result.chosenItems.begin(), result.chosenItems.end());

    // Save the table only when the caller needs to display it.
    if (recordTable) {
        result.dpTable = std::move(dp);
        result.backtrackCells = std::move(path);
    }

    return result;
}

// ─── Brute-Force Knapsack (for Test Verification) ────────────────────────

// Try every valid combination by branching on each item.
static void bruteForceHelper(const std::vector<SupplyItem>& items,
                             int index,
                             int currentWeight,
                             int currentUrgency,
                             int capacity,
                             std::vector<SupplyItem>& currentChosen,
                             int& maxUrgency,
                             std::vector<SupplyItem>& bestChosen,
                             int& bestWeight) {
    // Save this combination after checking every item.
    if (index == static_cast<int>(items.size())) {
        if (currentUrgency > maxUrgency) {
            maxUrgency = currentUrgency;
            bestChosen = currentChosen;
            bestWeight = currentWeight;
        }
        return;
    }

    // First try leaving this item out.
    bruteForceHelper(items, index + 1, currentWeight, currentUrgency, capacity,
                     currentChosen, maxUrgency, bestChosen, bestWeight);

    // Then try taking it if it fits.
    if (currentWeight + items[index].weightKg <= capacity) {
        currentChosen.push_back(items[index]);
        bruteForceHelper(items, index + 1, currentWeight + items[index].weightKg,
                         currentUrgency + items[index].urgency, capacity,
                         currentChosen, maxUrgency, bestChosen, bestWeight);
        currentChosen.pop_back(); // Undo the choice before trying another combination.
    }
}

// Return the best load found by checking every combination.
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

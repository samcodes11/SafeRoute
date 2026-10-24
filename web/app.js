/*
 * app.js — Frontend Controller for SafeRoute
 *
 * Implements the interactive disaster evacuation planner:
 * - Responsive SVG road network map with 8-direction collision-free label placement
 * - Pan and zoom controls (+, -, reset, and mouse drag-to-pan)
 * - Muted formal facility color palette & custom SVG facility shapes
 * - Seeded disaster scenario generator (Light, Moderate, Severe; default Moderate/2026)
 * - Multi-source shelter coverage view
 * - Nearest relief services grouping with demo contact links and direct routing
 * - Step-by-step Dijkstra min-heap execution stepper visualizer
 * - Emergency Numbers directory rendered from web/emergency.json
 * - Printable evacuation plan (window.print() support)
 */

// ─── Theme & Facility Configuration ──────────────────────────────
const FACILITY_CONFIG = {
  village:     { label: "Village",         color: "#5B7FA3", shape: "circle" },
  junction:    { label: "Junction",        color: "#9C8B78", shape: "diamond" },
  hospital:    { label: "Hospital",        color: "#B8473D", shape: "hospital" },
  shelter:     { label: "Shelter",         color: "#5E8C61", shape: "shelter" },
  ngo:         { label: "NGO Camp",        color: "#8A5A7B", shape: "ngo" },
  food:        { label: "Food Centre",     color: "#D1A027", shape: "food" },
  water:       { label: "Water Point",     color: "#2E8B8B", shape: "water" },
  fire_station:{ label: "Fire & Rescue",   color: "#E0912B", shape: "fire" },
  police:      { label: "Police Station",  color: "#3B4D75", shape: "police" }
};

// ─── Application State ───────────────────────────────────────────
const state = {
  nodes: new Map(),             // id -> Node
  roads: new Map(),             // id -> Road
  standardReasons: [],          // array of strings
  activeFilters: new Set(Object.keys(FACILITY_CONFIG)), // visible types
  selectedSource: "",
  selectedDest: "auto",
  currentComparison: null,
  coverageActive: false,
  coverageData: null,
  showAllDistances: false,
  hoveredRoadId: null,

  // Pan & Zoom tracking
  baseVb: { x: 0, y: 0, w: 1000, h: 620 },
  curVb:  { x: 0, y: 0, w: 1000, h: 620 },
  isPanning: false,
  panStart: { x: 0, y: 0 },

  // Dijkstra Stepper
  stepperTrace: null,
  stepperIndex: 0,
  stepperTimer: null,

  // Relief Loading (0/1 Knapsack) State
  suppliesData: null,
  currentLoadPlan: null,
  currentCapacity: 200
};

// ─── DOM References Cache ────────────────────────────────────────
const dom = {};

window.addEventListener("DOMContentLoaded", () => {
  cacheDomElements();
  setupNavigationTabs();
  setupZoomAndPan();
  setupEventListeners();
  loadInitialData();
});

// Cache elements by ID
function cacheDomElements() {
  dom.mapSvg = document.getElementById("map-svg");
  dom.mapViewport = document.getElementById("map-viewport");
  dom.layerRoads = document.getElementById("layer-roads");
  dom.layerRoutesOverlay = document.getElementById("layer-routes-overlay");
  dom.layerRoadMarkers = document.getElementById("layer-road-markers");
  dom.layerRoadDistances = document.getElementById("layer-road-distances");
  dom.layerNodes = document.getElementById("layer-nodes");
  dom.layerNodeLabels = document.getElementById("layer-node-labels");

  dom.filterChipsContainer = document.getElementById("filter-chips-container");
  dom.toggleCoverageView = document.getElementById("toggle-coverage-view");
  dom.toggleAllDistances = document.getElementById("toggle-all-distances");
  dom.coverageSummaryPanel = document.getElementById("coverage-summary-panel");
  dom.coverageStatsText = document.getElementById("coverage-stats-text");
  dom.coverageCutoffNames = document.getElementById("coverage-cutoff-names");

  dom.selectSource = document.getElementById("select-source");
  dom.selectDest = document.getElementById("select-dest");
  dom.checkEnsureDetour = document.getElementById("check-ensure-detour");
  dom.btnCalculateRoute = document.getElementById("btn-calculate-route");
  dom.btnClearRoute = document.getElementById("btn-clear-route");
  dom.btnOpenStepper = document.getElementById("btn-open-stepper");
  dom.btnPrintPlan = document.getElementById("btn-print-plan");
  dom.btnResetNetwork = document.getElementById("btn-reset-network");

  dom.selectSeverity = document.getElementById("select-severity");
  dom.inputScenarioSeed = document.getElementById("input-scenario-seed");
  dom.btnApplyScenario = document.getElementById("btn-apply-scenario");
  dom.btnClearScenario = document.getElementById("btn-clear-scenario");

  dom.activeObstructionsList = document.getElementById("active-obstructions-list");
  dom.obstructionCountBadge = document.getElementById("obstruction-count-badge");
  dom.selectManualRoad = document.getElementById("select-manual-road");
  dom.selectManualReason = document.getElementById("select-manual-reason");
  dom.btnManualBlock = document.getElementById("btn-manual-block");

  dom.nearestServicesContainer = document.getElementById("nearest-services-container");

  dom.resultsEmptyState = document.getElementById("results-empty-state");
  dom.resultsContentPanel = document.getElementById("results-content-panel");
  dom.routeMetaLabel = document.getElementById("route-meta-label");
  dom.disruptionAlertBanner = document.getElementById("disruption-alert-banner");
  dom.disruptionBannerText = document.getElementById("disruption-banner-text");
  dom.unreachableAlertBanner = document.getElementById("unreachable-alert-banner");
  dom.metricOrigDist = document.getElementById("metric-orig-dist");
  dom.metricRevDist = document.getElementById("metric-rev-dist");
  dom.metricExtraKm = document.getElementById("metric-extra-km");
  dom.metricExtraPct = document.getElementById("metric-extra-pct");
  dom.metricRouteBlocks = document.getElementById("metric-route-blocks");
  dom.metricTotalBlocks = document.getElementById("metric-total-blocks");
  dom.metricBoxRevised = document.getElementById("metric-box-revised");
  dom.pathOriginalChain = document.getElementById("path-original-chain");
  dom.pathRevisedChain = document.getElementById("path-revised-chain");
  dom.revisedChainContainer = document.getElementById("revised-chain-container");
  dom.blockedOnRouteSection = document.getElementById("blocked-on-route-section");
  dom.blockedOnRouteList = document.getElementById("blocked-on-route-list");

  // Stepper Modal
  dom.stepperModal = document.getElementById("stepper-modal");
  dom.btnCloseStepper = document.getElementById("btn-close-stepper");
  dom.btnStepPrev = document.getElementById("btn-step-prev");
  dom.btnStepPlay = document.getElementById("btn-step-play");
  dom.btnStepNext = document.getElementById("btn-step-next");
  dom.btnStepReset = document.getElementById("btn-step-reset");
  dom.sliderStepSpeed = document.getElementById("slider-step-speed");
  dom.labelStepCounter = document.getElementById("label-step-counter");
  dom.stepperStepDesc = document.getElementById("stepper-step-desc");
  dom.stepperPqList = document.getElementById("stepper-pq-list");
  dom.stepperDistTable = document.getElementById("stepper-dist-table");

  dom.toast = document.getElementById("toast");
  dom.printMetaTimestamp = document.getElementById("print-meta-timestamp");

  // Navigation Tabs & Panes
  dom.tabBtnPlanner = document.getElementById("tab-btn-planner");
  dom.tabBtnLoading = document.getElementById("tab-btn-loading");
  dom.tabBtnEmergency = document.getElementById("tab-btn-emergency");
  dom.panePlanner = document.getElementById("pane-planner");
  dom.paneLoading = document.getElementById("pane-loading");
  dom.paneEmergency = document.getElementById("pane-emergency");

  // Relief Loading (Knapsack)
  dom.selectLoadDepot = document.getElementById("select-load-depot");
  dom.selectLoadVillage = document.getElementById("select-load-village");
  dom.labelVillageTotalNeed = document.getElementById("label-village-total-need");
  dom.presetBtns = document.querySelectorAll(".preset-btn");
  dom.sliderTruckCapacity = document.getElementById("slider-truck-capacity");
  dom.labelCapacityValue = document.getElementById("label-capacity-value");
  dom.btnPlanLoading = document.getElementById("btn-plan-loading");
  dom.btnShowDemo = document.getElementById("btn-show-demo");
  dom.btnCloseDemo = document.getElementById("btn-close-demo");

  dom.cardTripSummary = document.getElementById("card-trip-summary");
  dom.loadingRouteStatus = document.getElementById("loading-route-status");
  dom.loadingTripMetrics = document.getElementById("loading-trip-metrics");
  dom.loadingTripWaypoints = document.getElementById("loading-trip-waypoints");
  dom.btnViewRouteOnMap = document.getElementById("btn-view-route-on-map");

  dom.loadingEmptyState = document.getElementById("loading-empty-state");
  dom.loadingUnreachableBanner = document.getElementById("loading-unreachable-banner");
  dom.loadingUnreachableText = document.getElementById("loading-unreachable-text");
  dom.loadingResultsContainer = document.getElementById("loading-results-container");
  dom.urgencyDeltaBanner = document.getElementById("urgency-delta-banner");

  dom.dpTotalUrgency = document.getElementById("dp-total-urgency");
  dom.dpWeightRatio = document.getElementById("dp-weight-ratio");
  dom.dpWeightBar = document.getElementById("dp-weight-bar");
  dom.dpWeightPct = document.getElementById("dp-weight-pct");
  dom.dpItemCount = document.getElementById("dp-item-count");
  dom.dpManifestTable = document.getElementById("dp-manifest-table");

  dom.greedyTotalUrgency = document.getElementById("greedy-total-urgency");
  dom.greedyWeightRatio = document.getElementById("greedy-weight-ratio");
  dom.greedyWeightBar = document.getElementById("greedy-weight-bar");
  dom.greedyWeightPct = document.getElementById("greedy-weight-pct");
  dom.greedyItemCount = document.getElementById("greedy-item-count");
  dom.greedyManifestTable = document.getElementById("greedy-manifest-table");

  dom.cardDemoContainer = document.getElementById("card-demo-container");
  dom.demoDpTableContainer = document.getElementById("demo-dp-table-container");
  dom.printReliefSection = document.getElementById("print-relief-section");
}

// ─── Toast Notification Helper ───────────────────────────────────
let toastTimer = null;
function showToast(msg) {
  if (toastTimer) clearTimeout(toastTimer);
  dom.toast.textContent = msg;
  dom.toast.classList.add("visible");
  toastTimer = setTimeout(() => {
    dom.toast.classList.remove("visible");
  }, 4000);
}

// ─── Navigation Tabs ─────────────────────────────────────────────
function setupNavigationTabs() {
  const switchTab = (tabName) => {
    dom.tabBtnPlanner.classList.toggle("active", tabName === "planner");
    dom.tabBtnLoading.classList.toggle("active", tabName === "loading");
    dom.tabBtnEmergency.classList.toggle("active", tabName === "emergency");

    dom.panePlanner.classList.toggle("active", tabName === "planner");
    dom.paneLoading.classList.toggle("active", tabName === "loading");
    dom.paneEmergency.classList.toggle("active", tabName === "emergency");

    if (tabName === "loading") {
      loadSuppliesData();
    } else if (tabName === "emergency") {
      loadEmergencyData();
    }
  };

  dom.tabBtnPlanner.addEventListener("click", () => switchTab("planner"));
  dom.tabBtnLoading.addEventListener("click", () => switchTab("loading"));
  dom.tabBtnEmergency.addEventListener("click", () => switchTab("emergency"));
}

// ─── Zoom & Pan on SVG Map ───────────────────────────────────────
function setupZoomAndPan() {
  const updateVb = () => {
    dom.mapSvg.setAttribute("viewBox", `${state.curVb.x} ${state.curVb.y} ${state.curVb.w} ${state.curVb.h}`);
  };

  document.getElementById("btn-zoom-in").addEventListener("click", () => {
    const factor = 0.8;
    const nw = state.curVb.w * factor;
    const nh = state.curVb.h * factor;
    state.curVb.x += (state.curVb.w - nw) / 2;
    state.curVb.y += (state.curVb.h - nh) / 2;
    state.curVb.w = nw;
    state.curVb.h = nh;
    updateVb();
  });

  document.getElementById("btn-zoom-out").addEventListener("click", () => {
    const factor = 1.25;
    const nw = state.curVb.w * factor;
    const nh = state.curVb.h * factor;
    state.curVb.x -= (nw - state.curVb.w) / 2;
    state.curVb.y -= (nh - state.curVb.h) / 2;
    state.curVb.w = nw;
    state.curVb.h = nh;
    updateVb();
  });

  document.getElementById("btn-zoom-reset").addEventListener("click", () => {
    state.curVb = { ...state.baseVb };
    updateVb();
  });

  // Drag-to-pan implementation
  dom.mapSvg.addEventListener("mousedown", (e) => {
    // Only left click drags
    if (e.button !== 0) return;
    state.isPanning = true;
    state.panStart = { x: e.clientX, y: e.clientY };
  });

  window.addEventListener("mousemove", (e) => {
    if (!state.isPanning) return;
    const dx = e.clientX - state.panStart.x;
    const dy = e.clientY - state.panStart.y;
    state.panStart = { x: e.clientX, y: e.clientY };

    const svgRect = dom.mapSvg.getBoundingClientRect();
    const scaleX = state.curVb.w / svgRect.width;
    const scaleY = state.curVb.h / svgRect.height;

    state.curVb.x -= dx * scaleX;
    state.curVb.y -= dy * scaleY;
    updateVb();
  });

  window.addEventListener("mouseup", () => {
    state.isPanning = false;
  });
}

// ─── Setup Event Listeners ───────────────────────────────────────
function setupEventListeners() {
  dom.selectSource.addEventListener("change", handleSourceChange);
  dom.selectDest.addEventListener("change", handleDestChange);
  dom.btnCalculateRoute.addEventListener("click", calculateRoute);
  dom.btnClearRoute.addEventListener("click", clearSelections);
  dom.btnResetNetwork.addEventListener("click", resetAllRoads);

  dom.btnApplyScenario.addEventListener("click", applyScenario);
  dom.btnClearScenario.addEventListener("click", resetAllRoads);
  dom.btnManualBlock.addEventListener("click", handleManualBlock);

  dom.toggleCoverageView.addEventListener("change", handleCoverageToggle);
  dom.toggleAllDistances.addEventListener("change", (e) => {
    state.showAllDistances = e.target.checked;
    renderRoadDistances();
  });

  dom.btnOpenStepper.addEventListener("click", openStepperModal);
  dom.btnCloseStepper.addEventListener("click", closeStepperModal);
  dom.btnStepNext.addEventListener("click", stepForward);
  dom.btnStepPrev.addEventListener("click", stepBackward);
  dom.btnStepPlay.addEventListener("click", toggleStepPlay);
  dom.btnStepReset.addEventListener("click", resetStepper);

  dom.btnPrintPlan.addEventListener("click", () => {
    dom.printMetaTimestamp.textContent = `Printed on ${new Date().toLocaleString()} | SafeRoute Academic Prototype`;
    renderPrintReliefSection();
    window.print();
  });

  // Relief Loading (0/1 Knapsack) listeners
  dom.presetBtns.forEach(btn => {
    btn.addEventListener("click", () => {
      dom.presetBtns.forEach(b => b.classList.remove("active"));
      btn.classList.add("active");
      const cap = parseInt(btn.dataset.cap, 10);
      dom.sliderTruckCapacity.value = cap;
      dom.labelCapacityValue.textContent = `${cap} kg`;
      state.currentCapacity = cap;
      updateCapacityDisplays(cap);
    });
  });

  dom.sliderTruckCapacity.addEventListener("input", (e) => {
    const cap = parseInt(e.target.value, 10);
    dom.labelCapacityValue.textContent = `${cap} kg`;
    state.currentCapacity = cap;
    dom.presetBtns.forEach(b => {
      b.classList.toggle("active", parseInt(b.dataset.cap, 10) === cap);
    });
    updateCapacityDisplays(cap);
  });

  dom.selectLoadVillage.addEventListener("change", handleVillageNeedUpdate);
  dom.btnPlanLoading.addEventListener("click", calculateLoadPlan);
  dom.btnShowDemo.addEventListener("click", showKnapsackDemo);
  dom.btnCloseDemo.addEventListener("click", () => {
    dom.cardDemoContainer.style.display = "none";
  });
  dom.btnViewRouteOnMap.addEventListener("click", showTripOnMap);
}

// ─── Initial Network Ingestion ───────────────────────────────────
async function loadInitialData() {
  try {
    const res = await fetch("/api/network");
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();

    state.nodes.clear();
    state.roads.clear();
    state.standardReasons = data.reasons || [];

    data.nodes.forEach(n => state.nodes.set(n.id, n));
    data.roads.forEach(r => state.roads.set(r.id, r));

    buildFilterChips();
    populateDropdowns();

    // Default Seeded Disaster Scenario on page load: Moderate with seed 2026
    await applyScenarioDefault(2026);

  } catch (err) {
    showToast("Error connecting to SafeRoute backend: " + err.message);
  }
}

// ─── Populate Select Controls ────────────────────────────────────
function populateDropdowns() {
  // 1. Source selector
  dom.selectSource.innerHTML = '<option value="">&mdash; Choose Affected Place &mdash;</option>';
  const sortedNodes = Array.from(state.nodes.values()).sort((a, b) => a.name.localeCompare(b.name));

  sortedNodes.forEach(node => {
    const opt = document.createElement("option");
    opt.value = node.id;
    opt.textContent = `${node.name} (${node.id.toUpperCase()} - ${FACILITY_CONFIG[node.type]?.label || node.type})`;
    dom.selectSource.appendChild(opt);
  });

  // 2. Destination selector (Shelters)
  dom.selectDest.innerHTML = '<option value="auto">Auto (Nearest Open Shelter)</option>';
  sortedNodes
    .filter(n => n.type === "shelter")
    .forEach(shelter => {
      const opt = document.createElement("option");
      opt.value = shelter.id;
      opt.textContent = `${shelter.name} (${shelter.id.toUpperCase()})`;
      dom.selectDest.appendChild(opt);
    });

  // 3. Manual Road block dropdowns
  dom.selectManualRoad.innerHTML = '<option value="">&mdash; Select Road Segment &mdash;</option>';
  const sortedRoads = Array.from(state.roads.values()).sort((a, b) => a.id.localeCompare(b.id, undefined, { numeric: true }));
  sortedRoads.forEach(r => {
    const fromName = state.nodes.get(r.from)?.name || r.from;
    const toName = state.nodes.get(r.to)?.name || r.to;
    const opt = document.createElement("option");
    opt.value = r.id;
    opt.textContent = `${r.id.toUpperCase()}: ${fromName} <-> ${toName} (${r.distance} km)`;
    dom.selectManualRoad.appendChild(opt);
  });

  // 4. Reasons dropdown
  dom.selectManualReason.innerHTML = "";
  state.standardReasons.forEach(rsn => {
    const opt = document.createElement("option");
    opt.value = rsn;
    opt.textContent = rsn;
    dom.selectManualReason.appendChild(opt);
  });
}

// ─── Filter Chips Component ──────────────────────────────────────
function buildFilterChips() {
  dom.filterChipsContainer.innerHTML = "";
  for (const [typeKey, cfg] of Object.entries(FACILITY_CONFIG)) {
    const chip = document.createElement("div");
    chip.className = "filter-chip active";
    chip.dataset.type = typeKey;
    chip.innerHTML = `<span class="chip-dot" style="background:${cfg.color}"></span> ${cfg.label}`;

    chip.addEventListener("click", () => {
      if (state.activeFilters.has(typeKey)) {
        state.activeFilters.delete(typeKey);
        chip.classList.remove("active");
      } else {
        state.activeFilters.add(typeKey);
        chip.classList.add("active");
      }
      renderNodesAndLabels();
    });

    dom.filterChipsContainer.appendChild(chip);
  }
}

// ─── SVG Helper ──────────────────────────────────────────────────
const SVG_NS = "http://www.w3.org/2000/svg";
function createSvg(tag, attrs = {}) {
  const el = document.createElementNS(SVG_NS, tag);
  for (const [k, v] of Object.entries(attrs)) {
    el.setAttribute(k, v);
  }
  return el;
}

// ─── Main Map Renderer ───────────────────────────────────────────
function renderMap() {
  renderRoadLines();
  renderRoadDistances();
  renderNodesAndLabels();
  renderRouteOverlays();
  updateActiveObstructionsUI();
}

// Draw roads
function renderRoadLines() {
  dom.layerRoads.innerHTML = "";
  dom.layerRoadMarkers.innerHTML = "";

  state.roads.forEach(road => {
    const u = state.nodes.get(road.from);
    const v = state.nodes.get(road.to);
    if (!u || !v) return;

    const line = createSvg("line", {
      x1: u.x, y1: u.y,
      x2: v.x, y2: v.y,
      class: road.blocked ? "road-line blocked" : "road-line",
      "data-road-id": road.id
    });

    // Midpoint coordinates
    const midX = (u.x + v.x) / 2;
    const midY = (u.y + v.y) / 2;

    // Hover tooltip
    const title = createSvg("title");
    title.textContent = `${road.id.toUpperCase()}: ${u.name} <-> ${v.name} (${road.distance} km)${road.blocked ? ' [BLOCKED: ' + road.reason + ']' : ''}`;
    line.appendChild(title);

    // Click to toggle obstruction
    line.addEventListener("click", () => toggleRoadBlock(road.id));

    line.addEventListener("mouseenter", () => {
      state.hoveredRoadId = road.id;
      renderRoadDistances();
    });
    line.addEventListener("mouseleave", () => {
      state.hoveredRoadId = null;
      renderRoadDistances();
    });

    dom.layerRoads.appendChild(line);

    // Warning triangle badge on blocked roads
    if (road.blocked) {
      const marker = createSvg("polygon", {
        points: `${midX},${midY - 9} ${midX + 8},${midY + 6} ${midX - 8},${midY + 6}`,
        fill: "#A8281E",
        stroke: "#FBF6EC",
        "stroke-width": 1.5,
        cursor: "pointer"
      });
      const excl = createSvg("text", {
        x: midX,
        y: midY + 4,
        "font-size": "10",
        "font-weight": "bold",
        fill: "#FFFFFF",
        "text-anchor": "middle",
        "pointer-events": "none"
      });
      excl.textContent = "!";
      marker.addEventListener("click", () => toggleRoadBlock(road.id));
      dom.layerRoadMarkers.appendChild(marker);
      dom.layerRoadMarkers.appendChild(excl);
    }
  });
}

// Distance labels on roads
function renderRoadDistances() {
  dom.layerRoadDistances.innerHTML = "";

  // Identify roads on active routes
  const routeRoads = new Set();
  if (state.currentComparison) {
    const comp = state.currentComparison;
    const collectPathRoads = (pathArr) => {
      if (!pathArr) return;
      for (let i = 0; i < pathArr.length - 1; i++) {
        const u = pathArr[i].id;
        const v = pathArr[i + 1].id;
        for (const [rid, r] of state.roads) {
          if ((r.from === u && r.to === v) || (r.from === v && r.to === u)) {
            routeRoads.add(rid);
          }
        }
      }
    };
    if (comp.original?.path) collectPathRoads(comp.original.path);
    if (comp.revised?.path) collectPathRoads(comp.revised.path);
  }

  state.roads.forEach(road => {
    // Show label if: 1. on route, 2. blocked, 3. hovered, 4. showAllDistances active
    const isVisible = state.showAllDistances ||
                      road.blocked ||
                      routeRoads.has(road.id) ||
                      state.hoveredRoadId === road.id;

    if (!isVisible) return;

    const u = state.nodes.get(road.from);
    const v = state.nodes.get(road.to);
    if (!u || !v) return;

    const midX = (u.x + v.x) / 2;
    const midY = (u.y + v.y) / 2;

    const text = createSvg("text", {
      x: midX,
      y: midY - 6,
      class: "road-distance-pill"
    });
    text.textContent = `${road.distance}k`;
    dom.layerRoadDistances.appendChild(text);
  });
}

// ─── Label Collision Placement Logic ─────────────────────────────
// Problem: In dense road networks, node names often overlap roads or neighboring labels.
// Solution: We test 8 candidate offsets around the icon (N, NE, E, SE, S, SW, W, NW).
// For each candidate spot, we calculate a bounding box around the text and count
// how many previously placed label boxes it intersects with.
// We choose the candidate offset with the fewest overlaps (ideally 0).
// Labels are rendered with a beige halo stroke behind the text so they remain legible over roads.
function renderNodesAndLabels() {
  dom.layerNodes.innerHTML = "";
  dom.layerNodeLabels.innerHTML = "";

  const placedLabelBoxes = [];

  // 8 Candidate Offset Vectors around node center
  const candidateOffsets = [
    { dx: 0,   dy: -18 }, // North
    { dx: 16,  dy: -12 }, // North-East
    { dx: 22,  dy: 4   }, // East
    { dx: 16,  dy: 18  }, // South-East
    { dx: 0,   dy: 22  }, // South
    { dx: -16, dy: 18  }, // South-West
    { dx: -22, dy: 4   }, // West
    { dx: -16, dy: -12 }  // North-West
  ];

  state.nodes.forEach(node => {
    if (!state.activeFilters.has(node.type)) return;

    // Determine fill color
    let fillColor = FACILITY_CONFIG[node.type]?.color || "#5B7FA3";

    // In Coverage View: color village by its assigned shelter
    if (state.coverageActive && state.coverageData && node.type === "village") {
      const vCov = state.coverageData.villageCoverages.find(v => v.villageId === node.id);
      if (vCov) {
        if (!vCov.covered) {
          fillColor = "#8C7B6D"; // Cut-off village
        } else {
          // Color based on assigned shelter ID
          const shelterColors = {
            s1: "#5E8C61",
            s2: "#3A7D65",
            s3: "#498C52",
            s4: "#70A274"
          };
          fillColor = shelterColors[vCov.assignedShelterId] || "#5E8C61";
        }
      }
    }

    // Node Shape
    const nodeEl = createNodeShape(node, fillColor);
    dom.layerNodes.appendChild(nodeEl);

    // ── Collision Resolution for Label Placement ──
    const labelText = node.name;
    const approxWidth = labelText.length * 7.5;
    const approxHeight = 14;

    let bestPos = candidateOffsets[0];
    let minOverlaps = 9999;

    for (const offset of candidateOffsets) {
      const candX = node.x + offset.dx;
      const candY = node.y + offset.dy;
      const box = {
        left: candX - approxWidth / 2,
        right: candX + approxWidth / 2,
        top: candY - approxHeight / 2,
        bottom: candY + approxHeight / 2
      };

      let overlaps = 0;
      for (const prev of placedLabelBoxes) {
        if (!(box.right < prev.left || box.left > prev.right || box.bottom < prev.top || box.top > prev.bottom)) {
          overlaps++;
        }
      }

      if (overlaps < minOverlaps) {
        minOverlaps = overlaps;
        bestPos = offset;
        if (overlaps === 0) break; // Perfect candidate found
      }
    }

    const finalX = node.x + bestPos.dx;
    const finalY = node.y + bestPos.dy;

    placedLabelBoxes.push({
      left: finalX - approxWidth / 2,
      right: finalX + approxWidth / 2,
      top: finalY - approxHeight / 2,
      bottom: finalY + approxHeight / 2
    });

    const textEl = createSvg("text", {
      x: finalX,
      y: finalY,
      class: "node-label-halo"
    });
    textEl.textContent = labelText;
    dom.layerNodeLabels.appendChild(textEl);
  });
}

// Creates distinct SVG node shapes per facility type
function createNodeShape(node, color) {
  const isSelected = (node.id === state.selectedSource || node.id === state.selectedDest);
  const strokeColor = isSelected ? "#C2542D" : "#FBF6EC";
  const strokeWidth = isSelected ? 3.5 : 2;

  let shape;
  if (node.type === "junction") {
    // Rotated Diamond
    shape = createSvg("rect", {
      x: node.x - 7, y: node.y - 7,
      width: 14, height: 14,
      transform: `rotate(45, ${node.x}, ${node.y})`,
      fill: color,
      stroke: strokeColor,
      "stroke-width": strokeWidth,
      cursor: "pointer"
    });
  } else if (node.type === "hospital") {
    // Red badge circle with white medical plus
    const g = createSvg("g", { cursor: "pointer" });
    const c = createSvg("circle", { cx: node.x, cy: node.y, r: 10, fill: color, stroke: strokeColor, "stroke-width": strokeWidth });
    const h = createSvg("rect", { x: node.x - 6, y: node.y - 2, width: 12, height: 4, fill: "#FFF", "pointer-events": "none" });
    const v = createSvg("rect", { x: node.x - 2, y: node.y - 6, width: 4, height: 12, fill: "#FFF", "pointer-events": "none" });
    g.appendChild(c); g.appendChild(h); g.appendChild(v);
    shape = g;
  } else if (node.type === "shelter") {
    // Sage green shelter with double outline ring
    const g = createSvg("g", { cursor: "pointer" });
    const c = createSvg("circle", { cx: node.x, cy: node.y, r: 10, fill: color, stroke: strokeColor, "stroke-width": strokeWidth });
    const inner = createSvg("circle", { cx: node.x, cy: node.y, r: 5, fill: "#FFF", "pointer-events": "none", opacity: 0.85 });
    g.appendChild(c); g.appendChild(inner);
    shape = g;
  } else {
    // Default circle
    shape = createSvg("circle", {
      cx: node.x, cy: node.y, r: 8,
      fill: color,
      stroke: strokeColor,
      "stroke-width": strokeWidth,
      cursor: "pointer"
    });
  }

  const tooltip = createSvg("title");
  tooltip.textContent = `${node.name} (${node.id.toUpperCase()}) &bull; ${FACILITY_CONFIG[node.type]?.label || node.type}\nDemo: ${node.contact}\nNote: ${node.note}`;
  shape.appendChild(tooltip);

  shape.addEventListener("click", () => handleNodeClick(node));
  return shape;
}

// ─── Route Overlays (Green Original, Orange Detour) ──────────────
function renderRouteOverlays() {
  dom.layerRoutesOverlay.innerHTML = "";
  if (!state.currentComparison) return;

  const comp = state.currentComparison;

  // 1. Draw Original Baseline (Deep Green #2F7D4F)
  if (comp.original?.reachable && comp.original.path) {
    for (let i = 0; i < comp.original.path.length - 1; i++) {
      const u = state.nodes.get(comp.original.path[i].id);
      const v = state.nodes.get(comp.original.path[i + 1].id);
      if (u && v) {
        const line = createSvg("line", {
          x1: u.x, y1: u.y,
          x2: v.x, y2: v.y,
          class: "route-original-line",
          opacity: 0.7
        });
        dom.layerRoutesOverlay.appendChild(line);
      }
    }
  }

  // 2. Draw Revised Detour (Terracotta Orange #C2542D)
  if (comp.revised?.reachable && comp.revised.path) {
    for (let i = 0; i < comp.revised.path.length - 1; i++) {
      const u = state.nodes.get(comp.revised.path[i].id);
      const v = state.nodes.get(comp.revised.path[i + 1].id);
      if (u && v) {
        const line = createSvg("line", {
          x1: u.x, y1: u.y,
          x2: v.x, y2: v.y,
          class: "route-revised-line"
        });
        dom.layerRoutesOverlay.appendChild(line);
      }
    }
  }
}

// ─── Node Click Handler ──────────────────────────────────────────
function handleNodeClick(node) {
  if (node.type === "shelter") {
    dom.selectDest.value = node.id;
    state.selectedDest = node.id;
  } else {
    dom.selectSource.value = node.id;
    handleSourceChange();
  }

  if (dom.selectSource.value) {
    calculateRoute();
  }
  renderNodesAndLabels();
}

// ─── Source & Destination Selection ──────────────────────────────
async function handleSourceChange() {
  state.selectedSource = dom.selectSource.value;
  renderNodesAndLabels();

  if (!state.selectedSource) {
    dom.nearestServicesContainer.innerHTML = '<div style="color:#7A6A5B;font-size:13px;text-align:center;padding:12px 0;">Choose an affected location above to locate nearest facilities.</div>';
    return;
  }

  // Fetch nearest relief services
  await updateNearestServices(state.selectedSource);

  // If destination is already set, recalculate route
  if (state.selectedSource) {
    calculateRoute();
  }
}

function handleDestChange() {
  state.selectedDest = dom.selectDest.value;
  renderNodesAndLabels();
  if (state.selectedSource) {
    calculateRoute();
  }
}

// ─── Nearest Relief Services Grouping ────────────────────────────
async function updateNearestServices(sourceId) {
  try {
    const res = await fetch("/api/services", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ source: sourceId })
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();

    dom.nearestServicesContainer.innerHTML = "";

    const categoriesOrder = ["shelter", "hospital", "ngo", "food", "water", "fire_station", "police"];

    categoriesOrder.forEach(catKey => {
      const list = data.services[catKey];
      if (!list || list.length === 0) return;

      const groupEl = document.createElement("div");
      groupEl.className = "service-type-group";

      const cfg = FACILITY_CONFIG[catKey] || { label: catKey, color: "#3A2E26" };

      const header = document.createElement("div");
      header.className = "service-group-header";
      header.innerHTML = `
        <span style="display:flex;align-items:center;gap:6px;">
          <span class="chip-dot" style="background:${cfg.color};width:8px;height:8px;border-radius:50%;display:inline-block;"></span>
          ${cfg.label}s (${list.length})
        </span>
        <span style="font-size:12px;color:#7A6A5B;">&#9662;</span>
      `;

      const content = document.createElement("div");
      content.className = "service-group-content";

      list.forEach(item => {
        const row = document.createElement("div");
        row.className = "service-row";

        const distText = item.reachable ? `${item.distance.toFixed(1)} km` : '<span style="color:#B8473D;font-weight:bold;">Cut off</span>';

        row.innerHTML = `
          <div>
            <div class="service-name">${item.name}</div>
            <div class="service-meta">
              Demo: <a href="tel:${item.contact.replace(/\s+/g, '')}" class="demo-phone-link" title="Fictional demo contact">${item.contact}</a>
              &bull; ${item.note}
            </div>
          </div>
          <div style="text-align:right;">
            <div style="font-weight:600;margin-bottom:4px;">${distText}</div>
            ${item.reachable ? `<button class="btn btn-secondary btn-sm" style="font-size:11px;padding:3px 8px;">Show route</button>` : ''}
          </div>
        `;

        if (item.reachable) {
          const btnShow = row.querySelector("button");
          btnShow.addEventListener("click", () => {
            dom.selectDest.value = item.id;
            state.selectedDest = item.id;
            renderNodesAndLabels();
            calculateRoute();
          });
        }

        content.appendChild(row);
      });

      // Toggle group expansion
      header.addEventListener("click", () => {
        const isHidden = content.style.display === "none";
        content.style.display = isHidden ? "flex" : "none";
        header.querySelector("span:last-child").innerHTML = isHidden ? "&#9662;" : "&#9656;";
      });

      groupEl.appendChild(header);
      groupEl.appendChild(content);
      dom.nearestServicesContainer.appendChild(groupEl);
    });

  } catch (err) {
    showToast("Could not retrieve nearest services: " + err.message);
  }
}

// ─── Calculate Evacuation Route ──────────────────────────────────
// API Interaction:
//   1. If destination is 'auto', calls POST /api/shelters to find the closest reachable shelter.
//   2. If 'Ensure visible detour' is checked and the corridor is unobstructed, calls
//      POST /api/disrupt-route to simulate a realistic hazard along the path.
//   3. Calls POST /api/compare with {source, destination} to compute both the baseline
//      (all roads open) and revised (open roads only) routes, distance overhead, and detour %.
async function calculateRoute() {
  const source = dom.selectSource.value;
  let dest = dom.selectDest.value;

  if (!source) {
    showToast("Please choose an affected location first.");
    return;
  }

  // Auto-destination mode
  if (dest === "auto") {
    try {
      const res = await fetch("/api/shelters", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ source })
      });
      const data = await res.json();
      const openShelter = data.shelters.find(s => s.reachable);
      dest = openShelter ? openShelter.id : data.shelters[0]?.id || "s1";
    } catch {
      dest = "s1";
    }
  }

  try {
    const res = await fetch("/api/compare", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ source, destination: dest })
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    let comp = await res.json();

    // ── "Ensure visible detour" feature ──
    dom.disruptionAlertBanner.style.display = "none";
    if (dom.checkEnsureDetour.checked && comp.original?.reachable && comp.revised?.reachable) {
      const diff = Math.abs((comp.revised.distance || 0) - (comp.original.distance || 0));
      // If no obstruction touched original route (distance is identical)
      if (diff < 0.001) {
        const disRes = await fetch("/api/disrupt-route", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ source, destination: dest, seed: Date.now() % 10000 })
        });
        if (disRes.ok) {
          const disData = await disRes.json();
          if (disData.disrupted) {
            // Re-fetch route comparison with the simulated disruption
            const reComp = await fetch("/api/compare", {
              method: "POST",
              headers: { "Content-Type": "application/json" },
              body: JSON.stringify({ source, destination: dest })
            });
            if (reComp.ok) {
              comp = await reComp.json();
              dom.disruptionBannerText.textContent = disData.message;
              dom.disruptionAlertBanner.style.display = "flex";
              // Refresh map road statuses
              await refreshRoadStatuses();
            }
          }
        }
      }
    }

    state.currentComparison = comp;
    renderComparisonCard(comp);
    renderMap();

  } catch (err) {
    showToast("Route calculation failed: " + err.message);
  }
}

// ─── Render Results Card ─────────────────────────────────────────
function renderComparisonCard(comp) {
  dom.resultsEmptyState.style.display = "none";
  dom.resultsContentPanel.style.display = "block";
  dom.routeMetaLabel.textContent = `Evacuation from ${comp.sourceName} to ${comp.destinationName}`;

  // 1. Original Baseline
  if (comp.original?.reachable) {
    dom.metricOrigDist.textContent = `${comp.original.distance.toFixed(1)} km`;
    renderWaypointsChain(dom.pathOriginalChain, comp.original.path);
  } else {
    dom.metricOrigDist.textContent = "Unreachable";
    dom.pathOriginalChain.innerHTML = "<em>No corridor</em>";
  }

  // 2. Revised Detour
  if (comp.revised?.reachable) {
    dom.metricRevDist.textContent = `${comp.revised.distance.toFixed(1)} km`;
    dom.revisedChainContainer.style.display = "block";
    dom.unreachableAlertBanner.style.display = "none";
    dom.metricBoxRevised.classList.remove("unreachable");
    renderWaypointsChain(dom.pathRevisedChain, comp.revised.path);
  } else {
    dom.metricRevDist.textContent = "CUT OFF";
    dom.revisedChainContainer.style.display = "none";
    dom.unreachableAlertBanner.style.display = "flex";
    dom.metricBoxRevised.classList.add("unreachable");
  }

  // 3. Detour Overhead
  if (comp.extraKm !== null && comp.extraKm !== undefined) {
    const extra = comp.extraKm;
    const pct = comp.percentIncrease;
    if (extra > 0.001) {
      dom.metricExtraKm.textContent = `+${extra.toFixed(1)} km`;
      dom.metricExtraPct.textContent = `+${pct.toFixed(1)}% detour penalty`;
    } else {
      dom.metricExtraKm.textContent = "0.0 km";
      dom.metricExtraPct.textContent = "0.0% (optimal direct route)";
    }
  } else {
    dom.metricExtraKm.textContent = "N/A";
    dom.metricExtraPct.textContent = "Isolated (no safe route)";
  }

  // 4. Blockages count
  const blockedOnOrig = comp.blockedOnOriginalRoute || [];
  dom.metricRouteBlocks.textContent = blockedOnOrig.length;
  dom.metricTotalBlocks.textContent = `${comp.totalBlockedRoads} network obstructions`;

  // 5. Blocked roads list on active corridor
  if (blockedOnOrig.length > 0) {
    dom.blockedOnRouteSection.style.display = "block";
    dom.blockedOnRouteList.innerHTML = "";
    blockedOnOrig.forEach(b => {
      const li = document.createElement("li");
      li.className = "obstruction-item";
      li.innerHTML = `
        <div>
          <span class="obstruction-title">${b.id.toUpperCase()}: ${b.fromName} &harr; ${b.toName}</span>
          <div class="obstruction-reason">${b.reason}</div>
        </div>
        <button class="btn btn-secondary btn-sm" onclick="restoreRoad('${b.id}')">Restore</button>
      `;
      dom.blockedOnRouteList.appendChild(li);
    });
  } else {
    dom.blockedOnRouteSection.style.display = "none";
  }
}

function renderWaypointsChain(container, pathArray) {
  container.innerHTML = "";
  if (!pathArray) return;
  pathArray.forEach((step, idx) => {
    const span = document.createElement("span");
    span.className = "waypoint-node";
    span.textContent = `${step.name} (${step.id.toUpperCase()})`;
    container.appendChild(span);

    if (idx < pathArray.length - 1) {
      const arr = document.createElement("span");
      arr.className = "waypoint-arrow";
      arr.innerHTML = "&rarr;";
      container.appendChild(arr);
    }
  });
}

// ─── Block / Restore Road Operations ─────────────────────────────
async function toggleRoadBlock(roadId) {
  const road = state.roads.get(roadId);
  if (!road) return;

  if (road.blocked) {
    await restoreRoad(roadId);
  } else {
    // Pick standard reason
    const reason = state.standardReasons[Math.floor(Math.random() * state.standardReasons.length)] || "Flooded road";
    await blockRoad(roadId, reason);
  }
}

async function blockRoad(roadId, reason) {
  try {
    const res = await fetch("/api/block", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ road: roadId, reason })
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const r = state.roads.get(roadId);
    if (r) {
      r.blocked = true;
      r.reason = reason;
    }
    await onNetworkStateChanged();
  } catch (err) {
    showToast(`Could not block ${roadId}: ` + err.message);
  }
}

window.restoreRoad = async function(roadId) {
  try {
    const res = await fetch("/api/restore", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ road: roadId })
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const r = state.roads.get(roadId);
    if (r) {
      r.blocked = false;
      r.reason = "";
    }
    await onNetworkStateChanged();
  } catch (err) {
    showToast(`Could not restore ${roadId}: ` + err.message);
  }
};

async function resetAllRoads() {
  try {
    const res = await fetch("/api/reset", { method: "POST" });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    state.roads.forEach(r => {
      r.blocked = false;
      r.reason = "";
    });
    showToast("All road obstructions cleared. Network is 100% open.");
    await onNetworkStateChanged();
  } catch (err) {
    showToast("Reset failed: " + err.message);
  }
}

async function handleManualBlock() {
  const roadId = dom.selectManualRoad.value;
  const reason = dom.selectManualReason.value;
  if (!roadId) {
    showToast("Please choose a road segment from the dropdown first.");
    return;
  }
  await blockRoad(roadId, reason);
}

// ─── Seeded Scenario Application ─────────────────────────────────
async function applyScenario() {
  const severity = dom.selectSeverity.value;
  const seed = parseInt(dom.inputScenarioSeed.value, 10) || 2026;
  await executeScenario(severity, seed);
}

async function applyScenarioDefault(seed) {
  await executeScenario("moderate", seed);
}

async function executeScenario(severity, seed) {
  try {
    const res = await fetch("/api/scenario", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ severity, seed })
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();

    // Sync local road state
    state.roads.forEach(r => {
      r.blocked = false;
      r.reason = "";
    });

    data.blockedRoads.forEach(b => {
      const r = state.roads.get(b.road);
      if (r) {
        r.blocked = true;
        r.reason = b.reason;
      }
    });

    showToast(`Applied ${severity.toUpperCase()} disaster scenario (${data.blockedCount} roads blocked).`);
    await onNetworkStateChanged();

  } catch (err) {
    showToast("Failed to apply disaster scenario: " + err.message);
  }
}

// Refresh network state on changes
async function onNetworkStateChanged() {
  renderMap();
  updateActiveObstructionsUI();

  if (state.coverageActive) {
    await fetchAndRenderCoverage();
  }

  if (state.selectedSource) {
    await updateNearestServices(state.selectedSource);
    calculateRoute();
  }
}

async function refreshRoadStatuses() {
  const res = await fetch("/api/network");
  if (res.ok) {
    const data = await res.json();
    data.roads.forEach(r => {
      const cur = state.roads.get(r.id);
      if (cur) {
        cur.blocked = r.blocked;
        cur.reason = r.reason;
      }
    });
  }
}

function updateActiveObstructionsUI() {
  const blockedList = Array.from(state.roads.values()).filter(r => r.blocked);
  dom.obstructionCountBadge.textContent = blockedList.length;

  if (blockedList.length === 0) {
    dom.activeObstructionsList.innerHTML = '<li style="color:#7A6A5B;font-size:13px;text-align:center;padding:12px 0;">No active road obstructions. Network is fully open.</li>';
    return;
  }

  dom.activeObstructionsList.innerHTML = "";
  blockedList.forEach(r => {
    const u = state.nodes.get(r.from)?.name || r.from;
    const v = state.nodes.get(r.to)?.name || r.to;

    const li = document.createElement("li");
    li.className = "obstruction-item";
    li.innerHTML = `
      <div>
        <span class="obstruction-title">${r.id.toUpperCase()}: ${u} &harr; ${v}</span>
        <div class="obstruction-reason">(${r.reason})</div>
      </div>
      <button class="btn btn-secondary btn-sm" onclick="restoreRoad('${r.id}')">Restore</button>
    `;
    dom.activeObstructionsList.appendChild(li);
  });
}

// ─── Multi-Source Coverage View ──────────────────────────────────
async function handleCoverageToggle(e) {
  state.coverageActive = e.target.checked;
  if (state.coverageActive) {
    dom.coverageSummaryPanel.style.display = "flex";
    await fetchAndRenderCoverage();
  } else {
    dom.coverageSummaryPanel.style.display = "none";
    state.coverageData = null;
    renderNodesAndLabels();
  }
}

async function fetchAndRenderCoverage() {
  try {
    const res = await fetch("/api/coverage");
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();
    state.coverageData = data;

    dom.coverageStatsText.textContent = `${data.coveredVillages} of ${data.totalVillages} villages covered (${data.cutoffVillages} cut off)`;

    if (data.cutoffVillages > 0) {
      const cutoffNames = data.villageCoverages
        .filter(v => !v.covered)
        .map(v => v.villageName)
        .join(", ");
      dom.coverageCutoffNames.textContent = `Cut off: ${cutoffNames}`;
    } else {
      dom.coverageCutoffNames.textContent = "100% Shelter Coverage";
    }

    renderNodesAndLabels();
  } catch (err) {
    showToast("Coverage query failed: " + err.message);
  }
}

// ─── Emergency Directory & Safety Tips ───────────────────────────
async function loadEmergencyData() {
  try {
    const res = await fetch("emergency.json");
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();

    const grid = document.getElementById("emergency-categories-grid");
    grid.innerHTML = "";

    data.categories.forEach(cat => {
      const groupEl = document.createElement("div");
      groupEl.className = "emergency-card-group";

      const h3 = document.createElement("h3");
      h3.style.fontSize = "18px";
      h3.style.marginBottom = "4px";
      h3.textContent = cat.title;
      groupEl.appendChild(h3);

      cat.numbers.forEach(item => {
        const div = document.createElement("div");
        div.className = "emergency-item";
        div.innerHTML = `
          <div class="emergency-item-left">
            <h3>${item.service}</h3>
            <p>${item.description}</p>
          </div>
          <a href="tel:${item.tel}" class="emergency-dial-btn" title="Tap to call ${item.number}">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
              <path d="M22 16.92v3a2 2 0 0 1-2.18 2 19.79 19.79 0 0 1-8.63-3.07 19.5 19.5 0 0 1-6-6 19.79 19.79 0 0 1-3.07-8.67A2 2 0 0 1 4.11 2h3a2 2 0 0 1 2 1.72 12.84 12.84 0 0 0 .7 2.81 2 2 0 0 1-.45 2.11L8.09 9.91a16 16 0 0 0 6 6l1.27-1.27a2 2 0 0 1 2.11-.45 12.84 12.84 0 0 0 2.81.7A2 2 0 0 1 22 16.92z"/>
            </svg>
            ${item.number}
          </a>
        `;
        groupEl.appendChild(div);
      });

      grid.appendChild(groupEl);
    });

    // Flood Tips
    const tipsGrid = document.getElementById("flood-tips-grid");
    tipsGrid.innerHTML = "";
    data.flood_safety_tips.forEach((tip, idx) => {
      const box = document.createElement("div");
      box.className = "tip-box";
      box.innerHTML = `
        <h4><span style="color:#C2542D;">#${idx + 1}</span> ${tip.title}</h4>
        <p>${tip.text}</p>
      `;
      tipsGrid.appendChild(box);
    });

  } catch (err) {
    showToast("Could not load emergency directory: " + err.message);
  }
}

// ─── Step-by-Step Dijkstra Stepper Visualizer ─────────────────────
async function openStepperModal() {
  const source = dom.selectSource.value;
  let dest = dom.selectDest.value;
  if (!source) {
    showToast("Please choose an affected location first.");
    return;
  }
  if (dest === "auto") {
    dest = "s1";
  }

  try {
    const res = await fetch("/api/trace", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ source, destination: dest })
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();

    state.stepperTrace = data.steps;
    state.stepperIndex = 0;

    dom.stepperModal.classList.add("open");
    renderStepperStep();

  } catch (err) {
    showToast("Could not record algorithm trace: " + err.message);
  }
}

function closeStepperModal() {
  dom.stepperModal.classList.remove("open");
  if (state.stepperTimer) {
    clearInterval(state.stepperTimer);
    state.stepperTimer = null;
    dom.btnStepPlay.textContent = "Play";
  }
}

function renderStepperStep() {
  if (!state.stepperTrace || state.stepperTrace.length === 0) {
    dom.stepperStepDesc.textContent = "No trace steps recorded.";
    return;
  }

  const step = state.stepperTrace[state.stepperIndex];
  dom.labelStepCounter.textContent = `Step ${state.stepperIndex + 1} / ${state.stepperTrace.length}`;

  let desc = "";
  if (step.action === "pop") {
    const nodeName = state.nodes.get(step.currentNode)?.name || step.currentNode;
    desc = `<strong>Popped from Heap:</strong> ${nodeName} (${step.currentNode}) with current settled distance <strong>${step.currentDistance.toFixed(1)} km</strong>. Exploring incident open roads.`;
  } else if (step.action === "relax") {
    const targetName = state.nodes.get(step.targetNode)?.name || step.targetNode;
    if (step.relaxed) {
      desc = `<strong>Edge Relaxed:</strong> Shorter path found to ${targetName} via ${step.currentNode} (dist ${step.newDistance.toFixed(1)} km). Updated priority queue.`;
    } else {
      desc = `<strong>Edge Checked:</strong> Path to ${targetName} via ${step.currentNode} (${step.newDistance.toFixed(1)} km) is not shorter than existing best (${step.oldDistance?.toFixed(1) || 'None'} km). Skipped.`;
    }
  } else if (step.action === "settle_destination") {
    desc = `<strong>Destination Finalised:</strong> Reached destination target with optimal shortest path length of <strong>${step.currentDistance.toFixed(1)} km</strong>. Terminating early!`;
  } else if (step.action === "skip_stale") {
    desc = `<strong>Stale Entry Skipped:</strong> Node ${step.currentNode} was already finalized by an earlier shorter path. Skipping outdated heap entry.`;
  }
  dom.stepperStepDesc.innerHTML = desc;

  // Render Priority Queue snapshot
  dom.stepperPqList.innerHTML = "";
  if (step.pqSnapshot.length === 0) {
    dom.stepperPqList.innerHTML = "<li>Queue is empty.</li>";
  } else {
    step.pqSnapshot.forEach(item => {
      const li = document.createElement("li");
      const name = state.nodes.get(item.node)?.name || item.node;
      li.innerHTML = `<strong>${name} (${item.node})</strong> &rarr; ${item.dist.toFixed(1)} km`;
      dom.stepperPqList.appendChild(li);
    });
  }

  // Render Distance Table snapshot
  dom.stepperDistTable.innerHTML = "";
  for (const [nid, d] of Object.entries(step.distancesSnapshot)) {
    const box = document.createElement("div");
    box.style.background = (nid === step.currentNode) ? "#EDE2CD" : "#FBF6EC";
    box.style.border = (nid === step.currentNode) ? "1px solid #5B7FA3" : "1px solid #E3D6BE";
    box.style.padding = "4px 8px";
    box.style.borderRadius = "4px";
    const name = state.nodes.get(nid)?.name || nid;
    box.innerHTML = `<div><strong>${name}</strong></div><div style="color:#7A6A5B;">${d.toFixed(1)} km</div>`;
    dom.stepperDistTable.appendChild(box);
  }
}

function stepForward() {
  if (!state.stepperTrace) return;
  if (state.stepperIndex < state.stepperTrace.length - 1) {
    state.stepperIndex++;
    renderStepperStep();
  } else if (state.stepperTimer) {
    clearInterval(state.stepperTimer);
    state.stepperTimer = null;
    dom.btnStepPlay.textContent = "Play";
  }
}

function stepBackward() {
  if (!state.stepperTrace) return;
  if (state.stepperIndex > 0) {
    state.stepperIndex--;
    renderStepperStep();
  }
}

function resetStepper() {
  state.stepperIndex = 0;
  if (state.stepperTimer) {
    clearInterval(state.stepperTimer);
    state.stepperTimer = null;
    dom.btnStepPlay.textContent = "Play";
  }
  renderStepperStep();
}

function toggleStepPlay() {
  if (state.stepperTimer) {
    clearInterval(state.stepperTimer);
    state.stepperTimer = null;
    dom.btnStepPlay.textContent = "Play";
  } else {
    dom.btnStepPlay.textContent = "Pause";
    const speed = parseInt(dom.sliderStepSpeed.value, 10) || 500;
    state.stepperTimer = setInterval(stepForward, speed);
  }
}

// ─── Clear Selections ────────────────────────────────────────────
function clearSelections() {
  dom.selectSource.value = "";
  dom.selectDest.value = "auto";
  state.selectedSource = "";
  state.selectedDest = "auto";
  state.currentComparison = null;

  dom.resultsEmptyState.style.display = "block";
  dom.resultsContentPanel.style.display = "none";
  dom.disruptionAlertBanner.style.display = "none";
  dom.unreachableAlertBanner.style.display = "none";
  dom.routeMetaLabel.textContent = "";

  dom.nearestServicesContainer.innerHTML = '<div style="color:#7A6A5B;font-size:13px;text-align:center;padding:12px 0;">Choose an affected location above to locate nearest facilities.</div>';

  renderMap();
}

// ═══════════════════════════════════════════════════════════════════
// RELIEF TRUCK LOADING (0/1 KNAPSACK) CONTROLLER
// ═══════════════════════════════════════════════════════════════════

async function loadSuppliesData() {
  if (state.suppliesData) return; // already ingested
  try {
    const res = await fetch("/api/supplies");
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();
    state.suppliesData = data;

    // Populate Depot dropdown (NGO camps and food distribution centres)
    dom.selectLoadDepot.innerHTML = '<option value="">&mdash; Choose Relief Depot &mdash;</option>';
    state.nodes.forEach(node => {
      if (node.type === "ngo" || node.type === "food") {
        const opt = document.createElement("option");
        opt.value = node.id;
        opt.textContent = `${node.name} (${node.type === "ngo" ? "NGO Camp" : "Food Depot"})`;
        dom.selectLoadDepot.appendChild(opt);
      }
    });

    // Populate Village dropdown
    dom.selectLoadVillage.innerHTML = '<option value="">&mdash; Choose Affected Village &mdash;</option>';
    state.nodes.forEach(node => {
      if (node.type === "village") {
        const opt = document.createElement("option");
        opt.value = node.id;
        opt.textContent = `${node.name} (${node.id})`;
        dom.selectLoadVillage.appendChild(opt);
      }
    });

    if (dom.selectLoadDepot.options.length > 1) {
      dom.selectLoadDepot.selectedIndex = 1;
    }
    if (dom.selectLoadVillage.options.length > 1) {
      dom.selectLoadVillage.selectedIndex = 1;
      handleVillageNeedUpdate();
    }
  } catch (err) {
    showToast("Failed to load supplies catalogue: " + err.message);
  }
}

function handleVillageNeedUpdate() {
  const vId = dom.selectLoadVillage.value;
  if (!vId || !state.suppliesData || !state.suppliesData.village_needs[vId]) {
    dom.labelVillageTotalNeed.textContent = "";
    return;
  }

  const catalogue = {};
  state.suppliesData.catalogue.forEach(item => { catalogue[item.name] = item; });

  const needs = state.suppliesData.village_needs[vId];
  let totalKg = 0;
  let totalUnits = 0;

  for (const [itemName, units] of Object.entries(needs)) {
    const item = catalogue[itemName];
    if (item) {
      totalUnits += units;
      totalKg += units * item.weightKg;
    }
  }

  dom.labelVillageTotalNeed.textContent = `Village Demand: ${totalUnits} units, ~${totalKg} kg total need (exceeds default payload capacity).`;
}

// ─── Plan Relief Truck Loading ───────────────────────────────────
// API Interaction:
//   Calls POST /api/load-plan with {depotId, villageId, capacityKg}.
//   The backend engine computes:
//     1. Ground delivery corridor distance and waypoints via Dijkstra on open roads.
//     2. Optimal cargo manifest using 0/1 Dynamic Programming.
//     3. Fast heuristic manifest using Greedy urgency-to-weight ratio.
//     4. Urgency points gained by DP over Greedy for the selected payload limit.
async function calculateLoadPlan() {
  const depotId = dom.selectLoadDepot.value;
  const villageId = dom.selectLoadVillage.value;
  const capacityKg = state.currentCapacity;

  if (!depotId) {
    showToast("Please choose an origin relief depot.");
    return;
  }
  if (!villageId) {
    showToast("Please choose a destination affected village.");
    return;
  }

  try {
    const res = await fetch("/api/load-plan", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ depotId, villageId, capacityKg })
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();
    state.currentLoadPlan = data;

    dom.loadingEmptyState.style.display = "none";
    dom.cardTripSummary.style.display = "block";

    const depotName = state.nodes.get(depotId)?.name || depotId;
    const villageName = state.nodes.get(villageId)?.name || villageId;

    if (!data.reachable) {
      dom.loadingUnreachableBanner.style.display = "flex";
      dom.loadingUnreachableText.innerHTML = `<strong>Village Cut Off:</strong> ${villageName} is unreachable from ${depotName} due to active road blockages. Dispatch rescue boats or aerial transport.`;
      dom.loadingResultsContainer.style.display = "none";

      dom.loadingRouteStatus.innerHTML = `<span style="color:#A8281E; font-weight:700;">Status: Cut off by obstructions (No land corridor)</span>`;
      dom.loadingTripMetrics.innerHTML = `<div>Destination: <strong>${villageName}</strong></div><div style="color:#A8281E;">0.0 km open</div>`;
      dom.loadingTripWaypoints.innerHTML = '<span class="waypoint-badge" style="background:#FDE8E8; border-color:#A8281E; color:#A8281E;">Isolated</span>';
      return;
    }

    // Reachable
    dom.loadingUnreachableBanner.style.display = "none";
    dom.loadingResultsContainer.style.display = "block";

    // Trip Summary
    const routeDistance = (typeof data.routeDistanceKm === 'number') ? data.routeDistanceKm : (typeof data.distance === 'number' ? data.distance : 0.0);
    const rawPath = data.routePath || data.path || [];
    const routePath = rawPath.map(p => (typeof p === 'object' && p !== null) ? p.id : p);

    dom.loadingRouteStatus.innerHTML = `<span style="color:#2F7D4F; font-weight:700;">Corridor Open: ${routeDistance.toFixed(1)} km</span> (${Math.max(0, routePath.length - 1)} road segments)`;
    dom.loadingTripMetrics.innerHTML = `
      <div style="font-size: 13px; color: #5C4C3E;">
        Origin: <strong>${depotName}</strong> &rarr; Destination: <strong>${villageName}</strong>
      </div>
    `;

    dom.loadingTripWaypoints.innerHTML = "";
    routePath.forEach((nid, i) => {
      const pill = document.createElement("span");
      pill.className = "waypoint-badge";
      pill.textContent = state.nodes.get(nid)?.name || nid;
      dom.loadingTripWaypoints.appendChild(pill);
      if (i < routePath.length - 1) {
        const arrow = document.createElement("span");
        arrow.style.color = "#9C8B78";
        arrow.textContent = " \u2192 ";
        dom.loadingTripWaypoints.appendChild(arrow);
      }
    });

    // Urgency Delta Callout Banner
    if (data.urgencyGain > 0) {
      dom.urgencyDeltaBanner.className = "delta-callout-banner";
      dom.urgencyDeltaBanner.innerHTML = `
        <div style="display:flex; align-items:center; gap:10px;">
          <span class="delta-pill">+${data.urgencyGain} Urgency</span>
          <span style="font-size:14px; color:#2F7D4F;">
            <strong>Dynamic Programming Gain:</strong> Optimal 0/1 knapsack yields <strong>+${data.urgencyGain}</strong> more urgency points than the greedy ratio heuristic for this ${data.capacityKg} kg payload limit.
          </span>
        </div>
      `;
    } else {
      dom.urgencyDeltaBanner.className = "delta-callout-banner tie";
      dom.urgencyDeltaBanner.innerHTML = `
        <div style="display:flex; align-items:center; gap:10px;">
          <span class="delta-pill" style="background:#5C4C3E;">Equal</span>
          <span style="font-size:14px; color:#5C4C3E;">
            <strong>Optimal Tie:</strong> Both Dynamic Programming and Greedy ratio heuristic achieved identical urgency (${data.dp.totalUrgency} points) on this allocation.
          </span>
        </div>
      `;
    }

    // DP Card
    const dpPct = Math.min(100, Math.round((data.dp.totalWeightKg / data.capacityKg) * 100));
    dom.dpTotalUrgency.textContent = data.dp.totalUrgency;
    dom.dpWeightRatio.textContent = `${data.dp.totalWeightKg} / ${data.capacityKg} kg`;
    dom.dpWeightBar.style.width = `${dpPct}%`;
    dom.dpWeightPct.textContent = `${dpPct}% payload capacity utilized (${data.capacityKg - data.dp.totalWeightKg} kg spare)`;
    renderManifestTable(dom.dpManifestTable.querySelector("tbody"), data.dp, dom.dpItemCount);

    // Greedy Card
    const grPct = Math.min(100, Math.round((data.greedy.totalWeightKg / data.capacityKg) * 100));
    dom.greedyTotalUrgency.textContent = data.greedy.totalUrgency;
    dom.greedyWeightRatio.textContent = `${data.greedy.totalWeightKg} / ${data.capacityKg} kg`;
    dom.greedyWeightBar.style.width = `${grPct}%`;
    dom.greedyWeightPct.textContent = `${grPct}% payload capacity utilized (${data.capacityKg - data.greedy.totalWeightKg} kg spare)`;
    renderManifestTable(dom.greedyManifestTable.querySelector("tbody"), data.greedy, dom.greedyItemCount);

  } catch (err) {
    showToast("Load planning query failed: " + err.message);
  }
}

// Dynamically updates capacity numbers in the weight ratio cards when the user changes capacity.
function updateCapacityDisplays(cap) {
  if (state.currentLoadPlan && state.currentLoadPlan.dp) {
    dom.dpWeightRatio.textContent = `${state.currentLoadPlan.dp.totalWeightKg} / ${cap} kg`;
    dom.greedyWeightRatio.textContent = `${state.currentLoadPlan.greedy.totalWeightKg} / ${cap} kg`;
  } else {
    dom.dpWeightRatio.textContent = `0 / ${cap} kg`;
    dom.greedyWeightRatio.textContent = `0 / ${cap} kg`;
  }
}

// Renders the knapsack cargo manifest breakdown table with sorted rows and bottom totals.
function renderManifestTable(tbody, manifestSource, totalCountElem) {
  tbody.innerHTML = "";

  if (!manifestSource) {
    tbody.innerHTML = `<tr><td colspan="5" style="text-align:center; padding:16px; color:#7A6A5B; font-style:italic;">No items fit within this capacity</td></tr>`;
    if (totalCountElem) totalCountElem.textContent = "0";
    return;
  }

  let rows = [];
  let totalItems = 0;
  let totalWeight = 0;
  let totalUrgency = 0;

  // Case 1: Backend provided pre-aggregated 'grouped' array
  if (manifestSource.grouped && manifestSource.grouped.length > 0) {
    rows = manifestSource.grouped.map(g => {
      const q = g.qty || g.count || 0;
      const uw = g.unitWeightKg || g.weightPerUnit || 0;
      const uurg = g.unitUrgency || g.urgencyPerUnit || 0;
      const tw = g.totalWeightKg ?? g.subtotalWeightKg ?? (q * uw);
      const tu = g.totalUrgency ?? g.subtotalUrgency ?? (q * uurg);
      return { name: g.name, qty: q, unitWeight: uw, totalWeight: tw, totalUrgency: tu };
    });
    totalItems = manifestSource.itemsCount ?? rows.reduce((s, r) => s + r.qty, 0);
    totalWeight = manifestSource.totalWeightKg ?? rows.reduce((s, r) => s + r.totalWeight, 0);
    totalUrgency = manifestSource.totalUrgency ?? rows.reduce((s, r) => s + r.totalUrgency, 0);
  } else {
    // Case 2: Aggregate discrete item array
    const items = Array.isArray(manifestSource) ? manifestSource : (manifestSource.chosenItems || []);
    if (items.length > 0) {
      const groupedMap = new Map();
      items.forEach(it => {
        if (!groupedMap.has(it.name)) {
          groupedMap.set(it.name, {
            name: it.name,
            qty: 0,
            unitWeight: it.weightKg,
            unitUrgency: it.urgency,
            totalWeight: 0,
            totalUrgency: 0
          });
        }
        const entry = groupedMap.get(it.name);
        entry.qty++;
        entry.totalWeight += it.weightKg;
        entry.totalUrgency += it.urgency;
        totalItems++;
        totalWeight += it.weightKg;
        totalUrgency += it.urgency;
      });
      rows = Array.from(groupedMap.values());
    }
  }

  if (totalCountElem) {
    totalCountElem.textContent = totalItems;
  }

  if (rows.length === 0) {
    tbody.innerHTML = `<tr><td colspan="5" style="text-align:center; padding:16px; color:#7A6A5B; font-style:italic;">No items fit within this capacity</td></tr>`;
    return;
  }

  // Sort rows by urgency (total for that row), highest first. Break ties by name.
  rows.sort((a, b) => {
    if (b.totalUrgency !== a.totalUrgency) {
      return b.totalUrgency - a.totalUrgency;
    }
    return a.name.localeCompare(b.name);
  });

  // Render each item row
  rows.forEach(r => {
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td><strong>${r.name}</strong></td>
      <td style="text-align:center;"><span style="background:#FAF3E6; border:1px solid #E3D6BE; padding:2px 8px; border-radius:12px; font-weight:600;">${r.qty}</span></td>
      <td style="text-align:right;">${r.unitWeight} kg</td>
      <td style="text-align:right; font-weight:600;">${r.totalWeight} kg</td>
      <td style="text-align:right; color:#2F7D4F; font-weight:700;">${r.totalUrgency}</td>
    `;
    tbody.appendChild(tr);
  });

  // Add totals row at the bottom matching card stats exactly
  const trTotal = document.createElement("tr");
  trTotal.className = "manifest-totals-row";
  trTotal.style.borderTop = "2px solid #E3D6BE";
  trTotal.style.background = "rgba(227, 214, 190, 0.25)";
  trTotal.style.fontWeight = "700";
  trTotal.innerHTML = `
    <td><strong>Total</strong></td>
    <td style="text-align:center;"><span style="background:#FAF3E6; border:1px solid #B89B72; padding:2px 8px; border-radius:12px; font-weight:700; color:#3A2E26;">${totalItems}</span></td>
    <td style="text-align:right; color:#7A6A5B;">—</td>
    <td style="text-align:right; font-weight:700; color:#3A2E26;">${totalWeight} kg</td>
    <td style="text-align:right; color:#2F7D4F; font-weight:800; font-size:15px;">${totalUrgency}</td>
  `;
  tbody.appendChild(trTotal);
}

// Switches to the Evacuation Planner tab and plots the relief delivery route on the map.
function showTripOnMap() {
  if (!state.currentLoadPlan || !state.currentLoadPlan.reachable) return;
  dom.tabBtnPlanner.click();
  dom.selectSource.value = state.currentLoadPlan.depotId;
  dom.selectDest.value = state.currentLoadPlan.villageId;
  calculateRoute();
  dom.mapViewport.scrollIntoView({ behavior: "smooth" });
}

// ─── Educational Knapsack Demo Visualizer ────────────────────────
// API Interaction:
//   Calls GET /api/load-demo to fetch the fixed 50 kg counterexample where greedy ratio fails.
//   Dynamically renders the full (4 x 51) 2D DP matrix table and highlights the exact
//   backtracking cells (i, w) that the algorithm traversed to select the optimal cargo.
//   Also fills both result cards and manifest tables demonstrating greedy vs DP.
async function showKnapsackDemo() {
  try {
    const res = await fetch("/api/load-demo");
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();

    // 1. Populate the Result Cards with the Demo Data
    dom.loadingEmptyState.style.display = "none";
    dom.loadingUnreachableBanner.style.display = "none";
    dom.cardTripSummary.style.display = "block";
    dom.loadingResultsContainer.style.display = "block";

    dom.loadingRouteStatus.innerHTML = '<span style="color:#2F7D4F; font-weight:700;">Demo Scenario: 50 kg Payload Limit</span>';
    dom.loadingTripMetrics.innerHTML = `
      <div style="font-size: 13px; color: #5C4C3E;">
        Curated Pedagogical Case: <strong>Where Greedy Ratio Heuristic Fails</strong>
      </div>
    `;
    dom.loadingTripWaypoints.innerHTML = '<span class="waypoint-badge">Relief Depot</span><span style="color:#9C8B78;"> \u2192 </span><span class="waypoint-badge">Target Village</span>';

    // Urgency Delta Callout Banner: +60 Urgency Points
    dom.urgencyDeltaBanner.className = "delta-callout-banner";
    dom.urgencyDeltaBanner.innerHTML = `
      <div style="display:flex; align-items:center; gap:10px;">
        <span class="delta-pill">+${data.urgencyGain} Urgency</span>
        <span style="font-size:14px; color:#2F7D4F;">
          <strong>Dynamic Programming Gain:</strong> Optimal 0/1 knapsack yields <strong>+${data.urgencyGain}</strong> more urgency points than greedy ratio heuristic (220 vs 160) for 50 kg payload limit.
        </span>
      </div>
    `;

    // DP Card (Blankets + Water = 50 kg, 220 Urgency)
    const dpPct = Math.min(100, Math.round((data.dp.totalWeightKg / data.capacityKg) * 100));
    dom.dpTotalUrgency.textContent = data.dp.totalUrgency;
    dom.dpWeightRatio.textContent = `${data.dp.totalWeightKg} / ${data.capacityKg} kg`;
    dom.dpWeightBar.style.width = `${dpPct}%`;
    dom.dpWeightPct.textContent = `${dpPct}% payload capacity utilized (${data.capacityKg - data.dp.totalWeightKg} kg spare)`;
    renderManifestTable(dom.dpManifestTable.querySelector("tbody"), data.dp, dom.dpItemCount);

    // Greedy Card (Medical kit + Blankets = 30 kg, 160 Urgency)
    const grPct = Math.min(100, Math.round((data.greedy.totalWeightKg / data.capacityKg) * 100));
    dom.greedyTotalUrgency.textContent = data.greedy.totalUrgency;
    dom.greedyWeightRatio.textContent = `${data.greedy.totalWeightKg} / ${data.capacityKg} kg`;
    dom.greedyWeightBar.style.width = `${grPct}%`;
    dom.greedyWeightPct.textContent = `${grPct}% payload capacity utilized (${data.capacityKg - data.greedy.totalWeightKg} kg spare)`;
    renderManifestTable(dom.greedyManifestTable.querySelector("tbody"), data.greedy, dom.greedyItemCount);

    // 2. Render Full 2D Dynamic Programming Matrix Table
    const container = dom.demoDpTableContainer;
    container.innerHTML = "";

    const table = document.createElement("table");
    table.className = "dp-matrix-table";

    // Header row: w = 0 to 50
    const thead = document.createElement("thead");
    const headerTr = document.createElement("tr");
    const thCorner = document.createElement("th");
    thCorner.textContent = "i \\ w (kg)";
    thCorner.style.minWidth = "160px";
    headerTr.appendChild(thCorner);

    for (let w = 0; w <= data.capacityKg; w += 5) {
      const th = document.createElement("th");
      th.textContent = `${w}kg`;
      headerTr.appendChild(th);
    }
    thead.appendChild(headerTr);
    table.appendChild(thead);

    const backtrackCells = data.backtrackCells || data.dp?.backtrackCells || [];
    const backtrackSet = new Set(backtrackCells.map(c => `${c.i ?? c.row},${c.w ?? c.col}`));

    const tbody = document.createElement("tbody");
    const itemLabels = [
      "0: None",
      "1: Med kit (10kg, 60)",
      "2: Blankets (20kg, 100)",
      "3: Water (30kg, 120)"
    ];

    const dpMatrix = data.dpTable || data.dp?.table || [];
    for (let i = 0; i < dpMatrix.length; i++) {
      const tr = document.createElement("tr");
      const rowTh = document.createElement("th");
      rowTh.textContent = itemLabels[i] || `Item ${i}`;
      rowTh.style.textAlign = "left";
      rowTh.style.paddingLeft = "8px";
      tr.appendChild(rowTh);

      for (let w = 0; w <= data.capacityKg; w += 5) {
        const td = document.createElement("td");
        td.textContent = dpMatrix[i][w];
        if (backtrackSet.has(`${i},${w}`)) {
          td.className = "backtrack-cell";
          td.title = `Backtracking State: i=${i}, w=${w}kg \u2192 Urgency ${dpMatrix[i][w]}`;
        }
        tr.appendChild(td);
      }
      tbody.appendChild(tr);
    }
    table.appendChild(tbody);
    container.appendChild(table);

    dom.cardDemoContainer.style.display = "block";
    dom.cardDemoContainer.scrollIntoView({ behavior: "smooth" });

  } catch (err) {
    showToast("Failed to load knapsack counterexample demo: " + err.message);
  }
}

// Injects active relief cargo allocation details into the printable view (window.print()).
function renderPrintReliefSection() {
  if (!state.currentLoadPlan) {
    dom.printReliefSection.style.display = "none";
    return;
  }

  const p = state.currentLoadPlan;
  const depotName = state.nodes.get(p.depotId)?.name || p.depotId;
  const villageName = state.nodes.get(p.villageId)?.name || p.villageId;

  dom.printReliefSection.style.display = "block";
  dom.printReliefSection.innerHTML = `
    <h3 style="font-size:18px; margin-bottom:8px;">Relief Truck Loading &amp; Deployment Manifest (0/1 Knapsack DP)</h3>
    <p style="font-size:13px; color:#444; margin-bottom:10px;">
      Dispatch Depot: <strong>${depotName}</strong> &bull; Target Village: <strong>${villageName}</strong> &bull; Truck Limit: <strong>${p.capacityKg} kg</strong> &bull; Trip Distance: <strong>${p.routeDistanceKm.toFixed(1)} km</strong>
    </p>
    <div style="font-size:13px; margin-bottom:8px;">
      <strong>Optimal Load Summary:</strong> Urgency: <strong>${p.dp.totalUrgency} pts</strong> (${p.urgencyGain > 0 ? `+${p.urgencyGain} pts over Greedy` : 'Matches Greedy'}) &bull; Total Payload: <strong>${p.dp.totalWeightKg} / ${p.capacityKg} kg</strong>
    </div>
    <div style="font-size:13px; margin-bottom:6px;"><strong>Delivery Corridor Waypoints:</strong> ${p.routePath.map(nid => state.nodes.get(nid)?.name || nid).join(" \u2192 ")}</div>
  `;
}


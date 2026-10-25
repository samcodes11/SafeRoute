// server.h — HTTP server interface for SafeRoute
//
// What this file does:
//   Declares the main entry point for the embedded HTTP web server.
//   It connects the C++ algorithm engine (graph routing, disaster scenarios,
//   and relief truck loading) to the web browser interface.
//
// What is inside:
//   - startServer: starts the HTTP listener and event loop.
//
// Other files that use this header:
//   - src/server.cpp: implements the REST API routes and static asset serving.
//   - src/main.cpp: calls startServer when running in default web mode.

// Include guard: prevents multiple inclusion errors during compilation.
#ifndef SAFEROUTE_SERVER_H
#define SAFEROUTE_SERVER_H

#include "graph.h"
#include <string>

// Starts the embedded HTTP web server on the specified port.
//
// How it works:
//   1. Mounts the local webRoot folder (typically "web") to serve HTML, CSS, JS,
//      and local font files directly to browsers.
//   2. Registers REST endpoints (/api/network, /api/compare, /api/load-plan, etc.).
//   3. Runs a blocking event loop on 0.0.0.0 listening for incoming HTTP connections.
//
// Input:
//   - Graph& graph: reference to the active road network in memory.
//   - int port: TCP port number to bind (default 8080).
//   - const std::string& webRoot: relative or absolute path to static frontend files.
//
// Output: None (blocks indefinitely until process termination or SIGINT).
// Errors: May throw std::runtime_error or log an error if the TCP port is unavailable.
void startServer(Graph& graph, int port,
                 const std::string& webRoot);

#endif // SAFEROUTE_SERVER_H

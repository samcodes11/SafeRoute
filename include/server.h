// Declares the function that starts the web server.
#ifndef SAFEROUTE_SERVER_H
#define SAFEROUTE_SERVER_H

#include "graph.h"
#include <string>

// Serve the web files and API on the given port.
void startServer(Graph& graph, int port,
                 const std::string& webRoot);

#endif // SAFEROUTE_SERVER_H

/*
 * POS Server Example (Worker Node)
 *
 * Demonstrates running a POS server on a worker node.
 * This server receives DFG deployment requests from client nodes (gRPC clients)
 * and manages DFG instances on the local FPGA.
 *
 * Usage: ./pos_server_example [options]
 *
 * Copyright (c) 2025, Systems Group, ETH Zurich
 * MIT License
 */

#include "pos_server.hpp"

#include <iostream>
#include <csignal>
#include <atomic>

// Global server pointer for signal handling
static pos::POSServer* g_server = nullptr;
static std::atomic<bool> g_running{true};

void signalHandler(int signum) {
    std::cout << "\nReceived signal " << signum << ", shutting down..." << std::endl;
    g_running = false;
    if (g_server) {
        g_server->stop();
    }
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  -p, --port PORT    Server port (default: 50052)\n"
              << "  -a, --address ADDR Server address (default: 0.0.0.0)\n"
              << "  -m, --max-msg SIZE Max message size in MB (default: 64)\n"
              << "  -h, --help         Show this help message\n"
              << "\n"
              << "Example:\n"
              << "  " << program << " --port 50052 --address 0.0.0.0\n";
}

int main(int argc, char* argv[]) {
    std::string address = "0.0.0.0";
    int port = 50052;
    int max_message_size_mb = 64;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if ((arg == "-a" || arg == "--address") && i + 1 < argc) {
            address = argv[++i];
        } else if ((arg == "-m" || arg == "--max-msg") && i + 1 < argc) {
            max_message_size_mb = std::stoi(argv[++i]);
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    // Build server address string
    std::string server_address = address + ":" + std::to_string(port);
    int max_message_size = max_message_size_mb * 1024 * 1024;

    std::cout << "======================================\n";
    std::cout << "     POS Server (Worker Node)\n";
    std::cout << "======================================\n";
    std::cout << "Address:          " << server_address << "\n";
    std::cout << "Max message size: " << max_message_size_mb << " MB\n";
    std::cout << "--------------------------------------\n";

    // Set up signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    try {
        // Create and start server
        pos::POSServer server(server_address, max_message_size);
        g_server = &server;

        std::cout << "Starting POS server...\n";

        // Run server (blocking)
        server.run();

        std::cout << "Server stopped.\n";

        // Print final statistics
        if (auto* service = server.getService()) {
            std::cout << "\n--- Final Statistics ---\n";
            std::cout << "Active DFG instances: " << service->getActiveInstanceCount() << "\n";
            std::cout << "Server uptime:        " << service->getUptimeSeconds() << " seconds\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    g_server = nullptr;
    return 0;
}

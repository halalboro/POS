/**
 * P4Runtime Server Application
 *
 * This example demonstrates how to run a P4Runtime gRPC server that allows
 * remote clients to program the FPGA routing tables.
 *
 * The server:
 * 1. Creates a Coyote thread (cThread) for FPGA communication
 * 2. Creates a POSRuntimeEngine for table management
 * 3. Starts a gRPC server that wraps the engine
 *
 * Usage:
 *   ./p4runtime_server --vfid=0 --port=50051 [--debug=1]
 *
 * Arguments:
 *   --vfid    : Virtual FPGA ID (default: 0)
 *   --port    : gRPC server port (default: 50051)
 *   --debug   : Debug level 0-2 (default: 1)
 *   --address : Bind address (default: 0.0.0.0)
 */

#include <iostream>
#include <csignal>
#include <atomic>

#include <boost/program_options.hpp>

#include "cThread.hpp"
#include "runtime_engine.hpp"
#include "p4runtime_server.hpp"

using namespace coyote;
namespace po = boost::program_options;

// Global flag for graceful shutdown
std::atomic<bool> g_shutdown_requested(false);

// Signal handler
void signalHandler(int signum) {
    std::cout << "\nReceived signal " << signum << ", shutting down..." << std::endl;
    g_shutdown_requested = true;
}

int main(int argc, char* argv[]) {
    // =========================================================================
    // Parse Command Line Arguments
    // =========================================================================

    po::options_description desc("P4Runtime Server Options");
    desc.add_options()
        ("help,h", "Show help message")
        ("vfid,v", po::value<int32_t>()->default_value(0), "Virtual FPGA ID")
        ("port,p", po::value<int>()->default_value(50051), "gRPC server port")
        ("address,a", po::value<std::string>()->default_value("0.0.0.0"), "Bind address")
        ("debug,d", po::value<int>()->default_value(1), "Debug level (0-2)")
        ("dev", po::value<uint32_t>()->default_value(0), "Device number");

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    } catch (const po::error& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << desc << std::endl;
        return 1;
    }

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 0;
    }

    int32_t vfid = vm["vfid"].as<int32_t>();
    int port = vm["port"].as<int>();
    std::string address = vm["address"].as<std::string>();
    int debug = vm["debug"].as<int>();
    uint32_t dev = vm["dev"].as<uint32_t>();

    std::string server_address = address + ":" + std::to_string(port);

    // =========================================================================
    // Setup Signal Handlers
    // =========================================================================

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // =========================================================================
    // Initialize FPGA and Runtime Engine
    // =========================================================================

    std::cout << "========================================" << std::endl;
    std::cout << "P4Runtime Server" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  vFPGA ID:       " << vfid << std::endl;
    std::cout << "  Device:         " << dev << std::endl;
    std::cout << "  Server Address: " << server_address << std::endl;
    std::cout << "  Debug Level:    " << debug << std::endl;
    std::cout << "========================================" << std::endl;

    try {
        // Create Coyote thread for FPGA communication
        std::cout << "\nInitializing Coyote thread..." << std::endl;
        cThread cthread(vfid, getpid(), dev);

        // Create runtime engine
        std::cout << "Initializing POS Runtime Engine..." << std::endl;
        pos::POSRuntimeEngine engine(&cthread, debug);

        // Verify hardware
        std::cout << "Verifying hardware..." << std::endl;
        if (!engine.verifyHardware()) {
            std::cerr << "WARNING: Hardware verification failed!" << std::endl;
            std::cerr << "Server will continue, but operations may fail." << std::endl;
        } else {
            std::cout << "Hardware verification passed." << std::endl;
        }

        // =====================================================================
        // Start gRPC Server
        // =====================================================================

        std::cout << "\nStarting gRPC server on " << server_address << "..." << std::endl;

        pos::P4RuntimeServer server(&engine, server_address);

        // Run server (blocks until shutdown)
        // Note: run() will block, so we can't check g_shutdown_requested in a loop.
        // The signal handler will cause the gRPC server to exit.
        // For more control, use server.start() + server.stop() pattern.

        std::cout << "\n========================================" << std::endl;
        std::cout << "Server is running. Press Ctrl+C to stop." << std::endl;
        std::cout << "========================================" << std::endl;

        server.run();

        std::cout << "\nServer stopped." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Goodbye!" << std::endl;
    return 0;
}

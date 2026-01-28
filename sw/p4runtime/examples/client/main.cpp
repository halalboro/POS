/**
 * P4Runtime Client Application
 *
 * This example demonstrates how to use the P4Runtime gRPC client to remotely
 * program FPGA routing tables on a server.
 *
 * The client can:
 * - Add forwarding rules
 * - Add drop rules
 * - Delete routes
 * - Clear all entries
 * - Query statistics
 *
 * Usage:
 *   ./test --server=192.168.1.100:50051 --action=add --ip=10.0.0.0/8 --mac=aa:bb:cc:dd:ee:ff --port=1
 *   ./test --server=192.168.1.100:50051 --action=drop --ip=192.168.0.0/16
 *   ./test --server=192.168.1.100:50051 --action=delete --ip=10.0.0.0/8
 *   ./test --server=192.168.1.100:50051 --action=clear
 *   ./test --server=192.168.1.100:50051 --action=stats
 *   ./test --server=192.168.1.100:50051 --action=verify
 *
 * Arguments:
 *   --server  : Server address (default: localhost:50051)
 *   --action  : Action to perform (add/drop/delete/clear/stats/verify/batch)
 *   --ip      : IP CIDR for add/drop/delete (e.g., "10.0.0.0/8")
 *   --mac     : MAC address for add (e.g., "aa:bb:cc:dd:ee:ff")
 *   --port    : Egress port for add
 *   --timeout : RPC timeout in ms (default: 5000)
 */

#include <iostream>
#include <vector>
#include <tuple>

#include <boost/program_options.hpp>

#include "p4runtime_client.hpp"

namespace po = boost::program_options;

void printUsage() {
    std::cout << "\nActions:" << std::endl;
    std::cout << "  add     - Add a forwarding rule (requires --ip, --mac, --port)" << std::endl;
    std::cout << "  drop    - Add a drop rule (requires --ip)" << std::endl;
    std::cout << "  delete  - Delete a route (requires --ip)" << std::endl;
    std::cout << "  clear   - Clear all routing entries" << std::endl;
    std::cout << "  stats   - Print routing table statistics" << std::endl;
    std::cout << "  verify  - Verify hardware connection" << std::endl;
    std::cout << "  batch   - Add example batch of rules (demo)" << std::endl;
    std::cout << "  lookup  - Look up a specific route (requires --ip)" << std::endl;
    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    // =========================================================================
    // Parse Command Line Arguments
    // =========================================================================

    po::options_description desc("P4Runtime Client Options");
    desc.add_options()
        ("help,h", "Show help message")
        ("server,s", po::value<std::string>()->default_value("localhost:50051"), "Server address (host:port)")
        ("action,a", po::value<std::string>()->default_value("stats"), "Action: add/drop/delete/clear/stats/verify/batch/lookup")
        ("ip,i", po::value<std::string>()->default_value(""), "IP CIDR (e.g., 10.0.0.0/8)")
        ("mac,m", po::value<std::string>()->default_value(""), "MAC address (e.g., aa:bb:cc:dd:ee:ff)")
        ("port,p", po::value<int>()->default_value(0), "Egress port number")
        ("timeout,t", po::value<int>()->default_value(5000), "RPC timeout (ms)");

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
        printUsage();
        return 0;
    }

    std::string server = vm["server"].as<std::string>();
    std::string action = vm["action"].as<std::string>();
    std::string ip = vm["ip"].as<std::string>();
    std::string mac = vm["mac"].as<std::string>();
    int port = vm["port"].as<int>();
    int timeout = vm["timeout"].as<int>();

    // =========================================================================
    // Create Client
    // =========================================================================

    std::cout << "P4Runtime Client" << std::endl;
    std::cout << "=================" << std::endl;
    std::cout << "Server: " << server << std::endl;
    std::cout << "Action: " << action << std::endl;
    std::cout << std::endl;

    pos::P4RuntimeClient client(server, timeout);

    // Wait for connection
    std::cout << "Connecting to server..." << std::endl;
    if (!client.waitForConnection(timeout)) {
        std::cerr << "Error: Could not connect to server at " << server << std::endl;
        std::cerr << "Make sure the P4Runtime server is running." << std::endl;
        return 1;
    }
    std::cout << "Connected!" << std::endl << std::endl;

    // =========================================================================
    // Execute Action
    // =========================================================================

    bool success = false;
    uint32_t idx = 0;

    if (action == "add") {
        // Add forwarding rule
        if (ip.empty()) {
            std::cerr << "Error: --ip is required for 'add' action" << std::endl;
            return 1;
        }
        if (mac.empty()) {
            std::cerr << "Error: --mac is required for 'add' action" << std::endl;
            return 1;
        }

        std::cout << "Adding forwarding rule:" << std::endl;
        std::cout << "  IP:   " << ip << std::endl;
        std::cout << "  MAC:  " << mac << std::endl;
        std::cout << "  Port: " << port << std::endl;

        success = client.addForwardingRule(ip, mac, static_cast<uint16_t>(port), &idx);

        if (success) {
            std::cout << "SUCCESS: Rule added at index " << idx << std::endl;
        } else {
            std::cerr << "FAILED: " << client.getLastError() << std::endl;
        }
    }
    else if (action == "drop") {
        // Add drop rule
        if (ip.empty()) {
            std::cerr << "Error: --ip is required for 'drop' action" << std::endl;
            return 1;
        }

        std::cout << "Adding drop rule:" << std::endl;
        std::cout << "  IP: " << ip << std::endl;

        success = client.addDropRule(ip, &idx);

        if (success) {
            std::cout << "SUCCESS: Drop rule added at index " << idx << std::endl;
        } else {
            std::cerr << "FAILED: " << client.getLastError() << std::endl;
        }
    }
    else if (action == "delete") {
        // Delete route
        if (ip.empty()) {
            std::cerr << "Error: --ip is required for 'delete' action" << std::endl;
            return 1;
        }

        std::cout << "Deleting route: " << ip << std::endl;

        success = client.deleteRoute(ip);

        if (success) {
            std::cout << "SUCCESS: Route deleted" << std::endl;
        } else {
            std::cerr << "FAILED: " << client.getLastError() << std::endl;
        }
    }
    else if (action == "clear") {
        // Clear all entries
        std::cout << "Clearing all routing entries..." << std::endl;

        success = client.clearAllEntries();

        if (success) {
            std::cout << "SUCCESS: All entries cleared" << std::endl;
        } else {
            std::cerr << "FAILED: " << client.getLastError() << std::endl;
        }
    }
    else if (action == "stats") {
        // Print statistics
        client.printStatistics();
        success = true;
    }
    else if (action == "verify") {
        // Verify hardware
        std::cout << "Verifying hardware..." << std::endl;

        success = client.verifyHardware();

        if (success) {
            std::cout << "SUCCESS: Hardware verification passed" << std::endl;
        } else {
            std::cerr << "FAILED: " << client.getLastError() << std::endl;
        }
    }
    else if (action == "lookup") {
        // Look up a route
        if (ip.empty()) {
            std::cerr << "Error: --ip is required for 'lookup' action" << std::endl;
            return 1;
        }

        std::cout << "Looking up route for: " << ip << std::endl;

        pos::ClientTableEntry entry;
        success = client.findRoute(ip, &entry);

        if (success) {
            std::cout << "FOUND:" << std::endl;
            std::cout << "  Table:   " << entry.table_name << std::endl;
            std::cout << "  Index:   " << entry.entry_idx << std::endl;
            std::cout << "  Prefix:  " << pos::P4RuntimeClient::formatIPAddress(entry.prefix)
                      << "/" << static_cast<int>(entry.prefix_len) << std::endl;
            std::cout << "  Action:  " << entry.action_name << std::endl;
            std::cout << "  MAC:     " << pos::P4RuntimeClient::formatMACAddress(entry.dst_mac) << std::endl;
            std::cout << "  Port:    " << entry.egress_port << std::endl;
        } else {
            std::cout << "NOT FOUND" << std::endl;
        }
    }
    else if (action == "batch") {
        // Demo: Add batch of rules
        std::cout << "Adding batch of example rules..." << std::endl;

        std::vector<std::tuple<std::string, std::string, uint16_t, std::string>> rules = {
            {"10.0.0.0/8",      "aa:bb:cc:dd:ee:01", 1, "forward"},
            {"192.168.1.0/24",  "aa:bb:cc:dd:ee:02", 2, "forward"},
            {"192.168.2.0/24",  "aa:bb:cc:dd:ee:03", 3, "forward"},
            {"172.16.0.0/12",   "",                  0, "drop"},
            {"0.0.0.0/0",       "aa:bb:cc:dd:ee:ff", 1, "forward"},  // Default route
        };

        int count = client.addRoutingRules(rules);

        std::cout << "Added " << count << "/" << rules.size() << " rules" << std::endl;
        success = (count == static_cast<int>(rules.size()));

        // Show current state
        std::cout << std::endl;
        client.printStatistics();
    }
    else {
        std::cerr << "Unknown action: " << action << std::endl;
        printUsage();
        return 1;
    }

    return success ? 0 : 1;
}

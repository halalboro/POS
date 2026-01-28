/*
 * POS Client Example (Client Node - gRPC client)
 *
 * Demonstrates using the POS client (gRPC client) to deploy and manage
 * DFGs on remote POS worker nodes.
 *
 * Usage: ./pos_client_example [server_address] [command] [args...]
 *
 * Copyright (c) 2025, Systems Group, ETH Zurich
 * MIT License
 */

#include "pos_client.hpp"

#include <iostream>
#include <iomanip>

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " <server_address> <command> [args...]\n"
              << "\n"
              << "Commands:\n"
              << "  health                         Check POS server health\n"
              << "  list                           List all deployed DFGs\n"
              << "  status <instance_id>           Get status of a specific DFG\n"
              << "  deploy-simple                  Deploy a simple example DFG\n"
              << "  deploy-smartnic                Deploy SmartNIC model example\n"
              << "  deploy-middlebox               Deploy host-based middlebox example\n"
              << "  undeploy <instance_id>         Undeploy a DFG\n"
              << "  execute <instance_id> <node>   Execute a node\n"
              << "  write <instance_id> <buffer>   Write test data to a buffer\n"
              << "  read <instance_id> <buffer>    Read data from a buffer\n"
              << "\n"
              << "Example:\n"
              << "  " << program << " localhost:50052 health\n"
              << "  " << program << " localhost:50052 deploy-simple\n"
              << "  " << program << " localhost:50052 status inst_1\n";
}

void printResult(const std::string& operation, bool success, const std::string& message) {
    std::cout << operation << ": " << (success ? "SUCCESS" : "FAILED") << "\n";
    if (!message.empty()) {
        std::cout << "  Message: " << message << "\n";
    }
}

std::string stateToString(pos::DFGInstanceStatus::State state) {
    switch (state) {
        case pos::DFGInstanceStatus::State::DEPLOYING: return "DEPLOYING";
        case pos::DFGInstanceStatus::State::RUNNING: return "RUNNING";
        case pos::DFGInstanceStatus::State::STALLED: return "STALLED";
        case pos::DFGInstanceStatus::State::ERROR: return "ERROR";
        case pos::DFGInstanceStatus::State::STOPPED: return "STOPPED";
        default: return "UNKNOWN";
    }
}

int cmdHealth(pos::POSClient& client) {
    auto result = client.healthCheck();
    if (result.success) {
        std::cout << "POS Server Health: " << (result.value.healthy ? "HEALTHY" : "UNHEALTHY") << "\n";
        std::cout << "  Active instances: " << result.value.active_dfgs << "\n";
        std::cout << "  Uptime: " << result.value.uptime_seconds << " seconds\n";
        std::cout << "  Server version: " << result.value.version << "\n";
        std::cout << "  Available vFPGAs: " << result.value.available_vfpgas << "\n";
        return 0;
    } else {
        std::cerr << "Health check failed: " << result.error_message << "\n";
        return 1;
    }
}

int cmdList(pos::POSClient& client) {
    auto result = client.listDFGs();
    if (result.success) {
        if (result.value.empty()) {
            std::cout << "No DFGs deployed.\n";
        } else {
            std::cout << "Deployed DFGs (" << result.value.size() << "):\n";
            std::cout << std::left << std::setw(25) << "Instance ID"
                      << std::setw(20) << "DFG ID"
                      << std::setw(12) << "State"
                      << "Uptime\n";
            std::cout << std::string(70, '-') << "\n";
            for (const auto& info : result.value) {
                std::cout << std::left << std::setw(25) << info.instance_id
                          << std::setw(20) << info.dfg_id
                          << std::setw(12) << stateToString(info.state)
                          << info.uptime_seconds << "s\n";
            }
        }
        return 0;
    } else {
        std::cerr << "List failed: " << result.error_message << "\n";
        return 1;
    }
}

int cmdStatus(pos::POSClient& client, const std::string& instance_id) {
    auto result = client.getDFGStatus(instance_id);
    if (result.success) {
        std::cout << "DFG Status:\n";
        std::cout << "  Instance ID:          " << result.value.instance_id << "\n";
        std::cout << "  DFG ID:               " << result.value.dfg_id << "\n";
        std::cout << "  State:                " << stateToString(result.value.state) << "\n";
        std::cout << "  Uptime:               " << result.value.uptime_seconds << " seconds\n";
        std::cout << "  Bytes processed:      " << result.value.bytes_processed << "\n";
        std::cout << "  Operations completed: " << result.value.operations_completed << "\n";
        if (!result.value.error_message.empty()) {
            std::cout << "  Error:                " << result.value.error_message << "\n";
        }
        return 0;
    } else {
        std::cerr << "Get status failed: " << result.error_message << "\n";
        return 1;
    }
}

int cmdDeploySimple(pos::POSClient& client) {
    std::cout << "Deploying simple example DFG...\n";

    // Build a simple DFG spec
    auto spec = pos::POSClient::createDFGSpec("simple_dfg", "example_app", 0, false);

    // Add a compute node
    pos::POSClient::addComputeNode(spec.get(), "compute_0", 0);

    // Add buffers
    pos::POSClient::addBuffer(spec.get(), "input_buf", 64 * 1024);
    pos::POSClient::addBuffer(spec.get(), "output_buf", 64 * 1024);

    // Add edges: input_buf -> compute_0 -> output_buf
    pos::POSClient::addEdge(spec.get(), "input_buf", "compute_0");
    pos::POSClient::addEdge(spec.get(), "compute_0", "output_buf");

    auto result = client.deployDFG(*spec);
    if (result.success) {
        std::cout << "Deployed successfully!\n";
        std::cout << "  Instance ID: " << result.value.instance_id << "\n";
        std::cout << "  DFG ID:      " << result.value.dfg_id << "\n";
        return 0;
    } else {
        std::cerr << "Deploy failed: " << result.error_message << "\n";
        return 1;
    }
}

int cmdDeploySmartNIC(pos::POSClient& client) {
    std::cout << "Deploying SmartNIC model example DFG...\n";

    auto spec = pos::POSClient::createDFGSpec("smartnic_dfg", "smartnic_app", 0, false);

    // Add RDMA network nodes
    pos::POSClient::addRDMANode(spec.get(), "rdma_ingress", 100, "10.0.0.1", 4791);
    pos::POSClient::addRDMANode(spec.get(), "rdma_egress", 100, "10.0.0.2", 4791);

    // Add compute node
    pos::POSClient::addComputeNode(spec.get(), "nf_compute", 0);

    // Add buffers
    pos::POSClient::addBuffer(spec.get(), "rx_buf", 128 * 1024);
    pos::POSClient::addBuffer(spec.get(), "tx_buf", 128 * 1024);

    // Edges: rdma_ingress -> rx_buf -> nf_compute -> tx_buf -> rdma_egress
    pos::POSClient::addEdge(spec.get(), "rdma_ingress", "rx_buf");
    pos::POSClient::addEdge(spec.get(), "rx_buf", "nf_compute");
    pos::POSClient::addEdge(spec.get(), "nf_compute", "tx_buf");
    pos::POSClient::addEdge(spec.get(), "tx_buf", "rdma_egress");

    auto result = client.deployDFG(*spec);
    if (result.success) {
        std::cout << "Deployed SmartNIC DFG successfully!\n";
        std::cout << "  Instance ID: " << result.value.instance_id << "\n";
        return 0;
    } else {
        std::cerr << "Deploy failed: " << result.error_message << "\n";
        return 1;
    }
}

int cmdDeployMiddlebox(pos::POSClient& client) {
    std::cout << "Deploying host-based middlebox example DFG...\n";

    auto spec = pos::POSClient::createDFGSpec("middlebox_dfg", "middlebox_app", 0, false);

    // Software nodes (run on host CPU)
    pos::POSClient::addParserNode(spec.get(), "parser", 1024*1024, 50.0, 1);
    pos::POSClient::addSoftwareNFNode(spec.get(), "deparser", 1024*1024, 50.0, 1);

    // FPGA accelerator
    pos::POSClient::addComputeNode(spec.get(), "fpga_accel", 0);

    // Buffers
    pos::POSClient::addBuffer(spec.get(), "input_buf", 64 * 1024);
    pos::POSClient::addBuffer(spec.get(), "output_buf", 64 * 1024);

    // Pipeline: parser -> input_buf -> fpga_accel -> output_buf -> deparser
    pos::POSClient::addEdge(spec.get(), "parser", "input_buf");
    pos::POSClient::addEdge(spec.get(), "input_buf", "fpga_accel");
    pos::POSClient::addEdge(spec.get(), "fpga_accel", "output_buf");
    pos::POSClient::addEdge(spec.get(), "output_buf", "deparser");

    auto result = client.deployDFG(*spec);
    if (result.success) {
        std::cout << "Deployed middlebox DFG successfully!\n";
        std::cout << "  Instance ID: " << result.value.instance_id << "\n";
        return 0;
    } else {
        std::cerr << "Deploy failed: " << result.error_message << "\n";
        return 1;
    }
}

int cmdUndeploy(pos::POSClient& client, const std::string& instance_id) {
    auto result = client.undeployDFG(instance_id);
    printResult("Undeploy", result.success, result.error_message);
    return result.success ? 0 : 1;
}

int cmdExecute(pos::POSClient& client, const std::string& instance_id,
               const std::string& node_id, const std::string& cap_id) {
    auto result = client.executeNode(instance_id, node_id, cap_id, 0, 0, 0, 0, true);
    printResult("Execute node", result.success, result.error_message);
    if (result.success) {
        std::cout << "  Completion ID: " << result.value << "\n";
    }
    return result.success ? 0 : 1;
}

int cmdWrite(pos::POSClient& client, const std::string& instance_id,
             const std::string& buffer_id, const std::string& cap_id) {
    // Write some test data
    std::string test_data = "Hello, DFG! This is test data from the POS client.";

    auto result = client.writeBuffer(instance_id, buffer_id, cap_id, 0, test_data);
    printResult("Write buffer", result.success, result.error_message);
    if (result.success) {
        std::cout << "  Bytes written: " << test_data.size() << "\n";
    }
    return result.success ? 0 : 1;
}

int cmdRead(pos::POSClient& client, const std::string& instance_id,
            const std::string& buffer_id, const std::string& cap_id) {
    auto result = client.readBuffer(instance_id, buffer_id, cap_id, 0, 256);
    if (result.success) {
        std::cout << "Read buffer: SUCCESS\n";
        std::cout << "  Bytes read: " << result.value.size() << "\n";
        if (!result.value.empty()) {
            std::cout << "  Data (as string): ";
            // Print printable characters
            for (char c : result.value) {
                if (c >= 32 && c < 127) {
                    std::cout << c;
                } else if (c == 0) {
                    break;  // Null terminator
                } else {
                    std::cout << '.';
                }
            }
            std::cout << "\n";
        }
        return 0;
    } else {
        std::cerr << "Read buffer failed: " << result.error_message << "\n";
        return 1;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    std::string server_address = argv[1];
    std::string command = argv[2];
    std::string client_id = "example_client";

    // Handle help
    if (command == "-h" || command == "--help" || command == "help") {
        printUsage(argv[0]);
        return 0;
    }

    std::cout << "Connecting to POS Server at " << server_address << "...\n";

    try {
        pos::POSClient client(server_address, client_id);

        if (command == "health") {
            return cmdHealth(client);
        } else if (command == "list") {
            return cmdList(client);
        } else if (command == "status") {
            if (argc < 4) {
                std::cerr << "Usage: status <instance_id>\n";
                return 1;
            }
            return cmdStatus(client, argv[3]);
        } else if (command == "deploy-simple") {
            return cmdDeploySimple(client);
        } else if (command == "deploy-smartnic") {
            return cmdDeploySmartNIC(client);
        } else if (command == "deploy-middlebox") {
            return cmdDeployMiddlebox(client);
        } else if (command == "undeploy") {
            if (argc < 4) {
                std::cerr << "Usage: undeploy <instance_id>\n";
                return 1;
            }
            return cmdUndeploy(client, argv[3]);
        } else if (command == "execute") {
            if (argc < 5) {
                std::cerr << "Usage: execute <instance_id> <node_id> [cap_id]\n";
                return 1;
            }
            std::string cap_id = (argc > 5) ? argv[5] : "root_cap";
            return cmdExecute(client, argv[3], argv[4], cap_id);
        } else if (command == "write") {
            if (argc < 5) {
                std::cerr << "Usage: write <instance_id> <buffer_id> [cap_id]\n";
                return 1;
            }
            std::string cap_id = (argc > 5) ? argv[5] : "root_cap";
            return cmdWrite(client, argv[3], argv[4], cap_id);
        } else if (command == "read") {
            if (argc < 5) {
                std::cerr << "Usage: read <instance_id> <buffer_id> [cap_id]\n";
                return 1;
            }
            std::string cap_id = (argc > 5) ? argv[5] : "root_cap";
            return cmdRead(client, argv[3], argv[4], cap_id);
        } else {
            std::cerr << "Unknown command: " << command << "\n";
            printUsage(argv[0]);
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

/*
 * P4Runtime gRPC Client
 *
 * Provides client-side interface for remote P4 table management.
 * Used by applications on the client node to communicate with the
 * P4Runtime server on the worker node.
 *
 * Copyright (c) 2025, Systems Group, ETH Zurich
 * MIT License
 */

#pragma once

#include <grpcpp/grpcpp.h>

#include <string>
#include <vector>
#include <tuple>
#include <memory>

// Forward declaration of generated protobuf types
// Note: proto package is "pos.p4runtime" which generates namespace pos::p4runtime
namespace pos {
namespace p4runtime {
    class P4RuntimeService;
} // namespace p4runtime
} // namespace pos

namespace pos {

/**
 * Client-side table entry representation
 */
struct ClientTableEntry {
    std::string table_name;
    uint32_t entry_idx;
    uint32_t prefix;
    uint8_t prefix_len;
    std::string action_name;
    uint64_t dst_mac;
    uint16_t egress_port;
    uint32_t priority;
    std::string description;

    ClientTableEntry() : entry_idx(0), prefix(0), prefix_len(0),
                         dst_mac(0), egress_port(0), priority(0) {}
};

/**
 * Client-side statistics
 */
struct ClientStats {
    uint32_t total_entries;
    uint32_t max_entries;
    uint32_t next_entry_idx;
    std::vector<ClientTableEntry> entries;
};

/**
 * P4Runtime gRPC Client
 *
 * Provides a simple interface for remote P4 table management.
 */
class P4RuntimeClient {
public:
    /**
     * Constructor
     * @param server_address - Server address (e.g., "localhost:50051")
     * @param timeout_ms - RPC timeout in milliseconds
     */
    P4RuntimeClient(const std::string& server_address = "localhost:50051",
                    int timeout_ms = 5000);

    ~P4RuntimeClient();

    // Non-copyable
    P4RuntimeClient(const P4RuntimeClient&) = delete;
    P4RuntimeClient& operator=(const P4RuntimeClient&) = delete;

    // ================================================================
    // Connection Management
    // ================================================================

    /**
     * Check if connected to server
     */
    bool isConnected() const;

    /**
     * Wait for connection to be established
     * @param timeout_ms - Timeout in milliseconds
     * @return true if connected within timeout
     */
    bool waitForConnection(int timeout_ms = 5000);

    /**
     * Get last error message
     */
    const std::string& getLastError() const { return last_error_; }

    // ================================================================
    // Core Table Operations
    // ================================================================

    /**
     * Install a table entry
     * @param entry - Entry to install
     * @param assigned_idx - Output: assigned entry index
     * @return true if successful
     */
    bool installTableEntry(const ClientTableEntry& entry, uint32_t* assigned_idx = nullptr);

    /**
     * Modify an existing table entry
     * @param entry_idx - Index of entry to modify
     * @param entry - New entry data
     * @return true if successful
     */
    bool modifyTableEntry(uint32_t entry_idx, const ClientTableEntry& entry);

    /**
     * Delete a table entry
     * @param entry_idx - Index of entry to delete
     * @return true if successful
     */
    bool deleteTableEntry(uint32_t entry_idx);

    /**
     * Read all table entries
     * @return Vector of all installed entries
     */
    std::vector<ClientTableEntry> readTableEntries();

    /**
     * Clear all table entries
     * @return true if successful
     */
    bool clearAllEntries();

    // ================================================================
    // High-Level Convenience API
    // ================================================================

    /**
     * Add a forwarding rule
     * @param ip_cidr - IP in CIDR notation (e.g., "192.168.1.0/24")
     * @param mac - Destination MAC address
     * @param port - Egress port
     * @param assigned_idx - Output: assigned entry index
     * @return true if successful
     */
    bool addForwardingRule(const std::string& ip_cidr,
                           const std::string& mac,
                           uint16_t port,
                           uint32_t* assigned_idx = nullptr);

    /**
     * Add a drop rule
     * @param ip_cidr - IP in CIDR notation
     * @param assigned_idx - Output: assigned entry index
     * @return true if successful
     */
    bool addDropRule(const std::string& ip_cidr,
                     uint32_t* assigned_idx = nullptr);

    /**
     * Add a default route
     * @param action - Action name ("forward", "drop", "noaction")
     * @param mac - Destination MAC (for forward)
     * @param port - Egress port (for forward)
     * @param assigned_idx - Output: assigned entry index
     * @return true if successful
     */
    bool addDefaultRoute(const std::string& action,
                         const std::string& mac = "",
                         uint16_t port = 0,
                         uint32_t* assigned_idx = nullptr);

    /**
     * Add multiple routing rules
     * @param rules - Vector of (ip_cidr, mac, port, action) tuples
     * @return Number of rules successfully installed
     */
    int addRoutingRules(const std::vector<std::tuple<std::string, std::string,
                                                      uint16_t, std::string>>& rules);

    // ================================================================
    // Route Management
    // ================================================================

    /**
     * Find a route by IP address
     * @param ip_cidr - IP in CIDR notation
     * @param entry - Output: found entry (if any)
     * @return true if route found
     */
    bool findRoute(const std::string& ip_cidr, ClientTableEntry* entry = nullptr);

    /**
     * Delete a route by IP address
     * @param ip_cidr - IP in CIDR notation
     * @return true if successful
     */
    bool deleteRoute(const std::string& ip_cidr);

    /**
     * Check if a route exists
     * @param ip_cidr - IP in CIDR notation
     * @return true if route exists
     */
    bool hasRoute(const std::string& ip_cidr);

    // ================================================================
    // Statistics and Monitoring
    // ================================================================

    /**
     * Get number of installed routes
     */
    uint32_t getRouteCount();

    /**
     * Get full statistics
     */
    ClientStats getStats();

    /**
     * Print statistics to stdout
     */
    void printStatistics();

    /**
     * Verify hardware is functioning
     * @return true if hardware OK
     */
    bool verifyHardware();

    // ================================================================
    // Static Helpers
    // ================================================================

    static std::pair<uint32_t, uint8_t> parseCIDR(const std::string& cidr);
    static uint32_t parseIPAddress(const std::string& ip_str);
    static uint64_t parseMACAddress(const std::string& mac_str);
    static std::string formatIPAddress(uint32_t ip);
    static std::string formatMACAddress(uint64_t mac);

private:
    std::unique_ptr<grpc::ClientContext> createContext() const;

    std::string server_address_;
    int timeout_ms_;
    std::string last_error_;

    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<pos::p4runtime::P4RuntimeService::Stub> stub_;
};

} // namespace pos

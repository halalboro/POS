/*
 * POS Runtime Engine
 *
 * Translates P4 control plane operations to hardware CSR writes.
 * This module is part of the POS (Programmable OS) kernel and provides
 * the Table Manager functionality for P4Runtime integration.
 *
 * Architecture:
 *   P4Runtime gRPC Server ──┐
 *                           ├──→ POS Shell Manager ──→ CCM ──→ vFPGA
 *   POS Application ────────┘
 *
 * Copyright (c) 2025, Systems Group, ETH Zurich
 * MIT License
 */

#pragma once

#include "cDefs.hpp"
#include "cThread.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <iostream>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>

namespace pos {

// ============================================================================
// Access Control Layer (ACL)
// ============================================================================

/**
 * ACL Permission flags for table operations
 */
enum class TablePermission : uint32_t {
    NONE        = 0,
    READ        = 1 << 0,   // Can read table entries
    WRITE       = 1 << 1,   // Can install/modify entries
    DELETE      = 1 << 2,   // Can delete entries
    CLEAR       = 1 << 3,   // Can clear all entries
    ADMIN       = 1 << 4,   // Can manage ACLs

    // Common combinations
    READ_ONLY   = READ,
    READ_WRITE  = READ | WRITE | DELETE,
    FULL_ACCESS = READ | WRITE | DELETE | CLEAR | ADMIN
};

// Bitwise operators for TablePermission
inline TablePermission operator|(TablePermission a, TablePermission b) {
    return static_cast<TablePermission>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline TablePermission operator&(TablePermission a, TablePermission b) {
    return static_cast<TablePermission>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool hasPermission(TablePermission granted, TablePermission required) {
    return (static_cast<uint32_t>(granted) & static_cast<uint32_t>(required)) == static_cast<uint32_t>(required);
}

/**
 * Client identity for ACL lookups
 */
struct ClientIdentity {
    std::string client_id;      // Unique client identifier (e.g., "app1", "vNFC_2")
    std::string client_ip;      // Client IP address (for logging/audit)
    uint32_t vfpga_id;          // Associated vFPGA (0 = any)

    ClientIdentity() : vfpga_id(0) {}
    ClientIdentity(const std::string& id) : client_id(id), vfpga_id(0) {}
    ClientIdentity(const std::string& id, const std::string& ip, uint32_t vfpga = 0)
        : client_id(id), client_ip(ip), vfpga_id(vfpga) {}
};

/**
 * ACL entry for a client-table pair
 */
struct ACLEntry {
    std::string client_id;
    std::string table_name;     // "*" means all tables
    TablePermission permissions;
    uint64_t expiry_time;       // 0 = never expires (Unix timestamp)

    ACLEntry() : permissions(TablePermission::NONE), expiry_time(0) {}
    ACLEntry(const std::string& client, const std::string& table, TablePermission perms)
        : client_id(client), table_name(table), permissions(perms), expiry_time(0) {}
};

/**
 * Table Access Controller
 *
 * Manages access control lists for P4 table operations.
 * Integrates with POSRuntimeEngine to enforce permissions.
 */
class TableAccessController {
public:
    TableAccessController();
    ~TableAccessController() = default;

    // ================================================================
    // ACL Management (requires ADMIN permission)
    // ================================================================

    /**
     * Grant permissions to a client for a table
     * @param admin - Admin client making the change
     * @param client_id - Client to grant permissions to
     * @param table_name - Table name ("*" for all tables)
     * @param permissions - Permissions to grant
     * @return true if successful
     */
    bool grantPermission(const ClientIdentity& admin,
                         const std::string& client_id,
                         const std::string& table_name,
                         TablePermission permissions);

    /**
     * Revoke permissions from a client
     * @param admin - Admin client making the change
     * @param client_id - Client to revoke from
     * @param table_name - Table name ("*" for all tables)
     * @return true if successful
     */
    bool revokePermission(const ClientIdentity& admin,
                          const std::string& client_id,
                          const std::string& table_name);

    /**
     * Register a new client with default permissions
     * @param admin - Admin client making the change
     * @param client_id - New client ID
     * @param default_perms - Default permissions (default: READ_ONLY)
     * @return true if successful
     */
    bool registerClient(const ClientIdentity& admin,
                        const std::string& client_id,
                        TablePermission default_perms = TablePermission::READ_ONLY);

    /**
     * Unregister a client (removes all their ACL entries)
     * @param admin - Admin client making the change
     * @param client_id - Client to remove
     * @return true if successful
     */
    bool unregisterClient(const ClientIdentity& admin,
                          const std::string& client_id);

    // ================================================================
    // Permission Checking
    // ================================================================

    /**
     * Check if client has permission for an operation
     * @param client - Client identity
     * @param table_name - Table being accessed
     * @param required - Required permission
     * @return true if permitted
     */
    bool checkPermission(const ClientIdentity& client,
                         const std::string& table_name,
                         TablePermission required) const;

    /**
     * Get permissions for a client on a table
     * @param client_id - Client ID
     * @param table_name - Table name
     * @return Combined permissions
     */
    TablePermission getPermissions(const std::string& client_id,
                                   const std::string& table_name) const;

    /**
     * Check if client is registered
     */
    bool isClientRegistered(const std::string& client_id) const;

    /**
     * Check if client is an admin
     */
    bool isAdmin(const std::string& client_id) const;

    // ================================================================
    // Configuration
    // ================================================================

    /**
     * Enable or disable ACL enforcement
     * When disabled, all operations are allowed (useful for testing)
     */
    void setEnforcementEnabled(bool enabled) { enforcement_enabled_ = enabled; }
    bool isEnforcementEnabled() const { return enforcement_enabled_; }

    /**
     * Set the root/superuser client ID (always has full access)
     */
    void setRootClientId(const std::string& root_id) { root_client_id_ = root_id; }

    /**
     * Get audit log of recent access attempts
     */
    std::vector<std::string> getAuditLog(size_t max_entries = 100) const;

    /**
     * Clear audit log
     */
    void clearAuditLog();

    /**
     * Print current ACL state
     */
    void printACLState() const;

private:
    // ACL storage: client_id -> (table_name -> permissions)
    std::unordered_map<std::string, std::unordered_map<std::string, TablePermission>> acl_entries_;

    // Registered clients
    std::unordered_set<std::string> registered_clients_;

    // Admin clients
    std::unordered_set<std::string> admin_clients_;

    // Configuration
    bool enforcement_enabled_;
    std::string root_client_id_;

    // Audit log (circular buffer)
    mutable std::vector<std::string> audit_log_;
    static constexpr size_t MAX_AUDIT_LOG_SIZE = 1000;

    // Thread safety
    mutable std::mutex acl_mutex_;

    // Internal helpers
    void logAccess(const std::string& client_id, const std::string& table_name,
                   TablePermission required, bool allowed) const;
    TablePermission getEffectivePermissions(const std::string& client_id,
                                            const std::string& table_name) const;
};

/**
 * Router action types (matching HDL implementation)
 */
enum class RouterAction : uint8_t {
    DROP = 0,
    FORWARD = 1,
    NOACTION = 2
};

/**
 * P4 Table Entry - Internal representation
 */
struct P4TableEntry {
    std::string table_name;
    uint32_t entry_idx;
    uint32_t prefix;
    uint8_t prefix_len;
    std::string action_name;
    uint64_t dst_mac;
    uint16_t egress_port;

    // Optional fields
    uint32_t priority;
    std::string description;

    // Constructor
    P4TableEntry() : entry_idx(0), prefix(0), prefix_len(0),
                     dst_mac(0), egress_port(0), priority(0) {}
};

/**
 * P4Info Metadata - Simplified for basic router
 */
struct P4InfoMetadata {
    std::unordered_map<std::string, uint32_t> table_name_to_id;
    std::unordered_map<std::string, RouterAction> action_name_to_code;
    std::unordered_map<std::string, uint32_t> table_max_size;

    P4InfoMetadata() {
        table_name_to_id["ipv4_lpm"] = 0;
        table_name_to_id["MyIngress.ipv4_lpm"] = 0;

        action_name_to_code["drop"] = RouterAction::DROP;
        action_name_to_code["ipv4_forward"] = RouterAction::FORWARD;
        action_name_to_code["NoAction"] = RouterAction::NOACTION;
        action_name_to_code["MyIngress.drop"] = RouterAction::DROP;
        action_name_to_code["MyIngress.ipv4_forward"] = RouterAction::FORWARD;

        table_max_size["ipv4_lpm"] = 1024;
    }
};

/**
 * POS Runtime Engine (Table Manager)
 *
 * Translates P4 control plane operations to hardware CSR writes.
 * Integrates with TableAccessController for ACL-based access control.
 *
 * Thread-safety: All public methods are thread-safe via internal mutex.
 *
 * Security Model:
 *   - All operations require a ClientIdentity for ACL checking
 *   - Operations without ClientIdentity use legacy API (for backward compatibility)
 *   - Legacy API checks against "anonymous" client (configurable permissions)
 */
class POSRuntimeEngine {
private:
    coyote::cThread* cthread;  // Note: POS uses fpga namespace
    P4InfoMetadata p4info;
    uint32_t next_entry_idx;
    int debug_level;

    std::unordered_map<uint32_t, P4TableEntry> installed_entries;
    mutable std::recursive_mutex entries_mutex;  // Recursive to allow nested locking

    // Access Control
    std::unique_ptr<TableAccessController> acl_;
    static const std::string ANONYMOUS_CLIENT_ID;  // "anonymous"

    // Internal unlocked versions for use when lock is already held
    bool installTableEntryInternal(const P4TableEntry& entry);
    bool deleteTableEntryInternal(uint32_t entry_idx);
    std::optional<P4TableEntry> findRouteByIPInternal(const std::string& ip) const;

    static constexpr int MAX_HW_RETRIES = 10000;
    static constexpr int RETRY_LOG_INTERVAL = 1000;

    static uint32_t parseIPv4(const std::string& ip_str);
    static uint64_t parseMAC(const std::string& mac_str);
    static std::string ipv4ToString(uint32_t ip);
    static std::string macToString(uint64_t mac);

    RouterAction translateAction(const std::string& action_name) const;
    bool validateTableEntry(const P4TableEntry& entry) const;
    bool validatePrefixLength(uint32_t& prefix, uint8_t prefix_len) const;

    void programRouteEntry(uint32_t entry_idx, uint32_t prefix, uint8_t prefix_len,
                          RouterAction action, uint64_t dst_mac, uint16_t egress_port);

    void log(int level, const std::string& msg) const;
    void logError(const std::string& msg) const;

public:
    /**
     * Constructor
     * @param thread - FPGA thread for hardware communication (coyote::cThread)
     * @param debug - Debug level (0=off, 1=info, 2=verbose)
     */
    POSRuntimeEngine(coyote::cThread* thread, int debug = 1);
    ~POSRuntimeEngine();

    // Non-copyable
    POSRuntimeEngine(const POSRuntimeEngine&) = delete;
    POSRuntimeEngine& operator=(const POSRuntimeEngine&) = delete;

    void setDebugLevel(int level) { debug_level = level; }

    // ================================================================
    // Access Control API
    // ================================================================

    /**
     * Get the ACL controller for direct ACL management
     */
    TableAccessController* getACL() { return acl_.get(); }
    const TableAccessController* getACL() const { return acl_.get(); }

    /**
     * Enable or disable ACL enforcement globally
     */
    void setACLEnabled(bool enabled) { acl_->setEnforcementEnabled(enabled); }
    bool isACLEnabled() const { return acl_->isEnforcementEnabled(); }

    // ================================================================
    // Core API (with ACL enforcement)
    // ================================================================

    bool loadP4Info(const std::string& p4info_path = "");
    bool loadControlPlaneRules(const std::string& json_path, const ClientIdentity& client);

    /**
     * Install a table entry (ACL-enforced)
     * @param entry - Entry to install
     * @param client - Client identity for ACL check
     * @return true if successful
     */
    bool installTableEntry(const P4TableEntry& entry, const ClientIdentity& client);

    /**
     * Modify an existing table entry (ACL-enforced)
     */
    bool modifyTableEntry(uint32_t entry_idx, const P4TableEntry& entry, const ClientIdentity& client);

    /**
     * Delete a table entry (ACL-enforced)
     */
    bool deleteTableEntry(uint32_t entry_idx, const ClientIdentity& client);

    /**
     * Read table entries (ACL-enforced)
     */
    std::vector<P4TableEntry> readTableEntries(const ClientIdentity& client) const;

    /**
     * Clear all entries (ACL-enforced, requires CLEAR permission)
     */
    void clearAllEntries(const ClientIdentity& client);

    // ================================================================
    // Legacy API (backward compatibility - uses "anonymous" client)
    // ================================================================

    bool loadControlPlaneRules(const std::string& json_path);
    bool installTableEntry(const P4TableEntry& entry);
    bool modifyTableEntry(uint32_t entry_idx, const P4TableEntry& entry);
    bool deleteTableEntry(uint32_t entry_idx);
    std::vector<P4TableEntry> readTableEntries() const;
    void clearAllEntries();

    // ================================================================
    // Statistics and Diagnostics (no ACL required)
    // ================================================================

    void printStatistics() const;
    void dumpTableToJSON(const std::string& output_path) const;
    bool verifyHardware() const;

    // ================================================================
    // High-Level Convenience API (with ACL enforcement)
    // ================================================================

    /**
     * Add a forwarding rule (ACL-enforced)
     * @param ip_cidr - IP address in CIDR notation (e.g., "192.168.1.0/24")
     * @param mac - MAC address as string (e.g., "11:22:33:44:55:66")
     * @param port - Egress port number
     * @param client - Client identity for ACL check
     * @return true if successful
     */
    bool addForwardingRule(const std::string& ip_cidr,
                          const std::string& mac,
                          uint16_t port,
                          const ClientIdentity& client);

    /**
     * Add a drop rule (ACL-enforced)
     */
    bool addDropRule(const std::string& ip_cidr, const ClientIdentity& client);

    /**
     * Add default route (ACL-enforced)
     */
    bool addDefaultRoute(const std::string& action,
                        const std::string& mac,
                        uint16_t port,
                        const ClientIdentity& client);

    /**
     * Add multiple routing rules (ACL-enforced)
     */
    int addRoutingRules(const std::vector<std::tuple<std::string, std::string, uint16_t, std::string>>& rules,
                        const ClientIdentity& client);

    /**
     * Find route by IP address (ACL-enforced, requires READ permission)
     */
    std::optional<P4TableEntry> findRouteByIP(const std::string& ip, const ClientIdentity& client) const;

    /**
     * Update route by IP address (ACL-enforced)
     */
    bool updateRoute(const std::string& ip_cidr,
                    const std::string& new_mac,
                    uint16_t new_port,
                    const ClientIdentity& client);

    /**
     * Delete route by IP address (ACL-enforced)
     */
    bool deleteRoute(const std::string& ip_cidr, const ClientIdentity& client);

    // ================================================================
    // High-Level Convenience API (Legacy - uses "anonymous" client)
    // ================================================================

    bool addForwardingRule(const std::string& ip_cidr,
                          const std::string& mac,
                          uint16_t port);
    bool addDropRule(const std::string& ip_cidr);
    bool addDefaultRoute(const std::string& action,
                        const std::string& mac = "",
                        uint16_t port = 0);
    int addRoutingRules(const std::vector<std::tuple<std::string, std::string, uint16_t, std::string>>& rules);
    std::optional<P4TableEntry> findRouteByIP(const std::string& ip) const;
    bool updateRoute(const std::string& ip_cidr,
                    const std::string& new_mac,
                    uint16_t new_port);
    bool deleteRoute(const std::string& ip_cidr);

    /**
     * Get number of installed routes (no ACL required)
     */
    size_t getRouteCount() const;

    /**
     * Check if a route exists for IP (no ACL required for existence check)
     */
    bool hasRoute(const std::string& ip) const;

    // ================================================================
    // Static Helpers
    // ================================================================

    static uint32_t parseIPAddress(const std::string& ip_str) { return parseIPv4(ip_str); }
    static uint64_t parseMACAddress(const std::string& mac_str) { return parseMAC(mac_str); }
    static std::string formatIPAddress(uint32_t ip) { return ipv4ToString(ip); }
    static std::string formatMACAddress(uint64_t mac) { return macToString(mac); }

    /**
     * Parse CIDR notation into prefix and length
     * @param cidr - String like "192.168.1.0/24"
     * @return pair of {prefix, prefix_len}
     */
    static std::pair<uint32_t, uint8_t> parseCIDR(const std::string& cidr);
};

/**
 * Simple JSON parser for control plane rules
 */
class SimpleJSONParser {
public:
    static std::vector<P4TableEntry> parseControlPlaneJSON(
        const std::string& json_content,
        const P4InfoMetadata& p4info);

private:
    static std::string extractString(const std::string& json, const std::string& key);
    static std::vector<std::string> extractArray(const std::string& json, const std::string& key);
    static uint32_t extractInt(const std::string& json, const std::string& key);
};

/**
 * Builder pattern for easy routing table configuration
 */
class RoutingTableBuilder {
private:
    POSRuntimeEngine* engine;

public:
    explicit RoutingTableBuilder(POSRuntimeEngine* eng) : engine(eng) {}

    RoutingTableBuilder& addForward(const std::string& ip_cidr,
                                   const std::string& mac,
                                   uint16_t port) {
        engine->addForwardingRule(ip_cidr, mac, port);
        return *this;
    }

    RoutingTableBuilder& addDrop(const std::string& ip_cidr) {
        engine->addDropRule(ip_cidr);
        return *this;
    }

    RoutingTableBuilder& setDefault(const std::string& action,
                                   const std::string& mac = "",
                                   uint16_t port = 0) {
        engine->addDefaultRoute(action, mac, port);
        return *this;
    }

    void build() {
        engine->printStatistics();
    }
};

} // namespace pos

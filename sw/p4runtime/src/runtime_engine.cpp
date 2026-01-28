/*
 * POS Runtime Engine Implementation
 *
 * Copyright (c) 2025, Systems Group, ETH Zurich
 * MIT License
 */

#include "runtime_engine.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <stdexcept>
#include <ctime>

namespace pos {

// ============================================================================
// TableAccessController Implementation
// ============================================================================

TableAccessController::TableAccessController()
    : enforcement_enabled_(true), root_client_id_("root") {
    // Register root client with full access
    registered_clients_.insert(root_client_id_);
    admin_clients_.insert(root_client_id_);
    acl_entries_[root_client_id_]["*"] = TablePermission::FULL_ACCESS;

    // Register anonymous client with no permissions by default
    registered_clients_.insert("anonymous");
    acl_entries_["anonymous"]["*"] = TablePermission::NONE;
}

bool TableAccessController::grantPermission(const ClientIdentity& admin,
                                            const std::string& client_id,
                                            const std::string& table_name,
                                            TablePermission permissions) {
    std::lock_guard<std::mutex> lock(acl_mutex_);

    // Check admin has ADMIN permission
    if (!hasPermission(getEffectivePermissions(admin.client_id, "*"), TablePermission::ADMIN)) {
        logAccess(admin.client_id, table_name, TablePermission::ADMIN, false);
        return false;
    }

    // Register client if not already registered
    if (registered_clients_.find(client_id) == registered_clients_.end()) {
        registered_clients_.insert(client_id);
    }

    // Grant permission
    acl_entries_[client_id][table_name] = permissions;

    // If granting ADMIN, add to admin set
    if (hasPermission(permissions, TablePermission::ADMIN)) {
        admin_clients_.insert(client_id);
    }

    logAccess(admin.client_id, table_name, TablePermission::ADMIN, true);
    return true;
}

bool TableAccessController::revokePermission(const ClientIdentity& admin,
                                             const std::string& client_id,
                                             const std::string& table_name) {
    std::lock_guard<std::mutex> lock(acl_mutex_);

    // Check admin has ADMIN permission
    if (!hasPermission(getEffectivePermissions(admin.client_id, "*"), TablePermission::ADMIN)) {
        logAccess(admin.client_id, table_name, TablePermission::ADMIN, false);
        return false;
    }

    // Cannot revoke root's permissions
    if (client_id == root_client_id_) {
        return false;
    }

    auto client_it = acl_entries_.find(client_id);
    if (client_it != acl_entries_.end()) {
        client_it->second.erase(table_name);
    }

    // Remove from admin set if revoking wildcard
    if (table_name == "*") {
        admin_clients_.erase(client_id);
    }

    logAccess(admin.client_id, table_name, TablePermission::ADMIN, true);
    return true;
}

bool TableAccessController::registerClient(const ClientIdentity& admin,
                                           const std::string& client_id,
                                           TablePermission default_perms) {
    std::lock_guard<std::mutex> lock(acl_mutex_);

    // Check admin has ADMIN permission
    if (!hasPermission(getEffectivePermissions(admin.client_id, "*"), TablePermission::ADMIN)) {
        return false;
    }

    if (registered_clients_.find(client_id) != registered_clients_.end()) {
        return false;  // Already registered
    }

    registered_clients_.insert(client_id);
    acl_entries_[client_id]["*"] = default_perms;

    if (hasPermission(default_perms, TablePermission::ADMIN)) {
        admin_clients_.insert(client_id);
    }

    return true;
}

bool TableAccessController::unregisterClient(const ClientIdentity& admin,
                                             const std::string& client_id) {
    std::lock_guard<std::mutex> lock(acl_mutex_);

    // Check admin has ADMIN permission
    if (!hasPermission(getEffectivePermissions(admin.client_id, "*"), TablePermission::ADMIN)) {
        return false;
    }

    // Cannot unregister root
    if (client_id == root_client_id_) {
        return false;
    }

    registered_clients_.erase(client_id);
    admin_clients_.erase(client_id);
    acl_entries_.erase(client_id);

    return true;
}

bool TableAccessController::checkPermission(const ClientIdentity& client,
                                            const std::string& table_name,
                                            TablePermission required) const {
    std::lock_guard<std::mutex> lock(acl_mutex_);

    // If enforcement is disabled, allow everything
    if (!enforcement_enabled_) {
        return true;
    }

    // Root always has access
    if (client.client_id == root_client_id_) {
        logAccess(client.client_id, table_name, required, true);
        return true;
    }

    // Check if client is registered
    if (registered_clients_.find(client.client_id) == registered_clients_.end()) {
        logAccess(client.client_id, table_name, required, false);
        return false;
    }

    TablePermission effective = getEffectivePermissions(client.client_id, table_name);
    bool allowed = hasPermission(effective, required);

    logAccess(client.client_id, table_name, required, allowed);
    return allowed;
}

TablePermission TableAccessController::getPermissions(const std::string& client_id,
                                                      const std::string& table_name) const {
    std::lock_guard<std::mutex> lock(acl_mutex_);
    return getEffectivePermissions(client_id, table_name);
}

TablePermission TableAccessController::getEffectivePermissions(const std::string& client_id,
                                                               const std::string& table_name) const {
    // Note: Caller must hold acl_mutex_

    auto client_it = acl_entries_.find(client_id);
    if (client_it == acl_entries_.end()) {
        return TablePermission::NONE;
    }

    const auto& client_perms = client_it->second;

    // Check specific table first
    auto table_it = client_perms.find(table_name);
    if (table_it != client_perms.end()) {
        return table_it->second;
    }

    // Fall back to wildcard
    auto wildcard_it = client_perms.find("*");
    if (wildcard_it != client_perms.end()) {
        return wildcard_it->second;
    }

    return TablePermission::NONE;
}

bool TableAccessController::isClientRegistered(const std::string& client_id) const {
    std::lock_guard<std::mutex> lock(acl_mutex_);
    return registered_clients_.find(client_id) != registered_clients_.end();
}

bool TableAccessController::isAdmin(const std::string& client_id) const {
    std::lock_guard<std::mutex> lock(acl_mutex_);
    return admin_clients_.find(client_id) != admin_clients_.end();
}

void TableAccessController::logAccess(const std::string& client_id,
                                      const std::string& table_name,
                                      TablePermission required,
                                      bool allowed) const {
    // Note: Caller must hold acl_mutex_

    std::time_t now = std::time(nullptr);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S")
        << " | " << (allowed ? "ALLOWED" : "DENIED ")
        << " | client=" << client_id
        << " | table=" << table_name
        << " | perm=" << static_cast<uint32_t>(required);

    audit_log_.push_back(oss.str());

    // Trim audit log if too large
    if (audit_log_.size() > MAX_AUDIT_LOG_SIZE) {
        audit_log_.erase(audit_log_.begin(),
                        audit_log_.begin() + (audit_log_.size() - MAX_AUDIT_LOG_SIZE));
    }
}

std::vector<std::string> TableAccessController::getAuditLog(size_t max_entries) const {
    std::lock_guard<std::mutex> lock(acl_mutex_);

    if (max_entries >= audit_log_.size()) {
        return audit_log_;
    }

    return std::vector<std::string>(
        audit_log_.end() - static_cast<long>(max_entries),
        audit_log_.end()
    );
}

void TableAccessController::clearAuditLog() {
    std::lock_guard<std::mutex> lock(acl_mutex_);
    audit_log_.clear();
}

void TableAccessController::printACLState() const {
    std::lock_guard<std::mutex> lock(acl_mutex_);

    std::cout << "\n=== Table Access Control State ===" << std::endl;
    std::cout << "Enforcement: " << (enforcement_enabled_ ? "ENABLED" : "DISABLED") << std::endl;
    std::cout << "Root client: " << root_client_id_ << std::endl;
    std::cout << "\nRegistered clients: " << registered_clients_.size() << std::endl;

    for (const auto& client_id : registered_clients_) {
        bool is_admin = admin_clients_.find(client_id) != admin_clients_.end();
        std::cout << "  - " << client_id << (is_admin ? " [ADMIN]" : "") << std::endl;

        auto client_it = acl_entries_.find(client_id);
        if (client_it != acl_entries_.end()) {
            for (const auto& [table, perms] : client_it->second) {
                std::cout << "      " << table << ": "
                          << static_cast<uint32_t>(perms) << std::endl;
            }
        }
    }

    std::cout << "\nAudit log entries: " << audit_log_.size() << std::endl;
    std::cout << "==================================\n" << std::endl;
}

// ============================================================================
// POSRuntimeEngine - Static members
// ============================================================================

const std::string POSRuntimeEngine::ANONYMOUS_CLIENT_ID = "anonymous";

// ============================================================================
// POSRuntimeEngine - Constructor / Destructor
// ============================================================================

POSRuntimeEngine::POSRuntimeEngine(coyote::cThread* thread, int debug)
    : cthread(thread), next_entry_idx(0), debug_level(debug),
      acl_(std::make_unique<TableAccessController>()) {

    if (!cthread) {
        throw std::runtime_error("POSRuntimeEngine: null thread pointer");
    }

    log(1, "POS Runtime Engine initialized");
    log(1, "  - Tables: " + std::to_string(p4info.table_name_to_id.size()));
    log(1, "  - Actions: " + std::to_string(p4info.action_name_to_code.size()));
    log(1, "  - ACL: enabled");
    log(2, "  - Debug level: " + std::to_string(debug_level));
}

POSRuntimeEngine::~POSRuntimeEngine() {
    log(1, "POS Runtime Engine destroyed");
}

// ======-------------------------------------------------------------------------------
// Logging Helpers
// ======-------------------------------------------------------------------------------

void POSRuntimeEngine::log(int level, const std::string& msg) const {
    if (debug_level >= level) {
        std::cout << msg << std::endl;
    }
}

void POSRuntimeEngine::logError(const std::string& msg) const {
    std::cerr << "ERROR: " << msg << std::endl;
}

// ======-------------------------------------------------------------------------------
// Parsing Helpers
// ======-------------------------------------------------------------------------------

uint32_t POSRuntimeEngine::parseIPv4(const std::string& ip_str) {
    uint32_t result = 0;
    std::istringstream iss(ip_str);
    std::string part;
    int shift = 24;

    int octet_count = 0;
    while (std::getline(iss, part, '.')) {
        if (octet_count >= 4) {
            throw std::runtime_error("Invalid IP address (too many octets): " + ip_str);
        }

        uint32_t byte = std::stoi(part);
        if (byte > 255) {
            throw std::runtime_error("Invalid IP address (octet > 255): " + ip_str);
        }
        result |= (byte << shift);
        shift -= 8;
        octet_count++;
    }

    if (octet_count != 4) {
        throw std::runtime_error("Invalid IP address (not enough octets): " + ip_str);
    }

    return result;
}

uint64_t POSRuntimeEngine::parseMAC(const std::string& mac_str) {
    uint64_t result = 0;
    std::istringstream iss(mac_str);
    std::string byte_str;
    int shift = 40;

    int byte_count = 0;
    while (std::getline(iss, byte_str, ':')) {
        if (byte_count >= 6) {
            throw std::runtime_error("Invalid MAC address (too many bytes): " + mac_str);
        }

        uint64_t byte = std::stoul(byte_str, nullptr, 16);
        if (byte > 255) {
            throw std::runtime_error("Invalid MAC address (byte > 0xFF): " + mac_str);
        }
        result |= (byte << shift);
        shift -= 8;
        byte_count++;
    }

    if (byte_count != 6) {
        throw std::runtime_error("Invalid MAC address (not 6 bytes): " + mac_str);
    }

    return result;
}

std::string POSRuntimeEngine::ipv4ToString(uint32_t ip) {
    std::ostringstream oss;
    oss << ((ip >> 24) & 0xFF) << "."
        << ((ip >> 16) & 0xFF) << "."
        << ((ip >> 8) & 0xFF) << "."
        << (ip & 0xFF);
    return oss.str();
}

std::string POSRuntimeEngine::macToString(uint64_t mac) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 40; i >= 0; i -= 8) {
        oss << std::setw(2) << ((mac >> i) & 0xFF);
        if (i > 0) oss << ":";
    }
    return oss.str();
}

RouterAction POSRuntimeEngine::translateAction(const std::string& action_name) const {
    auto it = p4info.action_name_to_code.find(action_name);
    if (it != p4info.action_name_to_code.end()) {
        return it->second;
    }

    logError("Unknown action '" + action_name + "', defaulting to NOACTION");
    return RouterAction::NOACTION;
}

// ======-------------------------------------------------------------------------------
// Validation
// ======-------------------------------------------------------------------------------

bool POSRuntimeEngine::validateTableEntry(const P4TableEntry& entry) const {
    // Check table exists
    if (p4info.table_name_to_id.find(entry.table_name) ==
        p4info.table_name_to_id.end()) {
        logError("Unknown table '" + entry.table_name + "'");
        return false;
    }

    // Check prefix length is valid
    if (entry.prefix_len > 32) {
        logError("Invalid prefix length " + std::to_string(entry.prefix_len));
        return false;
    }

    // Check action exists
    if (p4info.action_name_to_code.find(entry.action_name) ==
        p4info.action_name_to_code.end()) {
        logError("Unknown action '" + entry.action_name + "'");
        return false;
    }

    // Check table size limit
    auto table_size_it = p4info.table_max_size.find(entry.table_name);
    if (table_size_it != p4info.table_max_size.end()) {
        if (installed_entries.size() >= table_size_it->second) {
            logError("Table '" + entry.table_name + "' is full (max " +
                    std::to_string(table_size_it->second) + " entries)");
            return false;
        }
    }

    return true;
}

bool POSRuntimeEngine::validatePrefixLength(uint32_t& prefix, uint8_t prefix_len) const {
    if (prefix_len > 32) return false;
    if (prefix_len == 0) return true;  // Default route 0.0.0.0/0

    // Calculate the mask
    uint32_t mask = (~0U) << (32 - prefix_len);
    uint32_t masked_prefix = prefix & mask;

    // Check if prefix matches the mask
    if (masked_prefix != prefix) {
        log(1, "Warning: Prefix 0x" +
            std::to_string(prefix) + " doesn't match prefix length " +
            std::to_string(prefix_len) + ", auto-correcting to 0x" +
            std::to_string(masked_prefix));
        prefix = masked_prefix;  // Auto-correct
    }

    return true;
}

// ======-------------------------------------------------------------------------------
// Core Hardware Programming
// ======-------------------------------------------------------------------------------

void POSRuntimeEngine::programRouteEntry(
    uint32_t entry_idx,
    uint32_t prefix,
    uint8_t prefix_len,
    RouterAction action,
    uint64_t dst_mac,
    uint16_t egress_port) {

    log(2, "Programming hardware entry " + std::to_string(entry_idx));

    // Use RouterRegs from cDefs.hpp (fpga namespace for POS)
    using coyote::RouterRegs;

    // Write table address
    cthread->setCSR(entry_idx, static_cast<uint32_t>(RouterRegs::ADDR_REG));

    // Write prefix and length
    cthread->setCSR(prefix, static_cast<uint32_t>(RouterRegs::PREFIX_REG));
    cthread->setCSR(prefix_len, static_cast<uint32_t>(RouterRegs::PREFIX_LEN_REG));

    // Write action and valid bit
    uint32_t entry_ctrl = 0x1 | (static_cast<uint8_t>(action) << 6);
    cthread->setCSR(entry_ctrl, static_cast<uint32_t>(RouterRegs::ENTRY_CTRL_REG));

    // Write destination MAC
    uint32_t mac_low = dst_mac & 0xFFFFFFFF;
    uint32_t mac_high = (dst_mac >> 32) & 0xFFFF;
    cthread->setCSR(mac_low, static_cast<uint32_t>(RouterRegs::DST_MAC_LOW_REG));
    cthread->setCSR(mac_high, static_cast<uint32_t>(RouterRegs::DST_MAC_HIGH_REG));

    // Write egress port
    cthread->setCSR(egress_port, static_cast<uint32_t>(RouterRegs::EGRESS_PORT_REG));

    // Trigger write
    cthread->setCSR(0x1, static_cast<uint32_t>(RouterRegs::CTRL_REG));

    // Wait for ready with timeout protection
    int retry_count = 0;
    bool ready = false;

    while (retry_count < MAX_HW_RETRIES) {
        uint32_t status = cthread->getCSR(static_cast<uint32_t>(RouterRegs::STATUS_REG));
        if (status & 0x1) {
            ready = true;
            break;
        }

        retry_count++;

        // Log progress on long waits
        if (retry_count % RETRY_LOG_INTERVAL == 0) {
            log(2, "  Waiting for hardware ready... (" + std::to_string(retry_count) + " retries)");
        }
    }

    if (!ready) {
        throw std::runtime_error("Hardware timeout waiting for ready status after " +
                               std::to_string(MAX_HW_RETRIES) + " retries");
    }

    log(2, "Hardware write complete (retries: " + std::to_string(retry_count) + ")");
}

// ======-------------------------------------------------------------------------------
// P4Info Loading
// ======-------------------------------------------------------------------------------

bool POSRuntimeEngine::loadP4Info(const std::string& p4info_path) {
    if (p4info_path.empty()) {
        log(1, "Using built-in P4Info metadata");
        return true;
    }

    // TODO: Implement P4Info file parsing if needed
    log(1, "P4Info file loading not yet implemented, using defaults");
    return true;
}

// ======-------------------------------------------------------------------------------
// Control Plane Rule Loading
// ======-------------------------------------------------------------------------------

bool POSRuntimeEngine::loadControlPlaneRules(const std::string& json_path, const ClientIdentity& client) {
    // ACL check: requires WRITE permission
    if (!acl_->checkPermission(client, "*", TablePermission::WRITE)) {
        logError("ACL denied: client '" + client.client_id + "' cannot load control plane rules");
        return false;
    }

    log(1, "Loading control plane rules from: " + json_path);

    // Read file
    std::ifstream file(json_path);
    if (!file.is_open()) {
        logError("Failed to open JSON file: " + json_path);
        return false;
    }

    // Read entire file into string
    std::string json_content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
    file.close();

    log(2, "Read " + std::to_string(json_content.size()) + " bytes from JSON file");

    // Parse JSON (using simple parser)
    std::vector<P4TableEntry> entries =
        SimpleJSONParser::parseControlPlaneJSON(json_content, p4info);

    log(1, "Parsed " + std::to_string(entries.size()) + " table entries from JSON");

    // Install each entry (using ACL-enforced version)
    int installed_count = 0;
    for (const auto& entry : entries) {
        if (installTableEntry(entry, client)) {
            installed_count++;
        } else {
            logError("Failed to install entry " + std::to_string(installed_count));
            return false;
        }
    }

    log(1, "Successfully loaded " + std::to_string(installed_count) + " control plane rules");
    return true;
}

// Legacy version (uses anonymous client)
bool POSRuntimeEngine::loadControlPlaneRules(const std::string& json_path) {
    return loadControlPlaneRules(json_path, ClientIdentity(ANONYMOUS_CLIENT_ID));
}

// ======-------------------------------------------------------------------------------
// Table Entry Operations (ACL-enforced)
// ======-------------------------------------------------------------------------------

bool POSRuntimeEngine::installTableEntry(const P4TableEntry& entry, const ClientIdentity& client) {
    // ACL check: requires WRITE permission
    if (!acl_->checkPermission(client, entry.table_name, TablePermission::WRITE)) {
        logError("ACL denied: client '" + client.client_id + "' cannot write to table '" + entry.table_name + "'");
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(entries_mutex);
    return installTableEntryInternal(entry);
}

bool POSRuntimeEngine::modifyTableEntry(uint32_t entry_idx, const P4TableEntry& entry, const ClientIdentity& client) {
    // ACL check: requires WRITE permission
    if (!acl_->checkPermission(client, entry.table_name, TablePermission::WRITE)) {
        logError("ACL denied: client '" + client.client_id + "' cannot modify table '" + entry.table_name + "'");
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(entries_mutex);

    if (installed_entries.find(entry_idx) == installed_entries.end()) {
        logError("Entry " + std::to_string(entry_idx) + " does not exist");
        return false;
    }

    log(1, "Modifying entry " + std::to_string(entry_idx));

    P4TableEntry modified_entry = entry;
    modified_entry.entry_idx = entry_idx;

    return installTableEntryInternal(modified_entry);
}

bool POSRuntimeEngine::deleteTableEntry(uint32_t entry_idx, const ClientIdentity& client) {
    std::lock_guard<std::recursive_mutex> lock(entries_mutex);

    // Find the entry to get table name for ACL check
    auto it = installed_entries.find(entry_idx);
    if (it == installed_entries.end()) {
        logError("Entry " + std::to_string(entry_idx) + " does not exist");
        return false;
    }

    // ACL check: requires DELETE permission
    if (!acl_->checkPermission(client, it->second.table_name, TablePermission::DELETE)) {
        logError("ACL denied: client '" + client.client_id + "' cannot delete from table '" + it->second.table_name + "'");
        return false;
    }

    return deleteTableEntryInternal(entry_idx);
}

std::vector<P4TableEntry> POSRuntimeEngine::readTableEntries(const ClientIdentity& client) const {
    // ACL check: requires READ permission (on wildcard since reading all)
    if (!acl_->checkPermission(client, "*", TablePermission::READ)) {
        logError("ACL denied: client '" + client.client_id + "' cannot read table entries");
        return {};
    }

    std::lock_guard<std::recursive_mutex> lock(entries_mutex);

    std::vector<P4TableEntry> entries;
    entries.reserve(installed_entries.size());

    for (const auto& pair : installed_entries) {
        // Filter by table-specific permissions
        if (acl_->checkPermission(client, pair.second.table_name, TablePermission::READ)) {
            entries.push_back(pair.second);
        }
    }

    return entries;
}

void POSRuntimeEngine::clearAllEntries(const ClientIdentity& client) {
    // ACL check: requires CLEAR permission
    if (!acl_->checkPermission(client, "*", TablePermission::CLEAR)) {
        logError("ACL denied: client '" + client.client_id + "' cannot clear all entries");
        return;
    }

    log(1, "Clearing all table entries (client: " + client.client_id + ")");

    std::vector<uint32_t> indices_to_delete;
    {
        std::lock_guard<std::recursive_mutex> lock(entries_mutex);
        for (const auto& pair : installed_entries) {
            indices_to_delete.push_back(pair.first);
        }
    }

    for (uint32_t idx : indices_to_delete) {
        // Use internal delete (already ACL-checked above)
        std::lock_guard<std::recursive_mutex> lock(entries_mutex);
        deleteTableEntryInternal(idx);
    }

    {
        std::lock_guard<std::recursive_mutex> lock(entries_mutex);
        next_entry_idx = 0;
    }

    log(1, "All entries cleared");
}

// ======-------------------------------------------------------------------------------
// Table Entry Operations (Legacy - uses anonymous client)
// ======-------------------------------------------------------------------------------

bool POSRuntimeEngine::installTableEntry(const P4TableEntry& entry) {
    return installTableEntry(entry, ClientIdentity(ANONYMOUS_CLIENT_ID));
}

bool POSRuntimeEngine::installTableEntryInternal(const P4TableEntry& entry) {
    // Note: Caller must hold entries_mutex

    // Validate
    if (!validateTableEntry(entry)) {
        return false;
    }

    // Validate and possibly correct prefix
    uint32_t corrected_prefix = entry.prefix;
    if (!validatePrefixLength(corrected_prefix, entry.prefix_len)) {
        logError("Invalid prefix length for entry");
        return false;
    }

    // Assign entry index with improved logic
    uint32_t entry_idx;
    if (entry.entry_idx == 0) {
        // Auto-assign - find next available index
        entry_idx = next_entry_idx;

        // Skip already-used indices
        while (installed_entries.find(entry_idx) != installed_entries.end()) {
            entry_idx++;
            if (entry_idx >= 1024) {  // Prevent infinite loop
                logError("No available entry indices");
                return false;
            }
        }

        next_entry_idx = entry_idx + 1;
    } else {
        // Use specified index
        entry_idx = entry.entry_idx;

        // Check if already exists
        if (installed_entries.find(entry_idx) != installed_entries.end()) {
            log(1, "Warning: Entry " + std::to_string(entry_idx) +
                " already exists, will be overwritten");
        }

        // Update next_entry_idx if needed
        if (entry_idx >= next_entry_idx) {
            next_entry_idx = entry_idx + 1;
        }
    }

    // Translate action
    RouterAction action = translateAction(entry.action_name);

    log(1, "Installing entry " + std::to_string(entry_idx) + ": " +
        "prefix=" + ipv4ToString(corrected_prefix) + "/" + std::to_string(entry.prefix_len) +
        " action=" + entry.action_name);

    // Program hardware
    try {
        programRouteEntry(entry_idx, corrected_prefix, entry.prefix_len,
                         action, entry.dst_mac, entry.egress_port);
    } catch (const std::exception& e) {
        logError("Failed to program hardware: " + std::string(e.what()));
        return false;
    }

    // Track installed entry
    P4TableEntry stored_entry = entry;
    stored_entry.entry_idx = entry_idx;
    stored_entry.prefix = corrected_prefix;  // Store corrected prefix
    installed_entries[entry_idx] = stored_entry;

    log(1, "Entry " + std::to_string(entry_idx) + " installed successfully");
    return true;
}

// Legacy versions (use anonymous client)
bool POSRuntimeEngine::modifyTableEntry(uint32_t entry_idx, const P4TableEntry& entry) {
    return modifyTableEntry(entry_idx, entry, ClientIdentity(ANONYMOUS_CLIENT_ID));
}

bool POSRuntimeEngine::deleteTableEntry(uint32_t entry_idx) {
    return deleteTableEntry(entry_idx, ClientIdentity(ANONYMOUS_CLIENT_ID));
}

std::vector<P4TableEntry> POSRuntimeEngine::readTableEntries() const {
    return readTableEntries(ClientIdentity(ANONYMOUS_CLIENT_ID));
}

void POSRuntimeEngine::clearAllEntries() {
    clearAllEntries(ClientIdentity(ANONYMOUS_CLIENT_ID));
}

bool POSRuntimeEngine::deleteTableEntryInternal(uint32_t entry_idx) {
    // Note: Caller must hold entries_mutex

    if (installed_entries.find(entry_idx) == installed_entries.end()) {
        logError("Entry " + std::to_string(entry_idx) + " does not exist");
        return false;
    }

    log(1, "Deleting entry " + std::to_string(entry_idx));

    try {
        // Program a no-op entry
        programRouteEntry(entry_idx, 0, 0, RouterAction::NOACTION, 0, 0);
    } catch (const std::exception& e) {
        logError("Failed to delete entry: " + std::string(e.what()));
        return false;
    }

    installed_entries.erase(entry_idx);

    log(1, "Entry " + std::to_string(entry_idx) + " deleted");
    return true;
}

size_t POSRuntimeEngine::getRouteCount() const {
    std::lock_guard<std::recursive_mutex> lock(entries_mutex);
    return installed_entries.size();
}

// ======-------------------------------------------------------------------------------
// Statistics and Verification
// ======-------------------------------------------------------------------------------

void POSRuntimeEngine::printStatistics() const {
    std::lock_guard<std::recursive_mutex> lock(entries_mutex);

    std::cout << "\n=== POS Runtime Engine Statistics ===" << std::endl;
    std::cout << "Installed entries: " << installed_entries.size() << std::endl;
    std::cout << "Next entry index: " << next_entry_idx << std::endl;
    std::cout << "Debug level: " << debug_level << std::endl;

    if (installed_entries.empty()) {
        std::cout << "\nNo entries installed." << std::endl;
    } else {
        std::cout << "\nInstalled entries:" << std::endl;
        std::cout << "Idx | Prefix              | Len | Action      | MAC               | Port" << std::endl;
        std::cout << "----+---------------------+-----+-------------+-------------------+-----" << std::endl;

        for (const auto& pair : installed_entries) {
            const auto& entry = pair.second;
            std::cout << std::setw(3) << entry.entry_idx << " | "
                      << std::setw(15) << std::left << ipv4ToString(entry.prefix) << std::right << " | "
                      << std::setw(3) << static_cast<int>(entry.prefix_len) << " | "
                      << std::setw(11) << std::left << entry.action_name << std::right << " | "
                      << macToString(entry.dst_mac) << " | "
                      << std::setw(4) << entry.egress_port << std::endl;
        }
    }

    std::cout << "====================================\n" << std::endl;
}

void POSRuntimeEngine::dumpTableToJSON(const std::string& output_path) const {
    std::lock_guard<std::recursive_mutex> lock(entries_mutex);

    log(1, "Dumping routing table to: " + output_path);

    std::ofstream out(output_path);
    if (!out.is_open()) {
        logError("Failed to open output file: " + output_path);
        return;
    }

    out << "{\n";
    out << "  \"table_entries\": [\n";

    size_t count = 0;
    for (const auto& pair : installed_entries) {
        const auto& entry = pair.second;

        if (count > 0) out << ",\n";

        out << "    {\n";
        out << "      \"table\": \"" << entry.table_name << "\",\n";
        out << "      \"match\": {\n";
        out << "        \"hdr.ipv4.dstAddr\": [\"" << ipv4ToString(entry.prefix) << "\", "
            << static_cast<int>(entry.prefix_len) << "]\n";
        out << "      },\n";
        out << "      \"action_name\": \"" << entry.action_name << "\",\n";
        out << "      \"action_params\": {\n";
        out << "        \"dstAddr\": \"" << macToString(entry.dst_mac) << "\",\n";
        out << "        \"port\": " << entry.egress_port << "\n";
        out << "      }\n";
        out << "    }";

        count++;
    }

    out << "\n  ]\n";
    out << "}\n";

    out.close();
    log(1, "Table dumped successfully (" + std::to_string(count) + " entries)");
}

bool POSRuntimeEngine::verifyHardware() const {
    log(2, "Verifying hardware...");

    try {
        // Try to read status register
        using coyote::RouterRegs;
        uint32_t status = cthread->getCSR(static_cast<uint32_t>(RouterRegs::STATUS_REG));
        log(1, "Hardware verification: status = 0x" +
            std::to_string(status) + " (PASS)");
        return true;
    } catch (const std::exception& e) {
        logError("Hardware verification failed: " + std::string(e.what()));
        return false;
    }
}

// ======-------------------------------------------------------------------------------
// Simple JSON Parser
// ======-------------------------------------------------------------------------------

std::vector<P4TableEntry> SimpleJSONParser::parseControlPlaneJSON(
    const std::string& json_content,
    const P4InfoMetadata& p4info) {

    std::vector<P4TableEntry> entries;

    // Find "table_entries" array
    size_t table_entries_pos = json_content.find("\"table_entries\"");
    if (table_entries_pos == std::string::npos) {
        std::cerr << "No table_entries found in JSON" << std::endl;
        return entries;
    }

    // Find opening bracket of array
    size_t array_start = json_content.find('[', table_entries_pos);
    size_t array_end = json_content.find_last_of(']');

    if (array_start == std::string::npos || array_end == std::string::npos) {
        std::cerr << "Malformed JSON: could not find array bounds" << std::endl;
        return entries;
    }

    // Extract array content
    std::string array_content = json_content.substr(array_start + 1,
                                                     array_end - array_start - 1);

    // Parse each entry (simple brace matching)
    size_t pos = 0;
    while (pos < array_content.length()) {
        // Find next entry
        size_t entry_start = array_content.find('{', pos);
        if (entry_start == std::string::npos) break;

        // Find matching closing brace
        int brace_count = 1;
        size_t entry_end = entry_start + 1;
        while (entry_end < array_content.length() && brace_count > 0) {
            if (array_content[entry_end] == '{') brace_count++;
            else if (array_content[entry_end] == '}') brace_count--;
            entry_end++;
        }

        // Extract entry
        std::string entry_json = array_content.substr(entry_start,
                                                       entry_end - entry_start);

        // Parse entry
        P4TableEntry entry;

        // Parse table name
        entry.table_name = extractString(entry_json, "table");

        // Parse match fields
        size_t match_pos = entry_json.find("\"match\"");
        if (match_pos != std::string::npos) {
            size_t match_start = entry_json.find('{', match_pos);
            size_t match_end = entry_json.find('}', match_start);
            std::string match_content = entry_json.substr(match_start,
                                                          match_end - match_start + 1);

            // Extract IP address and prefix length
            std::vector<std::string> dst_addr = extractArray(match_content,
                                                            "hdr.ipv4.dstAddr");
            if (dst_addr.size() >= 2) {
                entry.prefix = POSRuntimeEngine::parseIPAddress(dst_addr[0]);
                entry.prefix_len = std::stoi(dst_addr[1]);
            }
        }

        // Parse action name
        entry.action_name = extractString(entry_json, "action_name");

        // Parse action parameters
        size_t params_pos = entry_json.find("\"action_params\"");
        if (params_pos != std::string::npos) {
            size_t params_start = entry_json.find('{', params_pos);
            size_t params_end = entry_json.find('}', params_start);
            std::string params_content = entry_json.substr(params_start,
                                                           params_end - params_start + 1);

            // Extract MAC address
            std::string mac_str = extractString(params_content, "dstAddr");
            if (!mac_str.empty()) {
                entry.dst_mac = POSRuntimeEngine::parseMACAddress(mac_str);
            }

            // Extract port
            entry.egress_port = extractInt(params_content, "port");
        }

        entries.push_back(entry);
        pos = entry_end;
    }

    return entries;
}

std::string SimpleJSONParser::extractString(const std::string& json,
                                            const std::string& key) {
    size_t key_pos = json.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return "";

    size_t value_start = json.find('\"', key_pos + key.length() + 2);
    if (value_start == std::string::npos) return "";
    value_start++;

    size_t value_end = json.find('\"', value_start);
    if (value_end == std::string::npos) return "";

    return json.substr(value_start, value_end - value_start);
}

std::vector<std::string> SimpleJSONParser::extractArray(const std::string& json,
                                                        const std::string& key) {
    std::vector<std::string> result;

    size_t key_pos = json.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return result;

    size_t array_start = json.find('[', key_pos);
    if (array_start == std::string::npos) return result;

    size_t array_end = json.find(']', array_start);
    if (array_end == std::string::npos) return result;

    std::string array_content = json.substr(array_start + 1,
                                           array_end - array_start - 1);

    // Parse array elements
    std::istringstream iss(array_content);
    std::string element;
    while (std::getline(iss, element, ',')) {
        // Trim whitespace and quotes
        element.erase(std::remove(element.begin(), element.end(), ' '), element.end());
        element.erase(std::remove(element.begin(), element.end(), '\"'), element.end());
        if (!element.empty()) {
            result.push_back(element);
        }
    }

    return result;
}

uint32_t SimpleJSONParser::extractInt(const std::string& json,
                                      const std::string& key) {
    size_t key_pos = json.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return 0;

    size_t colon_pos = json.find(':', key_pos);
    if (colon_pos == std::string::npos) return 0;

    // Skip whitespace
    size_t value_start = colon_pos + 1;
    while (value_start < json.length() && std::isspace(json[value_start])) {
        value_start++;
    }

    // Extract number
    size_t value_end = value_start;
    while (value_end < json.length() && std::isdigit(json[value_end])) {
        value_end++;
    }

    if (value_end > value_start) {
        return std::stoi(json.substr(value_start, value_end - value_start));
    }

    return 0;
}

// ======-------------------------------------------------------------------------------
// High-Level Convenience API (ACL-enforced)
// ======-------------------------------------------------------------------------------

std::pair<uint32_t, uint8_t> POSRuntimeEngine::parseCIDR(const std::string& cidr) {
    size_t slash_pos = cidr.find('/');

    if (slash_pos == std::string::npos) {
        // No prefix length, assume /32 (single host)
        return {parseIPv4(cidr), 32};
    }

    std::string ip_part = cidr.substr(0, slash_pos);
    std::string len_part = cidr.substr(slash_pos + 1);

    uint32_t prefix = parseIPv4(ip_part);
    uint8_t prefix_len = std::stoi(len_part);

    return {prefix, prefix_len};
}

bool POSRuntimeEngine::addForwardingRule(const std::string& ip_cidr,
                                        const std::string& mac,
                                        uint16_t port,
                                        const ClientIdentity& client) {
    auto [prefix, prefix_len] = parseCIDR(ip_cidr);

    P4TableEntry entry;
    entry.table_name = "ipv4_lpm";
    entry.prefix = prefix;
    entry.prefix_len = prefix_len;
    entry.action_name = "ipv4_forward";
    entry.dst_mac = parseMAC(mac);
    entry.egress_port = port;
    entry.description = "Forward " + ip_cidr + " to " + mac + " via port " + std::to_string(port);

    return installTableEntry(entry, client);
}

bool POSRuntimeEngine::addDropRule(const std::string& ip_cidr, const ClientIdentity& client) {
    auto [prefix, prefix_len] = parseCIDR(ip_cidr);

    P4TableEntry entry;
    entry.table_name = "ipv4_lpm";
    entry.prefix = prefix;
    entry.prefix_len = prefix_len;
    entry.action_name = "drop";
    entry.dst_mac = 0;
    entry.egress_port = 0;
    entry.description = "Drop " + ip_cidr;

    return installTableEntry(entry, client);
}

bool POSRuntimeEngine::addDefaultRoute(const std::string& action,
                                      const std::string& mac,
                                      uint16_t port,
                                      const ClientIdentity& client) {
    P4TableEntry entry;
    entry.table_name = "ipv4_lpm";
    entry.prefix = 0;  // 0.0.0.0
    entry.prefix_len = 0;  // /0 (match everything)
    entry.action_name = action;
    entry.dst_mac = mac.empty() ? 0 : parseMAC(mac);
    entry.egress_port = port;
    entry.description = "Default route: " + action;

    return installTableEntry(entry, client);
}

int POSRuntimeEngine::addRoutingRules(
    const std::vector<std::tuple<std::string, std::string, uint16_t, std::string>>& rules,
    const ClientIdentity& client) {

    int success_count = 0;

    for (const auto& [ip_cidr, mac, port, action] : rules) {
        bool success = false;

        if (action == "forward" || action == "ipv4_forward") {
            success = addForwardingRule(ip_cidr, mac, port, client);
        } else if (action == "drop") {
            success = addDropRule(ip_cidr, client);
        } else if (action == "noaction" || action == "NoAction") {
            success = addDefaultRoute("NoAction", mac, port, client);
        }

        if (success) {
            success_count++;
        } else {
            logError("Failed to install rule for " + ip_cidr);
        }
    }

    log(1, "Installed " + std::to_string(success_count) + "/" +
        std::to_string(rules.size()) + " routing rules");

    return success_count;
}

// ======-------------------------------------------------------------------------------
// High-Level Convenience API (Legacy - uses anonymous client)
// ======-------------------------------------------------------------------------------

bool POSRuntimeEngine::addForwardingRule(const std::string& ip_cidr,
                                        const std::string& mac,
                                        uint16_t port) {
    return addForwardingRule(ip_cidr, mac, port, ClientIdentity(ANONYMOUS_CLIENT_ID));
}

bool POSRuntimeEngine::addDropRule(const std::string& ip_cidr) {
    return addDropRule(ip_cidr, ClientIdentity(ANONYMOUS_CLIENT_ID));
}

bool POSRuntimeEngine::addDefaultRoute(const std::string& action,
                                      const std::string& mac,
                                      uint16_t port) {
    return addDefaultRoute(action, mac, port, ClientIdentity(ANONYMOUS_CLIENT_ID));
}

int POSRuntimeEngine::addRoutingRules(
    const std::vector<std::tuple<std::string, std::string, uint16_t, std::string>>& rules) {
    return addRoutingRules(rules, ClientIdentity(ANONYMOUS_CLIENT_ID));
}

// ======-------------------------------------------------------------------------------
// Route Management (ACL-enforced)
// ======-------------------------------------------------------------------------------

std::optional<P4TableEntry> POSRuntimeEngine::findRouteByIP(const std::string& ip, const ClientIdentity& client) const {
    // ACL check: requires READ permission
    if (!acl_->checkPermission(client, "ipv4_lpm", TablePermission::READ)) {
        logError("ACL denied: client '" + client.client_id + "' cannot read routes");
        return std::nullopt;
    }

    std::lock_guard<std::recursive_mutex> lock(entries_mutex);
    return findRouteByIPInternal(ip);
}

bool POSRuntimeEngine::updateRoute(const std::string& ip_cidr,
                                  const std::string& new_mac,
                                  uint16_t new_port,
                                  const ClientIdentity& client) {
    // ACL check: requires WRITE permission
    if (!acl_->checkPermission(client, "ipv4_lpm", TablePermission::WRITE)) {
        logError("ACL denied: client '" + client.client_id + "' cannot update routes");
        return false;
    }

    // Atomic operation: find and update under single lock
    std::lock_guard<std::recursive_mutex> lock(entries_mutex);

    auto entry_opt = findRouteByIPInternal(ip_cidr);

    if (!entry_opt) {
        logError("Route not found: " + ip_cidr);
        return false;
    }

    P4TableEntry modified_entry = *entry_opt;
    modified_entry.dst_mac = parseMAC(new_mac);
    modified_entry.egress_port = new_port;

    return installTableEntryInternal(modified_entry);
}

bool POSRuntimeEngine::deleteRoute(const std::string& ip_cidr, const ClientIdentity& client) {
    // ACL check: requires DELETE permission
    if (!acl_->checkPermission(client, "ipv4_lpm", TablePermission::DELETE)) {
        logError("ACL denied: client '" + client.client_id + "' cannot delete routes");
        return false;
    }

    // Atomic operation: find and delete under single lock
    std::lock_guard<std::recursive_mutex> lock(entries_mutex);

    auto entry_opt = findRouteByIPInternal(ip_cidr);

    if (!entry_opt) {
        logError("Route not found: " + ip_cidr);
        return false;
    }

    return deleteTableEntryInternal(entry_opt->entry_idx);
}

// ======-------------------------------------------------------------------------------
// Route Management (Legacy - uses anonymous client)
// ======-------------------------------------------------------------------------------

std::optional<P4TableEntry> POSRuntimeEngine::findRouteByIP(const std::string& ip) const {
    return findRouteByIP(ip, ClientIdentity(ANONYMOUS_CLIENT_ID));
}

bool POSRuntimeEngine::updateRoute(const std::string& ip_cidr,
                                  const std::string& new_mac,
                                  uint16_t new_port) {
    return updateRoute(ip_cidr, new_mac, new_port, ClientIdentity(ANONYMOUS_CLIENT_ID));
}

bool POSRuntimeEngine::deleteRoute(const std::string& ip_cidr) {
    return deleteRoute(ip_cidr, ClientIdentity(ANONYMOUS_CLIENT_ID));
}

// ======-------------------------------------------------------------------------------
// Internal helpers
// ======-------------------------------------------------------------------------------

std::optional<P4TableEntry> POSRuntimeEngine::findRouteByIPInternal(const std::string& ip) const {
    // Note: Caller must hold entries_mutex

    auto [prefix, prefix_len] = parseCIDR(ip);

    for (const auto& pair : installed_entries) {
        const auto& entry = pair.second;
        if (entry.prefix == prefix && entry.prefix_len == prefix_len) {
            return entry;  // Return copy
        }
    }

    return std::nullopt;
}

bool POSRuntimeEngine::hasRoute(const std::string& ip) const {
    std::lock_guard<std::recursive_mutex> lock(entries_mutex);

    auto [prefix, prefix_len] = parseCIDR(ip);

    for (const auto& pair : installed_entries) {
        const auto& entry = pair.second;
        if (entry.prefix == prefix && entry.prefix_len == prefix_len) {
            return true;
        }
    }

    return false;
}

} // namespace pos

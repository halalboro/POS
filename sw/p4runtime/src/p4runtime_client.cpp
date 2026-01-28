/**
 * P4Runtime gRPC Client Implementation
 */

#include "p4runtime_client.hpp"
#include "p4runtime_simple.grpc.pb.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace pos {

// =============================================================================
// Constructor / Destructor
// =============================================================================

P4RuntimeClient::P4RuntimeClient(const std::string& server_address, int timeout_ms)
    : server_address_(server_address),
      timeout_ms_(timeout_ms) {

    // Create channel
    channel_ = grpc::CreateChannel(server_address_, grpc::InsecureChannelCredentials());

    // Create stub
    stub_ = pos::p4runtime::P4RuntimeService::NewStub(channel_);
}

P4RuntimeClient::~P4RuntimeClient() {
    // Stub and channel are cleaned up automatically
}

// =============================================================================
// Connection Management
// =============================================================================

bool P4RuntimeClient::isConnected() const {
    if (!channel_) return false;
    auto state = channel_->GetState(false);
    return state == GRPC_CHANNEL_READY;
}

bool P4RuntimeClient::waitForConnection(int timeout_ms) {
    if (!channel_) return false;

    auto deadline = std::chrono::system_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    return channel_->WaitForConnected(deadline);
}

std::unique_ptr<grpc::ClientContext> P4RuntimeClient::createContext() const {
    auto context = std::make_unique<grpc::ClientContext>();
    auto deadline = std::chrono::system_clock::now() +
                    std::chrono::milliseconds(timeout_ms_);
    context->set_deadline(deadline);
    return context;
}

// =============================================================================
// Core Table Operations
// =============================================================================

bool P4RuntimeClient::installTableEntry(const ClientTableEntry& entry,
                                         uint32_t* assigned_idx) {
    pos::p4runtime::WriteRequest request;
    request.set_type(pos::p4runtime::UPDATE_INSERT);

    auto* proto_entry = request.mutable_entry();
    proto_entry->set_table_name(entry.table_name);
    proto_entry->set_entry_idx(entry.entry_idx);
    proto_entry->set_prefix(entry.prefix);
    proto_entry->set_prefix_len(entry.prefix_len);
    proto_entry->set_action_name(entry.action_name);
    proto_entry->set_dst_mac(entry.dst_mac);
    proto_entry->set_egress_port(entry.egress_port);
    proto_entry->set_priority(entry.priority);
    proto_entry->set_description(entry.description);

    pos::p4runtime::WriteResponse response;
    auto context = createContext();

    grpc::Status status = stub_->Write(context.get(), request, &response);

    if (!status.ok()) {
        last_error_ = "gRPC error: " + status.error_message();
        return false;
    }

    if (!response.success()) {
        last_error_ = response.error_message();
        return false;
    }

    if (assigned_idx) {
        *assigned_idx = response.entry_idx();
    }

    return true;
}

bool P4RuntimeClient::modifyTableEntry(uint32_t entry_idx,
                                        const ClientTableEntry& entry) {
    pos::p4runtime::WriteRequest request;
    request.set_type(pos::p4runtime::UPDATE_MODIFY);

    auto* proto_entry = request.mutable_entry();
    proto_entry->set_table_name(entry.table_name);
    proto_entry->set_entry_idx(entry_idx);
    proto_entry->set_prefix(entry.prefix);
    proto_entry->set_prefix_len(entry.prefix_len);
    proto_entry->set_action_name(entry.action_name);
    proto_entry->set_dst_mac(entry.dst_mac);
    proto_entry->set_egress_port(entry.egress_port);
    proto_entry->set_priority(entry.priority);
    proto_entry->set_description(entry.description);

    pos::p4runtime::WriteResponse response;
    auto context = createContext();

    grpc::Status status = stub_->Write(context.get(), request, &response);

    if (!status.ok()) {
        last_error_ = "gRPC error: " + status.error_message();
        return false;
    }

    if (!response.success()) {
        last_error_ = response.error_message();
        return false;
    }

    return true;
}

bool P4RuntimeClient::deleteTableEntry(uint32_t entry_idx) {
    pos::p4runtime::WriteRequest request;
    request.set_type(pos::p4runtime::UPDATE_DELETE);
    request.mutable_entry()->set_entry_idx(entry_idx);

    pos::p4runtime::WriteResponse response;
    auto context = createContext();

    grpc::Status status = stub_->Write(context.get(), request, &response);

    if (!status.ok()) {
        last_error_ = "gRPC error: " + status.error_message();
        return false;
    }

    if (!response.success()) {
        last_error_ = response.error_message();
        return false;
    }

    return true;
}

std::vector<ClientTableEntry> P4RuntimeClient::readTableEntries() {
    std::vector<ClientTableEntry> entries;

    pos::p4runtime::ReadRequest request;
    // Empty table_name and entry_idx=0 means "all entries"

    pos::p4runtime::ReadResponse response;
    auto context = createContext();

    grpc::Status status = stub_->Read(context.get(), request, &response);

    if (!status.ok()) {
        last_error_ = "gRPC error: " + status.error_message();
        return entries;
    }

    for (const auto& proto_entry : response.entries()) {
        ClientTableEntry entry;
        entry.table_name = proto_entry.table_name();
        entry.entry_idx = proto_entry.entry_idx();
        entry.prefix = proto_entry.prefix();
        entry.prefix_len = static_cast<uint8_t>(proto_entry.prefix_len());
        entry.action_name = proto_entry.action_name();
        entry.dst_mac = proto_entry.dst_mac();
        entry.egress_port = static_cast<uint16_t>(proto_entry.egress_port());
        entry.priority = proto_entry.priority();
        entry.description = proto_entry.description();
        entries.push_back(entry);
    }

    return entries;
}

bool P4RuntimeClient::clearAllEntries() {
    pos::p4runtime::ClearRequest request;
    // Empty table_name means "all tables"

    pos::p4runtime::ClearResponse response;
    auto context = createContext();

    grpc::Status status = stub_->Clear(context.get(), request, &response);

    if (!status.ok()) {
        last_error_ = "gRPC error: " + status.error_message();
        return false;
    }

    return response.success();
}

// =============================================================================
// High-Level Convenience API
// =============================================================================

bool P4RuntimeClient::addForwardingRule(const std::string& ip_cidr,
                                         const std::string& mac,
                                         uint16_t port,
                                         uint32_t* assigned_idx) {
    pos::p4runtime::ForwardingRuleRequest request;
    request.set_ip_cidr(ip_cidr);
    request.set_mac(mac);
    request.set_port(port);

    pos::p4runtime::RuleResponse response;
    auto context = createContext();

    grpc::Status status = stub_->AddForwardingRule(context.get(), request, &response);

    if (!status.ok()) {
        last_error_ = "gRPC error: " + status.error_message();
        return false;
    }

    if (!response.success()) {
        last_error_ = response.error_message();
        return false;
    }

    if (assigned_idx) {
        *assigned_idx = response.entry_idx();
    }

    return true;
}

bool P4RuntimeClient::addDropRule(const std::string& ip_cidr,
                                   uint32_t* assigned_idx) {
    pos::p4runtime::DropRuleRequest request;
    request.set_ip_cidr(ip_cidr);

    pos::p4runtime::RuleResponse response;
    auto context = createContext();

    grpc::Status status = stub_->AddDropRule(context.get(), request, &response);

    if (!status.ok()) {
        last_error_ = "gRPC error: " + status.error_message();
        return false;
    }

    if (!response.success()) {
        last_error_ = response.error_message();
        return false;
    }

    if (assigned_idx) {
        *assigned_idx = response.entry_idx();
    }

    return true;
}

bool P4RuntimeClient::addDefaultRoute(const std::string& action,
                                       const std::string& mac,
                                       uint16_t port,
                                       uint32_t* assigned_idx) {
    pos::p4runtime::DefaultRouteRequest request;
    request.set_action(action);
    request.set_mac(mac);
    request.set_port(port);

    pos::p4runtime::RuleResponse response;
    auto context = createContext();

    grpc::Status status = stub_->AddDefaultRoute(context.get(), request, &response);

    if (!status.ok()) {
        last_error_ = "gRPC error: " + status.error_message();
        return false;
    }

    if (!response.success()) {
        last_error_ = response.error_message();
        return false;
    }

    if (assigned_idx) {
        *assigned_idx = response.entry_idx();
    }

    return true;
}

int P4RuntimeClient::addRoutingRules(
    const std::vector<std::tuple<std::string, std::string, uint16_t, std::string>>& rules) {

    pos::p4runtime::BatchRoutingRulesRequest request;

    for (const auto& [ip_cidr, mac, port, action] : rules) {
        auto* rule = request.add_rules();
        rule->set_ip_cidr(ip_cidr);
        rule->set_mac(mac);
        rule->set_port(port);
        rule->set_action(action);
    }

    pos::p4runtime::BatchRoutingRulesResponse response;
    auto context = createContext();

    grpc::Status status = stub_->AddRoutingRules(context.get(), request, &response);

    if (!status.ok()) {
        last_error_ = "gRPC error: " + status.error_message();
        return 0;
    }

    return static_cast<int>(response.success_count());
}

// =============================================================================
// Route Management
// =============================================================================

bool P4RuntimeClient::findRoute(const std::string& ip_cidr, ClientTableEntry* entry) {
    pos::p4runtime::RouteLookupRequest request;
    request.set_ip_address(ip_cidr);

    pos::p4runtime::RouteLookupResponse response;
    auto context = createContext();

    grpc::Status status = stub_->LookupRoute(context.get(), request, &response);

    if (!status.ok()) {
        last_error_ = "gRPC error: " + status.error_message();
        return false;
    }

    if (!response.found()) {
        return false;
    }

    if (entry) {
        const auto& proto_entry = response.entry();
        entry->table_name = proto_entry.table_name();
        entry->entry_idx = proto_entry.entry_idx();
        entry->prefix = proto_entry.prefix();
        entry->prefix_len = static_cast<uint8_t>(proto_entry.prefix_len());
        entry->action_name = proto_entry.action_name();
        entry->dst_mac = proto_entry.dst_mac();
        entry->egress_port = static_cast<uint16_t>(proto_entry.egress_port());
        entry->priority = proto_entry.priority();
        entry->description = proto_entry.description();
    }

    return true;
}

bool P4RuntimeClient::deleteRoute(const std::string& ip_cidr) {
    pos::p4runtime::DropRuleRequest request;
    request.set_ip_cidr(ip_cidr);

    pos::p4runtime::RuleResponse response;
    auto context = createContext();

    grpc::Status status = stub_->DeleteRoute(context.get(), request, &response);

    if (!status.ok()) {
        last_error_ = "gRPC error: " + status.error_message();
        return false;
    }

    if (!response.success()) {
        last_error_ = response.error_message();
        return false;
    }

    return true;
}

bool P4RuntimeClient::hasRoute(const std::string& ip_cidr) {
    ClientTableEntry entry;
    return findRoute(ip_cidr, &entry);
}

// =============================================================================
// Statistics and Monitoring
// =============================================================================

uint32_t P4RuntimeClient::getRouteCount() {
    auto stats = getStats();
    return stats.total_entries;
}

ClientStats P4RuntimeClient::getStats() {
    ClientStats stats = {};

    pos::p4runtime::StatsRequest request;
    pos::p4runtime::StatsResponse response;
    auto context = createContext();

    grpc::Status status = stub_->GetStats(context.get(), request, &response);

    if (!status.ok()) {
        last_error_ = "gRPC error: " + status.error_message();
        return stats;
    }

    stats.total_entries = response.total_entries();
    stats.max_entries = response.max_entries();
    stats.next_entry_idx = response.next_entry_idx();

    for (const auto& proto_entry : response.entries()) {
        ClientTableEntry entry;
        entry.table_name = proto_entry.table_name();
        entry.entry_idx = proto_entry.entry_idx();
        entry.prefix = proto_entry.prefix();
        entry.prefix_len = static_cast<uint8_t>(proto_entry.prefix_len());
        entry.action_name = proto_entry.action_name();
        entry.dst_mac = proto_entry.dst_mac();
        entry.egress_port = static_cast<uint16_t>(proto_entry.egress_port());
        entry.priority = proto_entry.priority();
        entry.description = proto_entry.description();
        stats.entries.push_back(entry);
    }

    return stats;
}

void P4RuntimeClient::printStatistics() {
    auto stats = getStats();

    std::cout << "\n=== P4Runtime Client Statistics ===" << std::endl;
    std::cout << "Server: " << server_address_ << std::endl;
    std::cout << "Connected: " << (isConnected() ? "Yes" : "No") << std::endl;
    std::cout << "Installed entries: " << stats.total_entries << "/" << stats.max_entries << std::endl;
    std::cout << "Next entry index: " << stats.next_entry_idx << std::endl;

    if (stats.entries.empty()) {
        std::cout << "\nNo entries installed." << std::endl;
    } else {
        std::cout << "\nInstalled entries:" << std::endl;
        std::cout << "Idx | Prefix              | Len | Action      | MAC               | Port" << std::endl;
        std::cout << "----+---------------------+-----+-------------+-------------------+-----" << std::endl;

        for (const auto& entry : stats.entries) {
            std::cout << std::setw(3) << entry.entry_idx << " | "
                      << std::setw(15) << std::left << formatIPAddress(entry.prefix) << std::right << " | "
                      << std::setw(3) << static_cast<int>(entry.prefix_len) << " | "
                      << std::setw(11) << std::left << entry.action_name << std::right << " | "
                      << formatMACAddress(entry.dst_mac) << " | "
                      << std::setw(4) << entry.egress_port << std::endl;
        }
    }

    std::cout << "====================================\n" << std::endl;
}

bool P4RuntimeClient::verifyHardware() {
    pos::p4runtime::VerifyHardwareRequest request;
    pos::p4runtime::VerifyHardwareResponse response;
    auto context = createContext();

    grpc::Status status = stub_->VerifyHardware(context.get(), request, &response);

    if (!status.ok()) {
        last_error_ = "gRPC error: " + status.error_message();
        return false;
    }

    if (!response.hardware_ok()) {
        last_error_ = response.status_message();
    }

    return response.hardware_ok();
}

// =============================================================================
// Static Helpers
// =============================================================================

std::pair<uint32_t, uint8_t> P4RuntimeClient::parseCIDR(const std::string& cidr) {
    size_t slash_pos = cidr.find('/');

    if (slash_pos == std::string::npos) {
        // No prefix length, assume /32 (single host)
        return {parseIPAddress(cidr), 32};
    }

    std::string ip_part = cidr.substr(0, slash_pos);
    std::string len_part = cidr.substr(slash_pos + 1);

    uint32_t prefix = parseIPAddress(ip_part);
    uint8_t prefix_len = static_cast<uint8_t>(std::stoi(len_part));

    return {prefix, prefix_len};
}

uint32_t P4RuntimeClient::parseIPAddress(const std::string& ip_str) {
    uint32_t result = 0;
    std::istringstream iss(ip_str);
    std::string part;
    int shift = 24;

    while (std::getline(iss, part, '.') && shift >= 0) {
        uint32_t byte = std::stoi(part);
        if (byte > 255) {
            throw std::runtime_error("Invalid IP address: " + ip_str);
        }
        result |= (byte << shift);
        shift -= 8;
    }

    return result;
}

uint64_t P4RuntimeClient::parseMACAddress(const std::string& mac_str) {
    uint64_t result = 0;
    std::istringstream iss(mac_str);
    std::string byte_str;
    int shift = 40;

    while (std::getline(iss, byte_str, ':') && shift >= 0) {
        uint64_t byte = std::stoul(byte_str, nullptr, 16);
        if (byte > 255) {
            throw std::runtime_error("Invalid MAC address: " + mac_str);
        }
        result |= (byte << shift);
        shift -= 8;
    }

    return result;
}

std::string P4RuntimeClient::formatIPAddress(uint32_t ip) {
    std::ostringstream oss;
    oss << ((ip >> 24) & 0xFF) << "."
        << ((ip >> 16) & 0xFF) << "."
        << ((ip >> 8) & 0xFF) << "."
        << (ip & 0xFF);
    return oss.str();
}

std::string P4RuntimeClient::formatMACAddress(uint64_t mac) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 40; i >= 0; i -= 8) {
        oss << std::setw(2) << ((mac >> i) & 0xFF);
        if (i > 0) oss << ":";
    }
    return oss.str();
}

} // namespace pos

/**
 * P4Runtime gRPC Server Implementation
 *
 * Implements the gRPC service that wraps POSRuntimeEngine.
 */

#include "p4runtime_server.hpp"
#include "p4runtime_simple.grpc.pb.h"

#include <iostream>
#include <sstream>
#include <csignal>

namespace pos {

// =============================================================================
// P4RuntimeServer Implementation
// =============================================================================

P4RuntimeServer::P4RuntimeServer(POSRuntimeEngine* engine,
                                 const std::string& address,
                                 int max_message_size)
    : engine_(engine),
      server_address_(address),
      max_message_size_(max_message_size),
      running_(false) {

    if (!engine_) {
        throw std::runtime_error("P4RuntimeServer: null engine pointer");
    }

    // Create service as member variable for proper lifetime management
    service_ = std::make_unique<P4RuntimeServiceImpl>(engine_);
}

P4RuntimeServer::~P4RuntimeServer() {
    stop();
}

void P4RuntimeServer::buildServer() {
    grpc::EnableDefaultHealthCheckService(true);

    grpc::ServerBuilder builder;

    // Listen on the given address without authentication
    builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials());

    // Register the service (member variable, not stack-allocated)
    builder.RegisterService(service_.get());

    // Set max message sizes
    builder.SetMaxReceiveMessageSize(max_message_size_);
    builder.SetMaxSendMessageSize(max_message_size_);

    // Build and start the server
    server_ = builder.BuildAndStart();

    if (!server_) {
        throw std::runtime_error("Failed to start gRPC server on " + server_address_);
    }
}

void P4RuntimeServer::run() {
    grpc::EnableDefaultHealthCheckService(true);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials());

    // Register the service (member variable, not stack-allocated)
    builder.RegisterService(service_.get());

    builder.SetMaxReceiveMessageSize(max_message_size_);
    builder.SetMaxSendMessageSize(max_message_size_);

    server_ = builder.BuildAndStart();

    if (!server_) {
        throw std::runtime_error("Failed to start gRPC server on " + server_address_);
    }

    running_ = true;
    std::cout << "P4Runtime Server listening on " << server_address_ << std::endl;

    // Wait for the server to shutdown
    server_->Wait();

    running_ = false;
}

bool P4RuntimeServer::start() {
    if (running_) {
        return false; // Already running
    }

    try {
        server_thread_ = std::thread([this]() {
            this->run();
        });
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to start server thread: " << e.what() << std::endl;
        return false;
    }
}

void P4RuntimeServer::stop() {
    if (server_) {
        std::cout << "Shutting down P4Runtime Server..." << std::endl;
        server_->Shutdown();
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }

    running_ = false;
}

// =============================================================================
// P4RuntimeServiceImpl Implementation
// =============================================================================

P4RuntimeServiceImpl::P4RuntimeServiceImpl(POSRuntimeEngine* engine)
    : engine_(engine) {
}

ClientIdentity P4RuntimeServiceImpl::extractClientIdentity(grpc::ServerContext* context) {
    ClientIdentity client;

    // Extract client ID from metadata (if provided via "x-client-id" header)
    auto client_id_meta = context->client_metadata().find("x-client-id");
    if (client_id_meta != context->client_metadata().end()) {
        client.client_id = std::string(client_id_meta->second.data(), client_id_meta->second.size());
    } else {
        // Fall back to peer address as client ID
        client.client_id = context->peer();
    }

    // Extract client IP from peer
    std::string peer = context->peer();
    // peer format is typically "ipv4:127.0.0.1:12345" or "ipv6:[::1]:12345"
    size_t colon_pos = peer.find(':');
    if (colon_pos != std::string::npos) {
        size_t last_colon = peer.rfind(':');
        if (last_colon != colon_pos) {
            // Extract IP portion (between first and last colon)
            client.client_ip = peer.substr(colon_pos + 1, last_colon - colon_pos - 1);
        } else {
            client.client_ip = peer.substr(colon_pos + 1);
        }
    } else {
        client.client_ip = peer;
    }

    // Extract vFPGA ID from metadata (if provided via "x-vfpga-id" header)
    auto vfpga_meta = context->client_metadata().find("x-vfpga-id");
    if (vfpga_meta != context->client_metadata().end()) {
        try {
            client.vfpga_id = std::stoul(std::string(vfpga_meta->second.data(), vfpga_meta->second.size()));
        } catch (...) {
            client.vfpga_id = 0;
        }
    }

    return client;
}

void P4RuntimeServiceImpl::entryToProto(const P4TableEntry& entry,
                                         pos::p4runtime::TableEntry* proto) {
    proto->set_table_name(entry.table_name);
    proto->set_entry_idx(entry.entry_idx);
    proto->set_prefix(entry.prefix);
    proto->set_prefix_len(entry.prefix_len);
    proto->set_action_name(entry.action_name);
    proto->set_dst_mac(entry.dst_mac);
    proto->set_egress_port(entry.egress_port);
    proto->set_priority(entry.priority);
    proto->set_description(entry.description);
}

P4TableEntry P4RuntimeServiceImpl::protoToEntry(const pos::p4runtime::TableEntry& proto) {
    P4TableEntry entry;
    entry.table_name = proto.table_name();
    entry.entry_idx = proto.entry_idx();
    entry.prefix = proto.prefix();
    entry.prefix_len = static_cast<uint8_t>(proto.prefix_len());
    entry.action_name = proto.action_name();
    entry.dst_mac = proto.dst_mac();
    entry.egress_port = static_cast<uint16_t>(proto.egress_port());
    entry.priority = proto.priority();
    entry.description = proto.description();
    return entry;
}

// -----------------------------------------------------------------------------
// Core Table Operations (ACL-enforced)
// -----------------------------------------------------------------------------

grpc::Status P4RuntimeServiceImpl::Write(grpc::ServerContext* context,
                                          const pos::p4runtime::WriteRequest* request,
                                          pos::p4runtime::WriteResponse* response) {
    try {
        ClientIdentity client = extractClientIdentity(context);
        P4TableEntry entry = protoToEntry(request->entry());
        bool success = false;
        uint32_t assigned_idx = entry.entry_idx;

        switch (request->type()) {
            case pos::p4runtime::UPDATE_INSERT:
                success = engine_->installTableEntry(entry, client);
                // Get the assigned index from the installed entry
                if (success) {
                    auto entries = engine_->readTableEntries(client);
                    if (!entries.empty()) {
                        assigned_idx = entries.back().entry_idx;
                    }
                }
                break;

            case pos::p4runtime::UPDATE_MODIFY:
                success = engine_->modifyTableEntry(entry.entry_idx, entry, client);
                assigned_idx = entry.entry_idx;
                break;

            case pos::p4runtime::UPDATE_DELETE:
                success = engine_->deleteTableEntry(entry.entry_idx, client);
                assigned_idx = entry.entry_idx;
                break;

            default:
                response->set_success(false);
                response->set_error_message("Unknown update type");
                return grpc::Status::OK;
        }

        response->set_success(success);
        response->set_entry_idx(assigned_idx);
        if (!success) {
            response->set_error_message("Operation failed (check ACL permissions)");
        }

    } catch (const std::exception& e) {
        response->set_success(false);
        response->set_error_message(e.what());
    }

    return grpc::Status::OK;
}

grpc::Status P4RuntimeServiceImpl::BatchWrite(grpc::ServerContext* context,
                                               const pos::p4runtime::BatchWriteRequest* request,
                                               pos::p4runtime::BatchWriteResponse* response) {
    ClientIdentity client = extractClientIdentity(context);
    bool all_success = true;

    for (const auto& req : request->requests()) {
        auto* single_response = response->add_responses();

        try {
            P4TableEntry entry = protoToEntry(req.entry());
            bool success = false;

            switch (req.type()) {
                case pos::p4runtime::UPDATE_INSERT:
                    success = engine_->installTableEntry(entry, client);
                    break;
                case pos::p4runtime::UPDATE_MODIFY:
                    success = engine_->modifyTableEntry(entry.entry_idx, entry, client);
                    break;
                case pos::p4runtime::UPDATE_DELETE:
                    success = engine_->deleteTableEntry(entry.entry_idx, client);
                    break;
                default:
                    success = false;
            }

            single_response->set_success(success);
            if (!success) {
                all_success = false;
                single_response->set_error_message("Operation failed (check ACL permissions)");
            }

        } catch (const std::exception& e) {
            single_response->set_success(false);
            single_response->set_error_message(e.what());
            all_success = false;
        }
    }

    response->set_all_success(all_success);
    return grpc::Status::OK;
}

grpc::Status P4RuntimeServiceImpl::Read(grpc::ServerContext* context,
                                         const pos::p4runtime::ReadRequest* request,
                                         pos::p4runtime::ReadResponse* response) {
    try {
        ClientIdentity client = extractClientIdentity(context);
        auto entries = engine_->readTableEntries(client);

        for (const auto& entry : entries) {
            // Filter by table name if specified
            if (!request->table_name().empty() &&
                entry.table_name != request->table_name()) {
                continue;
            }

            // Filter by entry index if specified (non-zero)
            if (request->entry_idx() != 0 &&
                entry.entry_idx != request->entry_idx()) {
                continue;
            }

            auto* proto_entry = response->add_entries();
            entryToProto(entry, proto_entry);
        }

    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }

    return grpc::Status::OK;
}

grpc::Status P4RuntimeServiceImpl::Clear(grpc::ServerContext* context,
                                          const pos::p4runtime::ClearRequest* request,
                                          pos::p4runtime::ClearResponse* response) {
    try {
        ClientIdentity client = extractClientIdentity(context);
        uint32_t count_before = engine_->getRouteCount();
        engine_->clearAllEntries(client);
        uint32_t count_after = engine_->getRouteCount();

        response->set_success(count_after == 0);
        response->set_entries_cleared(count_before - count_after);

    } catch (const std::exception& e) {
        response->set_success(false);
        response->set_entries_cleared(0);
    }

    return grpc::Status::OK;
}

// -----------------------------------------------------------------------------
// High-Level Convenience Operations (ACL-enforced)
// -----------------------------------------------------------------------------

grpc::Status P4RuntimeServiceImpl::AddForwardingRule(grpc::ServerContext* context,
                                                      const pos::p4runtime::ForwardingRuleRequest* request,
                                                      pos::p4runtime::RuleResponse* response) {
    try {
        ClientIdentity client = extractClientIdentity(context);
        bool success = engine_->addForwardingRule(
            request->ip_cidr(),
            request->mac(),
            static_cast<uint16_t>(request->port()),
            client
        );

        response->set_success(success);
        if (success) {
            // Get the assigned index (last entry)
            auto entries = engine_->readTableEntries(client);
            if (!entries.empty()) {
                response->set_entry_idx(entries.back().entry_idx);
            }
        } else {
            response->set_error_message("Failed to add forwarding rule (check ACL permissions)");
        }

    } catch (const std::exception& e) {
        response->set_success(false);
        response->set_error_message(e.what());
    }

    return grpc::Status::OK;
}

grpc::Status P4RuntimeServiceImpl::AddDropRule(grpc::ServerContext* context,
                                                const pos::p4runtime::DropRuleRequest* request,
                                                pos::p4runtime::RuleResponse* response) {
    try {
        ClientIdentity client = extractClientIdentity(context);
        bool success = engine_->addDropRule(request->ip_cidr(), client);

        response->set_success(success);
        if (success) {
            auto entries = engine_->readTableEntries(client);
            if (!entries.empty()) {
                response->set_entry_idx(entries.back().entry_idx);
            }
        } else {
            response->set_error_message("Failed to add drop rule (check ACL permissions)");
        }

    } catch (const std::exception& e) {
        response->set_success(false);
        response->set_error_message(e.what());
    }

    return grpc::Status::OK;
}

grpc::Status P4RuntimeServiceImpl::AddDefaultRoute(grpc::ServerContext* context,
                                                    const pos::p4runtime::DefaultRouteRequest* request,
                                                    pos::p4runtime::RuleResponse* response) {
    try {
        ClientIdentity client = extractClientIdentity(context);
        bool success = engine_->addDefaultRoute(
            request->action(),
            request->mac(),
            static_cast<uint16_t>(request->port()),
            client
        );

        response->set_success(success);
        if (success) {
            auto entries = engine_->readTableEntries(client);
            if (!entries.empty()) {
                response->set_entry_idx(entries.back().entry_idx);
            }
        } else {
            response->set_error_message("Failed to add default route (check ACL permissions)");
        }

    } catch (const std::exception& e) {
        response->set_success(false);
        response->set_error_message(e.what());
    }

    return grpc::Status::OK;
}

grpc::Status P4RuntimeServiceImpl::AddRoutingRules(grpc::ServerContext* context,
                                                    const pos::p4runtime::BatchRoutingRulesRequest* request,
                                                    pos::p4runtime::BatchRoutingRulesResponse* response) {
    ClientIdentity client = extractClientIdentity(context);
    uint32_t success_count = 0;
    uint32_t total_count = request->rules_size();

    for (const auto& rule : request->rules()) {
        auto* rule_response = response->add_responses();

        try {
            bool success = false;

            if (rule.action() == "forward" || rule.action() == "ipv4_forward") {
                success = engine_->addForwardingRule(
                    rule.ip_cidr(),
                    rule.mac(),
                    static_cast<uint16_t>(rule.port()),
                    client
                );
            } else if (rule.action() == "drop") {
                success = engine_->addDropRule(rule.ip_cidr(), client);
            }

            rule_response->set_success(success);
            if (success) {
                success_count++;
                auto entries = engine_->readTableEntries(client);
                if (!entries.empty()) {
                    rule_response->set_entry_idx(entries.back().entry_idx);
                }
            } else {
                rule_response->set_error_message("Failed to add rule (check ACL permissions)");
            }

        } catch (const std::exception& e) {
            rule_response->set_success(false);
            rule_response->set_error_message(e.what());
        }
    }

    response->set_success_count(success_count);
    response->set_total_count(total_count);

    return grpc::Status::OK;
}

// -----------------------------------------------------------------------------
// Route Management (ACL-enforced)
// -----------------------------------------------------------------------------

grpc::Status P4RuntimeServiceImpl::LookupRoute(grpc::ServerContext* context,
                                                const pos::p4runtime::RouteLookupRequest* request,
                                                pos::p4runtime::RouteLookupResponse* response) {
    try {
        ClientIdentity client = extractClientIdentity(context);
        // findRouteByIP now returns std::optional<P4TableEntry>
        auto found = engine_->findRouteByIP(request->ip_address(), client);

        if (found) {
            response->set_found(true);
            entryToProto(*found, response->mutable_entry());
        } else {
            response->set_found(false);
        }

    } catch (const std::exception& e) {
        response->set_found(false);
    }

    return grpc::Status::OK;
}

grpc::Status P4RuntimeServiceImpl::DeleteRoute(grpc::ServerContext* context,
                                                const pos::p4runtime::DropRuleRequest* request,
                                                pos::p4runtime::RuleResponse* response) {
    try {
        ClientIdentity client = extractClientIdentity(context);
        bool success = engine_->deleteRoute(request->ip_cidr(), client);

        response->set_success(success);
        if (!success) {
            response->set_error_message("Route not found or delete failed (check ACL permissions)");
        }

    } catch (const std::exception& e) {
        response->set_success(false);
        response->set_error_message(e.what());
    }

    return grpc::Status::OK;
}

// -----------------------------------------------------------------------------
// Statistics and Monitoring (ACL-enforced)
// -----------------------------------------------------------------------------

grpc::Status P4RuntimeServiceImpl::GetStats(grpc::ServerContext* context,
                                             const pos::p4runtime::StatsRequest* request,
                                             pos::p4runtime::StatsResponse* response) {
    try {
        ClientIdentity client = extractClientIdentity(context);
        auto entries = engine_->readTableEntries(client);

        response->set_total_entries(static_cast<uint32_t>(entries.size()));
        response->set_max_entries(1024); // From P4InfoMetadata default
        response->set_next_entry_idx(static_cast<uint32_t>(entries.size()));

        for (const auto& entry : entries) {
            auto* proto_entry = response->add_entries();
            entryToProto(entry, proto_entry);
        }

    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }

    return grpc::Status::OK;
}

grpc::Status P4RuntimeServiceImpl::VerifyHardware(grpc::ServerContext* context,
                                                   const pos::p4runtime::VerifyHardwareRequest* request,
                                                   pos::p4runtime::VerifyHardwareResponse* response) {
    try {
        bool ok = engine_->verifyHardware();

        response->set_hardware_ok(ok);
        response->set_status_message(ok ? "Hardware verification passed" : "Hardware verification failed");

    } catch (const std::exception& e) {
        response->set_hardware_ok(false);
        response->set_status_message(std::string("Exception: ") + e.what());
    }

    return grpc::Status::OK;
}

} // namespace pos

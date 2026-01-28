/*
 * P4Runtime gRPC Server
 *
 * Provides gRPC interface for remote P4 table management.
 * This server will be integrated with the POS Shell Manager and CCM
 * for capability-based access control.
 *
 * Architecture:
 *   P4Runtime Client ──→ gRPC ──→ P4RuntimeServer ──→ POSRuntimeEngine ──→ vFPGA
 *
 * Copyright (c) 2025, Systems Group, ETH Zurich
 * MIT License
 */

#pragma once

#include "runtime_engine.hpp"

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <string>
#include <thread>
#include <memory>
#include <atomic>

// Forward declaration of generated protobuf types
// Note: proto package is "pos.p4runtime" which generates namespace pos::p4runtime
namespace pos {
namespace p4runtime {
    class TableEntry;
    class WriteRequest;
    class WriteResponse;
    class BatchWriteRequest;
    class BatchWriteResponse;
    class ReadRequest;
    class ReadResponse;
    class ClearRequest;
    class ClearResponse;
    class ForwardingRuleRequest;
    class DropRuleRequest;
    class DefaultRouteRequest;
    class RuleResponse;
    class BatchRoutingRulesRequest;
    class BatchRoutingRulesResponse;
    class RouteLookupRequest;
    class RouteLookupResponse;
    class StatsRequest;
    class StatsResponse;
    class VerifyHardwareRequest;
    class VerifyHardwareResponse;
    class P4RuntimeService;
} // namespace p4runtime
} // namespace pos

namespace pos {

/**
 * P4Runtime gRPC Service Implementation
 *
 * Implements the P4Runtime service RPCs by delegating to POSRuntimeEngine.
 * This class handles the protobuf serialization/deserialization.
 */
class P4RuntimeServiceImpl final : public pos::p4runtime::P4RuntimeService::Service {
public:
    explicit P4RuntimeServiceImpl(POSRuntimeEngine* engine);

    // Core Table Operations
    grpc::Status Write(grpc::ServerContext* context,
                       const pos::p4runtime::WriteRequest* request,
                       pos::p4runtime::WriteResponse* response) override;

    grpc::Status BatchWrite(grpc::ServerContext* context,
                            const pos::p4runtime::BatchWriteRequest* request,
                            pos::p4runtime::BatchWriteResponse* response) override;

    grpc::Status Read(grpc::ServerContext* context,
                      const pos::p4runtime::ReadRequest* request,
                      pos::p4runtime::ReadResponse* response) override;

    grpc::Status Clear(grpc::ServerContext* context,
                       const pos::p4runtime::ClearRequest* request,
                       pos::p4runtime::ClearResponse* response) override;

    // High-Level Convenience Operations
    grpc::Status AddForwardingRule(grpc::ServerContext* context,
                                   const pos::p4runtime::ForwardingRuleRequest* request,
                                   pos::p4runtime::RuleResponse* response) override;

    grpc::Status AddDropRule(grpc::ServerContext* context,
                             const pos::p4runtime::DropRuleRequest* request,
                             pos::p4runtime::RuleResponse* response) override;

    grpc::Status AddDefaultRoute(grpc::ServerContext* context,
                                 const pos::p4runtime::DefaultRouteRequest* request,
                                 pos::p4runtime::RuleResponse* response) override;

    grpc::Status AddRoutingRules(grpc::ServerContext* context,
                                 const pos::p4runtime::BatchRoutingRulesRequest* request,
                                 pos::p4runtime::BatchRoutingRulesResponse* response) override;

    // Route Management
    grpc::Status LookupRoute(grpc::ServerContext* context,
                             const pos::p4runtime::RouteLookupRequest* request,
                             pos::p4runtime::RouteLookupResponse* response) override;

    grpc::Status DeleteRoute(grpc::ServerContext* context,
                             const pos::p4runtime::DropRuleRequest* request,
                             pos::p4runtime::RuleResponse* response) override;

    // Statistics and Monitoring
    grpc::Status GetStats(grpc::ServerContext* context,
                          const pos::p4runtime::StatsRequest* request,
                          pos::p4runtime::StatsResponse* response) override;

    grpc::Status VerifyHardware(grpc::ServerContext* context,
                                const pos::p4runtime::VerifyHardwareRequest* request,
                                pos::p4runtime::VerifyHardwareResponse* response) override;

private:
    POSRuntimeEngine* engine_;

    // Conversion helpers
    static void entryToProto(const P4TableEntry& entry, pos::p4runtime::TableEntry* proto);
    static P4TableEntry protoToEntry(const pos::p4runtime::TableEntry& proto);

    // Extract client identity from gRPC context
    static ClientIdentity extractClientIdentity(grpc::ServerContext* context);
};

/**
 * P4Runtime gRPC Server
 *
 * Manages the gRPC server lifecycle and threading.
 */
class P4RuntimeServer {
public:
    /**
     * Constructor
     * @param engine - POSRuntimeEngine for table operations
     * @param address - Server address (e.g., "0.0.0.0:50051")
     * @param max_message_size - Max gRPC message size in bytes
     */
    P4RuntimeServer(POSRuntimeEngine* engine,
                    const std::string& address = "0.0.0.0:50051",
                    int max_message_size = 4 * 1024 * 1024);

    ~P4RuntimeServer();

    // Non-copyable
    P4RuntimeServer(const P4RuntimeServer&) = delete;
    P4RuntimeServer& operator=(const P4RuntimeServer&) = delete;

    /**
     * Start the server in a background thread
     * @return true if started successfully
     */
    bool start();

    /**
     * Run the server in the current thread (blocking)
     */
    void run();

    /**
     * Stop the server
     */
    void stop();

    /**
     * Check if server is running
     */
    bool isRunning() const { return running_; }

    /**
     * Get the server address
     */
    const std::string& getAddress() const { return server_address_; }

private:
    void buildServer();

    POSRuntimeEngine* engine_;
    std::string server_address_;
    int max_message_size_;

    std::unique_ptr<P4RuntimeServiceImpl> service_;  // Member variable for proper lifetime
    std::unique_ptr<grpc::Server> server_;
    std::thread server_thread_;
    std::atomic<bool> running_;
};

} // namespace pos

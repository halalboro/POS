/*
 * POS Server
 *
 * gRPC server running on POS worker nodes that receives deployment requests
 * from client nodes and manages DFG instances on the local FPGA.
 *
 * Architecture (from figure):
 *   Client node (gRPC client) ──→ Worker node (POS server) ──→ POS kernel ──→ vFPGA
 *
 * Copyright (c) 2025, Systems Group, ETH Zurich
 * MIT License
 */

#pragma once

#include "dfg.hpp"
#include "pipeline.hpp"

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <string>
#include <thread>
#include <memory>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <chrono>

// Forward declaration of generated protobuf types
namespace pos {
    class DFGSpec;
    class DFGHandle;
    class DFGStatus;
    class DeployDFGRequest;
    class DeployDFGResponse;
    class UndeployDFGRequest;
    class UndeployDFGResponse;
    class GetDFGStatusRequest;
    class GetDFGStatusResponse;
    class ListDFGsRequest;
    class ListDFGsResponse;
    class ExecuteNodeRequest;
    class ExecuteNodeResponse;
    class ReadBufferRequest;
    class ReadBufferResponse;
    class WriteBufferRequest;
    class WriteBufferResponse;
    class DelegateCapabilityRequest;
    class DelegateCapabilityResponse;
    class RevokeCapabilityRequest;
    class RevokeCapabilityResponse;
    class HealthCheckRequest;
    class HealthCheckResponse;
    class SetupRDMARequest;
    class SetupRDMAResponse;
    class ExecuteDFGRequest;
    class ExecuteDFGResponse;
    class POSService;
} // namespace pos

namespace pos {

/**
 * RDMA connection info for Multi-FPGA deployments
 */
struct RDMAConnectionInfo {
    uint32_t local_qpn;
    uint32_t remote_qpn;
    std::string local_ip;
    std::string remote_ip;
};

/**
 * Deployed DFG instance - tracks a running DFG on this worker node
 */
struct DeployedDFGInstance {
    std::string instance_id;
    std::string dfg_id;
    std::string client_id;
    std::shared_ptr<::dfg::DFG> dfg;
    std::chrono::time_point<std::chrono::system_clock> deploy_time;
    std::atomic<uint64_t> bytes_processed{0};
    std::atomic<uint64_t> operations_completed{0};

    enum class State {
        DEPLOYING,
        RUNNING,
        STALLED,
        ERROR,
        STOPPED
    };
    std::atomic<State> state{State::DEPLOYING};
    std::string error_message;

    // RDMA connections for Multi-FPGA (node_id -> connection info)
    std::unordered_map<std::string, RDMAConnectionInfo> rdma_connections;
};

/**
 * POS Service Implementation
 *
 * Implements the POSService RPCs by managing DFG instances on the worker node.
 */
class POSServiceImpl final : public pos::POSService::Service {
public:
    POSServiceImpl();
    ~POSServiceImpl();

    // DFG lifecycle management
    grpc::Status DeployDFG(grpc::ServerContext* context,
                           const pos::DeployDFGRequest* request,
                           pos::DeployDFGResponse* response) override;

    grpc::Status UndeployDFG(grpc::ServerContext* context,
                             const pos::UndeployDFGRequest* request,
                             pos::UndeployDFGResponse* response) override;

    grpc::Status GetDFGStatus(grpc::ServerContext* context,
                              const pos::GetDFGStatusRequest* request,
                              pos::GetDFGStatusResponse* response) override;

    grpc::Status ListDFGs(grpc::ServerContext* context,
                          const pos::ListDFGsRequest* request,
                          pos::ListDFGsResponse* response) override;

    // Node operations
    grpc::Status ExecuteNode(grpc::ServerContext* context,
                             const pos::ExecuteNodeRequest* request,
                             pos::ExecuteNodeResponse* response) override;

    // Buffer operations
    grpc::Status ReadBuffer(grpc::ServerContext* context,
                            const pos::ReadBufferRequest* request,
                            pos::ReadBufferResponse* response) override;

    grpc::Status WriteBuffer(grpc::ServerContext* context,
                             const pos::WriteBufferRequest* request,
                             pos::WriteBufferResponse* response) override;

    // Capability management
    grpc::Status DelegateCapability(grpc::ServerContext* context,
                                    const pos::DelegateCapabilityRequest* request,
                                    pos::DelegateCapabilityResponse* response) override;

    grpc::Status RevokeCapability(grpc::ServerContext* context,
                                  const pos::RevokeCapabilityRequest* request,
                                  pos::RevokeCapabilityResponse* response) override;

    // Health and monitoring
    grpc::Status HealthCheck(grpc::ServerContext* context,
                             const pos::HealthCheckRequest* request,
                             pos::HealthCheckResponse* response) override;

    // Multi-FPGA operations
    grpc::Status SetupRDMA(grpc::ServerContext* context,
                           const pos::SetupRDMARequest* request,
                           pos::SetupRDMAResponse* response) override;

    grpc::Status ExecuteDFG(grpc::ServerContext* context,
                            const pos::ExecuteDFGRequest* request,
                            pos::ExecuteDFGResponse* response) override;

    // Statistics
    size_t getActiveInstanceCount() const;
    uint64_t getUptimeSeconds() const;

private:
    // Instance management
    std::unordered_map<std::string, std::shared_ptr<DeployedDFGInstance>> instances_;
    mutable std::mutex instances_mutex_;

    // Server start time for uptime calculation
    std::chrono::time_point<std::chrono::system_clock> start_time_;

    // Instance ID generation
    std::atomic<uint64_t> next_instance_id_{1};

    // Helper functions
    std::string generateInstanceId();
    std::shared_ptr<DeployedDFGInstance> getInstance(const std::string& instance_id);

    // DFG construction from proto
    std::shared_ptr<::dfg::DFG> buildDFGFromSpec(const pos::DFGSpec& spec,
                                                  std::string& error_message);

    // Capability management helpers
    ::dfg::Capability* findCapability(::dfg::DFG* dfg, const std::string& cap_id);

    // Extract client identity from gRPC context
    std::string extractClientId(grpc::ServerContext* context,
                                const std::string& request_client_id);
};

/**
 * POS Server
 *
 * gRPC server running on worker nodes that manages DFG deployment requests.
 */
class POSServer {
public:
    /**
     * Constructor
     * @param address - Server address (e.g., "0.0.0.0:50052")
     * @param max_message_size - Max gRPC message size in bytes
     */
    POSServer(const std::string& address = "0.0.0.0:50052",
              int max_message_size = 64 * 1024 * 1024);  // 64MB default

    ~POSServer();

    // Non-copyable
    POSServer(const POSServer&) = delete;
    POSServer& operator=(const POSServer&) = delete;

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

    /**
     * Get the service implementation for direct access
     */
    POSServiceImpl* getService() { return service_.get(); }

private:
    void buildServer();

    std::string server_address_;
    int max_message_size_;

    std::unique_ptr<POSServiceImpl> service_;
    std::unique_ptr<grpc::Server> server_;
    std::thread server_thread_;
    std::atomic<bool> running_;
};

} // namespace pos

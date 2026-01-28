/*
 * POS Client (gRPC client)
 *
 * Client-side interface running on client nodes for deploying and managing DFGs
 * on remote POS worker nodes.
 *
 * Architecture (from figure):
 *   Client node (gRPC client) ──→ Worker node (POS server) ──→ POS kernel ──→ vFPGA
 *
 * Copyright (c) 2025, Systems Group, ETH Zurich
 * MIT License
 */

#pragma once

#include <grpcpp/grpcpp.h>

#include <string>
#include <memory>
#include <vector>
#include <chrono>
#include <functional>

// Forward declaration of generated protobuf types
namespace pos {
    class DFGSpec;
    class DFGHandle;
    class DFGStatus;
    class CapabilitySpec;
    class NodeSpec;
    class BufferSpec;
    class EdgeSpec;
    class POSService;
} // namespace pos

namespace pos {

/**
 * Result wrapper for client operations
 */
template<typename T>
struct ClientResult {
    bool success;
    std::string error_message;
    T value;

    ClientResult() : success(false) {}
    ClientResult(T v) : success(true), value(std::move(v)) {}
    ClientResult(const std::string& error) : success(false), error_message(error) {}

    operator bool() const { return success; }
};

// Specialization for void
template<>
struct ClientResult<void> {
    bool success;
    std::string error_message;

    ClientResult() : success(false) {}
    ClientResult(bool s) : success(s) {}
    ClientResult(const std::string& error) : success(false), error_message(error) {}

    operator bool() const { return success; }
};

/**
 * DFG Handle - returned after successful deployment
 */
struct DFGInstanceHandle {
    std::string dfg_id;
    std::string instance_id;
    uint64_t deployment_timestamp;
};

/**
 * DFG Status information
 */
struct DFGInstanceStatus {
    std::string instance_id;
    std::string dfg_id;

    enum class State {
        UNKNOWN,
        DEPLOYING,
        RUNNING,
        STALLED,
        ERROR,
        STOPPED
    };
    State state;

    uint64_t uptime_seconds;
    uint64_t bytes_processed;
    uint64_t operations_completed;
    std::string error_message;
};

/**
 * Server health information
 */
struct ServerHealth {
    bool healthy;
    std::string version;
    uint32_t active_dfgs;
    uint64_t uptime_seconds;
    uint64_t available_memory;
    uint32_t available_vfpgas;
};

/**
 * Capability specification for client use
 */
struct CapabilityInfo {
    std::string cap_id;
    uint32_t permissions;
    int scope;  // CapabilityScope enum value
    std::string parent_cap_id;
    uint64_t expiry_timestamp;
};

/**
 * POS Client (gRPC client)
 *
 * Provides a high-level interface for deploying and managing DFGs on
 * remote POS worker nodes via gRPC.
 */
class POSClient {
public:
    /**
     * Constructor
     * @param server_address - Server address (e.g., "localhost:50052")
     * @param client_id - Unique client identifier
     */
    POSClient(const std::string& server_address, const std::string& client_id);
    ~POSClient();

    // Non-copyable, but movable
    POSClient(const POSClient&) = delete;
    POSClient& operator=(const POSClient&) = delete;
    POSClient(POSClient&&) = default;
    POSClient& operator=(POSClient&&) = default;

    /**
     * Check if connected to the server
     */
    bool isConnected() const;

    /**
     * Set authentication token
     */
    void setAuthToken(const std::string& token) { auth_token_ = token; }

    /**
     * Set RPC timeout
     */
    void setTimeout(std::chrono::milliseconds timeout) { timeout_ = timeout; }

    // =========================================================================
    // DFG Lifecycle Management
    // =========================================================================

    /**
     * Deploy a DFG specification to the worker node
     * @param spec - The DFG specification (protobuf)
     * @return Handle to the deployed instance
     */
    ClientResult<DFGInstanceHandle> deployDFG(const pos::DFGSpec& spec);

    /**
     * Undeploy a DFG instance
     * @param instance_id - Instance ID to undeploy
     */
    ClientResult<void> undeployDFG(const std::string& instance_id);

    /**
     * Get status of a deployed DFG
     * @param instance_id - Instance ID
     */
    ClientResult<DFGInstanceStatus> getDFGStatus(const std::string& instance_id);

    /**
     * List all deployed DFGs (for this client)
     */
    ClientResult<std::vector<DFGInstanceStatus>> listDFGs();

    // =========================================================================
    // Node Operations
    // =========================================================================

    /**
     * Execute an operation on a compute node
     * @param instance_id - DFG instance
     * @param node_id - Node to execute on
     * @param cap_id - Capability for authorization
     * @param src_addr - Source address
     * @param src_len - Source length
     * @param dst_addr - Destination address
     * @param dst_len - Destination length
     * @param blocking - Wait for completion
     */
    ClientResult<uint64_t> executeNode(const std::string& instance_id,
                                       const std::string& node_id,
                                       const std::string& cap_id,
                                       uint64_t src_addr, uint32_t src_len,
                                       uint64_t dst_addr, uint32_t dst_len,
                                       bool blocking = true);

    // =========================================================================
    // Buffer Operations
    // =========================================================================

    /**
     * Read data from a buffer
     * @param instance_id - DFG instance
     * @param buffer_id - Buffer to read from
     * @param cap_id - Capability for authorization
     * @param offset - Offset in buffer
     * @param length - Number of bytes to read
     */
    ClientResult<std::string> readBuffer(const std::string& instance_id,
                                         const std::string& buffer_id,
                                         const std::string& cap_id,
                                         uint64_t offset, uint64_t length);

    /**
     * Write data to a buffer
     * @param instance_id - DFG instance
     * @param buffer_id - Buffer to write to
     * @param cap_id - Capability for authorization
     * @param offset - Offset in buffer
     * @param data - Data to write
     */
    ClientResult<void> writeBuffer(const std::string& instance_id,
                                   const std::string& buffer_id,
                                   const std::string& cap_id,
                                   uint64_t offset,
                                   const std::string& data);

    // =========================================================================
    // Capability Management
    // =========================================================================

    /**
     * Delegate a capability
     * @param instance_id - DFG instance
     * @param source_cap_id - Source capability to delegate from
     * @param new_cap_id - ID for the new delegated capability
     * @param permissions - Permissions (must be subset of source)
     * @param scope - Capability scope
     * @param expiry_timestamp - Expiry time (0 = no expiry)
     */
    ClientResult<CapabilityInfo> delegateCapability(
        const std::string& instance_id,
        const std::string& source_cap_id,
        const std::string& new_cap_id,
        uint32_t permissions,
        int scope = 0,  // LOCAL
        uint64_t expiry_timestamp = 0);

    /**
     * Revoke a capability
     * @param instance_id - DFG instance
     * @param cap_id - Capability to revoke
     * @param admin_cap_id - Capability with DELEGATE permission
     * @param recursive - Also revoke children
     */
    ClientResult<uint32_t> revokeCapability(const std::string& instance_id,
                                            const std::string& cap_id,
                                            const std::string& admin_cap_id,
                                            bool recursive = false);

    // =========================================================================
    // Health and Monitoring
    // =========================================================================

    /**
     * Check server health
     */
    ClientResult<ServerHealth> healthCheck();

    // =========================================================================
    // Multi-FPGA Support
    // =========================================================================

    /**
     * RDMA connection info returned by setupRDMA
     */
    struct RDMAConnectionInfo {
        uint32_t local_qpn;
        uint32_t remote_qpn;
        std::string local_ip;
        std::string remote_ip;
    };

    /**
     * Setup RDMA connection to another worker (Multi-FPGA)
     * @param instance_id - DFG instance on this worker
     * @param node_id - REMOTE node that needs RDMA
     * @param remote_ip - IP of the remote worker
     * @param remote_rdma_port - RDMA QP exchange port (default 18488)
     * @param buffer_size - RDMA buffer size
     * @param is_initiator - true = client (connect), false = server (listen)
     * @return Local and remote QPN info
     */
    ClientResult<RDMAConnectionInfo> setupRDMA(
        const std::string& instance_id,
        const std::string& node_id,
        const std::string& remote_ip,
        uint32_t remote_rdma_port = 18488,
        uint32_t buffer_size = 1024 * 1024,
        bool is_initiator = true);

    /**
     * Execute a deployed DFG (start the pipeline)
     * @param instance_id - DFG instance to execute
     * @param cap_id - Capability for authorization
     */
    ClientResult<void> executeDFG(const std::string& instance_id,
                                  const std::string& cap_id = "");

    // =========================================================================
    // DFG Specification Builder Helpers
    // =========================================================================

    /**
     * Create a new DFG specification
     */
    static std::unique_ptr<pos::DFGSpec> createDFGSpec(
        const std::string& dfg_id,
        const std::string& app_id,
        uint32_t device_id = 0,
        bool use_huge_pages = false);

    /**
     * Add a compute node to the specification
     */
    static void addComputeNode(pos::DFGSpec* spec,
                               const std::string& node_id,
                               int vfid,
                               uint32_t operation_type = 0);

    /**
     * Add an RDMA network node to the specification
     */
    static void addRDMANode(pos::DFGSpec* spec,
                            const std::string& node_id,
                            uint16_t vlan_id,
                            const std::string& remote_host = "",
                            uint32_t remote_port = 0);

    /**
     * Add a TCP network node to the specification
     */
    static void addTCPNode(pos::DFGSpec* spec,
                           const std::string& node_id,
                           bool is_server,
                           uint32_t port,
                           const std::string& remote_host = "");

    /**
     * Add a software parser node to the specification
     */
    static void addParserNode(pos::DFGSpec* spec,
                              const std::string& node_id,
                              uint64_t max_memory = 1024 * 1024,
                              double max_cpu = 50.0,
                              uint32_t max_threads = 1);

    /**
     * Add a software NF node to the specification
     */
    static void addSoftwareNFNode(pos::DFGSpec* spec,
                                  const std::string& node_id,
                                  uint64_t max_memory = 1024 * 1024,
                                  double max_cpu = 50.0,
                                  uint32_t max_threads = 1);

    /**
     * Add a buffer to the specification
     */
    static void addBuffer(pos::DFGSpec* spec,
                          const std::string& buffer_id,
                          uint64_t size,
                          bool use_huge_pages = false,
                          const std::string& initial_data = "");

    /**
     * Add an edge (connection) to the specification
     */
    static void addEdge(pos::DFGSpec* spec,
                        const std::string& source_id,
                        const std::string& target_id,
                        const std::string& edge_id = "");

private:
    std::string server_address_;
    std::string client_id_;
    std::string auth_token_;
    std::chrono::milliseconds timeout_{30000};  // 30 second default

    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<pos::POSService::Stub> stub_;

    // Helper to create context with deadline
    std::unique_ptr<grpc::ClientContext> createContext();
};

} // namespace pos

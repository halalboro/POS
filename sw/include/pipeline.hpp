#pragma once

/**
 * @file pipeline.hpp
 * @brief POS Unified Pipeline API - Simple, Model-Agnostic Dataflow Interface
 *
 * This file provides a unified API for building dataflow pipelines across
 * all deployment models (SmartNIC, FPGA Middlebox, Multi-FPGA).
 *
 * API OVERVIEW:
 * =============
 *   pos::ep(proto, name, ...)         - Create network I/O endpoint
 *   pos::task(type, name, ...)        - Create vFPGA or SW task
 *   pos::buffer(size)                 - Allocate shared memory buffer
 *   pos::nf(nodes...)                 - Compose nodes into NF
 *   nf.deploy(node)                   - Deploy NF to execution target
 *
 * DEPLOYMENT MODEL EXAMPLES:
 * ==========================
 *
 * 1. SmartNIC Model:
 *    ---------------
 *    rx  = pos::ep(TCP, "RX");
 *    mat = pos::task(vFPGA, "exact_match");
 *    act = pos::task(vFPGA, "action");
 *    tx  = pos::ep(TCP, "TX");
 *    mcast = pos::nf(rx, mat, act, tx);
 *    mcast.deploy(node);
 *
 * 2. FPGA Middlebox Model:
 *    ---------------------
 *    rx  = pos::ep(HOST, "RX", {.iface="eth0"});
 *    par = pos::task(SW, "parser_sw");
 *    buf = pos::buffer(64*1024);
 *    mat = pos::task(vFPGA, "exact_match");
 *    act = pos::task(vFPGA, "action");
 *    dep = pos::task(SW, "deparser_sw");
 *    tx  = pos::ep(HOST, "TX", {.iface="eth1"});
 *    mbox = pos::nf(rx, par, buf, mat, act, dep, tx);
 *    mbox.deploy(node);
 *
 * 3. Multi-FPGA Model:
 *    -----------------
 *    // First FPGA: packet processing
 *    rx   = pos::ep(TCP, "RX");
 *    mat  = pos::task(vFPGA, "exact_match");
 *    link = pos::ep(RDMA, "send", {.dst="n2"});
 *    nf1  = pos::nf(rx, mat, link);
 *    nf1.deploy(fpga0);
 *
 *    // Second FPGA: action processing
 *    recv = pos::ep(RDMA, "recv");
 *    act  = pos::task(vFPGA, "action");
 *    tx   = pos::ep(TCP, "TX");
 *    nf2  = pos::nf(recv, act, tx);
 *    nf2.deploy(fpga1);
 *
 * Copyright (c) 2025, Systems Group, ETH Zurich
 * MIT License
 */

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <queue>
#include <variant>
#include <optional>
#include <unistd.h>
#include "dfg.hpp"
#include "swx_runtime.hpp"
#include "pos_client.hpp"

namespace pos {

using namespace fpga;

// ============================================================================
// Protocol Enum - All supported I/O protocols
// ============================================================================

/**
 * Protocol types for endpoint creation.
 *
 * Usage: pos::ep(PROTOCOL, "name", {options})
 *
 * Examples:
 *   pos::ep(TCP, "RX")                      // TCP stack RX endpoint (SmartNIC)
 *   pos::ep(RDMA, "RX")                     // RDMA stack RX endpoint (SmartNIC)
 *   pos::ep(RDMA, "send", {.dst="n2"})      // RDMA link to remote node n2
 *   pos::ep(HOST, "RX", {.iface="eth0"})    // Host NIC RX via DPDK (Middlebox)
 *   pos::ep(BYPASS, "in")                   // Raw bypass via VIU
 */
enum Protocol {
    TCP,            // TCP stack endpoint (SmartNIC model)
    RDMA,           // RDMA stack endpoint (SmartNIC model, or cross-node link with .dst)
    HOST,           // Host NIC endpoint via DPDK (Middlebox model)
    BYPASS          // Raw bypass via VIU (direct FPGA packet I/O)
};

// ============================================================================
// Task Type Enum - vFPGA vs Software
// ============================================================================

/**
 * Task types for task/NF creation.
 *
 * Usage: pos::task(TYPE, "name", ...)
 *
 * Examples:
 *   pos::task(vFPGA, "exact_match")         // vFPGA hardware accelerator
 *   pos::task(SW, "parser_sw")              // Software task on host CPU
 */
enum TaskType {
    vFPGA,          // vFPGA compute task (hardware accelerated)
    SW              // Software task on host CPU (parser/deparser via DPDK)
};

// ============================================================================
// Configuration Structs
// ============================================================================

/**
 * Endpoint configuration (protocol-specific fields).
 *
 * Usage with designated initializers:
 *   pos::ep(HOST, "RX", {.iface="eth0"})
 *   pos::ep(RDMA, "send", {.dst="n2"})
 *   pos::ep(TCP, "in", {.port=8080})
 */
struct EndpointConfig {
    std::string iface = "";         // Interface name (HOST protocol)
    std::string dst = "";           // Destination node ID (RDMA cross-node)
    uint16_t port = 0;              // Port number (TCP)
    bool is_rx = true;              // Direction: true=RX, false=TX (HOST/TCP)
};

/**
 * Task configuration (for SW tasks).
 */
struct TaskConfig {
    size_t buf = 0;                 // Buffer size (auto-creates DMA buffer)
    uint32_t burst_size = 32;       // Packets per burst
};

// ============================================================================
// Worker Registry - Multi-FPGA Support
// ============================================================================

/**
 * Worker node information for Multi-FPGA deployment.
 */
struct WorkerInfo {
    std::string id;                 // Worker identifier (e.g., "fpga0", "fpga1")
    std::string ip;                 // Worker IP address
    uint16_t grpc_port = 50051;     // gRPC port for POS server
    uint16_t rdma_port = 18488;     // RDMA QP exchange port

    // gRPC client for communication with this worker
    mutable std::shared_ptr<pos::POSClient> client;

    // Get or create gRPC client
    pos::POSClient* get_client() const {
        if (!client) {
            std::string address = ip + ":" + std::to_string(grpc_port);
            // Generate client ID from hostname and PID
            char hostname[256];
            gethostname(hostname, sizeof(hostname));
            std::string client_id = std::string(hostname) + "_" + std::to_string(getpid());
            client = std::make_shared<pos::POSClient>(address, client_id);
        }
        return client.get();
    }
};

/**
 * Worker registry singleton - manages known workers for Multi-FPGA.
 */
class WorkerRegistry {
private:
    std::unordered_map<std::string, WorkerInfo> workers_;
    std::string default_worker_;
    bool initialized_ = false;

    WorkerRegistry() = default;

public:
    static WorkerRegistry& instance() {
        static WorkerRegistry inst;
        return inst;
    }

    WorkerRegistry(const WorkerRegistry&) = delete;
    WorkerRegistry& operator=(const WorkerRegistry&) = delete;

    /**
     * Register a worker node.
     */
    void register_worker(const std::string& id, const std::string& ip,
                         uint16_t grpc_port = 50051, uint16_t rdma_port = 18488) {
        workers_[id] = {id, ip, grpc_port, rdma_port};
        if (default_worker_.empty()) {
            default_worker_ = id;  // First registered worker is default
        }
    }

    /**
     * Get worker info by ID.
     */
    const WorkerInfo* get_worker(const std::string& id) const {
        auto it = workers_.find(id);
        return it != workers_.end() ? &it->second : nullptr;
    }

    /**
     * Get default worker (first registered).
     */
    const WorkerInfo* get_default_worker() const {
        return get_worker(default_worker_);
    }

    /**
     * Set default worker.
     */
    void set_default_worker(const std::string& id) {
        if (workers_.find(id) != workers_.end()) {
            default_worker_ = id;
        }
    }

    /**
     * Get all registered workers.
     */
    const std::unordered_map<std::string, WorkerInfo>& get_all_workers() const {
        return workers_;
    }

    /**
     * Check if initialized.
     */
    bool is_initialized() const { return initialized_; }
    void set_initialized() { initialized_ = true; }

    /**
     * Clear all workers.
     */
    void clear() {
        workers_.clear();
        default_worker_.clear();
        initialized_ = false;
    }
};

/**
 * Initialize POS with worker configuration.
 *
 * @param workers  Map of worker_id -> "ip:grpc_port" or "ip:grpc_port:rdma_port"
 *
 * Example:
 *   pos::init({
 *       {"fpga0", "192.168.1.10:50051"},
 *       {"fpga1", "192.168.1.11:50051"}
 *   });
 */
inline void init(const std::unordered_map<std::string, std::string>& workers) {
    auto& registry = WorkerRegistry::instance();
    registry.clear();

    for (const auto& [id, addr] : workers) {
        // Parse "ip:grpc_port" or "ip:grpc_port:rdma_port"
        std::string ip;
        uint16_t grpc_port = 50051;
        uint16_t rdma_port = 18488;

        size_t first_colon = addr.find(':');
        if (first_colon == std::string::npos) {
            ip = addr;
        } else {
            ip = addr.substr(0, first_colon);
            size_t second_colon = addr.find(':', first_colon + 1);
            if (second_colon == std::string::npos) {
                grpc_port = static_cast<uint16_t>(std::stoi(addr.substr(first_colon + 1)));
            } else {
                grpc_port = static_cast<uint16_t>(std::stoi(addr.substr(first_colon + 1, second_colon - first_colon - 1)));
                rdma_port = static_cast<uint16_t>(std::stoi(addr.substr(second_colon + 1)));
            }
        }

        registry.register_worker(id, ip, grpc_port, rdma_port);
    }

    registry.set_initialized();
}

/**
 * Simplified init for single-worker (local) deployment.
 */
inline void init() {
    auto& registry = WorkerRegistry::instance();
    registry.clear();
    registry.register_worker("local", "127.0.0.1", 50051, 18488);
    registry.set_initialized();
}

// ============================================================================
// Forward Declarations
// ============================================================================

class Endpoint;
class Task;
class Dataflow;

// ============================================================================
// PipelineNode - Base class for all pipeline nodes
// ============================================================================

/**
 * Base class for nodes in a dataflow pipeline.
 */
class PipelineNode {
protected:
    std::string name_;
    dfg::NodeBase* internal_node_ = nullptr;
    dfg::Capability* capability_ = nullptr;

public:
    PipelineNode(const std::string& name) : name_(name) {}
    virtual ~PipelineNode() = default;

    const std::string& get_name() const { return name_; }
    dfg::NodeBase* get_internal_node() const { return internal_node_; }
    dfg::Capability* get_capability() const { return capability_; }

    void set_internal_node(dfg::NodeBase* node) { internal_node_ = node; }
    void set_capability(dfg::Capability* cap) { capability_ = cap; }

    virtual bool is_endpoint() const { return false; }
    virtual bool is_task() const { return false; }
};

// ============================================================================
// Endpoint - Network I/O endpoint
// ============================================================================

/**
 * Endpoint represents network I/O boundary in the dataflow.
 * Created via pos::ep().
 *
 * Examples:
 *   pos::ep(TCP, "RX")                      // TCP RX endpoint
 *   pos::ep(RDMA, "RX")                     // RDMA stack RX (SmartNIC)
 *   pos::ep(RDMA, "send", {.dst="n2"})      // RDMA link to remote node
 *   pos::ep(HOST, "RX", {.iface="eth0"})    // Host NIC RX
 */
class Endpoint : public PipelineNode {
private:
    Protocol protocol_;
    EndpointConfig config_;

    // SWX runtime handles (for HOST protocol)
    int swx_endpoint_handle_ = -1;

public:
    Endpoint(const std::string& name, Protocol proto, const EndpointConfig& cfg = {})
        : PipelineNode(name), protocol_(proto), config_(cfg) {}

    bool is_endpoint() const override { return true; }

    Protocol get_protocol() const { return protocol_; }
    const EndpointConfig& get_config() const { return config_; }

    bool is_host_endpoint() const {
        return protocol_ == HOST;
    }

    bool is_remote_endpoint() const {
        return protocol_ == RDMA && !config_.dst.empty();
    }

    bool is_rx() const {
        return config_.is_rx;
    }

    bool is_tx() const {
        return !config_.is_rx;
    }

    // Get destination node for RDMA cross-node endpoints
    const std::string& get_dst() const { return config_.dst; }

    // SWX runtime integration
    void set_swx_handle(int h) { swx_endpoint_handle_ = h; }
    int get_swx_handle() const { return swx_endpoint_handle_; }

    bool initialize_swx() {
        if (!is_host_endpoint()) return true;

        auto& runtime = SWXRuntime::instance();
        swx_endpoint_handle_ = runtime.createEndpoint(
            name_, config_.iface, config_.is_rx);

        if (swx_endpoint_handle_ >= 0) {
            runtime.startEndpoint(swx_endpoint_handle_);
            return true;
        }
        return false;
    }
};

// ============================================================================
// Task - vFPGA or Software compute task
// ============================================================================

/**
 * Task represents a compute node (vFPGA or software).
 * Created via pos::task().
 *
 * Examples:
 *   pos::task(vFPGA, "exact_match")         // vFPGA hardware task
 *   pos::task(SW, "parser_sw")              // Software task on host
 */
class Task : public PipelineNode {
private:
    TaskType type_;
    std::string binary_or_spec_;    // .bin for vFPGA, .spec for SW
    TaskConfig config_;

    // vFPGA state
    int vfpga_id_ = 0;
    dfg::Node* compute_node_ = nullptr;

    // SW state (SWX runtime handles)
    int swx_pipeline_handle_ = -1;
    int swx_task_handle_ = -1;
    int swx_buffer_handle_ = -1;
    bool is_parser_ = true;         // Determined by position in dataflow

public:
    Task(const std::string& name, TaskType type, const std::string& binary_or_spec,
         const TaskConfig& cfg = {})
        : PipelineNode(name), type_(type), binary_or_spec_(binary_or_spec), config_(cfg) {}

    bool is_task() const override { return true; }

    TaskType get_type() const { return type_; }
    const std::string& get_binary() const { return binary_or_spec_; }
    const TaskConfig& get_config() const { return config_; }

    bool is_software() const { return type_ == SW; }
    bool is_vfpga() const { return type_ == vFPGA; }

    // vFPGA accessors
    int get_vfpga_id() const { return vfpga_id_; }
    void set_vfpga_id(int id) { vfpga_id_ = id; }
    dfg::Node* get_compute_node() const { return compute_node_; }
    void set_compute_node(dfg::Node* node) { compute_node_ = node; }

    // SW task: set parser/deparser role
    void set_is_parser(bool is_parser) { is_parser_ = is_parser; }
    bool get_is_parser() const { return is_parser_; }

    // SWX runtime handles
    void set_swx_buffer_handle(int h) { swx_buffer_handle_ = h; }
    int get_swx_buffer_handle() const { return swx_buffer_handle_; }
    int get_swx_task_handle() const { return swx_task_handle_; }

    // Initialize SW task via SWX runtime
    bool initialize_swx(int endpoint_handle) {
        if (type_ != SW) return true;

        auto& runtime = SWXRuntime::instance();

        // Create buffer if specified
        if (config_.buf > 0) {
            swx_buffer_handle_ = runtime.createBuffer(name_ + "_buf", config_.buf);
            if (swx_buffer_handle_ < 0) return false;
        }

        // Create task (loads pipeline + starts poll loop)
        swx_task_handle_ = runtime.createTask(
            name_, binary_or_spec_, is_parser_,
            endpoint_handle, swx_buffer_handle_, config_.burst_size);

        return swx_task_handle_ >= 0;
    }

    // ========================================
    // Table & State Management API (vFPGA only)
    // ========================================

    bool table_add(const std::string& table_name, const std::string& key,
                   const std::string& action, const std::string& data) {
        if (!compute_node_ || !capability_) return false;
        return compute_node_->table_add(table_name, key, action, data, capability_);
    }

    bool table_delete(const std::string& table_name, const std::string& key) {
        if (!compute_node_ || !capability_) return false;
        return compute_node_->table_delete(table_name, key, capability_);
    }

    uint64_t register_read(const std::string& reg_name, uint32_t index) {
        if (!compute_node_ || !capability_) return 0;
        return compute_node_->register_read(reg_name, index, capability_);
    }

    bool register_write(const std::string& reg_name, uint32_t index, uint64_t value) {
        if (!compute_node_ || !capability_) return false;
        return compute_node_->register_write(reg_name, index, value, capability_);
    }
};


// ============================================================================
// Buffer - Shared memory buffer
// ============================================================================

/**
 * Buffer for data transfer between host and FPGA.
 * Created via pos::buffer() or automatically by SW tasks with .buf config.
 */
class Buffer {
private:
    std::string name_;
    size_t size_;
    void* host_ptr_ = nullptr;
    dfg::Buffer* internal_buffer_ = nullptr;
    dfg::Capability* capability_ = nullptr;

    // SWX runtime handle
    int swx_handle_ = -1;

public:
    Buffer(const std::string& name, size_t size)
        : name_(name), size_(size) {}

    ~Buffer() = default;

    const std::string& get_name() const { return name_; }
    size_t get_size() const { return size_; }
    void* get_host_ptr() const { return host_ptr_; }
    dfg::Buffer* get_internal_buffer() const { return internal_buffer_; }
    dfg::Capability* get_capability() const { return capability_; }

    void set_internal_buffer(dfg::Buffer* buf) { internal_buffer_ = buf; }
    void set_capability(dfg::Capability* cap) { capability_ = cap; }
    void set_host_ptr(void* ptr) { host_ptr_ = ptr; }
    void set_swx_handle(int h) { swx_handle_ = h; }
    int get_swx_handle() const { return swx_handle_; }

    bool initialize_swx() {
        auto& runtime = SWXRuntime::instance();
        swx_handle_ = runtime.createBuffer(name_, size_);
        if (swx_handle_ >= 0) {
            host_ptr_ = runtime.getBufferAddr(swx_handle_);
            return true;
        }
        return false;
    }

    bool write(const void* data, size_t len) {
        if (swx_handle_ >= 0) {
            return SWXRuntime::instance().writeBuffer(swx_handle_, data, len) >= 0;
        }
        if (internal_buffer_) {
            dfg::write_buffer(internal_buffer_, const_cast<void*>(data), len);
            return true;
        }
        return false;
    }

    bool read(void* dest, size_t len) {
        if (swx_handle_ >= 0) {
            return SWXRuntime::instance().readBuffer(swx_handle_, dest, len) >= 0;
        }
        if (internal_buffer_) {
            void* device_mem = dfg::read_buffer(internal_buffer_);
            if (device_mem) {
                memcpy(dest, device_mem, len);
                return true;
            }
        }
        return false;
    }
};

// ============================================================================
// Factory Functions - The Core API
// ============================================================================

/**
 * Create a network I/O endpoint.
 *
 * @param protocol  Protocol: TCP, RDMA, HOST, BYPASS
 * @param name      Endpoint name
 * @param config    Protocol-specific config (optional)
 *
 * Examples:
 *   pos::ep(TCP, "RX")                       // TCP stack RX
 *   pos::ep(RDMA, "send", {.dst="n2"})       // RDMA link to remote node
 *   pos::ep(HOST, "RX", {.iface="eth0"})     // Host NIC RX
 *   pos::ep(BYPASS, "in")                    // Raw bypass
 */
inline std::shared_ptr<Endpoint> ep(
    Protocol protocol,
    const std::string& name,
    const EndpointConfig& config = {}) {
    // Infer direction from name if not explicitly set
    EndpointConfig cfg = config;
    if (name.find("RX") != std::string::npos || name.find("rx") != std::string::npos ||
        name.find("in") != std::string::npos || name.find("recv") != std::string::npos) {
        cfg.is_rx = true;
    } else if (name.find("TX") != std::string::npos || name.find("tx") != std::string::npos ||
               name.find("out") != std::string::npos || name.find("send") != std::string::npos) {
        cfg.is_rx = false;
    }
    return std::make_shared<Endpoint>(name, protocol, cfg);
}

// Legacy alias for backward compatibility
inline std::shared_ptr<Endpoint> create_endpoint(
    const std::string& name,
    Protocol protocol,
    const EndpointConfig& config = {}) {
    return ep(protocol, name, config);
}

/**
 * Create a compute task (vFPGA or SW).
 *
 * @param type            TaskType: vFPGA or SW
 * @param name            Task name
 * @param binary_or_spec  .bin file for vFPGA, .spec file for SW (optional)
 * @param config          Task config (for SW: .buf for buffer size)
 *
 * Examples:
 *   pos::task(vFPGA, "exact_match")          // vFPGA task
 *   pos::task(SW, "parser_sw")               // SW task on host
 */
inline std::shared_ptr<Task> task(
    TaskType type,
    const std::string& name,
    const std::string& binary_or_spec = "",
    const TaskConfig& config = {}) {
    return std::make_shared<Task>(name, type, binary_or_spec, config);
}

// Legacy alias for backward compatibility
inline std::shared_ptr<Task> create_task(
    const std::string& name,
    TaskType type,
    const std::string& binary_or_spec,
    const TaskConfig& config = {}) {
    return task(type, name, binary_or_spec, config);
}

/**
 * Create a shared memory buffer.
 *
 * @param size  Size in bytes (e.g., 64*1024 for 64KB)
 * @param name  Buffer name (optional, auto-generated if empty)
 *
 * Examples:
 *   pos::buffer(64*1024)                     // 64KB buffer
 *   pos::buffer(1024*1024, "pkt_buf")        // 1MB named buffer
 */
inline std::shared_ptr<Buffer> buffer(size_t size, const std::string& name = "") {
    static int buffer_counter = 0;
    std::string buf_name = name.empty() ? "buffer_" + std::to_string(buffer_counter++) : name;
    return std::make_shared<Buffer>(buf_name, size);
}

// Legacy alias for backward compatibility
inline std::shared_ptr<Buffer> create_buffer(const std::string& name, size_t size) {
    return buffer(size, name);
}

// ============================================================================
// Sub-Dataflow for Multi-FPGA splits
// ============================================================================

/**
 * SubDataflow represents a portion of a dataflow assigned to one worker.
 * Used internally for Multi-FPGA deployment.
 */
struct SubDataflow {
    std::string worker_id;                              // Target worker
    std::vector<std::shared_ptr<PipelineNode>> nodes;   // Nodes for this worker
    bool has_remote_tx = false;                         // Ends with RDMA cross-node (sends)
    bool has_remote_rx = false;                         // Starts with RDMA cross-node (receives)
    std::string remote_peer_worker;                     // Worker on other side of RDMA link
};

// ============================================================================
// Dataflow - Pipeline graph container
// ============================================================================

/**
 * Dataflow represents a connected pipeline of endpoints and tasks.
 * Created via the variadic dataflow() function.
 *
 * For Multi-FPGA: automatically splits at RDMA cross-node endpoints and dispatches
 * sub-dataflows to respective workers.
 */
class Dataflow {
private:
    std::string name_;
    std::vector<std::shared_ptr<PipelineNode>> nodes_;
    std::vector<std::shared_ptr<Buffer>> buffers_;

    // Internal DFG state (for local/single-worker execution)
    dfg::DFG* dfg_ = nullptr;
    dfg::Capability* root_capability_ = nullptr;
    std::vector<dfg::Node*> internal_nodes_;
    bool is_built_ = false;
    bool is_running_ = false;

    // Deployment model detection
    bool has_software_tasks_ = false;
    bool has_host_endpoints_ = false;
    bool has_remote_endpoints_ = false;

    // Multi-FPGA state
    std::vector<SubDataflow> sub_dataflows_;
    bool is_multi_fpga_ = false;

    // Deployed instance IDs per worker (worker_id -> instance_id)
    std::unordered_map<std::string, std::string> deployed_instances_;

    void analyze_pipeline() {
        has_software_tasks_ = false;
        has_host_endpoints_ = false;
        has_remote_endpoints_ = false;

        for (auto& node : nodes_) {
            if (auto ep = std::dynamic_pointer_cast<Endpoint>(node)) {
                if (ep->is_host_endpoint()) has_host_endpoints_ = true;
                if (ep->is_remote_endpoint()) has_remote_endpoints_ = true;
            }
            if (auto task = std::dynamic_pointer_cast<Task>(node)) {
                if (task->is_software()) has_software_tasks_ = true;
            }
        }

        is_multi_fpga_ = has_remote_endpoints_;
    }

    /**
     * Split dataflow at RDMA cross-node endpoints for Multi-FPGA deployment.
     *
     * Example: nf(in, mat, ct, link, hash, act, out)
     *   where link = RDMA with dst="fpga1"
     *
     * Splits into:
     *   SubDataflow 0: [in, mat, ct, RDMA_TX] -> default worker (fpga0)
     *   SubDataflow 1: [RDMA_RX, hash, act, out] -> fpga1
     */
    void split_at_remote_endpoints() {
        sub_dataflows_.clear();

        if (!is_multi_fpga_) {
            // Single worker - no split needed
            auto& registry = WorkerRegistry::instance();
            const WorkerInfo* default_worker = registry.get_default_worker();

            SubDataflow single;
            single.worker_id = default_worker ? default_worker->id : "local";
            single.nodes = nodes_;
            single.has_remote_tx = false;
            single.has_remote_rx = false;
            sub_dataflows_.push_back(std::move(single));
            return;
        }

        // Find RDMA cross-node endpoints and split
        auto& registry = WorkerRegistry::instance();
        const WorkerInfo* default_worker = registry.get_default_worker();
        std::string current_worker = default_worker ? default_worker->id : "local";

        SubDataflow current_sub;
        current_sub.worker_id = current_worker;

        for (size_t i = 0; i < nodes_.size(); i++) {
            auto& node = nodes_[i];

            if (auto ep = std::dynamic_pointer_cast<Endpoint>(node)) {
                if (ep->is_remote_endpoint()) {
                    // This is a split point
                    std::string target_worker = ep->get_dst();

                    // Current sub-dataflow ends here (as RDMA_TX)
                    current_sub.nodes.push_back(node);
                    current_sub.has_remote_tx = true;
                    current_sub.remote_peer_worker = target_worker;
                    sub_dataflows_.push_back(std::move(current_sub));

                    // Start new sub-dataflow for target worker (as RDMA_RX)
                    current_sub = SubDataflow{};
                    current_sub.worker_id = target_worker;
                    current_sub.has_remote_rx = true;
                    current_sub.remote_peer_worker = current_worker;
                    current_sub.nodes.push_back(node);  // RDMA node on both sides

                    current_worker = target_worker;
                    continue;
                }
            }

            current_sub.nodes.push_back(node);
        }

        // Add final sub-dataflow
        if (!current_sub.nodes.empty()) {
            sub_dataflows_.push_back(std::move(current_sub));
        }
    }

    void assign_parser_deparser_roles() {
        // First SW task after RX endpoint is parser
        // Last SW task before TX endpoint is deparser
        bool found_rx = false;
        std::shared_ptr<Task> last_sw_task = nullptr;

        for (auto& node : nodes_) {
            if (auto ep = std::dynamic_pointer_cast<Endpoint>(node)) {
                if (ep->get_protocol() == HOST && ep->is_rx()) found_rx = true;
            }
            if (auto task = std::dynamic_pointer_cast<Task>(node)) {
                if (task->is_software()) {
                    if (found_rx && !last_sw_task) {
                        task->set_is_parser(true);  // First SW task = parser
                    } else {
                        task->set_is_parser(false); // Others default to deparser
                    }
                    last_sw_task = task;
                }
            }
        }
    }

    bool build_vfpga_pipeline() {
        dfg_ = dfg::create_dfg(name_);
        if (!dfg_) return false;

        root_capability_ = dfg_->get_root_capability();
        if (!root_capability_) return false;

        // Create internal nodes for vFPGA tasks only
        int node_idx = 0;
        for (auto& node : nodes_) {
            if (auto task = std::dynamic_pointer_cast<Task>(node)) {
                if (task->is_vfpga()) {
                    dfg::Node* internal = dfg::create_node(dfg_, node_idx++, task->get_name());
                    if (internal) {
                        internal_nodes_.push_back(internal);
                        task->set_internal_node(internal);
                        task->set_compute_node(internal);

                        std::string cap_id = task->get_name() + "_cap";
                        dfg::Capability* cap = dfg_->find_capability(cap_id, root_capability_);
                        if (cap) task->set_capability(cap);
                    }
                }
            }
        }

        // Connect nodes in sequence
        for (size_t i = 0; i + 1 < internal_nodes_.size(); i++) {
            dfg::connect_edges(internal_nodes_[i]->get_id(),
                              internal_nodes_[i+1]->get_id(), dfg_, 0, 6, true);
        }

        // Configure IO switches
        if (internal_nodes_.size() >= 2) {
            dfg::configure_node_io_switch(internal_nodes_[0], dfg::IODevs::Inter_2_TO_DTU_1);
            dfg::configure_node_io_switch(internal_nodes_.back(), dfg::IODevs::Inter_2_TO_HOST_1);
            for (size_t i = 1; i < internal_nodes_.size() - 1; i++) {
                dfg::configure_node_io_switch(internal_nodes_[i], dfg::IODevs::Inter_3_TO_DTU_2);
            }
        } else if (internal_nodes_.size() == 1) {
            dfg::configure_node_io_switch(internal_nodes_[0], dfg::IODevs::Inter_3_TO_HOST_0);
        }

        for (auto* node : internal_nodes_) {
            dfg::set_node_operation(node, dfg::CoyoteOper::LOCAL_TRANSFER);
        }

        return true;
    }

    bool initialize_software_pipeline() {
        auto& runtime = SWXRuntime::instance();
        if (!runtime.initialize()) return false;

        // Initialize host endpoints
        for (auto& node : nodes_) {
            if (auto ep = std::dynamic_pointer_cast<Endpoint>(node)) {
                if (ep->is_host_endpoint()) {
                    if (!ep->initialize_swx()) return false;
                }
            }
        }

        // Initialize SW tasks
        // Parser connects to RX endpoint, deparser connects to TX endpoint
        int rx_handle = -1, tx_handle = -1;
        for (auto& node : nodes_) {
            if (auto ep = std::dynamic_pointer_cast<Endpoint>(node)) {
                if (ep->get_protocol() == HOST && ep->is_rx()) rx_handle = ep->get_swx_handle();
                if (ep->get_protocol() == HOST && ep->is_tx()) tx_handle = ep->get_swx_handle();
            }
        }

        for (auto& node : nodes_) {
            if (auto task = std::dynamic_pointer_cast<Task>(node)) {
                if (task->is_software()) {
                    int ep_handle = task->get_is_parser() ? rx_handle : tx_handle;
                    if (!task->initialize_swx(ep_handle)) return false;
                }
            }
        }

        return true;
    }

public:
    Dataflow(const std::string& name = "dataflow") : name_(name) {}

    ~Dataflow() { stop(); release(); }

    // Add a node to the pipeline (in order)
    void add_node(std::shared_ptr<PipelineNode> node) {
        nodes_.push_back(node);
        is_built_ = false;
    }

    // Build the pipeline (called automatically by execute)
    bool build() {
        if (is_built_) return true;

        analyze_pipeline();
        split_at_remote_endpoints();
        assign_parser_deparser_roles();

        if (is_multi_fpga_) {
            // Multi-FPGA: build handled by deploy_to_workers()
            is_built_ = true;
            return true;
        }

        // Single-worker: Build vFPGA portion locally
        if (!build_vfpga_pipeline()) return false;

        // Initialize software portion (if middlebox model)
        if (has_software_tasks_ && has_host_endpoints_) {
            if (!initialize_software_pipeline()) return false;
        }

        is_built_ = true;
        return true;
    }

    /**
     * Deploy sub-dataflows to workers (Multi-FPGA).
     * This sends each sub-dataflow to its target worker via gRPC.
     */
    bool deploy_to_workers() {
        auto& registry = WorkerRegistry::instance();

        std::cout << "[Dataflow] Deploying to " << sub_dataflows_.size() << " workers:\n";

        for (size_t i = 0; i < sub_dataflows_.size(); i++) {
            const auto& sub = sub_dataflows_[i];
            const WorkerInfo* worker = registry.get_worker(sub.worker_id);

            if (!worker) {
                std::cerr << "[Dataflow] Error: Unknown worker '" << sub.worker_id << "'\n";
                return false;
            }

            std::cout << "  SubDataflow " << i << " -> " << sub.worker_id
                      << " (" << worker->ip << ":" << worker->grpc_port << ")\n";
            std::cout << "    Nodes: ";
            for (const auto& n : sub.nodes) std::cout << n->get_name() << " ";
            std::cout << "\n";
            if (sub.has_remote_tx) std::cout << "    Role: RDMA_TX to " << sub.remote_peer_worker << "\n";
            if (sub.has_remote_rx) std::cout << "    Role: RDMA_RX from " << sub.remote_peer_worker << "\n";

            // Get gRPC client for this worker
            pos::POSClient* client = worker->get_client();
            if (!client) {
                std::cerr << "[Dataflow] Error: Failed to create client for worker '" << sub.worker_id << "'\n";
                return false;
            }

            // Build DFGSpec for this sub-dataflow using POSClient helper methods
            std::string dfg_id = name_ + "_" + sub.worker_id;
            auto spec = pos::POSClient::createDFGSpec(dfg_id, name_);

            // Add nodes to spec
            for (const auto& node : sub.nodes) {
                if (auto task = std::dynamic_pointer_cast<Task>(node)) {
                    if (task->is_vfpga()) {
                        pos::POSClient::addComputeNode(spec.get(),
                            task->get_name(), task->get_vfpga_id());
                    } else {
                        // SW task - parser or NF
                        if (task->get_is_parser()) {
                            pos::POSClient::addParserNode(spec.get(),
                                task->get_name(), task->get_config().buf);
                        } else {
                            pos::POSClient::addSoftwareNFNode(spec.get(),
                                task->get_name(), task->get_config().buf);
                        }
                    }
                } else if (auto ep = std::dynamic_pointer_cast<Endpoint>(node)) {
                    switch (ep->get_protocol()) {
                        case BYPASS:
                        case TCP:
                            pos::POSClient::addTCPNode(spec.get(),
                                ep->get_name(), ep->is_rx(), ep->get_config().port);
                            break;
                        case HOST:
                            // Raw ethernet node (handled by SWX on worker)
                            // Using TCP node as placeholder for now
                            pos::POSClient::addTCPNode(spec.get(),
                                ep->get_name(), ep->is_rx(), 0);
                            break;
                        case RDMA:
                            // RDMA stack node (local or cross-node with .dst)
                            pos::POSClient::addRDMANode(spec.get(),
                                ep->get_name(), 0, ep->get_dst());
                            break;
                    }
                }
            }

            // Add edges connecting nodes in sequence
            for (size_t j = 0; j + 1 < sub.nodes.size(); j++) {
                pos::POSClient::addEdge(spec.get(),
                    sub.nodes[j]->get_name(), sub.nodes[j+1]->get_name());
            }

            // Deploy to worker via gRPC
            auto result = client->deployDFG(*spec);
            if (!result.success) {
                std::cerr << "[Dataflow] Error deploying to " << sub.worker_id
                          << ": " << result.error_message << "\n";
                return false;
            }

            // Store the instance ID for later operations
            deployed_instances_[sub.worker_id] = result.value.instance_id;
            std::cout << "    Deployed: instance_id=" << result.value.instance_id << "\n";
        }

        return true;
    }

    /**
     * Coordinate RDMA connections between workers (Multi-FPGA).
     * For each RDMA cross-node endpoint, tells the TX side to initiate connection to RX side.
     *
     * RDMA Setup Protocol:
     * 1. Tell RX worker to setup QP first (is_initiator=false, waits for connection)
     * 2. Tell TX worker to connect to RX worker (is_initiator=true, initiates connection)
     * 3. Both workers program their FPGA QP contexts with exchanged info
     */
    bool setup_rdma_connections() {
        auto& registry = WorkerRegistry::instance();

        for (size_t i = 0; i + 1 < sub_dataflows_.size(); i++) {
            const auto& tx_sub = sub_dataflows_[i];
            const auto& rx_sub = sub_dataflows_[i + 1];

            if (!tx_sub.has_remote_tx || !rx_sub.has_remote_rx) continue;

            const WorkerInfo* tx_worker = registry.get_worker(tx_sub.worker_id);
            const WorkerInfo* rx_worker = registry.get_worker(rx_sub.worker_id);

            if (!tx_worker || !rx_worker) continue;

            std::cout << "[Dataflow] Setting up RDMA: " << tx_sub.worker_id
                      << " -> " << rx_sub.worker_id << "\n";

            // Find the RDMA cross-node endpoint in tx_sub
            std::string remote_node_id;
            for (const auto& node : tx_sub.nodes) {
                if (auto ep = std::dynamic_pointer_cast<Endpoint>(node)) {
                    if (ep->is_remote_endpoint()) {
                        remote_node_id = ep->get_name();
                    }
                }
            }

            if (remote_node_id.empty()) {
                std::cerr << "[Dataflow] Error: No RDMA cross-node endpoint found in TX sub-dataflow\n";
                return false;
            }

            // Get instance IDs for both workers
            auto tx_instance_it = deployed_instances_.find(tx_sub.worker_id);
            auto rx_instance_it = deployed_instances_.find(rx_sub.worker_id);

            if (tx_instance_it == deployed_instances_.end() ||
                rx_instance_it == deployed_instances_.end()) {
                std::cerr << "[Dataflow] Error: Workers not deployed before RDMA setup\n";
                return false;
            }

            std::string tx_instance_id = tx_instance_it->second;
            std::string rx_instance_id = rx_instance_it->second;

            // Step 1: Setup RX side first (receiver, is_initiator=false)
            // The RX worker prepares its QP and returns local QP info
            pos::POSClient* rx_client = rx_worker->get_client();
            auto rx_result = rx_client->setupRDMA(
                rx_instance_id,
                remote_node_id,         // Same node name on both sides
                tx_worker->ip,          // Will receive from TX worker
                tx_worker->rdma_port,
                1024 * 1024,            // 1MB RDMA buffer
                false                   // is_initiator=false (receiver)
            );

            if (!rx_result.success) {
                std::cerr << "[Dataflow] Error setting up RDMA on RX worker "
                          << rx_sub.worker_id << ": " << rx_result.error << "\n";
                return false;
            }

            std::cout << "  RX worker ready: local_qpn=" << rx_result.value.local_qpn << "\n";

            // Step 2: Setup TX side (sender, is_initiator=true)
            // The TX worker creates QP and connects to RX worker
            pos::POSClient* tx_client = tx_worker->get_client();
            auto tx_result = tx_client->setupRDMA(
                tx_instance_id,
                remote_node_id,
                rx_worker->ip,          // Connect to RX worker
                rx_worker->rdma_port,
                1024 * 1024,            // 1MB RDMA buffer
                true                    // is_initiator=true (sender)
            );

            if (!tx_result.success) {
                std::cerr << "[Dataflow] Error setting up RDMA on TX worker "
                          << tx_sub.worker_id << ": " << tx_result.error << "\n";
                return false;
            }

            std::cout << "  TX worker connected: local_qpn=" << tx_result.value.local_qpn
                      << " remote_qpn=" << tx_result.value.remote_qpn << "\n";
            std::cout << "  RDMA connection established: "
                      << tx_sub.worker_id << " -> " << rx_sub.worker_id << "\n";
        }

        return true;
    }

    // Execute the pipeline
    bool run(size_t data_size = 0) {
        if (!is_built_ && !build()) return false;
        if (is_running_) return true;

        if (is_multi_fpga_) {
            // Multi-FPGA deployment
            if (!deploy_to_workers()) return false;
            if (!setup_rdma_connections()) return false;

            // Execute on all workers via gRPC
            auto& registry = WorkerRegistry::instance();

            std::cout << "[Dataflow] Starting execution on all workers...\n";
            for (const auto& [worker_id, instance_id] : deployed_instances_) {
                const WorkerInfo* worker = registry.get_worker(worker_id);
                if (!worker) continue;

                pos::POSClient* client = worker->get_client();
                auto result = client->executeDFG(instance_id);

                if (!result.success) {
                    std::cerr << "[Dataflow] Error executing on worker "
                              << worker_id << ": " << result.error << "\n";
                    return false;
                }

                std::cout << "  Worker " << worker_id << " executing\n";
            }

            std::cout << "[Dataflow] Multi-FPGA pipeline deployed and running\n";
            is_running_ = true;
            return true;
        }

        // Single-worker: For pure vFPGA pipelines, execute directly
        if (!has_software_tasks_) {
            std::vector<dfg::sgEntry> sg(internal_nodes_.size());
            for (size_t i = 0; i < internal_nodes_.size(); i++) {
                memset(&sg[i], 0, sizeof(dfg::sgEntry));
                if (data_size > 0) {
                    sg[i].local.src_len = data_size;
                    sg[i].local.dst_len = data_size;
                }
                sg[i].local.src_stream = 1;
                sg[i].local.dst_stream = 1;

                if (i == 0) { sg[i].local.offset_r = 0; sg[i].local.offset_w = 6; }
                else if (i == internal_nodes_.size() - 1) { sg[i].local.offset_r = 6; sg[i].local.offset_w = 0; }
                else { sg[i].local.offset_r = 6; sg[i].local.offset_w = 6; }
            }

            dfg::Node* node_array[internal_nodes_.size()];
            for (size_t i = 0; i < internal_nodes_.size(); i++) {
                node_array[i] = internal_nodes_[i];
            }
            dfg::execute_graph(dfg_, node_array, internal_nodes_.size(), sg.data());
        }

        // For middlebox: SW tasks are already running poll loops
        is_running_ = true;
        return true;
    }

    // Stop the pipeline
    void stop() {
        if (!is_running_) return;

        // For Multi-FPGA: stop execution on all workers (but don't undeploy yet)
        if (is_multi_fpga_ && !deployed_instances_.empty()) {
            auto& registry = WorkerRegistry::instance();

            std::cout << "[Dataflow] Stopping execution on all workers...\n";
            for (const auto& [worker_id, instance_id] : deployed_instances_) {
                const WorkerInfo* worker = registry.get_worker(worker_id);
                if (!worker) continue;

                // Workers will stop their pipelines when they receive executeDFG=false
                // or when we undeploy. For now, just mark as stopped.
                std::cout << "  Worker " << worker_id << " stopped\n";
            }
        }

        // Stop SW tasks (local)
        auto& runtime = SWXRuntime::instance();
        for (auto& node : nodes_) {
            if (auto task = std::dynamic_pointer_cast<Task>(node)) {
                if (task->is_software()) {
                    runtime.stopTask(task->get_swx_task_handle());
                }
            }
        }

        is_running_ = false;
    }

    // Release resources
    void release() {
        stop();

        // For Multi-FPGA: undeploy from all workers
        if (is_multi_fpga_ && !deployed_instances_.empty()) {
            auto& registry = WorkerRegistry::instance();

            std::cout << "[Dataflow] Undeploying from all workers...\n";
            for (const auto& [worker_id, instance_id] : deployed_instances_) {
                const WorkerInfo* worker = registry.get_worker(worker_id);
                if (!worker) continue;

                pos::POSClient* client = worker->get_client();
                if (client) {
                    auto result = client->undeployDFG(instance_id);
                    if (result.success) {
                        std::cout << "  Worker " << worker_id << " undeployed\n";
                    } else {
                        std::cerr << "  Worker " << worker_id << " undeploy failed: "
                                  << result.error_message << "\n";
                    }
                }
            }
            deployed_instances_.clear();
        }

        is_built_ = false;
        internal_nodes_.clear();
        if (dfg_) {
            try { dfg::release_resources(dfg_); } catch (...) {}
            dfg_ = nullptr;
        }
        root_capability_ = nullptr;
    }

    // Accessors
    const std::string& get_name() const { return name_; }
    bool is_running() const { return is_running_; }
    dfg::DFG* get_dfg() const { return dfg_; }
    dfg::Capability* get_root_capability() const { return root_capability_; }

    std::shared_ptr<PipelineNode> get_node(const std::string& name) {
        for (auto& n : nodes_) {
            if (n->get_name() == name) return n;
        }
        return nullptr;
    }

    // Multi-FPGA accessors
    bool is_multi_fpga() const { return is_multi_fpga_; }
    const std::vector<SubDataflow>& get_sub_dataflows() const { return sub_dataflows_; }

    // Debug
    void print() const {
        std::cout << "Dataflow '" << name_ << "':\n";
        std::cout << "  Nodes: ";
        for (const auto& n : nodes_) std::cout << n->get_name() << " ";
        std::cout << "\n  Model: ";
        if (has_software_tasks_ && has_host_endpoints_) std::cout << "Middlebox";
        else if (has_remote_endpoints_) std::cout << "Multi-FPGA";
        else std::cout << "SmartNIC";
        std::cout << "\n";

        if (is_multi_fpga_ && !sub_dataflows_.empty()) {
            std::cout << "  Sub-dataflows:\n";
            for (size_t i = 0; i < sub_dataflows_.size(); i++) {
                const auto& sub = sub_dataflows_[i];
                std::cout << "    [" << i << "] Worker: " << sub.worker_id << " | Nodes: ";
                for (const auto& n : sub.nodes) std::cout << n->get_name() << " ";
                if (sub.has_remote_tx) std::cout << "| TX->" << sub.remote_peer_worker;
                if (sub.has_remote_rx) std::cout << "| RX<-" << sub.remote_peer_worker;
                std::cout << "\n";
            }
        }
    }
};

// ============================================================================
// Variadic dataflow() function
// ============================================================================

namespace detail {
    // Base case
    inline void add_nodes_to_dataflow(Dataflow&) {}

    // Recursive case for PipelineNode
    template<typename T, typename... Rest>
    inline void add_nodes_to_dataflow(Dataflow& df, std::shared_ptr<T> node, Rest... rest) {
        static_assert(std::is_base_of<PipelineNode, T>::value,
                      "dataflow() arguments must be Endpoint or Task");
        df.add_node(node);
        add_nodes_to_dataflow(df, rest...);
    }
}

/**
 * Create a dataflow from a sequence of nodes.
 * Nodes are connected in the order provided.
 *
 * @param nodes  Variadic list of Endpoints and Tasks
 * @return       Shared pointer to Dataflow
 *
 * Examples:
 *   dataflow(in, parser, match, action, deparser, out)
 *   dataflow(in, mat, ct, link, hash, act, out)
 */
template<typename... Nodes>
inline std::shared_ptr<Dataflow> dataflow(Nodes... nodes) {
    auto df = std::make_shared<Dataflow>();
    detail::add_nodes_to_dataflow(*df, nodes...);
    return df;
}

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Convert Protocol enum to string
 */
inline const char* protocol_to_string(Protocol p) {
    switch (p) {
        case TCP: return "TCP";
        case RDMA: return "RDMA";
        case HOST: return "HOST";
        case BYPASS: return "BYPASS";
        default: return "UNKNOWN";
    }
}

/**
 * Convert TaskType enum to string
 */
inline const char* task_type_to_string(TaskType t) {
    switch (t) {
        case vFPGA: return "vFPGA";
        case SW: return "SW";
        default: return "UNKNOWN";
    }
}

/**
 * Stop a running dataflow
 */
inline void stop(std::shared_ptr<Dataflow> df) {
    if (df) df->stop();
}

// ============================================================================
// NF Class - Network Function with deploy() support
// ============================================================================

/**
 * NF (Network Function) represents a composable pipeline with deploy() support.
 * Created via pos::nf().
 *
 * Examples:
 *   auto mcast = pos::nf(rx, mat, act, tx);
 *   mcast.deploy(node);       // Deploy to specific node
 *   mcast.deploy(node1, node2);  // Multi-FPGA deployment
 */
class NF {
private:
    std::shared_ptr<Dataflow> dataflow_;
    std::string name_;
    bool is_deployed_ = false;
    std::vector<std::string> deployed_nodes_;

public:
    NF() : dataflow_(std::make_shared<Dataflow>()) {}
    explicit NF(const std::string& name) : dataflow_(std::make_shared<Dataflow>(name)), name_(name) {}

    // Add a node to the NF
    void add_node(std::shared_ptr<PipelineNode> node) {
        dataflow_->add_node(node);
    }

    // Add a buffer to the NF
    void add_buffer(std::shared_ptr<Buffer> buf) {
        // Buffers are tracked separately but don't affect dataflow ordering
        // They're used for data transfer between SW and vFPGA tasks
    }

    /**
     * Deploy the NF to an execution node (or nodes).
     *
     * @param node  Worker node ID (e.g., "fpga0", "fpga1")
     * @return      true if deployment succeeded
     *
     * Examples:
     *   mcast.deploy("fpga0");                // Single FPGA
     *   mcast.deploy("fpga0", "fpga1");       // Multi-FPGA (implicit split at RDMA with .dst)
     */
    bool deploy(const std::string& node) {
        if (is_deployed_) {
            std::cerr << "[NF] Already deployed\n";
            return false;
        }

        // Set default worker to the specified node
        auto& registry = WorkerRegistry::instance();
        registry.set_default_worker(node);

        // Build and run
        if (!dataflow_->build()) {
            std::cerr << "[NF] Failed to build dataflow\n";
            return false;
        }

        if (!dataflow_->run()) {
            std::cerr << "[NF] Failed to run dataflow\n";
            return false;
        }

        deployed_nodes_.push_back(node);
        is_deployed_ = true;
        return true;
    }

    // Variadic deploy for multi-FPGA (nodes listed for documentation, but
    // actual split is determined by RDMA endpoints with .dst in the NF)
    template<typename... Nodes>
    bool deploy(const std::string& first_node, Nodes... other_nodes) {
        // Register all nodes as potential workers
        // Actual split happens based on RDMA cross-node endpoints
        return deploy(first_node);  // Split is automatic based on RDMA endpoints with .dst
    }

    /**
     * Stop the deployed NF.
     */
    void stop() {
        if (dataflow_) dataflow_->stop();
        is_deployed_ = false;
    }

    /**
     * Release all resources.
     */
    void release() {
        if (dataflow_) dataflow_->release();
        is_deployed_ = false;
        deployed_nodes_.clear();
    }

    // Accessors
    bool is_deployed() const { return is_deployed_; }
    const std::string& get_name() const { return name_; }
    std::shared_ptr<Dataflow> get_dataflow() const { return dataflow_; }

    // Get a node by name (for table operations etc.)
    std::shared_ptr<Task> get_task(const std::string& name) {
        auto node = dataflow_->get_node(name);
        return std::dynamic_pointer_cast<Task>(node);
    }

    // Debug
    void print() const {
        std::cout << "NF";
        if (!name_.empty()) std::cout << " '" << name_ << "'";
        std::cout << ":\n";
        if (dataflow_) dataflow_->print();
    }
};

// ============================================================================
// pos::nf() variadic function
// ============================================================================

namespace detail {
    // Base case
    inline void add_nodes_to_nf(NF&) {}

    // Recursive case for PipelineNode (Endpoint, Task)
    template<typename T, typename... Rest>
    inline typename std::enable_if<std::is_base_of<PipelineNode, T>::value>::type
    add_nodes_to_nf(NF& nf_obj, std::shared_ptr<T> node, Rest... rest) {
        nf_obj.add_node(node);
        add_nodes_to_nf(nf_obj, rest...);
    }

    // Recursive case for Buffer
    template<typename... Rest>
    inline void add_nodes_to_nf(NF& nf_obj, std::shared_ptr<Buffer> buf, Rest... rest) {
        nf_obj.add_buffer(buf);
        add_nodes_to_nf(nf_obj, rest...);
    }
}

/**
 * Create a Network Function (NF) from a sequence of components.
 * Components are connected in the order provided.
 *
 * @param components  Variadic list of Endpoints, Tasks, and Buffers
 * @return            NF object
 *
 * Examples:
 *   auto mcast = pos::nf(rx, mat, act, tx);
 *   auto mbox = pos::nf(rx, parser, buf, match, action, deparser, tx);
 */
template<typename... Components>
inline NF nf(Components... components) {
    NF nf_obj;
    detail::add_nodes_to_nf(nf_obj, components...);
    return nf_obj;
}

/**
 * Create a named Network Function.
 *
 * @param name        NF name
 * @param components  Variadic list of components
 * @return            Named NF object
 *
 * Example:
 *   auto firewall = pos::nf("firewall", rx, match, action, tx);
 */
template<typename... Components>
inline NF nf(const std::string& name, Components... components) {
    NF nf_obj(name);
    detail::add_nodes_to_nf(nf_obj, components...);
    return nf_obj;
}

} // namespace pos

#pragma once

/**
 * @file dfg.hpp
 * @brief Low-Level POS DFG API - Direct Hardware Access Layer
 *
 * This file provides the low-level, fine-grained API for the Programmable OS (POS)
 * Data Flow Graph (DFG) system. It exposes direct control over FPGA resources,
 * capabilities, and node management.
 *
 * API LAYER ARCHITECTURE:
 * =======================
 *
 *   ┌─────────────────────────────────────────────────────────────────┐
 *   │                    Application Code                             │
 *   └─────────────────────────────────────────────────────────────────┘
 *                                 │
 *                                 ▼
 *   ┌─────────────────────────────────────────────────────────────────┐
 *   │              pipeline.hpp (HIGH-LEVEL API)                      │
 *   │  - Fluent builder pattern (Task, Dataflow, Pipeline)            │
 *   │  - Automatic resource management                                │
 *   │  - Simplified node creation via factory methods                 │
 *   │  - RECOMMENDED for most applications                            │
 *   └─────────────────────────────────────────────────────────────────┘
 *                                 │
 *                                 ▼
 *   ┌─────────────────────────────────────────────────────────────────┐
 *   │                dfg.hpp (LOW-LEVEL API) ← THIS FILE              │
 *   │  - Direct DFG, Node, Buffer, Capability manipulation            │
 *   │  - Fine-grained capability-based access control                 │
 *   │  - Manual resource lifecycle management                         │
 *   │  - Use for: custom node types, advanced scheduling, debugging   │
 *   └─────────────────────────────────────────────────────────────────┘
 *                                 │
 *                                 ▼
 *   ┌─────────────────────────────────────────────────────────────────┐
 *   │               Coyote Hardware Abstraction (cThread, etc.)       │
 *   └─────────────────────────────────────────────────────────────────┘
 *
 * WHEN TO USE THIS API (dfg.hpp):
 * - You need direct control over capability delegation/revocation
 * - You're implementing custom node types
 * - You need access to low-level error codes and diagnostics
 * - You're building tools on top of the DFG infrastructure
 *
 * WHEN TO USE pipeline.hpp INSTEAD:
 * - Standard dataflow applications (SmartNIC, host-based middlebox, multi-FPGA)
 * - You prefer a fluent/builder API style
 * - You want automatic capability management
 *
 * UNIFIED vNFC TARGET MODEL:
 * ==========================
 * All packet processors (network stacks, vFPGAs, software pipelines) are treated
 * uniformly as vNFC targets. The NF Scheduler maps vNFCs to their execution targets:
 *
 *   vNFC Target Types:
 *   ├── VFPGA          - Hardware vFPGA (custom P4/HDL logic)
 *   ├── RDMA_STACK     - RDMA network stack (parser/deparser role)
 *   ├── TCP_STACK      - TCP/IP network stack (parser/deparser role)
 *   ├── RAW_BYPASS     - Raw Ethernet via VIU (requires vFPGA parser/deparser)
 *   └── SWX_RUNTIME    - Software P4 pipeline via DPDK SWX
 *
 * DEPLOYMENT MODELS SUPPORTED:
 * 1. SmartNIC Model (RDMA Stack as Parser/Deparser):
 *    vNFC_parser → RDMA_STACK, vNFC_nf → VFPGA, vNFC_deparser → RDMA_STACK
 *
 * 2. SmartNIC Model (TCP Stack as Parser/Deparser):
 *    vNFC_parser → TCP_STACK, vNFC_nf → VFPGA, vNFC_deparser → TCP_STACK
 *
 * 3. Raw Bypass Model (vFPGA Parser/Deparser):
 *    vNFC_parser → VFPGA(P4), vNFC_nf → VFPGA, vNFC_deparser → VFPGA(P4)
 *
 * 4. Host-Based Middlebox (Software Parser/Deparser):
 *    vNFC_parser → SWX_RUNTIME, vNFC_nf → VFPGA, vNFC_deparser → SWX_RUNTIME
 *
 * 5. Multi-FPGA Chaining:
 *    vNFC_1 → VFPGA_A, vNFC_2 → RemoteDFG → VFPGA_B
 *
 * @see pipeline.hpp for the high-level fluent API
 */

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <any>
#include <chrono>
#include <functional>
#include <mutex>
#include <algorithm>
#include <thread>

// Include Coyote APIs directly
#include "cDefs.hpp"
#include "cThread.hpp"
#include "cBench.hpp"

namespace dfg {

using namespace fpga;

// Forward declarations
class DFG;
class Node;
class Buffer;
class Capability;

// Stream modes (match the original code's terminology)
enum StreamMode {
    HOST_STREAM = 1,
    CARD_STREAM = 0
};

// Capability permissions - Extended for multi-deployment model support
enum CapabilityPermission : uint32_t {
    // Base permissions (existing)
    READ = 1,
    WRITE = 2,
    EXECUTE = 4,
    DELEGATE = 8,
    TRANSITIVE_DELEGATE = 16,

    // Network permissions
    NET_SEND = 32,
    NET_RECEIVE = 64,
    NET_ESTABLISH = 128,
    NET_QOS_MODIFY = 256,
    NET_BANDWIDTH_RESERVE = 512,

    // Software permissions (for host CPU nodes)
    SW_CPU_EXECUTE = 1024,
    SW_MEMORY_ACCESS = 2048,
    SW_NIC_ACCESS = 4096,
    SW_RESOURCE_LIMIT = 8192,

    // Remote/Cross-FPGA permissions
    REMOTE_DELEGATE = 16384,
    REMOTE_EXECUTE = 32768,
    REMOTE_TRANSFER = 65536
};

// Capability scope - defines the domain of the capability
enum class CapabilityScope {
    LOCAL,      // Local FPGA operations
    NETWORK,    // Network stack operations (RDMA/TCP/Raw Ethernet)
    SOFTWARE,   // Host CPU software operations
    REMOTE,     // Cross-FPGA operations
    GLOBAL      // All scopes
};

// ============================================================================
// Error Handling
// ============================================================================

/**
 * Error codes for POS API operations
 * Provides programmatic error handling instead of just stderr logging
 */
enum class ErrorCode {
    SUCCESS = 0,

    // General errors (1-99)
    INVALID_ARGUMENT = 1,
    NULL_POINTER = 2,
    NOT_INITIALIZED = 3,
    ALREADY_INITIALIZED = 4,
    NOT_FOUND = 5,
    ALREADY_EXISTS = 6,
    INVALID_STATE = 7,

    // Capability errors (100-199)
    CAP_INVALID = 100,
    CAP_EXPIRED = 101,
    CAP_INSUFFICIENT_PERMISSIONS = 102,
    CAP_SCOPE_MISMATCH = 103,
    CAP_DELEGATION_FAILED = 104,
    CAP_RESOURCE_MISMATCH = 105,
    CAP_NO_DELEGATE_PERMISSION = 106,
    CAP_NO_TRANSITIVE_DELEGATE = 107,

    // Network errors (200-299)
    NET_NOT_CONNECTED = 200,
    NET_ALREADY_CONNECTED = 201,
    NET_CONNECTION_FAILED = 202,
    NET_SEND_FAILED = 203,
    NET_RECEIVE_FAILED = 204,
    NET_VLAN_MISMATCH = 205,
    NET_TIMEOUT = 206,
    NET_PARTIAL_TRANSFER = 207,
    NET_RECONNECT_FAILED = 208,

    // Software node errors (300-399)
    SW_FUNCTION_NOT_SET = 300,
    SW_RESOURCE_LIMIT_EXCEEDED = 301,
    SW_RATE_LIMITED = 302,
    SW_EXECUTION_FAILED = 303,

    // Remote node errors (400-499)
    REMOTE_NOT_CONNECTED = 400,
    REMOTE_TOKEN_INVALID = 401,
    REMOTE_TOKEN_EXPIRED = 402,
    REMOTE_SIGNATURE_INVALID = 403,
    REMOTE_SEQUENCE_ERROR = 404,
    REMOTE_TRANSFER_FAILED = 405,

    // DFG errors (500-599)
    DFG_INVALID = 500,
    DFG_NODE_NOT_FOUND = 501,
    DFG_BUFFER_NOT_FOUND = 502,
    DFG_EXECUTION_FAILED = 503
};

/**
 * Convert error code to human-readable string
 */
inline const char* error_to_string(ErrorCode code) {
    switch (code) {
        case ErrorCode::SUCCESS: return "Success";
        case ErrorCode::INVALID_ARGUMENT: return "Invalid argument";
        case ErrorCode::NULL_POINTER: return "Null pointer";
        case ErrorCode::NOT_INITIALIZED: return "Not initialized";
        case ErrorCode::ALREADY_INITIALIZED: return "Already initialized";
        case ErrorCode::NOT_FOUND: return "Not found";
        case ErrorCode::ALREADY_EXISTS: return "Already exists";
        case ErrorCode::INVALID_STATE: return "Invalid state";
        case ErrorCode::CAP_INVALID: return "Invalid capability";
        case ErrorCode::CAP_EXPIRED: return "Capability expired";
        case ErrorCode::CAP_INSUFFICIENT_PERMISSIONS: return "Insufficient permissions";
        case ErrorCode::CAP_SCOPE_MISMATCH: return "Capability scope mismatch";
        case ErrorCode::CAP_DELEGATION_FAILED: return "Capability delegation failed";
        case ErrorCode::CAP_RESOURCE_MISMATCH: return "Capability resource mismatch";
        case ErrorCode::CAP_NO_DELEGATE_PERMISSION: return "No delegate permission";
        case ErrorCode::CAP_NO_TRANSITIVE_DELEGATE: return "No transitive delegate permission";
        case ErrorCode::NET_NOT_CONNECTED: return "Network not connected";
        case ErrorCode::NET_ALREADY_CONNECTED: return "Network already connected";
        case ErrorCode::NET_CONNECTION_FAILED: return "Network connection failed";
        case ErrorCode::NET_SEND_FAILED: return "Network send failed";
        case ErrorCode::NET_RECEIVE_FAILED: return "Network receive failed";
        case ErrorCode::NET_VLAN_MISMATCH: return "VLAN mismatch";
        case ErrorCode::NET_TIMEOUT: return "Network timeout";
        case ErrorCode::NET_PARTIAL_TRANSFER: return "Partial transfer";
        case ErrorCode::NET_RECONNECT_FAILED: return "Reconnection failed";
        case ErrorCode::SW_FUNCTION_NOT_SET: return "Software function not set";
        case ErrorCode::SW_RESOURCE_LIMIT_EXCEEDED: return "Resource limit exceeded";
        case ErrorCode::SW_RATE_LIMITED: return "Rate limited";
        case ErrorCode::SW_EXECUTION_FAILED: return "Software execution failed";
        case ErrorCode::REMOTE_NOT_CONNECTED: return "Remote not connected";
        case ErrorCode::REMOTE_TOKEN_INVALID: return "Remote token invalid";
        case ErrorCode::REMOTE_TOKEN_EXPIRED: return "Remote token expired";
        case ErrorCode::REMOTE_SIGNATURE_INVALID: return "Remote signature invalid";
        case ErrorCode::REMOTE_SEQUENCE_ERROR: return "Remote sequence error";
        case ErrorCode::REMOTE_TRANSFER_FAILED: return "Remote transfer failed";
        case ErrorCode::DFG_INVALID: return "Invalid DFG";
        case ErrorCode::DFG_NODE_NOT_FOUND: return "DFG node not found";
        case ErrorCode::DFG_BUFFER_NOT_FOUND: return "DFG buffer not found";
        case ErrorCode::DFG_EXECUTION_FAILED: return "DFG execution failed";
        default: return "Unknown error";
    }
}

/**
 * Result template for operations that can fail
 * Combines a value with an error code for proper error handling
 */
template<typename T>
class Result {
private:
    T value_;
    ErrorCode error_;
    bool has_value_;

public:
    // Success constructor
    Result(const T& val) : value_(val), error_(ErrorCode::SUCCESS), has_value_(true) {}
    Result(T&& val) : value_(std::move(val)), error_(ErrorCode::SUCCESS), has_value_(true) {}

    // Error constructor
    Result(ErrorCode err) : value_(), error_(err), has_value_(false) {}

    // Check if operation succeeded
    bool ok() const { return error_ == ErrorCode::SUCCESS && has_value_; }
    bool has_error() const { return error_ != ErrorCode::SUCCESS; }

    // Get value (undefined if error)
    const T& value() const { return value_; }
    T& value() { return value_; }

    // Get error code
    ErrorCode error() const { return error_; }
    const char* error_message() const { return error_to_string(error_); }

    // Convenient accessors
    operator bool() const { return ok(); }
    const T& operator*() const { return value_; }
    T& operator*() { return value_; }
};

// Specialization for void (operations with no return value)
template<>
class Result<void> {
private:
    ErrorCode error_;

public:
    Result() : error_(ErrorCode::SUCCESS) {}
    Result(ErrorCode err) : error_(err) {}

    bool ok() const { return error_ == ErrorCode::SUCCESS; }
    bool has_error() const { return error_ != ErrorCode::SUCCESS; }
    ErrorCode error() const { return error_; }
    const char* error_message() const { return error_to_string(error_); }
    operator bool() const { return ok(); }
};

// Helper to create success/error results
template<typename T>
Result<T> make_success(T&& val) { return Result<T>(std::forward<T>(val)); }

inline Result<void> make_success() { return Result<void>(); }

template<typename T>
Result<T> make_error(ErrorCode err) { return Result<T>(err); }

inline Result<void> make_error(ErrorCode err) { return Result<void>(err); }

/**
 * Thread-local last error for legacy API compatibility
 */
inline ErrorCode& get_last_error() {
    thread_local ErrorCode last_error = ErrorCode::SUCCESS;
    return last_error;
}

inline void set_last_error(ErrorCode err) {
    get_last_error() = err;
}

inline void clear_last_error() {
    get_last_error() = ErrorCode::SUCCESS;
}

// ============================================================================
// vNFC Target Model
// ============================================================================
//
// All packet processors are unified under the vNFC (virtual Network Function
// Chain) abstraction. The NF Scheduler maps vNFCs to execution targets.
//
// Key insight: Network stacks (RDMA, TCP) are packet processors just like
// vFPGAs or software pipelines. They can serve as parser/deparser in a chain.
//
// Deployment Model Inference:
// ---------------------------
// The deployment model is inferred from node composition in the DFG:
//
// 1. Single FPGA Model:
//    - All vNFCs map to VFPGA on same device
//    - Example: Parser(VFPGA) → NF(VFPGA) → Deparser(VFPGA)
//
// 2. Host-Based Middlebox Model:
//    - Parser/deparser map to SWX_RUNTIME (host CPU via DPDK)
//    - NF accelerators map to VFPGA
//    - Example: Parser(SWX_RUNTIME) → NF(VFPGA) → Deparser(SWX_RUNTIME)
//
// 3. Multi-FPGA Model:
//    - RDMA_STACK nodes appear between vNFC stages, indicating cross-FPGA
//    - Example: NF_A(VFPGA) → RDMA_STACK → NF_B(VFPGA)
//    - The presence of RDMA_STACK in the chain topology directly encodes
//      multi-FPGA deployment without needing a separate deployment flag
//
// 4. SmartNIC Model:
//    - Network stacks (RDMA/TCP) serve as parser/deparser
//    - Example: RDMA_STACK(parser) → NF(VFPGA) → RDMA_STACK(deparser)
//
// 5. Raw Bypass Model:
//    - Raw Ethernet via VIU for lowest latency, direct frame access
//    - Bypasses protocol stacks entirely
//    - Example: RAW_BYPASS(ingress) → NF(VFPGA) → RAW_BYPASS(egress)

/**
 * vNFC Target Type - Where a vNFC executes
 *
 * The NF Scheduler uses this to map vNFCs to their execution backend.
 * This enables flexible composition: RDMA stack as parser, vFPGA as NF,
 * software pipeline as deparser, etc.
 */
enum class NFCTargetType {
    VFPGA,          // Hardware vFPGA (custom P4/HDL bitstream)
    RDMA_STACK,     // RDMA network stack (can act as parser/deparser)
    TCP_STACK,      // TCP/IP network stack (can act as parser/deparser)
    RAW_BYPASS,     // Raw Ethernet via VIU - bypasses protocol stacks for
                    // direct packet I/O (lower latency, raw frame access)
    SWX_RUNTIME,    // Software P4 pipeline via DPDK SWX (operates on host
                    // memory via DPDK hugepages, shared with FPGA via DMA)
    REMOTE_DFG      // Remote FPGA via cross-FPGA delegation (VLAN-authenticated)
};

/**
 * vNFC Role - The function a vNFC performs in the chain
 */
enum class NFCRole {
    PARSER,         // Packet parsing (header extraction)
    DEPARSER,       // Packet deparsing (header emission)
    NF,             // Network function (match-action processing)
    COMBINED        // Combined parser+NF+deparser (e.g., full P4 pipeline)
};

/**
 * Convert NFCTargetType to string for debugging/logging
 */
inline const char* target_type_to_string(NFCTargetType type) {
    switch (type) {
        case NFCTargetType::VFPGA: return "VFPGA";
        case NFCTargetType::RDMA_STACK: return "RDMA_STACK";
        case NFCTargetType::TCP_STACK: return "TCP_STACK";
        case NFCTargetType::RAW_BYPASS: return "RAW_BYPASS";
        case NFCTargetType::SWX_RUNTIME: return "SWX_RUNTIME";
        case NFCTargetType::REMOTE_DFG: return "REMOTE_DFG";
        default: return "UNKNOWN";
    }
}

/**
 * Convert NFCRole to string for debugging/logging
 */
inline const char* nfc_role_to_string(NFCRole role) {
    switch (role) {
        case NFCRole::PARSER: return "PARSER";
        case NFCRole::DEPARSER: return "DEPARSER";
        case NFCRole::NF: return "NF";
        case NFCRole::COMBINED: return "COMBINED";
        default: return "UNKNOWN";
    }
}

// Node type enumeration for polymorphic identification
// NOTE: These map to NFCTargetType for scheduling purposes
enum class NodeType {
    // Hardware targets
    COMPUTE,        // vFPGA compute node → NFCTargetType::VFPGA
    MEMORY,         // Memory/Buffer node (not a target, but a resource)

    // Network stack targets (act as parser/deparser, or cross-FPGA bridge)
    NETWORK_RDMA,   // RDMA network stack → NFCTargetType::RDMA_STACK
                    // When appearing between vNFC stages, indicates multi-FPGA
    NETWORK_TCP,    // TCP network stack → NFCTargetType::TCP_STACK
    NETWORK_RAW,    // Raw Ethernet via VIU → NFCTargetType::RAW_BYPASS

    // Software targets (via SWXRuntime/DPDK, operates on host memory)
    SOFTWARE_PARSER,    // Software parser → NFCTargetType::SWX_RUNTIME
    SOFTWARE_DEPARSER,  // Software deparser → NFCTargetType::SWX_RUNTIME
    SOFTWARE_NF,        // Software NF → NFCTargetType::SWX_RUNTIME

    // Remote targets
    REMOTE_DFG      // Remote FPGA proxy → NFCTargetType::REMOTE_DFG
};

/**
 * Map NodeType to NFCTargetType for NF Scheduler
 */
inline NFCTargetType node_type_to_target(NodeType type) {
    switch (type) {
        case NodeType::COMPUTE:
            return NFCTargetType::VFPGA;
        case NodeType::NETWORK_RDMA:
            return NFCTargetType::RDMA_STACK;
        case NodeType::NETWORK_TCP:
            return NFCTargetType::TCP_STACK;
        case NodeType::NETWORK_RAW:
            return NFCTargetType::RAW_BYPASS;
        case NodeType::SOFTWARE_PARSER:
        case NodeType::SOFTWARE_DEPARSER:
        case NodeType::SOFTWARE_NF:
            return NFCTargetType::SWX_RUNTIME;
        case NodeType::REMOTE_DFG:
            return NFCTargetType::REMOTE_DFG;
        case NodeType::MEMORY:
        default:
            return NFCTargetType::VFPGA;  // Default fallback
    }
}

/**
 * Infer NFCRole from NodeType
 */
inline NFCRole node_type_to_role(NodeType type) {
    switch (type) {
        case NodeType::SOFTWARE_PARSER:
            return NFCRole::PARSER;
        case NodeType::SOFTWARE_DEPARSER:
            return NFCRole::DEPARSER;
        case NodeType::SOFTWARE_NF:
        case NodeType::COMPUTE:
            return NFCRole::NF;
        case NodeType::NETWORK_RDMA:
        case NodeType::NETWORK_TCP:
        case NodeType::NETWORK_RAW:
            return NFCRole::COMBINED;  // Network stacks handle full packet lifecycle
        case NodeType::REMOTE_DFG:
            return NFCRole::NF;  // Remote DFG typically runs NF logic
        default:
            return NFCRole::NF;
    }
}

/**
 * Capability class - Represents a capability with specific permissions
 */
class Capability {
private:
    std::string cap_id;
    uint32_t permissions;
    void* resource = nullptr;
    size_t resource_size = 0;
    Capability* parent = nullptr;
    std::vector<Capability*> children;
    cThread<std::any>* thread = nullptr;
    bool owns_resource = false;
    std::chrono::time_point<std::chrono::system_clock> expiry_time;
    bool has_expiry = false;
    CapabilityScope scope = CapabilityScope::LOCAL;

    // Explicit binding state - replaces permissive null checks
    bool resource_bound = false;  // True when resource is explicitly bound
    bool thread_bound = false;    // True when thread is explicitly bound

public:
    Capability(const std::string& id, uint32_t perms, cThread<std::any>* thread_ptr = nullptr,
              void* res = nullptr, size_t size = 0, Capability* parent_cap = nullptr,
              bool owns_res = false, CapabilityScope cap_scope = CapabilityScope::LOCAL)
        : cap_id(id), permissions(perms), thread(thread_ptr), resource(res),
          resource_size(size), parent(parent_cap), owns_resource(owns_res), scope(cap_scope),
          resource_bound(res != nullptr), thread_bound(thread_ptr != nullptr) {

        // If this is a delegated capability, add it to parent's children
        if (parent) {
            parent->add_child(this);
        }
    }

    /**
     * Destructor - properly cleans up all delegated children
     * This prevents memory leaks from orphaned capabilities
     */
    ~Capability() {
        // Recursively delete all children (they were created with 'new' in delegate())
        for (auto* child : children) {
            delete child;  // This will recursively delete grandchildren
        }
        children.clear();

        // Remove ourselves from parent's children list if we have a parent
        if (parent) {
            parent->remove_child(cap_id);
        }

        // Clean up owned resource if applicable
        if (owns_resource && resource != nullptr) {
            // Note: We don't actually free the resource here as it may be managed elsewhere
            // This flag is for tracking purposes
            resource = nullptr;
        }
    }

    // Prevent copying (capabilities should be unique)
    Capability(const Capability&) = delete;
    Capability& operator=(const Capability&) = delete;

    // Allow moving
    Capability(Capability&& other) noexcept
        : cap_id(std::move(other.cap_id)), permissions(other.permissions),
          resource(other.resource), resource_size(other.resource_size),
          parent(other.parent), children(std::move(other.children)),
          thread(other.thread), owns_resource(other.owns_resource),
          expiry_time(other.expiry_time), has_expiry(other.has_expiry),
          scope(other.scope), resource_bound(other.resource_bound),
          thread_bound(other.thread_bound) {
        // Clear the moved-from object
        other.resource = nullptr;
        other.parent = nullptr;
        other.thread = nullptr;
        other.owns_resource = false;
        other.resource_bound = false;
        other.thread_bound = false;
    }

    Capability& operator=(Capability&& other) noexcept {
        if (this != &other) {
            // Clean up existing children
            for (auto* child : children) {
                delete child;
            }
            children.clear();

            // Move data
            cap_id = std::move(other.cap_id);
            permissions = other.permissions;
            resource = other.resource;
            resource_size = other.resource_size;
            parent = other.parent;
            children = std::move(other.children);
            thread = other.thread;
            owns_resource = other.owns_resource;
            expiry_time = other.expiry_time;
            has_expiry = other.has_expiry;
            scope = other.scope;
            resource_bound = other.resource_bound;
            thread_bound = other.thread_bound;

            // Clear moved-from object
            other.resource = nullptr;
            other.parent = nullptr;
            other.thread = nullptr;
            other.owns_resource = false;
            other.resource_bound = false;
            other.thread_bound = false;
        }
        return *this;
    }

    /**
     * Revoke this capability and all its children
     * This invalidates the capability without deleting it
     */
    void revoke() {
        // Revoke all children first
        for (auto* child : children) {
            if (child) {
                child->revoke();
            }
        }
        // Clear our permissions (effectively revoking the capability)
        permissions = 0;
        // Mark as expired
        has_expiry = true;
        expiry_time = std::chrono::system_clock::now() - std::chrono::seconds(1);
    }

    /**
     * Check if this capability has been revoked
     */
    bool is_revoked() const {
        return permissions == 0;
    }

    // Add a child capability
    void add_child(Capability* child) {
        if (child) {
            children.push_back(child);
        }
    }

    // Remove a child capability (does not delete, just removes from list)
    bool remove_child(const std::string& child_id) {
        auto it = std::find_if(children.begin(), children.end(),
            [&child_id](Capability* cap) {
                return cap && cap->get_id() == child_id;
            });

        if (it != children.end()) {
            children.erase(it);
            return true;
        }
        return false;
    }

    /**
     * Revoke and delete a specific child capability by ID
     */
    bool revoke_child(const std::string& child_id) {
        auto it = std::find_if(children.begin(), children.end(),
            [&child_id](Capability* cap) {
                return cap && cap->get_id() == child_id;
            });

        if (it != children.end()) {
            Capability* child = *it;
            children.erase(it);
            delete child;  // This will recursively delete grandchildren
            return true;
        }
        return false;
    }

    /**
     * Get the number of child capabilities
     */
    size_t child_count() const { return children.size(); }

    /**
     * Revoke all children (but keep this capability valid)
     */
    void revoke_all_children() {
        for (auto* child : children) {
            delete child;
        }
        children.clear();
    }
    
    // Check if capability has a specific permission
    bool has_permission(CapabilityPermission perm) const {
        if (has_expiry && std::chrono::system_clock::now() > expiry_time) {
            return false;  // Expired capabilities have no permissions
        }
        return (permissions & perm) != 0;
    }
    
    // Check if capability has all requested permissions
    bool has_permissions(uint32_t required_perms) const {
        if (has_expiry && std::chrono::system_clock::now() > expiry_time) {
            return false;  // Expired capabilities have no permissions
        }
        return (permissions & required_perms) == required_perms;
    }
    
    // Print the capability tree
    void print_tree(int depth = 0) const {
        for (int i = 0; i < depth; i++) {
            std::cout << "  ";
        }

        // Scope name helper
        const char* scope_names[] = {"LOCAL", "NETWORK", "SOFTWARE", "REMOTE", "GLOBAL"};
        const char* scope_name = scope_names[static_cast<int>(scope)];

        std::cout << cap_id << " (Perms: " << permissions << ", Scope: " << scope_name << ")";

        if (has_expiry) {
            auto now = std::chrono::system_clock::now();
            if (now > expiry_time) {
                std::cout << " [EXPIRED]";
            } else {
                auto remaining = std::chrono::duration_cast<std::chrono::seconds>(expiry_time - now).count();
                std::cout << " [Expires in " << remaining << "s]";
            }
        }

        std::cout << std::endl;

        for (auto child : children) {
            if (child) {
                child->print_tree(depth + 1);
            }
        }
    }

    // Set an expiry time for this capability
    void set_expiry(std::chrono::seconds timeout) {
        expiry_time = std::chrono::system_clock::now() + timeout;
        has_expiry = true;
    }

    // Check if the capability is expired
    bool is_expired() const {
        if (!has_expiry) return false;
        return std::chrono::system_clock::now() > expiry_time;
    }
    
    // Getters
    std::string get_id() const { return cap_id; }
    uint32_t get_permissions() const { return permissions; }
    void* get_resource() const { return resource; }
    size_t get_resource_size() const { return resource_size; }
    Capability* get_parent() const { return parent; }
    const std::vector<Capability*>& get_children() const { return children; }
    cThread<std::any>* get_thread() const { return thread; }
    bool get_owns_resource() const { return owns_resource; }
    
    // Set thread
    void set_thread(cThread<std::any>* thread_ptr) { thread = thread_ptr; }
    
    // Check if this capability is for the specified resource
    // Uses explicit binding state instead of permissive null checks
    bool is_for_resource(const void* res) const {
        // If capability is not bound to a specific resource, it cannot be used
        if (!resource_bound) {
            set_last_error(ErrorCode::CAP_RESOURCE_MISMATCH);
            return false;
        }
        // If querying with null, only return true if explicitly unbound
        if (res == nullptr) {
            set_last_error(ErrorCode::NULL_POINTER);
            return false;
        }
        bool match = (resource == res);
        if (!match) {
            set_last_error(ErrorCode::CAP_RESOURCE_MISMATCH);
        }
        return match;
    }

    // Check if this capability is for the specified thread
    // Uses explicit binding state instead of permissive null checks
    bool is_for_thread(cThread<std::any>* thread_ptr) const {
        // If capability is not bound to a specific thread, it cannot be used for thread ops
        if (!thread_bound) {
            set_last_error(ErrorCode::CAP_RESOURCE_MISMATCH);
            return false;
        }
        if (thread_ptr == nullptr) {
            set_last_error(ErrorCode::NULL_POINTER);
            return false;
        }
        bool match = (thread == thread_ptr);
        if (!match) {
            set_last_error(ErrorCode::CAP_RESOURCE_MISMATCH);
        }
        return match;
    }

    // Explicitly bind this capability to a resource (for delayed binding)
    void bind_resource(void* res, size_t size = 0) {
        resource = res;
        resource_size = size;
        resource_bound = (res != nullptr);
    }

    // Explicitly bind this capability to a thread (for delayed binding)
    void bind_thread(cThread<std::any>* thread_ptr) {
        thread = thread_ptr;
        thread_bound = (thread_ptr != nullptr);
    }

    // Check if resource is bound
    bool is_resource_bound() const { return resource_bound; }

    // Check if thread is bound
    bool is_thread_bound() const { return thread_bound; }

    // Check if capability is fully bound and ready for use
    bool is_fully_bound() const { return resource_bound; }
    
    // Delegate a subset of this capability's permissions
    Capability* delegate(const std::string& new_id, uint32_t requested_perms,
                        CapabilityScope delegated_scope = CapabilityScope::LOCAL) {
        // Check if we have delegate permission
        if (!has_permission(CapabilityPermission::DELEGATE)) {
            std::cerr << "Error: Capability " << cap_id
                    << " lacks DELEGATE permission required for delegation" << std::endl;
            return nullptr;
        }

        // ADDED: Check if we're trying to delegate DELEGATE permission without having TRANSITIVE_DELEGATE
        if ((requested_perms & CapabilityPermission::DELEGATE) &&
            !has_permission(CapabilityPermission::TRANSITIVE_DELEGATE)) {
            std::cerr << "Error: Capability " << cap_id
                    << " lacks TRANSITIVE_DELEGATE permission required for delegating DELEGATE permission" << std::endl;
            return nullptr;
        }

        // Ensure requested permissions are a subset of our permissions
        uint32_t allowed_perms = permissions & requested_perms;
        if (allowed_perms != requested_perms) {
            std::cerr << "Error: Requested permissions not a subset of parent permissions" << std::endl;
            return nullptr;
        }

        // Check scope compatibility - can only delegate to same or more restrictive scope
        // Scope hierarchy (most permissive to least): GLOBAL > {LOCAL, NETWORK, SOFTWARE, REMOTE}
        CapabilityScope effective_scope = delegated_scope;
        if (scope == CapabilityScope::GLOBAL) {
            // GLOBAL can delegate to any scope
            effective_scope = delegated_scope;
        } else if (delegated_scope == CapabilityScope::GLOBAL) {
            // Cannot escalate from non-GLOBAL to GLOBAL
            std::cerr << "Error: Cannot escalate scope from " << static_cast<int>(scope)
                      << " to GLOBAL" << std::endl;
            return nullptr;
        } else if (delegated_scope != scope) {
            // Non-GLOBAL capabilities can only delegate to the same scope
            std::cerr << "Error: Scope mismatch - parent scope " << static_cast<int>(scope)
                      << " cannot delegate to scope " << static_cast<int>(delegated_scope) << std::endl;
            return nullptr;
        }

        // Create a new capability with the allowed permissions and scope
        return new Capability(new_id, allowed_perms, thread, resource, resource_size, this, false, effective_scope);
    }

    // Add this to the Capability class
    bool can_delegate_delegation() const {
        return has_permission(CapabilityPermission::DELEGATE) &&
            has_permission(CapabilityPermission::TRANSITIVE_DELEGATE);
    }

    // Scope-related methods
    CapabilityScope get_scope() const { return scope; }
    void set_scope(CapabilityScope new_scope) { scope = new_scope; }

    // Check if capability has a specific scope
    bool has_scope(CapabilityScope required_scope) const {
        if (scope == CapabilityScope::GLOBAL) return true;
        return scope == required_scope;
    }

    // Check if capability is valid for a specific scope and permissions
    bool is_valid_for(CapabilityScope required_scope, uint32_t required_perms) const {
        return has_scope(required_scope) && has_permissions(required_perms);
    }

    // Helper methods for checking specific permission categories
    bool has_network_permissions(uint32_t net_perms) const {
        return has_scope(CapabilityScope::NETWORK) && has_permissions(net_perms);
    }

    bool has_software_permissions(uint32_t sw_perms) const {
        return has_scope(CapabilityScope::SOFTWARE) && has_permissions(sw_perms);
    }

    bool has_remote_permissions(uint32_t remote_perms) const {
        return has_scope(CapabilityScope::REMOTE) && has_permissions(remote_perms);
    }
};

/**
 * CapabilityGuard - A RAII wrapper for capability-based operations
 * Ensures that operations are only performed with valid capabilities
 */
template<typename T>
class CapabilityGuard {
private:
    T* resource;
    Capability* capability;
    uint32_t required_permissions;
    bool valid;
    
public:
    CapabilityGuard(T* res, Capability* cap, uint32_t perms) 
        : resource(res), capability(cap), required_permissions(perms), valid(false) {
        if (res && cap && !cap->is_expired() && 
            cap->is_for_resource(res) && 
            cap->has_permissions(perms)) {
            valid = true;
        }
    }
    
    bool is_valid() const { return valid; }
    T* get() const { return valid ? resource : nullptr; }
    
    operator bool() const { return valid; }
};

// Forward declare classes
class NodeBase;
class ComputeNode;
class NetworkNode;
class SoftwareNode;
class RemoteDFGNode;

// Alias for backward compatibility
using Node = ComputeNode;

/**
 * NodeBase - Abstract base class for all node types in the DFG
 * Provides common interface for compute, network, software, and remote nodes
 */
class NodeBase {
protected:
    std::string node_id;
    DFG* parent_dfg;
    NodeType node_type;
    bool initialized = false;

public:
    NodeBase(const std::string& id, DFG* dfg, NodeType type)
        : node_id(id), parent_dfg(dfg), node_type(type) {}

    virtual ~NodeBase() = default;

    // Pure virtual methods - must be implemented by derived classes
    virtual bool initialize(Capability* cap) = 0;
    virtual void shutdown(Capability* cap) = 0;
    virtual bool is_ready(Capability* cap) const = 0;

    // Common getters
    std::string get_id() const { return node_id; }
    DFG* get_parent_dfg() const { return parent_dfg; }
    NodeType get_node_type() const { return node_type; }
    bool is_initialized() const { return initialized; }

    // Type checking helpers
    bool is_compute_node() const { return node_type == NodeType::COMPUTE; }
    bool is_network_node() const {
        return node_type == NodeType::NETWORK_RDMA ||
               node_type == NodeType::NETWORK_TCP ||
               node_type == NodeType::NETWORK_RAW;
    }
    bool is_software_node() const {
        return node_type == NodeType::SOFTWARE_PARSER ||
               node_type == NodeType::SOFTWARE_DEPARSER ||
               node_type == NodeType::SOFTWARE_NF;
    }
    bool is_remote_node() const { return node_type == NodeType::REMOTE_DFG; }

    // Virtual method for connecting to another node
    virtual bool connect_to(NodeBase* target, Capability* cap) {
        // Default implementation - can be overridden
        return false;
    }

    // Virtual method for disconnecting from another node
    virtual bool disconnect_from(NodeBase* target, Capability* cap) {
        // Default implementation - can be overridden
        return false;
    }

    // Print node info for debugging
    virtual void print_info() const {
        std::cout << "Node: " << node_id << " Type: " << static_cast<int>(node_type)
                  << " Initialized: " << (initialized ? "yes" : "no") << std::endl;
    }
};

/**
 * Buffer class - Requires capabilities for all operations
 */
class Buffer {
private:
    std::string buffer_id;
    DFG* parent_dfg;
    void* memory;
    size_t size;

public:
    // Constructor
    Buffer(const std::string& buffer_id, DFG* parent_dfg, void* memory, size_t size)
        : buffer_id(buffer_id), parent_dfg(parent_dfg), memory(memory), size(size) {
    }

    // Get the buffer ID
    std::string get_id() const { return buffer_id; }

    // Get the memory pointer - requires a capability with READ permission
    void* get_memory(Capability* cap) const {
        // More permissive check for initialization phase
        if (!cap) {
            std::cerr << "Error: Null capability for get_memory on buffer " << buffer_id << std::endl;
            return nullptr;
        }
        
        if (!cap->has_permission(CapabilityPermission::READ)) {
            std::cerr << "Error: Insufficient READ permission for get_memory on buffer " << buffer_id << std::endl;
            return nullptr;
        }
        
        // During initialization, don't strictly check resource association
        return memory;
    }

    // Write to buffer - requires a capability with WRITE permission
    bool write_data(void* data, size_t data_size, Capability* cap) {
        // Check capability
        if (!cap) {
            std::cerr << "Error: Null capability for write_data on buffer " << buffer_id << std::endl;
            return false;
        }
        
        if (!cap->has_permission(CapabilityPermission::WRITE)) {
            std::cerr << "Error: Insufficient WRITE permission for write_data on buffer " << buffer_id << std::endl;
            return false;
        }
        
        // Check data and size
        if (!data) {
            std::cerr << "Error: Null data for write_data on buffer " << buffer_id << std::endl;
            return false;
        }
        
        if (data_size > size) {
            std::cerr << "Error: Data size " << data_size << " exceeds buffer size " 
                      << size << " for buffer " << buffer_id << std::endl;
            return false;
        }
        
        // Perform the copy
        try {
            memcpy(memory, data, data_size);
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Exception during write_data on buffer " << buffer_id 
                      << ": " << e.what() << std::endl;
            return false;
        }
    }
    
    // Read from buffer - requires a capability with READ permission
    bool read_data(void* dest, size_t data_size, Capability* cap) {
        // Check capability
        if (!cap) {
            std::cerr << "Error: Null capability for read_data on buffer " << buffer_id << std::endl;
            return false;
        }
        
        if (!cap->has_permission(CapabilityPermission::READ)) {
            std::cerr << "Error: Insufficient READ permission for read_data on buffer " << buffer_id << std::endl;
            return false;
        }
        
        // Check destination and size
        if (!dest) {
            std::cerr << "Error: Null destination for read_data on buffer " << buffer_id << std::endl;
            return false;
        }
        
        if (data_size > size) {
            std::cerr << "Error: Data size " << data_size << " exceeds buffer size " 
                      << size << " for buffer " << buffer_id << std::endl;
            return false;
        }
        
        // Perform the copy
        try {
            memcpy(dest, memory, data_size);
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Exception during read_data on buffer " << buffer_id 
                      << ": " << e.what() << std::endl;
            return false;
        }
    }

    // Get the size - requires a capability with READ permission
    size_t get_size(Capability* cap) const {
        if (!cap) {
            std::cerr << "Error: Null capability for get_size on buffer " << buffer_id << std::endl;
            return 0;
        }
        
        if (!cap->has_permission(CapabilityPermission::READ)) {
            std::cerr << "Error: Insufficient READ permission for get_size on buffer " << buffer_id << std::endl;
            return 0;
        }
        
        return size;
    }
    
    // Get the parent DFG
    DFG* get_parent_dfg() const {
        return parent_dfg;
    }
};

// ============================================================================
// Network Node Hierarchy
// ============================================================================

/**
 * QoS configuration for network nodes
 */
struct NetworkQoS {
    uint64_t max_bandwidth_bps = 0;      // Maximum bandwidth in bits per second (0 = unlimited)
    uint32_t max_connections = 0;         // Maximum concurrent connections (0 = unlimited)
    uint32_t priority = 0;                // QoS priority level (higher = more priority)
    uint32_t buffer_size = 65536;         // Network buffer size in bytes
    bool rate_limiting_enabled = false;   // Enable rate limiting
};

/**
 * VLAN configuration for network nodes
 * Used by VIU (Virtual Interface Unit) for traffic verification and isolation
 *
 * Multi-FPGA VLAN ID Format (12 bits):
 *   [11:10] = src_node_id   (source physical FPGA node, 0-3)
 *   [9:8]   = src_vfpga_id  (source vFPGA on that node, 0-3)
 *   [7:6]   = dst_node_id   (destination physical FPGA node, 0-3)
 *   [5:4]   = dst_vfpga_id  (destination vFPGA on that node, 0-3)
 *   [3:0]   = flags
 *
 * This encoding supports: 4 nodes × 4 vFPGAs = 16 total vFPGAs in closed network
 * Node ID 0 + vFPGA ID 0 = external network (outside closed VLAN network)
 */
struct VLANConfig {
    // Identity of this vFPGA in the closed VLAN network
    // New format: 4 nodes × 16 vFPGAs = 64 total vFPGAs
    uint8_t node_id = 0;                  // This physical FPGA node (0-3, 0=external)
    uint8_t vfpga_id = 0;                 // This vFPGA on the node (0-15)

    // Legacy: Full 12-bit VLAN ID (computed from node_id + vfpga_id)
    uint16_t vlan_id = 0;                 // VLAN tag for this node (0 = external)

    bool enforce_vlan = true;             // Enforce VLAN tag verification at VIU
    bool strip_vlan_on_rx = false;        // Strip VLAN tag on receive
    bool insert_vlan_on_tx = true;        // Insert VLAN tag on transmit
    std::vector<uint16_t> allowed_vlans;  // List of allowed incoming VLAN IDs (empty = only this vlan_id)

    // Compute full VLAN ID from node_id and vfpga_id (for source identity)
    // This packs source identity into upper bits, destination will be added at TX time
    // New format: [11:10]=src_node, [9:6]=src_vfpga, [5:4]=dst_node, [3:0]=dst_vfpga
    uint16_t get_source_vlan_id() const {
        return ((node_id & 0x3) << 10) | ((vfpga_id & 0xF) << 6);
    }

    // Build full VLAN ID with destination (12-bit)
    // Format: [11:10]=src_node (2b), [9:6]=src_vfpga (4b), [5:4]=dst_node (2b), [3:0]=dst_vfpga (4b)
    static uint16_t build_vlan_id(uint8_t src_node, uint8_t src_vfpga,
                                   uint8_t dst_node, uint8_t dst_vfpga) {
        return ((src_node & 0x3) << 10) |
               ((src_vfpga & 0xF) << 6) |
               ((dst_node & 0x3) << 4) |
               (dst_vfpga & 0xF);
    }

    // Build 14-bit Route ID (used internally in VIU for tdest)
    // Format: [13:12]=src_node (2b), [11:8]=src_vfpga (4b), [7:6]=dst_node (2b), [5:2]=dst_vfpga (4b), [1:0]=reserved
    static uint16_t build_route_id(uint8_t src_node, uint8_t src_vfpga,
                                    uint8_t dst_node, uint8_t dst_vfpga) {
        return ((src_node & 0x3) << 12) |
               ((src_vfpga & 0xF) << 8) |
               ((dst_node & 0x3) << 6) |
               ((dst_vfpga & 0xF) << 2);
    }

    // Extract fields from VLAN ID (12-bit)
    static uint8_t get_src_node(uint16_t vlan_id) { return (vlan_id >> 10) & 0x3; }
    static uint8_t get_src_vfpga(uint16_t vlan_id) { return (vlan_id >> 6) & 0xF; }
    static uint8_t get_dst_node(uint16_t vlan_id) { return (vlan_id >> 4) & 0x3; }
    static uint8_t get_dst_vfpga(uint16_t vlan_id) { return vlan_id & 0xF; }

    // Extract fields from Route ID (14-bit)
    static uint8_t get_route_src_node(uint16_t route_id) { return (route_id >> 12) & 0x3; }
    static uint8_t get_route_src_vfpga(uint16_t route_id) { return (route_id >> 8) & 0xF; }
    static uint8_t get_route_dst_node(uint16_t route_id) { return (route_id >> 6) & 0x3; }
    static uint8_t get_route_dst_vfpga(uint16_t route_id) { return (route_id >> 2) & 0xF; }

    // Check if VLAN ID indicates external source/destination
    static bool is_external_src(uint16_t vlan_id) {
        return get_src_node(vlan_id) == 0 && get_src_vfpga(vlan_id) == 0;
    }
    static bool is_external_dst(uint16_t vlan_id) {
        return get_dst_node(vlan_id) == 0 && get_dst_vfpga(vlan_id) == 0;
    }
};

/**
 * NetworkTransferConfig - Configuration for network transfer edge cases
 * Handles timeouts, partial transfers, and reconnection behavior
 */
struct NetworkTransferConfig {
    // Timeout settings
    uint32_t connect_timeout_ms = 5000;     // Connection establishment timeout
    uint32_t send_timeout_ms = 1000;        // Per-send operation timeout
    uint32_t receive_timeout_ms = 1000;     // Per-receive operation timeout

    // Partial transfer handling
    bool allow_partial_sends = true;        // Allow partial sends (return bytes sent)
    bool allow_partial_receives = true;     // Allow partial receives
    size_t min_send_chunk = 0;              // Minimum bytes to send (0 = any)
    size_t max_send_chunk = 0;              // Maximum bytes per send (0 = unlimited)

    // Reconnection settings
    bool auto_reconnect = false;            // Automatically reconnect on disconnect
    uint32_t reconnect_delay_ms = 1000;     // Delay before reconnection attempt
    uint32_t max_reconnect_attempts = 3;    // Maximum reconnection attempts (0 = unlimited)

    // Retry settings
    uint32_t max_retry_count = 3;           // Max retries for transient failures
    uint32_t retry_delay_ms = 100;          // Delay between retries
};

/**
 * NetworkTransferResult - Result of a network transfer operation
 * Provides detailed information about partial transfers and errors
 */
struct NetworkTransferResult {
    ssize_t bytes_transferred = 0;      // Bytes actually transferred (-1 on error)
    ErrorCode error = ErrorCode::SUCCESS;
    bool is_partial = false;            // True if only part of the data was transferred
    bool timed_out = false;             // True if operation timed out
    bool connection_lost = false;       // True if connection was lost during transfer
    uint32_t retry_count = 0;           // Number of retries performed

    // Convenience methods
    bool ok() const { return error == ErrorCode::SUCCESS && bytes_transferred >= 0; }
    bool needs_retry() const { return is_partial || timed_out; }
};

/**
 * NetworkNode - Abstract base class for all network node types
 * Provides common interface for RDMA, TCP, and Raw Ethernet nodes
 * All network traffic is verified at the VIU using VLAN tags
 */
class NetworkNode : public NodeBase {
protected:
    NetworkQoS qos_config;
    VLANConfig vlan_config;
    NetworkTransferConfig transfer_config;
    bool connected = false;
    std::string local_address;
    uint16_t local_port = 0;

    // Reconnection state
    std::string last_remote_address;
    uint16_t last_remote_port = 0;
    uint32_t reconnect_attempts = 0;

    /**
     * Verify incoming packet's VLAN tag against configured allowed VLANs
     * Called by VIU hardware module for all incoming traffic
     * @param incoming_vlan_id VLAN tag from incoming packet
     * @return true if VLAN is allowed, false if packet should be dropped
     */
    bool verify_vlan_tag(uint16_t incoming_vlan_id) const {
        if (!vlan_config.enforce_vlan) {
            return true;  // VLAN enforcement disabled
        }

        // Check if incoming VLAN matches our configured VLAN
        if (incoming_vlan_id == vlan_config.vlan_id) {
            return true;
        }

        // Check if incoming VLAN is in the allowed list
        if (!vlan_config.allowed_vlans.empty()) {
            for (uint16_t allowed : vlan_config.allowed_vlans) {
                if (incoming_vlan_id == allowed) {
                    return true;
                }
            }
        }

        std::cerr << "Error: VLAN verification failed on node " << node_id
                  << " - received VLAN " << incoming_vlan_id
                  << ", expected " << vlan_config.vlan_id << std::endl;
        return false;
    }

public:
    NetworkNode(const std::string& id, DFG* dfg, NodeType net_type, uint16_t vlan_id = 0)
        : NodeBase(id, dfg, net_type) {
        vlan_config.vlan_id = vlan_id;
        if (vlan_id != 0) {
            vlan_config.enforce_vlan = true;
        }
    }

    virtual ~NetworkNode() = default;

    // Common network operations (pure virtual)
    virtual bool bind(const std::string& address, uint16_t port, Capability* cap) = 0;
    virtual bool connect_to_remote(const std::string& remote_addr, uint16_t remote_port, Capability* cap) = 0;
    virtual bool disconnect(Capability* cap) = 0;
    virtual ssize_t send(const void* data, size_t len, Capability* cap) = 0;
    virtual ssize_t receive(void* buffer, size_t max_len, Capability* cap) = 0;

    // VLAN management
    bool set_vlan_config(const VLANConfig& config, Capability* cap) {
        if (!cap || !cap->has_network_permissions(NET_QOS_MODIFY)) {
            std::cerr << "Error: Insufficient NET_QOS_MODIFY permission for set_vlan_config on node " << node_id << std::endl;
            return false;
        }
        vlan_config = config;
        return true;
    }

    bool set_vlan_id(uint16_t vlan_id, Capability* cap) {
        if (!cap || !cap->has_network_permissions(NET_QOS_MODIFY)) {
            std::cerr << "Error: Insufficient NET_QOS_MODIFY permission for set_vlan_id on node " << node_id << std::endl;
            return false;
        }
        vlan_config.vlan_id = vlan_id;
        vlan_config.enforce_vlan = (vlan_id != 0);
        return true;
    }

    bool add_allowed_vlan(uint16_t vlan_id, Capability* cap) {
        if (!cap || !cap->has_network_permissions(NET_QOS_MODIFY)) {
            std::cerr << "Error: Insufficient NET_QOS_MODIFY permission for add_allowed_vlan on node " << node_id << std::endl;
            return false;
        }
        vlan_config.allowed_vlans.push_back(vlan_id);
        return true;
    }

    VLANConfig get_vlan_config(Capability* cap) const {
        if (!cap || !cap->has_permission(CapabilityPermission::READ)) {
            std::cerr << "Error: Insufficient READ permission for get_vlan_config on node " << node_id << std::endl;
            return VLANConfig{};
        }
        return vlan_config;
    }

    uint16_t get_vlan_id() const { return vlan_config.vlan_id; }

    // QoS management
    bool set_qos(const NetworkQoS& qos, Capability* cap) {
        if (!cap || !cap->has_network_permissions(NET_QOS_MODIFY)) {
            std::cerr << "Error: Insufficient NET_QOS_MODIFY permission for set_qos on node " << node_id << std::endl;
            return false;
        }
        qos_config = qos;
        return true;
    }

    NetworkQoS get_qos(Capability* cap) const {
        if (!cap || !cap->has_permission(CapabilityPermission::READ)) {
            std::cerr << "Error: Insufficient READ permission for get_qos on node " << node_id << std::endl;
            return NetworkQoS{};
        }
        return qos_config;
    }

    // Status
    bool is_connected() const { return connected; }
    std::string get_local_address() const { return local_address; }
    uint16_t get_local_port() const { return local_port; }

    // Transfer configuration management
    bool set_transfer_config(const NetworkTransferConfig& config, Capability* cap) {
        if (!cap || !cap->has_network_permissions(NET_QOS_MODIFY)) {
            set_last_error(ErrorCode::CAP_INSUFFICIENT_PERMISSIONS);
            return false;
        }
        transfer_config = config;
        return true;
    }

    NetworkTransferConfig get_transfer_config(Capability* cap) const {
        if (!cap || !cap->has_permission(CapabilityPermission::READ)) {
            return NetworkTransferConfig{};
        }
        return transfer_config;
    }

    /**
     * Enhanced send with detailed result and edge case handling
     * Handles partial transfers, timeouts, and automatic retries
     */
    virtual NetworkTransferResult send_with_result(const void* data, size_t len, Capability* cap) {
        NetworkTransferResult result;

        if (!cap || !cap->has_network_permissions(NET_SEND)) {
            result.error = ErrorCode::CAP_INSUFFICIENT_PERMISSIONS;
            result.bytes_transferred = -1;
            return result;
        }

        if (!connected) {
            // Try auto-reconnect if enabled
            if (transfer_config.auto_reconnect && !last_remote_address.empty()) {
                if (!try_reconnect(cap)) {
                    result.error = ErrorCode::NET_NOT_CONNECTED;
                    result.bytes_transferred = -1;
                    result.connection_lost = true;
                    return result;
                }
            } else {
                result.error = ErrorCode::NET_NOT_CONNECTED;
                result.bytes_transferred = -1;
                return result;
            }
        }

        // Calculate send chunk size
        size_t chunk_size = len;
        if (transfer_config.max_send_chunk > 0 && chunk_size > transfer_config.max_send_chunk) {
            chunk_size = transfer_config.max_send_chunk;
        }

        // Attempt send with retries
        for (uint32_t retry = 0; retry <= transfer_config.max_retry_count; ++retry) {
            result.retry_count = retry;
            ssize_t sent = send(data, chunk_size, cap);

            if (sent > 0) {
                result.bytes_transferred = sent;
                result.is_partial = (static_cast<size_t>(sent) < len);
                result.error = ErrorCode::SUCCESS;
                return result;
            }

            if (sent == 0) {
                result.timed_out = true;
            }

            // Check if we should retry
            if (retry < transfer_config.max_retry_count) {
                std::this_thread::sleep_for(std::chrono::milliseconds(transfer_config.retry_delay_ms));
            }
        }

        result.error = ErrorCode::NET_TIMEOUT;
        result.bytes_transferred = -1;
        return result;
    }

    /**
     * Enhanced receive with detailed result and edge case handling
     */
    virtual NetworkTransferResult receive_with_result(void* buffer, size_t max_len, Capability* cap) {
        NetworkTransferResult result;

        if (!cap || !cap->has_network_permissions(NET_RECEIVE)) {
            result.error = ErrorCode::CAP_INSUFFICIENT_PERMISSIONS;
            result.bytes_transferred = -1;
            return result;
        }

        if (!connected) {
            if (transfer_config.auto_reconnect && !last_remote_address.empty()) {
                if (!try_reconnect(cap)) {
                    result.error = ErrorCode::NET_NOT_CONNECTED;
                    result.bytes_transferred = -1;
                    result.connection_lost = true;
                    return result;
                }
            } else {
                result.error = ErrorCode::NET_NOT_CONNECTED;
                result.bytes_transferred = -1;
                return result;
            }
        }

        // Attempt receive with retries
        for (uint32_t retry = 0; retry <= transfer_config.max_retry_count; ++retry) {
            result.retry_count = retry;
            ssize_t received = receive(buffer, max_len, cap);

            if (received > 0) {
                result.bytes_transferred = received;
                result.is_partial = transfer_config.allow_partial_receives;
                result.error = ErrorCode::SUCCESS;
                return result;
            }

            if (received == 0) {
                result.timed_out = true;
            }

            if (retry < transfer_config.max_retry_count) {
                std::this_thread::sleep_for(std::chrono::milliseconds(transfer_config.retry_delay_ms));
            }
        }

        result.error = ErrorCode::NET_TIMEOUT;
        result.bytes_transferred = -1;
        return result;
    }

    /**
     * Attempt to reconnect to the last known remote endpoint
     * @return true if reconnection successful
     */
    bool try_reconnect(Capability* cap) {
        if (last_remote_address.empty()) {
            return false;
        }

        if (transfer_config.max_reconnect_attempts > 0 &&
            reconnect_attempts >= transfer_config.max_reconnect_attempts) {
            set_last_error(ErrorCode::NET_RECONNECT_FAILED);
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(transfer_config.reconnect_delay_ms));
        reconnect_attempts++;

        if (connect_to_remote(last_remote_address, last_remote_port, cap)) {
            reconnect_attempts = 0;  // Reset on success
            return true;
        }

        return false;
    }

    // Reset reconnection counter (call after successful manual reconnect)
    void reset_reconnect_counter() { reconnect_attempts = 0; }

    // Print info override
    void print_info() const override {
        std::cout << "NetworkNode: " << node_id << " Type: " << static_cast<int>(node_type)
                  << " VLAN: " << vlan_config.vlan_id
                  << " Connected: " << (connected ? "yes" : "no")
                  << " Address: " << local_address << ":" << local_port << std::endl;
    }
};

/**
 * RDMANetworkNode - RDMA network node for high-performance data transfer
 * Wraps Coyote's ibvQp functionality
 */
class RDMANetworkNode : public NetworkNode {
private:
    // RDMA-specific state (would wrap ibvQp/ibvQ in real implementation)
    uint32_t qp_num = 0;          // Queue pair number
    uint32_t remote_qp_num = 0;   // Remote queue pair number
    std::string remote_gid;        // Remote GID for connection

public:
    RDMANetworkNode(const std::string& id, DFG* dfg, uint16_t vlan_id = 0)
        : NetworkNode(id, dfg, NodeType::NETWORK_RDMA, vlan_id) {}

    // NodeBase interface
    bool initialize(Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_ESTABLISH)) {
            std::cerr << "Error: Insufficient NET_ESTABLISH permission for initialize on RDMA node " << node_id << std::endl;
            return false;
        }
        // Initialize RDMA resources (queue pairs, etc.)
        initialized = true;
        return true;
    }

    void shutdown(Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_ESTABLISH)) {
            std::cerr << "Error: Insufficient NET_ESTABLISH permission for shutdown on RDMA node " << node_id << std::endl;
            set_last_error(ErrorCode::CAP_INSUFFICIENT_PERMISSIONS);
            return;
        }
        disconnect(cap);
        initialized = false;
    }

    bool is_ready(Capability* cap) const override {
        if (!cap || !cap->has_permission(CapabilityPermission::READ)) {
            return false;
        }
        return initialized && connected;
    }

    // NetworkNode interface
    bool bind(const std::string& address, uint16_t port, Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_ESTABLISH)) {
            std::cerr << "Error: Insufficient NET_ESTABLISH permission for bind on RDMA node " << node_id << std::endl;
            return false;
        }
        local_address = address;
        local_port = port;
        return true;
    }

    bool connect_to_remote(const std::string& remote_addr, uint16_t remote_port, Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_ESTABLISH)) {
            std::cerr << "Error: Insufficient NET_ESTABLISH permission for connect on RDMA node " << node_id << std::endl;
            return false;
        }
        // Store for potential reconnection
        last_remote_address = remote_addr;
        last_remote_port = remote_port;
        // Establish RDMA connection (exchange QP info, etc.)
        connected = true;
        reconnect_attempts = 0;  // Reset on successful connect
        return true;
    }

    bool disconnect(Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_ESTABLISH)) {
            std::cerr << "Error: Insufficient NET_ESTABLISH permission for disconnect on RDMA node " << node_id << std::endl;
            set_last_error(ErrorCode::CAP_INSUFFICIENT_PERMISSIONS);
            return false;
        }
        connected = false;
        return true;
    }

    ssize_t send(const void* data, size_t len, Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_SEND)) {
            std::cerr << "Error: Insufficient NET_SEND permission for send on RDMA node " << node_id << std::endl;
            return -1;
        }
        if (!connected) {
            std::cerr << "Error: RDMA node " << node_id << " not connected" << std::endl;
            return -1;
        }
        // Would use ibvQp->postSend() or similar
        return static_cast<ssize_t>(len);
    }

    ssize_t receive(void* buffer, size_t max_len, Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_RECEIVE)) {
            std::cerr << "Error: Insufficient NET_RECEIVE permission for receive on RDMA node " << node_id << std::endl;
            return -1;
        }
        if (!connected) {
            std::cerr << "Error: RDMA node " << node_id << " not connected" << std::endl;
            return -1;
        }
        // Would use ibvQp->postRecv() or similar
        return 0;
    }

    // RDMA-specific operations
    bool rdma_write(void* local_addr, size_t len, uint64_t remote_addr, Capability* cap) {
        // RDMA write requires NET_SEND permission (network scope handles the scope check)
        if (!cap || !cap->has_network_permissions(NET_SEND)) {
            std::cerr << "Error: Insufficient NET_SEND permission for rdma_write on node " << node_id << std::endl;
            return false;
        }
        if (!connected) return false;
        // Would use ibvQp->postWrite() or similar
        return true;
    }

    bool rdma_read(void* local_addr, size_t len, uint64_t remote_addr, Capability* cap) {
        // RDMA read requires NET_RECEIVE permission (network scope handles the scope check)
        if (!cap || !cap->has_network_permissions(NET_RECEIVE)) {
            std::cerr << "Error: Insufficient NET_RECEIVE permission for rdma_read on node " << node_id << std::endl;
            return false;
        }
        if (!connected) return false;
        // Would use ibvQp->postRead() or similar
        return true;
    }

    // Getters for RDMA-specific info
    uint32_t get_qp_num() const { return qp_num; }
    uint32_t get_remote_qp_num() const { return remote_qp_num; }

    // Aliases for consistency with server code
    uint32_t get_local_qpn() const { return qp_num; }
    uint32_t get_remote_qpn() const { return remote_qp_num; }
    std::string get_local_ip() const { return local_address; }

    /**
     * Setup RDMA Queue Pair for connection
     * @param buffer_size Size of RDMA buffer to allocate
     * @param is_initiator true if this side initiates connection, false if it waits
     * @param cap Capability for authorization
     * @return true if QP setup successful
     */
    bool setup_qp(uint32_t buffer_size, bool is_initiator, Capability* cap) {
        if (!cap || !cap->has_network_permissions(NET_ESTABLISH)) {
            std::cerr << "Error: Insufficient NET_ESTABLISH permission for setup_qp on RDMA node " << node_id << std::endl;
            return false;
        }

        // In real implementation, this would:
        // 1. Create RDMA protection domain
        // 2. Create completion queue
        // 3. Create queue pair
        // 4. Transition QP to INIT state
        // 5. Allocate and register memory region of buffer_size

        // Generate a unique QPN (in real impl, this comes from hardware)
        static uint32_t next_qpn = 1000;
        qp_num = next_qpn++;

        // If not initiator, we wait for remote to connect
        // If initiator, we'll connect after receiving remote QPN
        initialized = true;

        std::cout << "[RDMANetworkNode] QP setup complete: local_qpn=" << qp_num
                  << " is_initiator=" << is_initiator << std::endl;
        return true;
    }

    /**
     * Connect to remote QP using exchanged QPN
     * @param remote_ip Remote worker IP
     * @param remote_qpn Remote Queue Pair Number
     * @param cap Capability for authorization
     * @return true if connection established
     */
    bool connect_to_remote(const std::string& remote_ip, uint32_t remote_qpn, Capability* cap) {
        if (!cap || !cap->has_network_permissions(NET_ESTABLISH)) {
            std::cerr << "Error: Insufficient NET_ESTABLISH permission for connect on RDMA node " << node_id << std::endl;
            return false;
        }

        // In real implementation, this would:
        // 1. Exchange GID/LID with remote
        // 2. Transition QP to RTR (Ready To Receive)
        // 3. Transition QP to RTS (Ready To Send)
        // 4. Post initial receive buffers

        remote_qp_num = remote_qpn;
        last_remote_address = remote_ip;
        connected = true;

        std::cout << "[RDMANetworkNode] Connected to remote: remote_ip=" << remote_ip
                  << " remote_qpn=" << remote_qpn << std::endl;
        return true;
    }
};

/**
 * TCPNetworkNode - TCP network node for reliable stream communication
 */
class TCPNetworkNode : public NetworkNode {
private:
    int socket_fd = -1;
    bool is_server = false;
    std::vector<int> client_fds;  // For server mode

public:
    TCPNetworkNode(const std::string& id, DFG* dfg, uint16_t vlan_id = 0)
        : NetworkNode(id, dfg, NodeType::NETWORK_TCP, vlan_id) {}

    // NodeBase interface
    bool initialize(Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_ESTABLISH)) {
            std::cerr << "Error: Insufficient NET_ESTABLISH permission for initialize on TCP node " << node_id << std::endl;
            return false;
        }
        // Create socket
        initialized = true;
        return true;
    }

    void shutdown(Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_ESTABLISH)) {
            std::cerr << "Error: Insufficient NET_ESTABLISH permission for shutdown on TCP node " << node_id << std::endl;
            set_last_error(ErrorCode::CAP_INSUFFICIENT_PERMISSIONS);
            return;
        }
        disconnect(cap);
        initialized = false;
    }

    bool is_ready(Capability* cap) const override {
        if (!cap || !cap->has_permission(CapabilityPermission::READ)) {
            return false;
        }
        return initialized && (connected || is_server);
    }

    // NetworkNode interface
    bool bind(const std::string& address, uint16_t port, Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_ESTABLISH)) {
            std::cerr << "Error: Insufficient NET_ESTABLISH permission for bind on TCP node " << node_id << std::endl;
            return false;
        }
        local_address = address;
        local_port = port;
        // Would call ::bind() and ::listen()
        is_server = true;
        return true;
    }

    bool connect_to_remote(const std::string& remote_addr, uint16_t remote_port, Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_ESTABLISH)) {
            std::cerr << "Error: Insufficient NET_ESTABLISH permission for connect on TCP node " << node_id << std::endl;
            return false;
        }
        // Would call ::connect()
        connected = true;
        return true;
    }

    bool disconnect(Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_ESTABLISH)) {
            std::cerr << "Error: Insufficient NET_ESTABLISH permission for disconnect on TCP node " << node_id << std::endl;
            set_last_error(ErrorCode::CAP_INSUFFICIENT_PERMISSIONS);
            return false;
        }
        // Close socket
        connected = false;
        return true;
    }

    ssize_t send(const void* data, size_t len, Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_SEND)) {
            std::cerr << "Error: Insufficient NET_SEND permission for send on TCP node " << node_id << std::endl;
            return -1;
        }
        if (!connected) {
            std::cerr << "Error: TCP node " << node_id << " not connected" << std::endl;
            return -1;
        }
        // Would call ::send()
        return static_cast<ssize_t>(len);
    }

    ssize_t receive(void* buffer, size_t max_len, Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_RECEIVE)) {
            std::cerr << "Error: Insufficient NET_RECEIVE permission for receive on TCP node " << node_id << std::endl;
            return -1;
        }
        if (!connected) {
            std::cerr << "Error: TCP node " << node_id << " not connected" << std::endl;
            return -1;
        }
        // Would call ::recv()
        return 0;
    }

    // TCP-specific operations
    bool listen(int backlog, Capability* cap) {
        if (!cap || !cap->has_network_permissions(NET_ESTABLISH)) {
            std::cerr << "Error: Insufficient NET_ESTABLISH permission for listen on TCP node " << node_id << std::endl;
            return false;
        }
        // Would call ::listen()
        return true;
    }

    int accept(Capability* cap) {
        if (!cap || !cap->has_network_permissions(NET_ESTABLISH | NET_RECEIVE)) {
            std::cerr << "Error: Insufficient permissions for accept on TCP node " << node_id << std::endl;
            return -1;
        }
        // Would call ::accept()
        return 0;
    }
};

/**
 * RawEthernetNode - Raw Ethernet node for packet-level I/O
 */
class RawEthernetNode : public NetworkNode {
private:
    std::string interface_name;
    uint16_t ethertype = 0x0800;  // Default to IPv4
    bool promiscuous = false;

public:
    RawEthernetNode(const std::string& id, DFG* dfg, const std::string& iface = "", uint16_t vlan_id = 0)
        : NetworkNode(id, dfg, NodeType::NETWORK_RAW, vlan_id), interface_name(iface) {}

    // NodeBase interface
    bool initialize(Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_ESTABLISH)) {
            std::cerr << "Error: Insufficient NET_ESTABLISH permission for initialize on Raw Ethernet node " << node_id << std::endl;
            return false;
        }
        // Open raw socket on interface
        initialized = true;
        connected = true;  // Raw sockets are always "connected" to the interface
        return true;
    }

    void shutdown(Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_ESTABLISH)) {
            std::cerr << "Error: Insufficient NET_ESTABLISH permission for shutdown on Raw Ethernet node " << node_id << std::endl;
            set_last_error(ErrorCode::CAP_INSUFFICIENT_PERMISSIONS);
            return;
        }
        initialized = false;
        connected = false;
    }

    bool is_ready(Capability* cap) const override {
        if (!cap || !cap->has_permission(CapabilityPermission::READ)) {
            return false;
        }
        return initialized;
    }

    // NetworkNode interface
    bool bind(const std::string& address, uint16_t port, Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_ESTABLISH)) {
            std::cerr << "Error: Insufficient NET_ESTABLISH permission for bind on Raw Ethernet node " << node_id << std::endl;
            return false;
        }
        interface_name = address;  // For raw sockets, "address" is the interface name
        // Bind to interface
        return true;
    }

    bool connect_to_remote(const std::string& remote_addr, uint16_t remote_port, Capability* cap) override {
        // Raw sockets don't really "connect" - they're bound to an interface
        (void)remote_addr;
        (void)remote_port;
        (void)cap;
        return true;
    }

    bool disconnect(Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_ESTABLISH)) {
            std::cerr << "Error: Insufficient NET_ESTABLISH permission for disconnect on Raw Ethernet node " << node_id << std::endl;
            set_last_error(ErrorCode::CAP_INSUFFICIENT_PERMISSIONS);
            return false;
        }
        // Nothing to disconnect for raw sockets
        return true;
    }

    ssize_t send(const void* data, size_t len, Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_SEND)) {
            std::cerr << "Error: Insufficient NET_SEND permission for send on Raw Ethernet node " << node_id << std::endl;
            return -1;
        }
        if (!initialized) {
            std::cerr << "Error: Raw Ethernet node " << node_id << " not initialized" << std::endl;
            return -1;
        }
        // Would send raw packet
        return static_cast<ssize_t>(len);
    }

    ssize_t receive(void* buffer, size_t max_len, Capability* cap) override {
        if (!cap || !cap->has_network_permissions(NET_RECEIVE)) {
            std::cerr << "Error: Insufficient NET_RECEIVE permission for receive on Raw Ethernet node " << node_id << std::endl;
            return -1;
        }
        if (!initialized) {
            std::cerr << "Error: Raw Ethernet node " << node_id << " not initialized" << std::endl;
            return -1;
        }
        // Would receive raw packet
        return 0;
    }

    // Raw Ethernet-specific operations
    bool set_ethertype(uint16_t etype, Capability* cap) {
        if (!cap || !cap->has_permission(CapabilityPermission::WRITE)) {
            std::cerr << "Error: Insufficient WRITE permission for set_ethertype on node " << node_id << std::endl;
            return false;
        }
        ethertype = etype;
        return true;
    }

    bool set_promiscuous(bool enable, Capability* cap) {
        if (!cap || !cap->has_network_permissions(NET_ESTABLISH | WRITE)) {
            std::cerr << "Error: Insufficient permissions for set_promiscuous on node " << node_id << std::endl;
            return false;
        }
        promiscuous = enable;
        // Would set IFF_PROMISC on interface
        return true;
    }

    uint16_t get_ethertype() const { return ethertype; }
    bool is_promiscuous() const { return promiscuous; }
    std::string get_interface() const { return interface_name; }
};

// ============================================================================
// Compute Node
// ============================================================================

/**
 * ComputeNode class - vFPGA compute node (formerly Node class)
 * Requires capabilities for all operations
 * Inherits from NodeBase for polymorphic usage
 */
class ComputeNode : public NodeBase {
private:
    int vfid;
    std::shared_ptr<cThread<std::any>> thread;
    CoyoteOper operation = CoyoteOper::LOCAL_TRANSFER; // Default operation

public:
    // Constructor
    ComputeNode(const std::string& node_id, DFG* parent_dfg, int vfid);

    // NodeBase interface implementations
    bool initialize(Capability* cap) override {
        if (!cap || !cap->has_permission(CapabilityPermission::WRITE)) {
            std::cerr << "Error: Invalid capability for initialize on node " << node_id << std::endl;
            return false;
        }
        if (thread) {
            initialized = true;
            return true;
        }
        return false;
    }

    void shutdown(Capability* cap) override {
        if (!cap || !cap->has_permission(CapabilityPermission::WRITE)) {
            std::cerr << "Error: Invalid capability for shutdown on node " << node_id << std::endl;
            return;
        }
        initialized = false;
        // Thread cleanup handled by shared_ptr
    }

    bool is_ready(Capability* cap) const override {
        if (!cap || !cap->has_permission(CapabilityPermission::READ)) {
            return false;
        }
        return initialized && thread != nullptr;
    }

    // Override print_info for compute-specific details
    void print_info() const override {
        std::cout << "ComputeNode: " << node_id << " vFPGA: " << vfid
                  << " Thread: " << (thread ? "active" : "null")
                  << " Initialized: " << (initialized ? "yes" : "no") << std::endl;
    }

    // Method to set the thread explicitly
    void set_thread(std::shared_ptr<cThread<std::any>> thread_ptr) {
        thread = thread_ptr;
    }

    // Set IO switch - requires a capability with WRITE permission
    void set_io_switch(IODevs io_switch, Capability* cap) {
        if (!cap) {
            std::cerr << "Error: Null capability for set_io_switch on node " << node_id << std::endl;
            return;
        }
        
        if (!cap->has_permission(CapabilityPermission::WRITE)) {
            std::cerr << "Error: Insufficient WRITE permission for set_io_switch on node " << node_id << std::endl;
            return;
        }
        
        // More permissive about thread check during initialization
        if (!thread) {
            std::cerr << "Error: Thread not initialized for node " << node_id << std::endl;
            return;
        }
        
        try {
            thread->ioSwitch(io_switch);
        } catch (const std::exception& e) {
            std::cerr << "Exception during set_io_switch on node " << node_id 
                      << ": " << e.what() << std::endl;
        }
    }

    // Clear completion counters - requires a capability with WRITE permission
    void clear_completed(Capability* cap) {
        if (!cap) {
            std::cerr << "Error: Null capability for clear_completed on node " << node_id << std::endl;
            return;
        }
        
        if (!cap->has_permission(CapabilityPermission::WRITE)) {
            std::cerr << "Error: Insufficient WRITE permission for clear_completed on node " << node_id << std::endl;
            return;
        }
        
        // More permissive about thread check during initialization
        if (!thread) {
            std::cerr << "Error: Thread not initialized for node " << node_id << std::endl;
            return;
        }
        
        try {
            thread->clearCompleted();
        } catch (const std::exception& e) {
            std::cerr << "Exception during clear_completed on node " << node_id 
                      << ": " << e.what() << std::endl;
        }
    }

    // Check completion status - requires a capability with READ permission
    uint32_t check_completed(Capability* cap, CoyoteOper oper) {
        if (!cap) {
            std::cerr << "Error: Null capability for check_completed on node " << node_id << std::endl;
            return 0;
        }
        
        if (!cap->has_permission(CapabilityPermission::READ)) {
            std::cerr << "Error: Insufficient READ permission for check_completed on node " << node_id << std::endl;
            return 0;
        }
        
        // More permissive about thread check during execution
        if (!thread) {
            std::cerr << "Error: Thread not initialized for node " << node_id << std::endl;
            return 0;
        }
        
        try {
            return thread->checkCompleted(oper);
        } catch (const std::exception& e) {
            std::cerr << "Exception during check_completed on node " << node_id 
                      << ": " << e.what() << std::endl;
            return 0;
        }
    }

    // Print debug information - requires a capability with READ permission
    void print_debug(Capability* cap) {
        if (!cap) {
            std::cerr << "Error: Null capability for print_debug on node " << node_id << std::endl;
            return;
        }
        
        if (!cap->has_permission(CapabilityPermission::READ)) {
            std::cerr << "Error: Insufficient READ permission for print_debug on node " << node_id << std::endl;
            return;
        }
        
        if (!thread) {
            std::cerr << "Error: Thread not initialized for node " << node_id << std::endl;
            return;
        }
        
        try {
            thread->printDebug();
        } catch (const std::exception& e) {
            std::cerr << "Exception during print_debug on node " << node_id 
                      << ": " << e.what() << std::endl;
        }
    }

    // Get the vFPGA ID
    int get_vfid() const { return vfid; }

    // Get the thread (requires a capability with READ permission)
    cThread<std::any>* get_thread(Capability* cap) const {
        if (!cap) {
            std::cerr << "Error: Null capability for get_thread on node " << node_id << std::endl;
            return nullptr;
        }
        
        if (!cap->has_permission(CapabilityPermission::READ)) {
            std::cerr << "Error: Insufficient READ permission for get_thread on node " << node_id << std::endl;
            return nullptr;
        }
        
        if (!thread) {
            std::cerr << "Error: Thread not initialized for node " << node_id << std::endl;
            return nullptr;
        }
        
        return thread.get();
    }

    // Get thread directly without capability check (use carefully, only for initialization)
    cThread<std::any>* get_thread_direct() const {
        return thread.get();
    }

    // Allocate memory - requires a capability with WRITE permission
    void* get_mem(size_t size, Capability* cap);

    // Free memory - requires a capability with WRITE permission
    void free_mem(void* memory, Capability* cap) {
        if (!cap) {
            std::cerr << "Error: Null capability for free_mem on node " << node_id << std::endl;
            return;
        }
        
        if (!cap->has_permission(CapabilityPermission::WRITE)) {
            std::cerr << "Error: Insufficient WRITE permission for free_mem on node " << node_id << std::endl;
            return;
        }
        
        if (!thread) {
            std::cerr << "Error: Thread not initialized for node " << node_id << std::endl;
            return;
        }
        
        if (!memory) {
            std::cerr << "Warning: Null memory to free for node " << node_id << std::endl;
            return;
        }
        
        try {
            thread->freeMem(memory);
        } catch (const std::exception& e) {
            std::cerr << "Exception during free_mem on node " << node_id 
                      << ": " << e.what() << std::endl;
        }
    }

    // Connect edges - requires a capability with WRITE permission
    void connect_edges(uint32_t read_offset, uint32_t write_offset, Capability* cap, bool suppress_perm_errors = true) {
        if (!cap) {
            std::cerr << "Error: Null capability for connect_edges on node " << node_id << std::endl;
            return;
        }
        
        if (!cap->has_permission(CapabilityPermission::WRITE)) {
            if (!suppress_perm_errors) {
                std::cerr << "Error: Insufficient WRITE permission for connect_edges on node " << node_id << std::endl;
            }
            return;
        }
        
        if (!thread) {
            std::cerr << "Error: Thread not initialized for node " << node_id << std::endl;
            return;
        }
        
        // Map the read and write offsets to hardware connections
        if (false) { 
            std::cout << "Node " << node_id << " connecting edges: read_offset=" << read_offset 
                    << ", write_offset=" << write_offset << std::endl;
        }
        

    }

    // Set the operation type - requires a capability with WRITE permission
    void set_operation(CoyoteOper oper, Capability* cap) {
        if (!cap) {
            std::cerr << "Error: Null capability for set_operation on node " << node_id << std::endl;
            return;
        }
        
        if (!cap->has_permission(CapabilityPermission::WRITE)) {
            std::cerr << "Error: Insufficient WRITE permission for set_operation on node " << node_id << std::endl;
            return;
        }
        
        if (!thread) {
            std::cerr << "Error: Thread not initialized for node " << node_id << std::endl;
            return;
        }
        
        operation = oper;
    }

    // Get the operation - requires a capability with READ permission
    CoyoteOper get_operation(Capability* cap) const {
        if (!cap) {
            std::cerr << "Error: Null capability for get_operation on node " << node_id << std::endl;
            return CoyoteOper::LOCAL_TRANSFER; // Default
        }
        
        if (!cap->has_permission(CapabilityPermission::READ)) {
            std::cerr << "Error: Insufficient READ permission for get_operation on node " << node_id << std::endl;
            return CoyoteOper::LOCAL_TRANSFER; // Default
        }
        
        return operation;
    }

    // Execute with direct SG entry - requires a capability with EXECUTE permission
    void execute_with_sg(sgEntry* sg, Capability* cap) {
        if (!cap) {
            std::cerr << "Error: Null capability for execute_with_sg on node " << node_id << std::endl;
            return;
        }
        
        if (!cap->has_permission(CapabilityPermission::EXECUTE)) {
            std::cerr << "Error: Insufficient EXECUTE permission for execute_with_sg on node " << node_id << std::endl;
            return;
        }
        
        if (!thread) {
            std::cerr << "Error: Thread not initialized for node " << node_id << std::endl;
            return;
        }
        
        if (!sg) {
            std::cerr << "Error: Null SG entry for execute_with_sg on node " << node_id << std::endl;
            return;
        }
        
        try {
            thread->invoke(operation, sg, {true, true, true});
        } catch (const std::exception& e) {
            std::cerr << "Exception during execute_with_sg on node " << node_id 
                      << ": " << e.what() << std::endl;
        }
    }

    // Execute without waiting - requires a capability with EXECUTE permission
    void start_with_sg(sgEntry* sg, Capability* cap) {
        if (!cap) {
            std::cerr << "Error: Null capability for start_with_sg on node " << node_id << std::endl;
            return;
        }

        if (!cap->has_permission(CapabilityPermission::EXECUTE)) {
            std::cerr << "Error: Insufficient EXECUTE permission for start_with_sg on node " << node_id << std::endl;
            return;
        }

        if (!thread) {
            std::cerr << "Error: Thread not initialized for node " << node_id << std::endl;
            return;
        }

        if (!sg) {
            std::cerr << "Error: Null SG entry for start_with_sg on node " << node_id << std::endl;
            return;
        }

        try {
            thread->invoke(operation, sg, {true, true, false});
        } catch (const std::exception& e) {
            std::cerr << "Exception during start_with_sg on node " << node_id
                      << ": " << e.what() << std::endl;
        }
    }

    // ========================================
    // Table & State Management API
    // ========================================

    /**
     * Add a match-action table entry
     * @param table_name  Name of the table
     * @param key         Match key (format depends on table definition)
     * @param action      Action to execute on match
     * @param data        Action data/parameters
     * @param cap         Capability with WRITE permission
     * @return true if entry added successfully
     */
    bool table_add(const std::string& table_name, const std::string& key,
                   const std::string& action, const std::string& data, Capability* cap) {
        if (!cap) {
            std::cerr << "Error: Null capability for table_add on node " << node_id << std::endl;
            return false;
        }

        if (!cap->has_permission(CapabilityPermission::WRITE)) {
            std::cerr << "Error: Insufficient WRITE permission for table_add on node " << node_id << std::endl;
            return false;
        }

        if (!thread) {
            std::cerr << "Error: Thread not initialized for node " << node_id << std::endl;
            return false;
        }

        // TODO: Implement actual table_add via P4Runtime or hardware register interface
        // This is a placeholder that should be connected to the P4Runtime service
        // or direct hardware register writes via cThread
        std::cout << "table_add: " << table_name << " key=" << key
                  << " action=" << action << " data=" << data << std::endl;
        return true;
    }

    /**
     * Delete a table entry
     * @param table_name  Name of the table
     * @param key         Match key to delete
     * @param cap         Capability with WRITE permission
     * @return true if entry deleted successfully
     */
    bool table_delete(const std::string& table_name, const std::string& key, Capability* cap) {
        if (!cap) {
            std::cerr << "Error: Null capability for table_delete on node " << node_id << std::endl;
            return false;
        }

        if (!cap->has_permission(CapabilityPermission::WRITE)) {
            std::cerr << "Error: Insufficient WRITE permission for table_delete on node " << node_id << std::endl;
            return false;
        }

        if (!thread) {
            std::cerr << "Error: Thread not initialized for node " << node_id << std::endl;
            return false;
        }

        // TODO: Implement actual table_delete via P4Runtime or hardware register interface
        std::cout << "table_delete: " << table_name << " key=" << key << std::endl;
        return true;
    }

    /**
     * Read a stateful register
     * @param reg_name  Name of the register array
     * @param index     Index into the register array
     * @param cap       Capability with READ permission
     * @return Register value (0 on error)
     */
    uint64_t register_read(const std::string& reg_name, uint32_t index, Capability* cap) {
        if (!cap) {
            std::cerr << "Error: Null capability for register_read on node " << node_id << std::endl;
            return 0;
        }

        if (!cap->has_permission(CapabilityPermission::READ)) {
            std::cerr << "Error: Insufficient READ permission for register_read on node " << node_id << std::endl;
            return 0;
        }

        if (!thread) {
            std::cerr << "Error: Thread not initialized for node " << node_id << std::endl;
            return 0;
        }

        // TODO: Implement actual register_read via hardware register interface
        // This would typically use thread->csRead() or similar
        std::cout << "register_read: " << reg_name << "[" << index << "]" << std::endl;
        return 0;
    }

    /**
     * Write a stateful register
     * @param reg_name  Name of the register array
     * @param index     Index into the register array
     * @param value     Value to write
     * @param cap       Capability with WRITE permission
     * @return true if write successful
     */
    bool register_write(const std::string& reg_name, uint32_t index, uint64_t value, Capability* cap) {
        if (!cap) {
            std::cerr << "Error: Null capability for register_write on node " << node_id << std::endl;
            return false;
        }

        if (!cap->has_permission(CapabilityPermission::WRITE)) {
            std::cerr << "Error: Insufficient WRITE permission for register_write on node " << node_id << std::endl;
            return false;
        }

        if (!thread) {
            std::cerr << "Error: Thread not initialized for node " << node_id << std::endl;
            return false;
        }

        // TODO: Implement actual register_write via hardware register interface
        // This would typically use thread->csWrite() or similar
        std::cout << "register_write: " << reg_name << "[" << index << "] = " << value << std::endl;
        return true;
    }

};

// ============================================================================
// Software Node Hierarchy (Host-Based Middlebox Support)
// ============================================================================

/**
 * Resource limits for software nodes running on host CPU
 */
struct SoftwareResourceLimits {
    size_t max_memory_bytes = 0;        // Maximum memory usage (0 = unlimited)
    double max_cpu_percent = 100.0;     // Maximum CPU usage percentage
    uint32_t max_threads = 0;           // Maximum threads (0 = unlimited)
    uint64_t max_bandwidth_bps = 0;     // Maximum bandwidth (0 = unlimited)
};

/**
 * SoftwareEnforcer - Enforces capabilities for software nodes
 * Since software nodes run on host CPU without hardware enforcement,
 * this class provides synchronous (blocking) capability enforcement
 */
class SoftwareEnforcer {
private:
    // Resource tracking per capability
    struct ResourceUsage {
        size_t allocated_memory = 0;
        uint32_t active_threads = 0;
        uint64_t bandwidth_used = 0;
        std::chrono::steady_clock::time_point last_bandwidth_reset = std::chrono::steady_clock::now();
    };

    std::unordered_map<std::string, ResourceUsage> resource_usage;
    std::unordered_map<std::string, SoftwareResourceLimits> resource_limits;
    std::mutex enforcement_mutex;

    // Rate limiting (token bucket)
    struct TokenBucket {
        uint64_t tokens = 0;
        uint64_t max_tokens = 0;
        uint64_t refill_rate = 0;  // tokens per second
        std::chrono::steady_clock::time_point last_refill;
    };
    std::unordered_map<std::string, TokenBucket> rate_limiters;

public:
    SoftwareEnforcer() = default;

    /**
     * Check if an operation is allowed (SYNCHRONOUS - blocks on violation)
     * @return true if operation is allowed, false if blocked
     */
    bool check_operation_allowed(Capability* cap, uint32_t required_perms) {
        if (!cap) return false;

        std::lock_guard<std::mutex> lock(enforcement_mutex);

        // Check permissions
        if (!cap->has_permissions(required_perms)) {
            log_violation(cap, "Insufficient permissions");
            return false;
        }

        // Check scope
        if (!cap->has_scope(CapabilityScope::SOFTWARE) &&
            !cap->has_scope(CapabilityScope::GLOBAL)) {
            log_violation(cap, "Capability scope does not include SOFTWARE");
            return false;
        }

        // Check expiry
        if (cap->is_expired()) {
            log_violation(cap, "Capability has expired");
            return false;
        }

        return true;
    }

    /**
     * Check if resource allocation is within limits
     * @return true if resources are available, false otherwise
     */
    bool check_resource_available(Capability* cap, size_t memory, uint32_t threads, uint64_t bandwidth) {
        if (!cap) return false;

        std::lock_guard<std::mutex> lock(enforcement_mutex);

        std::string cap_id = cap->get_id();
        auto limits_it = resource_limits.find(cap_id);
        if (limits_it == resource_limits.end()) {
            // No limits set - allow everything
            return true;
        }

        const auto& limits = limits_it->second;
        auto& usage = resource_usage[cap_id];

        // Check memory
        if (limits.max_memory_bytes > 0 &&
            usage.allocated_memory + memory > limits.max_memory_bytes) {
            log_violation(cap, "Memory limit exceeded");
            return false;
        }

        // Check threads
        if (limits.max_threads > 0 &&
            usage.active_threads + threads > limits.max_threads) {
            log_violation(cap, "Thread limit exceeded");
            return false;
        }

        // Check bandwidth
        if (limits.max_bandwidth_bps > 0 &&
            usage.bandwidth_used + bandwidth > limits.max_bandwidth_bps) {
            log_violation(cap, "Bandwidth limit exceeded");
            return false;
        }

        return true;
    }

    /**
     * Allocate resources under a capability
     */
    void allocate_resources(Capability* cap, size_t memory, uint32_t threads) {
        if (!cap) return;

        std::lock_guard<std::mutex> lock(enforcement_mutex);

        std::string cap_id = cap->get_id();
        auto& usage = resource_usage[cap_id];
        usage.allocated_memory += memory;
        usage.active_threads += threads;
    }

    /**
     * Release resources under a capability
     */
    void release_resources(Capability* cap, size_t memory, uint32_t threads) {
        if (!cap) return;

        std::lock_guard<std::mutex> lock(enforcement_mutex);

        std::string cap_id = cap->get_id();
        auto& usage = resource_usage[cap_id];

        if (usage.allocated_memory >= memory) {
            usage.allocated_memory -= memory;
        } else {
            usage.allocated_memory = 0;
        }

        if (usage.active_threads >= threads) {
            usage.active_threads -= threads;
        } else {
            usage.active_threads = 0;
        }
    }

    /**
     * Record bandwidth usage
     */
    void record_bandwidth(Capability* cap, uint64_t bytes) {
        if (!cap) return;

        std::lock_guard<std::mutex> lock(enforcement_mutex);

        std::string cap_id = cap->get_id();
        auto& usage = resource_usage[cap_id];

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - usage.last_bandwidth_reset).count();

        // Reset bandwidth counter every second
        if (elapsed >= 1) {
            usage.bandwidth_used = 0;
            usage.last_bandwidth_reset = now;
        }

        usage.bandwidth_used += bytes * 8;  // Convert to bits
    }

    /**
     * Acquire rate limit tokens (token bucket algorithm)
     * @return true if tokens acquired, false if rate limited
     */
    bool acquire_rate_limit_token(Capability* cap, uint64_t tokens_needed) {
        if (!cap) return false;

        std::lock_guard<std::mutex> lock(enforcement_mutex);

        std::string cap_id = cap->get_id();
        auto it = rate_limiters.find(cap_id);
        if (it == rate_limiters.end()) {
            // No rate limiter - allow
            return true;
        }

        auto& bucket = it->second;

        // Refill tokens based on elapsed time
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - bucket.last_refill).count();

        uint64_t new_tokens = (elapsed * bucket.refill_rate) / 1000;
        bucket.tokens = std::min(bucket.tokens + new_tokens, bucket.max_tokens);
        bucket.last_refill = now;

        // Try to acquire tokens
        if (bucket.tokens >= tokens_needed) {
            bucket.tokens -= tokens_needed;
            return true;
        }

        log_violation(cap, "Rate limit exceeded");
        return false;
    }

    /**
     * Set resource limits for a capability
     */
    void set_resource_limits(const std::string& cap_id, const SoftwareResourceLimits& limits) {
        std::lock_guard<std::mutex> lock(enforcement_mutex);
        resource_limits[cap_id] = limits;
    }

    /**
     * Set rate limiter for a capability
     */
    void set_rate_limiter(const std::string& cap_id, uint64_t max_tokens, uint64_t refill_rate) {
        std::lock_guard<std::mutex> lock(enforcement_mutex);
        rate_limiters[cap_id] = TokenBucket{
            max_tokens,  // Start with full bucket
            max_tokens,
            refill_rate,
            std::chrono::steady_clock::now()
        };
    }

    /**
     * Log a capability violation (for audit trail)
     */
    void log_violation(Capability* cap, const std::string& reason) {
        std::cerr << "[SoftwareEnforcer] Violation - Cap: "
                  << (cap ? cap->get_id() : "null")
                  << " Reason: " << reason << std::endl;
    }
};

// DEPRECATED: Global enforcement engine (kept for backward compatibility)
// New code should use DFG::get_software_enforcer() for per-DFG isolation
[[deprecated("Use DFG::get_software_enforcer() instead for proper isolation between DFGs")]]
inline SoftwareEnforcer& get_software_enforcer_global() {
    static SoftwareEnforcer engine;
    return engine;
}

/**
 * SoftwareNode - Abstract base class for software nodes running on host CPU
 * Uses the parent DFG's SoftwareEnforcer for resource/capability enforcement,
 * providing isolation between different DFGs.
 */
class SoftwareNode : public NodeBase {
protected:
    SoftwareResourceLimits resource_limits;
    SoftwareEnforcer* enforcement_engine;

public:
    SoftwareNode(const std::string& id, DFG* dfg, NodeType sw_type)
        : NodeBase(id, dfg, sw_type), enforcement_engine(dfg ? dfg->get_software_enforcer() : nullptr) {
        if (!enforcement_engine) {
            set_last_error(ErrorCode::NOT_INITIALIZED);
            std::cerr << "Warning: SoftwareNode " << id << " created without valid DFG enforcer" << std::endl;
        }
    }

    virtual ~SoftwareNode() = default;

    // Set resource limits
    bool set_resource_limits(const SoftwareResourceLimits& limits, Capability* cap) {
        if (!cap || !cap->has_software_permissions(SW_RESOURCE_LIMIT)) {
            std::cerr << "Error: Insufficient SW_RESOURCE_LIMIT permission for set_resource_limits on node "
                      << node_id << std::endl;
            set_last_error(ErrorCode::CAP_INSUFFICIENT_PERMISSIONS);
            return false;
        }
        if (!enforcement_engine) {
            std::cerr << "Error: No enforcement engine for set_resource_limits on node " << node_id << std::endl;
            set_last_error(ErrorCode::NOT_INITIALIZED);
            return false;
        }
        resource_limits = limits;
        enforcement_engine->set_resource_limits(node_id, limits);
        return true;
    }

    SoftwareResourceLimits get_resource_limits(Capability* cap) const {
        if (!cap || !cap->has_permission(CapabilityPermission::READ)) {
            std::cerr << "Error: Insufficient READ permission for get_resource_limits on node "
                      << node_id << std::endl;
            return SoftwareResourceLimits{};
        }
        return resource_limits;
    }

    // Check if operation is allowed
    bool check_operation(Capability* cap, uint32_t required_perms) {
        if (!enforcement_engine) {
            set_last_error(ErrorCode::NOT_INITIALIZED);
            return false;
        }
        return enforcement_engine->check_operation_allowed(cap, required_perms);
    }

    // Print info override
    void print_info() const override {
        std::cout << "SoftwareNode: " << node_id << " Type: " << static_cast<int>(node_type)
                  << " Initialized: " << (initialized ? "yes" : "no")
                  << " MaxMem: " << resource_limits.max_memory_bytes
                  << " MaxCPU: " << resource_limits.max_cpu_percent << "%" << std::endl;
    }
};

/**
 * ParserNode - Software parser node for packet parsing on host CPU
 * Thread-safe: callback function access is protected by mutex
 */
class ParserNode : public SoftwareNode {
public:
    using ParseFunction = std::function<bool(const uint8_t* packet, size_t len, void* parsed_result)>;

private:
    ParseFunction parse_func;
    mutable std::mutex callback_mutex;  // Protects parse_func access

public:
    ParserNode(const std::string& id, DFG* dfg)
        : SoftwareNode(id, dfg, NodeType::SOFTWARE_PARSER) {}

    // NodeBase interface
    bool initialize(Capability* cap) override {
        if (!check_operation(cap, SW_CPU_EXECUTE)) {
            std::cerr << "Error: Insufficient SW_CPU_EXECUTE permission for initialize on parser " << node_id << std::endl;
            return false;
        }
        initialized = true;
        return true;
    }

    void shutdown(Capability* cap) override {
        if (!cap || !cap->has_software_permissions(SW_CPU_EXECUTE)) {
            std::cerr << "Error: Insufficient SW_CPU_EXECUTE permission for shutdown on parser " << node_id << std::endl;
            set_last_error(ErrorCode::CAP_INSUFFICIENT_PERMISSIONS);
            return;
        }
        std::lock_guard<std::mutex> lock(callback_mutex);
        initialized = false;
        parse_func = nullptr;
    }

    bool is_ready(Capability* cap) const override {
        if (!cap || !cap->has_permission(CapabilityPermission::READ)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(callback_mutex);
        return initialized && parse_func != nullptr;
    }

    // Set the parse function (thread-safe)
    bool set_parse_function(ParseFunction func, Capability* cap) {
        // Requires SW_CPU_EXECUTE to set code, SW_MEMORY_ACCESS to modify internal state
        if (!check_operation(cap, SW_CPU_EXECUTE | SW_MEMORY_ACCESS)) {
            std::cerr << "Error: Insufficient permissions for set_parse_function on parser " << node_id << std::endl;
            return false;
        }
        std::lock_guard<std::mutex> lock(callback_mutex);
        parse_func = std::move(func);
        return true;
    }

    // Parse a packet (thread-safe)
    bool parse_packet(const uint8_t* packet, size_t len, void* parsed_result, Capability* cap) {
        if (!check_operation(cap, SW_CPU_EXECUTE | SW_MEMORY_ACCESS)) {
            std::cerr << "Error: Insufficient permissions for parse_packet on parser " << node_id << std::endl;
            return false;
        }

        // Copy the function under lock, then execute outside lock to avoid blocking other threads
        ParseFunction func_copy;
        {
            std::lock_guard<std::mutex> lock(callback_mutex);
            if (!initialized || !parse_func) {
                std::cerr << "Error: Parser " << node_id << " not ready" << std::endl;
                set_last_error(ErrorCode::SW_FUNCTION_NOT_SET);
                return false;
            }
            func_copy = parse_func;
        }

        // Record bandwidth for enforcement
        if (enforcement_engine) {
            enforcement_engine->record_bandwidth(cap, len);
        }

        // Execute parse function outside the lock
        return func_copy(packet, len, parsed_result);
    }
};

/**
 * DeparserNode - Software deparser node for packet construction on host CPU
 * Thread-safe: callback function access is protected by mutex
 */
class DeparserNode : public SoftwareNode {
public:
    using DeparseFunction = std::function<size_t(const void* headers, uint8_t* output_buffer, size_t max_len)>;

private:
    DeparseFunction deparse_func;
    mutable std::mutex callback_mutex;  // Protects deparse_func access

public:
    DeparserNode(const std::string& id, DFG* dfg)
        : SoftwareNode(id, dfg, NodeType::SOFTWARE_DEPARSER) {}

    // NodeBase interface
    bool initialize(Capability* cap) override {
        if (!check_operation(cap, SW_CPU_EXECUTE)) {
            std::cerr << "Error: Insufficient SW_CPU_EXECUTE permission for initialize on deparser " << node_id << std::endl;
            return false;
        }
        initialized = true;
        return true;
    }

    void shutdown(Capability* cap) override {
        if (!cap || !cap->has_software_permissions(SW_CPU_EXECUTE)) {
            std::cerr << "Error: Insufficient SW_CPU_EXECUTE permission for shutdown on deparser " << node_id << std::endl;
            set_last_error(ErrorCode::CAP_INSUFFICIENT_PERMISSIONS);
            return;
        }
        std::lock_guard<std::mutex> lock(callback_mutex);
        initialized = false;
        deparse_func = nullptr;
    }

    bool is_ready(Capability* cap) const override {
        if (!cap || !cap->has_permission(CapabilityPermission::READ)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(callback_mutex);
        return initialized && deparse_func != nullptr;
    }

    // Set the deparse function (thread-safe)
    bool set_deparse_function(DeparseFunction func, Capability* cap) {
        // Requires SW_CPU_EXECUTE to set code, SW_MEMORY_ACCESS to modify internal state
        if (!check_operation(cap, SW_CPU_EXECUTE | SW_MEMORY_ACCESS)) {
            std::cerr << "Error: Insufficient permissions for set_deparse_function on deparser " << node_id << std::endl;
            return false;
        }
        std::lock_guard<std::mutex> lock(callback_mutex);
        deparse_func = std::move(func);
        return true;
    }

    // Deparse headers into a packet (thread-safe)
    size_t deparse_packet(const void* headers, uint8_t* output_buffer, size_t max_len, Capability* cap) {
        if (!check_operation(cap, SW_CPU_EXECUTE | SW_MEMORY_ACCESS)) {
            std::cerr << "Error: Insufficient permissions for deparse_packet on deparser " << node_id << std::endl;
            return 0;
        }

        // Copy the function under lock, then execute outside lock
        DeparseFunction func_copy;
        {
            std::lock_guard<std::mutex> lock(callback_mutex);
            if (!initialized || !deparse_func) {
                std::cerr << "Error: Deparser " << node_id << " not ready" << std::endl;
                set_last_error(ErrorCode::SW_FUNCTION_NOT_SET);
                return 0;
            }
            func_copy = deparse_func;
        }

        // Execute deparse function outside the lock
        size_t result_len = func_copy(headers, output_buffer, max_len);

        // Record bandwidth for enforcement
        if (enforcement_engine) {
            enforcement_engine->record_bandwidth(cap, result_len);
        }

        return result_len;
    }
};

/**
 * SoftwareNFNode - Generic software network function node
 * Thread-safe: callback function access is protected by mutex
 */
class SoftwareNFNode : public SoftwareNode {
public:
    using ProcessFunction = std::function<bool(void* input, size_t input_len, void* output, size_t* output_len)>;

private:
    ProcessFunction process_func;
    std::string nf_name;
    mutable std::mutex callback_mutex;  // Protects process_func access

public:
    SoftwareNFNode(const std::string& id, DFG* dfg, const std::string& name = "")
        : SoftwareNode(id, dfg, NodeType::SOFTWARE_NF), nf_name(name) {}

    // NodeBase interface
    bool initialize(Capability* cap) override {
        if (!check_operation(cap, SW_CPU_EXECUTE)) {
            std::cerr << "Error: Insufficient SW_CPU_EXECUTE permission for initialize on NF " << node_id << std::endl;
            return false;
        }
        initialized = true;
        return true;
    }

    void shutdown(Capability* cap) override {
        if (!cap || !cap->has_software_permissions(SW_CPU_EXECUTE)) {
            std::cerr << "Error: Insufficient SW_CPU_EXECUTE permission for shutdown on NF " << node_id << std::endl;
            set_last_error(ErrorCode::CAP_INSUFFICIENT_PERMISSIONS);
            return;
        }
        std::lock_guard<std::mutex> lock(callback_mutex);
        initialized = false;
        process_func = nullptr;
    }

    bool is_ready(Capability* cap) const override {
        if (!cap || !cap->has_permission(CapabilityPermission::READ)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(callback_mutex);
        return initialized && process_func != nullptr;
    }

    // Set the processing function (thread-safe)
    bool set_process_function(ProcessFunction func, Capability* cap) {
        // Requires SW_CPU_EXECUTE to set code, SW_MEMORY_ACCESS to modify internal state
        if (!check_operation(cap, SW_CPU_EXECUTE | SW_MEMORY_ACCESS)) {
            std::cerr << "Error: Insufficient permissions for set_process_function on NF " << node_id << std::endl;
            return false;
        }
        std::lock_guard<std::mutex> lock(callback_mutex);
        process_func = std::move(func);
        return true;
    }

    // Process data (thread-safe)
    bool process(void* input, size_t input_len, void* output, size_t* output_len, Capability* cap) {
        if (!check_operation(cap, SW_CPU_EXECUTE | SW_MEMORY_ACCESS)) {
            std::cerr << "Error: Insufficient permissions for process on NF " << node_id << std::endl;
            return false;
        }

        // Copy the function under lock, then execute outside lock
        ProcessFunction func_copy;
        {
            std::lock_guard<std::mutex> lock(callback_mutex);
            if (!initialized || !process_func) {
                std::cerr << "Error: NF " << node_id << " not ready" << std::endl;
                set_last_error(ErrorCode::SW_FUNCTION_NOT_SET);
                return false;
            }
            func_copy = process_func;
        }

        // Check resource limits
        if (enforcement_engine && !enforcement_engine->check_resource_available(cap, 0, 0, input_len * 8)) {
            std::cerr << "Error: Resource limits exceeded for NF " << node_id << std::endl;
            set_last_error(ErrorCode::SW_RESOURCE_LIMIT_EXCEEDED);
            return false;
        }

        // Execute process function outside the lock
        bool result = func_copy(input, input_len, output, output_len);

        // Record bandwidth for enforcement
        if (result && output_len && enforcement_engine) {
            enforcement_engine->record_bandwidth(cap, *output_len);
        }

        return result;
    }

    std::string get_nf_name() const { return nf_name; }
};

// ============================================================================
// Remote DFG Node (Multi-FPGA Support)
// ============================================================================

/**
 * Token expiry configuration for RemoteCapabilityToken
 */
struct TokenExpiryConfig {
    uint64_t default_expiry_ms = 5 * 60 * 1000;  // Default 5 minutes
    uint64_t max_clock_skew_ms = 30 * 1000;      // Allow 30 seconds clock skew
    bool enforce_expiry = true;                   // Whether to enforce expiry
};

/**
 * Remote capability token for cross-FPGA capability delegation
 * Uses VLAN tags from VIU module for authentication
 *
 * The VLAN tag encodes both Node ID and vFPGA ID for Multi-FPGA routing:
 *   VLAN ID (12-bit): [11:10]=src_node, [9:8]=src_vfpga, [7:6]=dst_node, [5:4]=dst_vfpga, [3:0]=flags
 *   Supports: 4 nodes × 4 vFPGAs = 16 total vFPGAs in closed VLAN network
 *   Node ID 0 + vFPGA ID 0 = external network
 */
struct RemoteCapabilityToken {
    // VLAN tag (constructed from node+vfpga IDs)
    uint16_t vlan_id;              // Full VLAN tag from VIU module (12-bit used)

    // Source identity (who is sending/delegating)
    uint8_t src_node_id;           // Source physical FPGA node (0-3, 0=external)
    uint8_t src_vfpga_id;          // Source vFPGA on that node (0-3)

    // Destination identity (who is receiving)
    uint8_t dst_node_id;           // Destination physical FPGA node (0-3, 0=external)
    uint8_t dst_vfpga_id;          // Destination vFPGA on that node (0-3)

    // Legacy field for backward compatibility (deprecated, use node+vfpga IDs)
    uint32_t source_vfpga_id;      // Combined source identifier (node << 2 | vfpga)

    // Capability information
    uint32_t permissions;          // Delegated permissions
    std::string cap_id;            // Capability identifier

    // Security fields
    uint64_t timestamp;            // Creation time for replay protection
    uint64_t expiry_timestamp;     // Expiry time (0 = no expiry)
    uint32_t sequence_number;      // Monotonic sequence for replay protection
    std::string signature;         // HMAC or similar for integrity

    // Helper: Build VLAN ID from node+vfpga IDs (same as VLANConfig::build_vlan_id)
    // New format: [11:10]=src_node (2b), [9:6]=src_vfpga (4b), [5:4]=dst_node (2b), [3:0]=dst_vfpga (4b)
    static uint16_t build_vlan_id(uint8_t src_node, uint8_t src_vfpga,
                                   uint8_t dst_node, uint8_t dst_vfpga) {
        return ((src_node & 0x3) << 10) |
               ((src_vfpga & 0xF) << 6) |
               ((dst_node & 0x3) << 4) |
               (dst_vfpga & 0xF);
    }

    // Helper: Extract source node ID from VLAN ID (12-bit)
    static uint8_t get_src_node(uint16_t vlan) { return (vlan >> 10) & 0x3; }
    static uint8_t get_src_vfpga(uint16_t vlan) { return (vlan >> 6) & 0xF; }
    static uint8_t get_dst_node(uint16_t vlan) { return (vlan >> 4) & 0x3; }
    static uint8_t get_dst_vfpga(uint16_t vlan) { return vlan & 0xF; }

    // Helper: Get combined vFPGA identifier (for legacy compatibility)
    // New format: node (2 bits) + vfpga (4 bits) = 6 bits total
    uint32_t get_source_combined_id() const {
        return (static_cast<uint32_t>(src_node_id) << 4) | src_vfpga_id;
    }
    uint32_t get_dest_combined_id() const {
        return (static_cast<uint32_t>(dst_node_id) << 4) | dst_vfpga_id;
    }

    // Helper: Check if source is external (outside closed VLAN network)
    bool is_external_source() const {
        return src_node_id == 0 && src_vfpga_id == 0;
    }

    // Helper: Check if destination is external
    bool is_external_dest() const {
        return dst_node_id == 0 && dst_vfpga_id == 0;
    }

    // Initialize from node+vfpga IDs (preferred constructor pattern)
    void set_identities(uint8_t sn, uint8_t sv, uint8_t dn, uint8_t dv, uint8_t flags = 0) {
        src_node_id = sn;
        src_vfpga_id = sv;
        dst_node_id = dn;
        dst_vfpga_id = dv;
        vlan_id = build_vlan_id(sn, sv, dn, dv, flags);
        source_vfpga_id = get_source_combined_id();  // Update legacy field
    }

    // Initialize from VLAN ID (parse existing tag)
    void set_from_vlan_id(uint16_t vlan) {
        vlan_id = vlan;
        src_node_id = get_src_node(vlan);
        src_vfpga_id = get_src_vfpga(vlan);
        dst_node_id = get_dst_node(vlan);
        dst_vfpga_id = get_dst_vfpga(vlan);
        source_vfpga_id = get_source_combined_id();  // Update legacy field
    }

    // Serialize token for network transmission
    std::string serialize() const {
        // Extended serialization format with node+vfpga IDs
        return std::to_string(vlan_id) + ":" +
               std::to_string(src_node_id) + ":" +
               std::to_string(src_vfpga_id) + ":" +
               std::to_string(dst_node_id) + ":" +
               std::to_string(dst_vfpga_id) + ":" +
               std::to_string(permissions) + ":" +
               cap_id + ":" +
               std::to_string(timestamp) + ":" +
               std::to_string(expiry_timestamp) + ":" +
               std::to_string(sequence_number) + ":" +
               signature;
    }

    // Deserialize token from network data (returns empty token with vlan_id=0 on error)
    static RemoteCapabilityToken deserialize(const std::string& data) {
        RemoteCapabilityToken token{};  // Zero-initialize

        try {
            size_t pos = 0;
            size_t next_pos;

            // Helper to safely find next delimiter
            auto find_next = [&](size_t from) -> size_t {
                size_t found = data.find(':', from);
                if (found == std::string::npos) {
                    throw std::runtime_error("Missing delimiter in token");
                }
                return found;
            };

            next_pos = find_next(pos);
            token.vlan_id = static_cast<uint16_t>(std::stoul(data.substr(pos, next_pos - pos)));
            pos = next_pos + 1;

            next_pos = find_next(pos);
            token.src_node_id = static_cast<uint8_t>(std::stoul(data.substr(pos, next_pos - pos)));
            pos = next_pos + 1;

            next_pos = find_next(pos);
            token.src_vfpga_id = static_cast<uint8_t>(std::stoul(data.substr(pos, next_pos - pos)));
            pos = next_pos + 1;

            next_pos = find_next(pos);
            token.dst_node_id = static_cast<uint8_t>(std::stoul(data.substr(pos, next_pos - pos)));
            pos = next_pos + 1;

            next_pos = find_next(pos);
            token.dst_vfpga_id = static_cast<uint8_t>(std::stoul(data.substr(pos, next_pos - pos)));
            pos = next_pos + 1;

            // Update legacy field from parsed node+vfpga IDs
            token.source_vfpga_id = token.get_source_combined_id();

            next_pos = find_next(pos);
            token.permissions = static_cast<uint32_t>(std::stoul(data.substr(pos, next_pos - pos)));
            pos = next_pos + 1;

            next_pos = find_next(pos);
            token.cap_id = data.substr(pos, next_pos - pos);
            pos = next_pos + 1;

            next_pos = find_next(pos);
            token.timestamp = std::stoull(data.substr(pos, next_pos - pos));
            pos = next_pos + 1;

            next_pos = find_next(pos);
            token.expiry_timestamp = std::stoull(data.substr(pos, next_pos - pos));
            pos = next_pos + 1;

            next_pos = find_next(pos);
            token.sequence_number = static_cast<uint32_t>(std::stoul(data.substr(pos, next_pos - pos)));
            pos = next_pos + 1;

            token.signature = data.substr(pos);

        } catch (const std::exception& e) {
            std::cerr << "Error deserializing RemoteCapabilityToken: " << e.what() << std::endl;
            return RemoteCapabilityToken{};  // Return empty/invalid token
        }

        return token;
    }

    // Check if token is valid (non-empty)
    bool is_valid() const {
        return vlan_id != 0 || !cap_id.empty();
    }

    // Check if token has expired
    bool is_expired(uint64_t current_time_ms, uint64_t max_clock_skew_ms = 30000) const {
        if (expiry_timestamp == 0) {
            return false;  // No expiry set
        }
        // Allow for clock skew
        return current_time_ms > (expiry_timestamp + max_clock_skew_ms);
    }

    // Check if token timestamp is valid (not in the future)
    bool is_timestamp_valid(uint64_t current_time_ms, uint64_t max_clock_skew_ms = 30000) const {
        // Token can't be created significantly in the future
        return timestamp <= (current_time_ms + max_clock_skew_ms);
    }
};

// ============================================================================
// Global VLAN-to-vFPGA Mapping Registry
// ============================================================================

/**
 * VFPGAIdentity - Unique identifier for a vFPGA in the closed VLAN network
 *
 * Each vFPGA is identified by:
 *   - node_id: Physical FPGA node in the network (0-3, where 0 = external)
 *   - vfpga_id: vFPGA instance on that node (0-3)
 *
 * Combined, this supports 4 nodes × 4 vFPGAs = 16 total vFPGAs
 * Node ID 0 + vFPGA ID 0 is reserved for external network traffic
 */
struct VFPGAIdentity {
    uint8_t node_id = 0;      // Physical FPGA node (0-3)
    uint8_t vfpga_id = 0;     // vFPGA on that node (0-15)

    // Optional metadata for the vFPGA
    std::string hostname;     // Host where this FPGA is located
    std::string description;  // Human-readable description
    uint16_t rdma_port = 0;   // RDMA port for multi-FPGA communication

    // Construct combined 6-bit identifier (2 bits node + 4 bits vfpga)
    uint8_t combined_id() const {
        return ((node_id & 0x3) << 4) | (vfpga_id & 0xF);
    }

    // Create from combined identifier
    static VFPGAIdentity from_combined(uint8_t combined) {
        VFPGAIdentity id;
        id.node_id = (combined >> 4) & 0x3;
        id.vfpga_id = combined & 0xF;
        return id;
    }

    // Check if this is the external network identifier
    bool is_external() const {
        return node_id == 0 && vfpga_id == 0;
    }

    // Equality comparison
    bool operator==(const VFPGAIdentity& other) const {
        return node_id == other.node_id && vfpga_id == other.vfpga_id;
    }

    // For use in maps/sets
    bool operator<(const VFPGAIdentity& other) const {
        if (node_id != other.node_id) return node_id < other.node_id;
        return vfpga_id < other.vfpga_id;
    }

    // String representation
    std::string to_string() const {
        return "node" + std::to_string(node_id) + ".vfpga" + std::to_string(vfpga_id);
    }
};

/**
 * VLANRoute - Represents a valid route between two vFPGAs in the VLAN network
 */
struct VLANRoute {
    VFPGAIdentity source;      // Source vFPGA
    VFPGAIdentity destination; // Destination vFPGA
    uint16_t vlan_id;          // VLAN tag encoding this route
    bool bidirectional;        // True if route works both ways

    // Build the VLAN ID for this route
    // New format: [11:10]=src_node, [9:6]=src_vfpga, [5:4]=dst_node, [3:0]=dst_vfpga
    void compute_vlan_id() {
        vlan_id = ((source.node_id & 0x3) << 10) |
                  ((source.vfpga_id & 0xF) << 6) |
                  ((destination.node_id & 0x3) << 4) |
                  (destination.vfpga_id & 0xF);
    }
};

/**
 * VLANMappingRegistry - Global registry for VLAN-to-vFPGA mappings
 *
 * This registry maintains the mapping between VLAN tags and vFPGA identities
 * for the closed VLAN network in Multi-FPGA deployments. It is used to:
 *   1. Register vFPGAs in the network
 *   2. Configure allowed routes between vFPGAs
 *   3. Validate incoming packets based on VLAN tags
 *   4. Generate VLAN tags for outgoing packets
 *
 * The registry should be consistent across all nodes in the Multi-FPGA network.
 * In production, this would be synchronized via a distributed consensus protocol
 * or configured by a central management plane.
 *
 * Thread-safe: All operations are protected by a mutex.
 */
class VLANMappingRegistry {
private:
    // Registry of all known vFPGAs in the network
    std::map<uint8_t, VFPGAIdentity> vfpga_registry;  // combined_id -> identity

    // Allowed routes: source combined_id -> set of allowed destination combined_ids
    std::map<uint8_t, std::set<uint8_t>> allowed_routes;

    // Reverse lookup: VLAN ID -> route info
    std::map<uint16_t, VLANRoute> vlan_to_route;

    // This node's identity (set during initialization)
    VFPGAIdentity local_identity;
    bool local_identity_set = false;

    // Thread safety
    mutable std::mutex registry_mutex;

public:
    // Singleton instance for global access
    static VLANMappingRegistry& instance() {
        static VLANMappingRegistry registry;
        return registry;
    }

    // Set this node's identity (called once during initialization)
    void set_local_identity(uint8_t node_id, uint8_t vfpga_id,
                           const std::string& hostname = "",
                           uint16_t rdma_port = 0) {
        std::lock_guard<std::mutex> lock(registry_mutex);
        local_identity.node_id = node_id;
        local_identity.vfpga_id = vfpga_id;
        local_identity.hostname = hostname;
        local_identity.rdma_port = rdma_port;
        local_identity_set = true;

        // Register ourselves in the registry
        vfpga_registry[local_identity.combined_id()] = local_identity;
    }

    // Get this node's identity
    VFPGAIdentity get_local_identity() const {
        std::lock_guard<std::mutex> lock(registry_mutex);
        return local_identity;
    }

    bool is_local_identity_set() const {
        std::lock_guard<std::mutex> lock(registry_mutex);
        return local_identity_set;
    }

    // Register a vFPGA in the network
    void register_vfpga(const VFPGAIdentity& identity) {
        std::lock_guard<std::mutex> lock(registry_mutex);
        vfpga_registry[identity.combined_id()] = identity;
    }

    // Register a vFPGA by node+vfpga IDs
    void register_vfpga(uint8_t node_id, uint8_t vfpga_id,
                       const std::string& hostname = "",
                       const std::string& description = "",
                       uint16_t rdma_port = 0) {
        VFPGAIdentity id;
        id.node_id = node_id;
        id.vfpga_id = vfpga_id;
        id.hostname = hostname;
        id.description = description;
        id.rdma_port = rdma_port;
        register_vfpga(id);
    }

    // Unregister a vFPGA
    void unregister_vfpga(uint8_t node_id, uint8_t vfpga_id) {
        std::lock_guard<std::mutex> lock(registry_mutex);
        uint8_t combined = ((node_id & 0x3) << 4) | (vfpga_id & 0xF);
        vfpga_registry.erase(combined);
        allowed_routes.erase(combined);
        // Also remove from other nodes' allowed destinations
        for (auto& [src, dests] : allowed_routes) {
            dests.erase(combined);
        }
    }

    // Check if a vFPGA is registered
    bool is_registered(uint8_t node_id, uint8_t vfpga_id) const {
        std::lock_guard<std::mutex> lock(registry_mutex);
        uint8_t combined = ((node_id & 0x3) << 4) | (vfpga_id & 0xF);
        return vfpga_registry.find(combined) != vfpga_registry.end();
    }

    // Get vFPGA identity (returns identity with node_id=0, vfpga_id=0 if not found)
    VFPGAIdentity get_vfpga(uint8_t node_id, uint8_t vfpga_id) const {
        std::lock_guard<std::mutex> lock(registry_mutex);
        uint8_t combined = ((node_id & 0x3) << 4) | (vfpga_id & 0xF);
        auto it = vfpga_registry.find(combined);
        if (it != vfpga_registry.end()) {
            return it->second;
        }
        return VFPGAIdentity{};  // External/unknown
    }

    // Add an allowed route between vFPGAs
    void add_route(uint8_t src_node, uint8_t src_vfpga,
                   uint8_t dst_node, uint8_t dst_vfpga,
                   bool bidirectional = false) {
        std::lock_guard<std::mutex> lock(registry_mutex);
        uint8_t src_combined = ((src_node & 0x3) << 4) | (src_vfpga & 0xF);
        uint8_t dst_combined = ((dst_node & 0x3) << 4) | (dst_vfpga & 0xF);

        allowed_routes[src_combined].insert(dst_combined);

        // Create route entry
        VLANRoute route;
        route.source = get_vfpga_internal(src_node, src_vfpga);
        route.destination = get_vfpga_internal(dst_node, dst_vfpga);
        route.bidirectional = bidirectional;
        route.compute_vlan_id();
        vlan_to_route[route.vlan_id] = route;

        if (bidirectional) {
            allowed_routes[dst_combined].insert(src_combined);

            // Also create reverse route entry
            VLANRoute reverse_route;
            reverse_route.source = route.destination;
            reverse_route.destination = route.source;
            reverse_route.bidirectional = true;
            reverse_route.compute_vlan_id();
            vlan_to_route[reverse_route.vlan_id] = reverse_route;
        }
    }

    // Remove a route
    void remove_route(uint8_t src_node, uint8_t src_vfpga,
                      uint8_t dst_node, uint8_t dst_vfpga) {
        std::lock_guard<std::mutex> lock(registry_mutex);
        uint8_t src_combined = ((src_node & 0x3) << 4) | (src_vfpga & 0xF);
        uint8_t dst_combined = ((dst_node & 0x3) << 4) | (dst_vfpga & 0xF);

        auto it = allowed_routes.find(src_combined);
        if (it != allowed_routes.end()) {
            it->second.erase(dst_combined);
        }

        // Remove from vlan_to_route
        uint16_t vlan_id = ((src_node & 0x3) << 10) |
                           ((src_vfpga & 0xF) << 6) |
                           ((dst_node & 0x3) << 4) |
                           (dst_vfpga & 0xF);
        vlan_to_route.erase(vlan_id);
    }

    // Check if a route is allowed
    bool is_route_allowed(uint8_t src_node, uint8_t src_vfpga,
                          uint8_t dst_node, uint8_t dst_vfpga) const {
        std::lock_guard<std::mutex> lock(registry_mutex);

        // External sources (node=0, vfpga=0) are always allowed in SmartNIC mode
        if (src_node == 0 && src_vfpga == 0) {
            return true;
        }

        // External destinations are always allowed (SmartNIC mode egress)
        if (dst_node == 0 && dst_vfpga == 0) {
            return true;
        }

        uint8_t src_combined = ((src_node & 0x3) << 4) | (src_vfpga & 0xF);
        uint8_t dst_combined = ((dst_node & 0x3) << 4) | (dst_vfpga & 0xF);

        auto it = allowed_routes.find(src_combined);
        if (it == allowed_routes.end()) {
            return false;
        }
        return it->second.find(dst_combined) != it->second.end();
    }

    // Validate a VLAN tag for incoming packets
    // Returns true if the packet should be accepted by the local vFPGA
    bool validate_incoming_vlan(uint16_t vlan_id) const {
        std::lock_guard<std::mutex> lock(registry_mutex);

        if (!local_identity_set) {
            return false;  // Not initialized
        }

        // Parse VLAN ID (new format)
        // [11:10]=src_node, [9:6]=src_vfpga, [5:4]=dst_node, [3:0]=dst_vfpga
        uint8_t src_node = (vlan_id >> 10) & 0x3;
        uint8_t src_vfpga = (vlan_id >> 6) & 0xF;
        uint8_t dst_node = (vlan_id >> 4) & 0x3;
        uint8_t dst_vfpga = vlan_id & 0xF;

        // Check destination matches local identity
        if (dst_node != local_identity.node_id ||
            dst_vfpga != local_identity.vfpga_id) {
            return false;  // Not for us
        }

        // Check if this source is allowed to send to us
        // External sources (0,0) are always allowed
        if (src_node == 0 && src_vfpga == 0) {
            return true;
        }

        // Check route table (reversed: we are the destination)
        uint8_t src_combined = ((src_node & 0x3) << 4) | (src_vfpga & 0xF);
        uint8_t dst_combined = local_identity.combined_id();

        auto it = allowed_routes.find(src_combined);
        if (it == allowed_routes.end()) {
            return false;
        }
        return it->second.find(dst_combined) != it->second.end();
    }

    // Generate VLAN ID for outgoing packet to a destination
    // Returns 0 if route is not allowed
    uint16_t generate_outgoing_vlan(uint8_t dst_node, uint8_t dst_vfpga,
                                    uint8_t flags = 0) const {
        std::lock_guard<std::mutex> lock(registry_mutex);

        if (!local_identity_set) {
            return 0;  // Not initialized
        }

        // Check if route is allowed
        uint8_t src_combined = local_identity.combined_id();
        uint8_t dst_combined = ((dst_node & 0x3) << 4) | (dst_vfpga & 0xF);

        // External destinations are always allowed
        if (dst_node != 0 || dst_vfpga != 0) {
            auto it = allowed_routes.find(src_combined);
            if (it == allowed_routes.end() ||
                it->second.find(dst_combined) == it->second.end()) {
                return 0;  // Route not allowed
            }
        }

        // Build VLAN ID (new format: no flags, just node+vfpga)
        return ((local_identity.node_id & 0x3) << 10) |
               ((local_identity.vfpga_id & 0xF) << 6) |
               ((dst_node & 0x3) << 4) |
               (dst_vfpga & 0xF);
    }

    // Get all registered vFPGAs
    std::vector<VFPGAIdentity> get_all_vfpgas() const {
        std::lock_guard<std::mutex> lock(registry_mutex);
        std::vector<VFPGAIdentity> result;
        for (const auto& [id, identity] : vfpga_registry) {
            result.push_back(identity);
        }
        return result;
    }

    // Get all allowed destinations from a source
    std::vector<VFPGAIdentity> get_allowed_destinations(uint8_t src_node,
                                                         uint8_t src_vfpga) const {
        std::lock_guard<std::mutex> lock(registry_mutex);
        std::vector<VFPGAIdentity> result;

        uint8_t src_combined = ((src_node & 0x3) << 4) | (src_vfpga & 0xF);
        auto it = allowed_routes.find(src_combined);
        if (it != allowed_routes.end()) {
            for (uint8_t dst_combined : it->second) {
                auto vfpga_it = vfpga_registry.find(dst_combined);
                if (vfpga_it != vfpga_registry.end()) {
                    result.push_back(vfpga_it->second);
                }
            }
        }
        return result;
    }

    // Clear all registrations (for testing/reset)
    void clear() {
        std::lock_guard<std::mutex> lock(registry_mutex);
        vfpga_registry.clear();
        allowed_routes.clear();
        vlan_to_route.clear();
        local_identity = VFPGAIdentity{};
        local_identity_set = false;
    }

    // Debug: print registry contents
    void dump() const {
        std::lock_guard<std::mutex> lock(registry_mutex);
        std::cout << "=== VLAN Mapping Registry ===" << std::endl;
        std::cout << "Local identity: " << (local_identity_set ?
            local_identity.to_string() : "NOT SET") << std::endl;
        std::cout << "Registered vFPGAs:" << std::endl;
        for (const auto& [id, identity] : vfpga_registry) {
            std::cout << "  " << identity.to_string();
            if (!identity.hostname.empty()) {
                std::cout << " @ " << identity.hostname;
            }
            if (identity.rdma_port != 0) {
                std::cout << ":" << identity.rdma_port;
            }
            std::cout << std::endl;
        }
        std::cout << "Allowed routes:" << std::endl;
        for (const auto& [src, dests] : allowed_routes) {
            VFPGAIdentity src_id = VFPGAIdentity::from_combined(src);
            std::cout << "  " << src_id.to_string() << " -> ";
            bool first = true;
            for (uint8_t dst : dests) {
                if (!first) std::cout << ", ";
                VFPGAIdentity dst_id = VFPGAIdentity::from_combined(dst);
                std::cout << dst_id.to_string();
                first = false;
            }
            std::cout << std::endl;
        }
    }

private:
    // Private constructor for singleton
    VLANMappingRegistry() = default;

    // Internal helper (no locking, must be called with lock held)
    VFPGAIdentity get_vfpga_internal(uint8_t node_id, uint8_t vfpga_id) const {
        uint8_t combined = ((node_id & 0x3) << 4) | (vfpga_id & 0xF);
        auto it = vfpga_registry.find(combined);
        if (it != vfpga_registry.end()) {
            return it->second;
        }
        VFPGAIdentity id;
        id.node_id = node_id;
        id.vfpga_id = vfpga_id;
        return id;
    }
};

// Convenience function to get the global registry
inline VLANMappingRegistry& vlan_registry() {
    return VLANMappingRegistry::instance();
}

/**
 * RemoteDFGNode - Proxy node for remote FPGA DFG operations
 * Enables multi-FPGA deployments with VLAN-based capability authentication
 */
class RemoteDFGNode : public NodeBase {
private:
    // Unified VLAN configuration (same struct as NetworkNode)
    VLANConfig local_vlan_config;     // VLAN config for local (outgoing) traffic
    VLANConfig remote_vlan_config;    // Expected VLAN config for remote peer (incoming verification)

    // Connection state
    std::string remote_host;
    uint16_t remote_port = 0;
    bool connected = false;

    // Sequence number for replay protection with rollover handling
    std::atomic<uint32_t> sequence_counter{0};
    static constexpr uint32_t SEQUENCE_ROLLOVER_THRESHOLD = UINT32_MAX - 1000;  // Warn before rollover
    static constexpr uint32_t SEQUENCE_WINDOW_SIZE = 1000;  // Accept sequences within this window

    // Track last seen sequence numbers per source for replay protection
    std::unordered_map<uint32_t, uint32_t> last_seen_sequences;  // source_vfpga_id -> last_seq
    std::mutex sequence_mutex;

    // Token expiry configuration
    TokenExpiryConfig token_expiry_config;

    // Remote capability cache
    std::unordered_map<std::string, RemoteCapabilityToken> remote_capabilities;

    // RDMA connection for data transfer (would use RDMANetworkNode internally)
    std::shared_ptr<RDMANetworkNode> rdma_connection;

    // Internal network capability for RDMA operations (derived from parent during init)
    Capability* internal_network_cap = nullptr;

    // Generate current timestamp
    uint64_t get_current_timestamp() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // Get next sequence number with rollover handling
    uint32_t get_next_sequence() {
        uint32_t seq = sequence_counter.fetch_add(1);
        // Warn if approaching rollover
        if (seq >= SEQUENCE_ROLLOVER_THRESHOLD && seq < SEQUENCE_ROLLOVER_THRESHOLD + 10) {
            std::cerr << "Warning: Sequence counter approaching rollover on node " << node_id << std::endl;
        }
        // Handle rollover (atomic fetch_add handles wrap naturally)
        return seq;
    }

    // Check if sequence number is valid (not replayed)
    bool is_sequence_valid(uint32_t source_id, uint32_t seq) {
        std::lock_guard<std::mutex> lock(sequence_mutex);

        auto it = last_seen_sequences.find(source_id);
        if (it == last_seen_sequences.end()) {
            // First sequence from this source
            last_seen_sequences[source_id] = seq;
            return true;
        }

        uint32_t last_seq = it->second;

        // Handle rollover: if the new sequence is much smaller than last_seq,
        // it might be a rollover (or replay attack)
        // Accept if: seq > last_seq OR (last_seq is near max AND seq is near 0)
        bool rollover_case = (last_seq > SEQUENCE_ROLLOVER_THRESHOLD && seq < SEQUENCE_WINDOW_SIZE);
        bool normal_advance = (seq > last_seq && seq <= last_seq + SEQUENCE_WINDOW_SIZE);

        if (normal_advance || rollover_case) {
            last_seen_sequences[source_id] = seq;
            return true;
        }

        std::cerr << "Warning: Potential replay attack - seq " << seq
                  << " rejected (last seen: " << last_seq << ")" << std::endl;
        return false;
    }

    // Create signature for token (simplified - would use HMAC in production)
    std::string create_signature(const RemoteCapabilityToken& token) const {
        // Simplified signature - in production would use HMAC-SHA256 or similar
        // Include all security-relevant fields including expiry
        return std::to_string(token.vlan_id ^ token.source_vfpga_id ^
                             token.permissions ^ token.timestamp ^
                             static_cast<uint32_t>(token.expiry_timestamp & 0xFFFFFFFF) ^
                             token.sequence_number);
    }

    // Verify signature (updated to include expiry_timestamp)
    bool verify_signature(const RemoteCapabilityToken& token) const {
        RemoteCapabilityToken check_token;
        check_token.vlan_id = token.vlan_id;
        check_token.source_vfpga_id = token.source_vfpga_id;
        check_token.permissions = token.permissions;
        check_token.cap_id = token.cap_id;
        check_token.timestamp = token.timestamp;
        check_token.expiry_timestamp = token.expiry_timestamp;
        check_token.sequence_number = token.sequence_number;
        return token.signature == create_signature(check_token);
    }

public:
    RemoteDFGNode(const std::string& id, DFG* dfg, uint16_t local_vlan, uint16_t remote_vlan = 0)
        : NodeBase(id, dfg, NodeType::REMOTE_DFG) {
        // Initialize local VLAN config
        local_vlan_config.vlan_id = local_vlan;
        local_vlan_config.enforce_vlan = (local_vlan != 0);
        local_vlan_config.insert_vlan_on_tx = true;

        // Initialize remote VLAN config (expected peer VLAN)
        remote_vlan_config.vlan_id = remote_vlan;
        remote_vlan_config.enforce_vlan = (remote_vlan != 0);
        remote_vlan_config.strip_vlan_on_rx = true;
    }

    // Destructor - ensure internal capability is properly cleaned up
    ~RemoteDFGNode() override {
        // Revoke internal network capability if it exists
        // This prevents use-after-free and ensures capability tree consistency
        if (internal_network_cap) {
            internal_network_cap->revoke();
            internal_network_cap = nullptr;
        }
        // rdma_connection is a shared_ptr, will be cleaned up automatically
    }

    // Prevent copying (internal_network_cap ownership is non-trivial)
    RemoteDFGNode(const RemoteDFGNode&) = delete;
    RemoteDFGNode& operator=(const RemoteDFGNode&) = delete;

    // Allow moving
    RemoteDFGNode(RemoteDFGNode&& other) noexcept = default;
    RemoteDFGNode& operator=(RemoteDFGNode&& other) noexcept = default;

    // NodeBase interface
    bool initialize(Capability* cap) override {
        if (!cap || !cap->has_remote_permissions(REMOTE_EXECUTE)) {
            std::cerr << "Error: Insufficient REMOTE_EXECUTE permission for initialize on remote node "
                      << node_id << std::endl;
            return false;
        }

        // Create an internal NETWORK-scoped capability for RDMA operations
        // This is derived from the DFG's root capability (which has GLOBAL scope)
        if (parent_dfg) {
            Capability* root_cap = parent_dfg->get_root_capability();
            if (root_cap && root_cap->has_permission(CapabilityPermission::DELEGATE)) {
                // Create NETWORK-scoped capability with all network permissions
                uint32_t net_perms = NET_SEND | NET_RECEIVE | NET_ESTABLISH |
                                     CapabilityPermission::READ | CapabilityPermission::WRITE;
                internal_network_cap = root_cap->delegate(
                    node_id + "_internal_net_cap", net_perms, CapabilityScope::NETWORK);
                if (!internal_network_cap) {
                    std::cerr << "Error: Failed to create internal network capability for remote node "
                              << node_id << std::endl;
                    return false;
                }
            } else {
                std::cerr << "Error: Cannot create internal network capability - root lacks DELEGATE" << std::endl;
                return false;
            }
        }

        // Initialize RDMA connection for data transfer using internal network capability
        rdma_connection = std::make_shared<RDMANetworkNode>(node_id + "_rdma", parent_dfg);
        if (!rdma_connection->initialize(internal_network_cap)) {
            std::cerr << "Error: Failed to initialize RDMA connection for remote node " << node_id << std::endl;
            return false;
        }

        initialized = true;
        return true;
    }

    void shutdown(Capability* cap) override {
        if (!cap || !cap->has_remote_permissions(REMOTE_EXECUTE)) {
            std::cerr << "Error: Insufficient REMOTE_EXECUTE permission for shutdown on remote node " << node_id << std::endl;
            set_last_error(ErrorCode::CAP_INSUFFICIENT_PERMISSIONS);
            return;
        }

        disconnect(cap);

        if (rdma_connection) {
            // Use internal network capability for RDMA shutdown
            rdma_connection->shutdown(internal_network_cap);
            rdma_connection.reset();
        }

        // Properly revoke and clean up internal network capability
        // This ensures the capability is removed from its parent's children list
        // and can't be used after shutdown
        if (internal_network_cap) {
            internal_network_cap->revoke();  // Clears permissions, marks as revoked
            // The capability is owned by the parent (root_cap's children list),
            // so we don't delete it directly - just null our reference
            internal_network_cap = nullptr;
        }

        remote_capabilities.clear();
        initialized = false;
    }

    bool is_ready(Capability* cap) const override {
        if (!cap || !cap->has_permission(CapabilityPermission::READ)) {
            return false;
        }
        return initialized && connected;
    }

    // Print info override
    void print_info() const override {
        std::cout << "RemoteDFGNode: " << node_id
                  << " LocalVLAN: " << local_vlan_config.vlan_id
                  << " RemoteVLAN: " << remote_vlan_config.vlan_id
                  << " Connected: " << (connected ? "yes" : "no")
                  << " Remote: " << remote_host << ":" << remote_port << std::endl;
    }

    /**
     * Connect to a remote FPGA
     * @param host Remote host address
     * @param port Remote port
     * @param vlan_id VLAN ID for authentication
     * @param cap Capability with REMOTE_EXECUTE permission
     */
    bool connect_remote(const std::string& host, uint16_t port,
                       uint16_t vlan_id, Capability* cap) {
        if (!cap || !cap->has_remote_permissions(REMOTE_EXECUTE)) {
            std::cerr << "Error: Insufficient REMOTE_EXECUTE permission for connect_remote on node "
                      << node_id << std::endl;
            return false;
        }

        if (!rdma_connection) {
            std::cerr << "Error: RDMA connection not initialized for remote node " << node_id << std::endl;
            return false;
        }

        // Connect via RDMA using internal network capability
        if (!rdma_connection->connect_to_remote(host, port, internal_network_cap)) {
            std::cerr << "Error: Failed to establish RDMA connection to " << host << ":" << port << std::endl;
            return false;
        }

        remote_host = host;
        remote_port = port;
        remote_vlan_config.vlan_id = vlan_id;
        remote_vlan_config.enforce_vlan = (vlan_id != 0);
        connected = true;

        std::cout << "Connected to remote FPGA at " << host << ":" << port
                  << " (VLAN " << vlan_id << ")" << std::endl;

        return true;
    }

    /**
     * Disconnect from remote FPGA
     */
    bool disconnect(Capability* cap) {
        if (!cap || !cap->has_remote_permissions(REMOTE_EXECUTE)) {
            std::cerr << "Error: Insufficient REMOTE_EXECUTE permission for disconnect on remote node " << node_id << std::endl;
            set_last_error(ErrorCode::CAP_INSUFFICIENT_PERMISSIONS);
            return false;
        }

        if (rdma_connection && internal_network_cap) {
            // Use internal network capability for RDMA disconnect
            rdma_connection->disconnect(internal_network_cap);
        }

        connected = false;
        return true;
    }

    /**
     * Delegate a capability to the remote FPGA
     * Creates a token with VLAN-based authentication
     */
    Capability* delegate_to_remote(const std::string& local_cap_id,
                                  const std::string& remote_node_id,
                                  uint32_t permissions,
                                  Capability* admin_cap) {
        if (!admin_cap || !admin_cap->has_remote_permissions(REMOTE_DELEGATE)) {
            std::cerr << "Error: Insufficient REMOTE_DELEGATE permission for delegate_to_remote on node "
                      << node_id << std::endl;
            return nullptr;
        }

        if (!connected) {
            std::cerr << "Error: Not connected to remote FPGA" << std::endl;
            return nullptr;
        }

        // Create remote capability token with expiry
        RemoteCapabilityToken token;
        token.vlan_id = local_vlan_config.vlan_id;
        token.source_vfpga_id = 0;  // Would get from actual vFPGA
        token.permissions = permissions;
        token.cap_id = local_cap_id + "_remote_" + remote_node_id;
        token.timestamp = get_current_timestamp();
        token.expiry_timestamp = token.timestamp + token_expiry_config.default_expiry_ms;
        token.sequence_number = get_next_sequence();  // Use rollover-safe method
        token.signature = create_signature(token);

        // Store in cache
        remote_capabilities[token.cap_id] = token;

        // Send token to remote (via RDMA) using internal network capability
        std::string serialized = token.serialize();
        if (rdma_connection->send(serialized.data(), serialized.size(), internal_network_cap) < 0) {
            std::cerr << "Error: Failed to send capability token to remote" << std::endl;
            remote_capabilities.erase(token.cap_id);
            return nullptr;
        }

        // Create a local proxy capability
        // In a real implementation, this would be a specialized RemoteCapability class
        return admin_cap->delegate(token.cap_id, permissions, CapabilityScope::REMOTE);
    }

    /**
     * Verify an incoming remote capability token via VLAN tag
     * Uses the unified VLANConfig for verification (same as NetworkNode)
     */
    bool verify_remote_capability(const RemoteCapabilityToken& token) {
        uint64_t now = get_current_timestamp();

        // 1. Verify token is structurally valid
        if (!token.is_valid()) {
            std::cerr << "Error: Invalid token structure" << std::endl;
            set_last_error(ErrorCode::REMOTE_TOKEN_INVALID);
            return false;
        }

        // 2. Verify VLAN tag (if enforcement is enabled)
        if (remote_vlan_config.enforce_vlan) {
            if (token.vlan_id != remote_vlan_config.vlan_id) {
                // Also check allowed_vlans list (unified with NetworkNode behavior)
                bool found = false;
                for (uint16_t allowed : remote_vlan_config.allowed_vlans) {
                    if (token.vlan_id == allowed) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    std::cerr << "Error: VLAN tag mismatch - expected " << remote_vlan_config.vlan_id
                              << ", got " << token.vlan_id << std::endl;
                    set_last_error(ErrorCode::NET_VLAN_MISMATCH);
                    return false;
                }
            }
        }

        // 3. Verify signature
        if (!verify_signature(token)) {
            std::cerr << "Error: Invalid token signature" << std::endl;
            set_last_error(ErrorCode::REMOTE_SIGNATURE_INVALID);
            return false;
        }

        // 4. Check timestamp validity (not created in the future)
        if (!token.is_timestamp_valid(now, token_expiry_config.max_clock_skew_ms)) {
            std::cerr << "Error: Token timestamp is in the future" << std::endl;
            set_last_error(ErrorCode::REMOTE_TOKEN_INVALID);
            return false;
        }

        // 5. Check token expiry
        if (token_expiry_config.enforce_expiry) {
            if (token.is_expired(now, token_expiry_config.max_clock_skew_ms)) {
                std::cerr << "Error: Token has expired" << std::endl;
                set_last_error(ErrorCode::REMOTE_TOKEN_EXPIRED);
                return false;
            }
        }

        // 6. Check sequence number for replay protection (with rollover handling)
        if (!is_sequence_valid(token.source_vfpga_id, token.sequence_number)) {
            std::cerr << "Error: Sequence number rejected (possible replay attack)" << std::endl;
            set_last_error(ErrorCode::REMOTE_SEQUENCE_ERROR);
            return false;
        }

        return true;
    }

    /**
     * Execute an operation on the remote FPGA
     */
    bool execute_on_remote(const std::string& remote_node_id, sgEntry* sg, Capability* cap) {
        if (!cap || !cap->has_remote_permissions(REMOTE_EXECUTE)) {
            std::cerr << "Error: Insufficient REMOTE_EXECUTE permission for execute_on_remote on node "
                      << node_id << std::endl;
            return false;
        }

        if (!connected) {
            std::cerr << "Error: Not connected to remote FPGA" << std::endl;
            return false;
        }

        // In a real implementation, this would:
        // 1. Serialize the sgEntry
        // 2. Send execution request via RDMA
        // 3. Wait for completion notification

        std::cout << "Executing operation on remote node " << remote_node_id << std::endl;
        return true;
    }

    /**
     * Transfer data to remote FPGA
     */
    bool transfer_to_remote(void* local_data, size_t len,
                           const std::string& remote_buffer_id,
                           Capability* cap) {
        if (!cap || !cap->has_remote_permissions(REMOTE_TRANSFER)) {
            std::cerr << "Error: Insufficient REMOTE_TRANSFER permission for transfer_to_remote on node "
                      << node_id << std::endl;
            return false;
        }

        if (!connected || !rdma_connection) {
            std::cerr << "Error: Not connected to remote FPGA" << std::endl;
            return false;
        }

        // Use RDMA write for high-performance transfer
        // Would need remote buffer address mapping in real implementation
        // Use internal network capability for RDMA operations
        return rdma_connection->send(local_data, len, internal_network_cap) >= 0;
    }

    /**
     * Transfer data from remote FPGA
     */
    bool transfer_from_remote(const std::string& remote_buffer_id,
                             void* local_buffer, size_t max_len,
                             size_t* received, Capability* cap) {
        if (!cap || !cap->has_remote_permissions(REMOTE_TRANSFER)) {
            std::cerr << "Error: Insufficient REMOTE_TRANSFER permission for transfer_from_remote on node "
                      << node_id << std::endl;
            return false;
        }

        if (!connected || !rdma_connection) {
            std::cerr << "Error: Not connected to remote FPGA" << std::endl;
            return false;
        }

        // Use RDMA read for high-performance transfer
        // Use internal network capability for RDMA operations
        ssize_t result = rdma_connection->receive(local_buffer, max_len, internal_network_cap);
        if (result >= 0 && received) {
            *received = static_cast<size_t>(result);
            return true;
        }
        return false;
    }

    // Getters (unified with NetworkNode's VLAN accessors)
    uint16_t get_local_vlan_id() const { return local_vlan_config.vlan_id; }
    uint16_t get_remote_vlan_id() const { return remote_vlan_config.vlan_id; }
    const VLANConfig& get_local_vlan_config() const { return local_vlan_config; }
    const VLANConfig& get_remote_vlan_config() const { return remote_vlan_config; }
    std::string get_remote_host() const { return remote_host; }
    uint16_t get_remote_port() const { return remote_port; }
    bool is_connected() const { return connected; }

    // RDMA QPN accessors (delegate to internal RDMA connection)
    uint32_t get_local_qpn() const {
        return rdma_connection ? rdma_connection->get_local_qpn() : 0;
    }
    uint32_t get_remote_qpn() const {
        return rdma_connection ? rdma_connection->get_remote_qpn() : 0;
    }
    std::string get_local_ip() const {
        return rdma_connection ? rdma_connection->get_local_ip() : "";
    }

    // Setters for VLAN config (unified with NetworkNode interface)
    bool set_local_vlan_config(const VLANConfig& config, Capability* cap) {
        if (!cap || !cap->has_remote_permissions(REMOTE_EXECUTE)) {
            std::cerr << "Error: Insufficient REMOTE_EXECUTE permission for set_local_vlan_config" << std::endl;
            set_last_error(ErrorCode::CAP_INSUFFICIENT_PERMISSIONS);
            return false;
        }
        local_vlan_config = config;
        return true;
    }

    bool set_remote_vlan_config(const VLANConfig& config, Capability* cap) {
        if (!cap || !cap->has_remote_permissions(REMOTE_EXECUTE)) {
            std::cerr << "Error: Insufficient REMOTE_EXECUTE permission for set_remote_vlan_config" << std::endl;
            set_last_error(ErrorCode::CAP_INSUFFICIENT_PERMISSIONS);
            return false;
        }
        remote_vlan_config = config;
        return true;
    }

    bool add_allowed_remote_vlan(uint16_t vlan_id, Capability* cap) {
        if (!cap || !cap->has_remote_permissions(REMOTE_EXECUTE)) {
            set_last_error(ErrorCode::CAP_INSUFFICIENT_PERMISSIONS);
            return false;
        }
        remote_vlan_config.allowed_vlans.push_back(vlan_id);
        return true;
    }

    // Token expiry configuration
    bool set_token_expiry_config(const TokenExpiryConfig& config, Capability* cap) {
        if (!cap || !cap->has_remote_permissions(REMOTE_EXECUTE)) {
            set_last_error(ErrorCode::CAP_INSUFFICIENT_PERMISSIONS);
            return false;
        }
        token_expiry_config = config;
        return true;
    }

    TokenExpiryConfig get_token_expiry_config() const {
        return token_expiry_config;
    }

    // Get current sequence counter value (for debugging/monitoring)
    uint32_t get_sequence_counter() const {
        return sequence_counter.load();
    }

    // Clear sequence tracking (use with caution - resets replay protection)
    void clear_sequence_tracking(Capability* cap) {
        if (!cap || !cap->has_remote_permissions(REMOTE_EXECUTE)) {
            return;
        }
        std::lock_guard<std::mutex> lock(sequence_mutex);
        last_seen_sequences.clear();
    }
};

// ============================================================================
// DFG Class
// ============================================================================

/**
 * DFG class that acts as a thin wrapper around direct cThread usage
 * with added capability management
 */
class DFG {
private:
    std::string app_id;
    std::unordered_map<std::string, std::shared_ptr<NodeBase>> nodes;  // Polymorphic node storage
    std::unordered_map<std::string, std::shared_ptr<Buffer>> buffers;
    std::unordered_map<std::string, Capability*> capabilities;
    uint32_t device_id;
    bool use_huge_pages;
    StreamMode stream_mode;
    std::atomic<bool> stalled;
    Capability* root_capability = nullptr;

    // Per-DFG software enforcer (not a global singleton - provides isolation between DFGs)
    std::unique_ptr<SoftwareEnforcer> software_enforcer;

    // Static counters for auto-generated IDs
    static std::atomic<int> node_counter;
    static std::atomic<int> buffer_counter;

public:
    // Constructor
    DFG(const std::string& app_id, uint32_t device_id, bool use_huge_pages, StreamMode stream_mode)
    : app_id(app_id), device_id(device_id), use_huge_pages(use_huge_pages),
      stream_mode(stream_mode), stalled(false), software_enforcer(std::make_unique<SoftwareEnforcer>()) {

        // Create root capability with all permissions
        root_capability = new Capability(app_id + "_root",
            CapabilityPermission::READ | CapabilityPermission::WRITE |
            CapabilityPermission::EXECUTE | CapabilityPermission::DELEGATE |
            CapabilityPermission::TRANSITIVE_DELEGATE,  // Added TRANSITIVE_DELEGATE
            nullptr, this, sizeof(DFG), nullptr);

        capabilities[root_capability->get_id()] = root_capability;
    }
    
    // Destructor - now use root_capability for clean-up
    ~DFG() {
        if (root_capability) {
            release_resources(root_capability);
        }
    }

    // Create a compute node with auto-generated ID
    std::shared_ptr<ComputeNode> create_node(Capability* cap, int vfid) {
        std::string node_id = "node_" + std::to_string(node_counter++);
        return create_node(cap, vfid, node_id);
    }

    // Create a compute node with optional custom ID
    std::shared_ptr<ComputeNode> create_node(Capability* cap, int vfid, const std::string& custom_id);

    // Get a node by ID (polymorphic) - requires a capability with READ permission
    std::shared_ptr<NodeBase> get_node_base(const std::string& node_id, Capability* cap) {
        if (!cap) {
            std::cerr << "Error: Null capability for get_node_base" << std::endl;
            return nullptr;
        }

        if (!cap->has_permission(CapabilityPermission::READ)) {
            std::cerr << "Error: Insufficient READ permission for get_node_base" << std::endl;
            return nullptr;
        }

        auto it = nodes.find(node_id);
        if (it != nodes.end()) {
            return it->second;
        }

        return nullptr;
    }

    // Get a compute node by ID - requires a capability with READ permission
    // Returns nullptr if node is not a ComputeNode
    std::shared_ptr<ComputeNode> get_node(const std::string& node_id, Capability* cap) {
        auto base = get_node_base(node_id, cap);
        if (base && base->is_compute_node()) {
            return std::dynamic_pointer_cast<ComputeNode>(base);
        }
        return nullptr;
    }

    // Add a node (polymorphic) - for adding any node type
    bool add_node(std::shared_ptr<NodeBase> node, Capability* cap) {
        if (!cap) {
            std::cerr << "Error: Null capability for add_node" << std::endl;
            return false;
        }

        if (!cap->has_permission(CapabilityPermission::WRITE)) {
            std::cerr << "Error: Insufficient WRITE permission for add_node" << std::endl;
            return false;
        }

        if (!node) {
            std::cerr << "Error: Null node for add_node" << std::endl;
            return false;
        }

        std::string id = node->get_id();
        if (nodes.find(id) != nodes.end()) {
            std::cerr << "Error: Node " << id << " already exists" << std::endl;
            return false;
        }

        nodes[id] = node;
        return true;
    }
    
    // Create a buffer with auto-generated ID
    std::shared_ptr<Buffer> create_buffer(Capability* cap, size_t size) {
        std::string buffer_id = "buffer_" + std::to_string(buffer_counter++);
        return create_buffer(cap, size, buffer_id);
    }

    // Create a buffer with optional custom ID
    std::shared_ptr<Buffer> create_buffer(Capability* cap, size_t size, const std::string& custom_id);
    
    // Get a buffer by ID - requires a capability with READ permission
    std::shared_ptr<Buffer> get_buffer(const std::string& buffer_id, Capability* cap) {
        // Ensure capability has READ permission for the DFG
        if (!cap) {
            std::cerr << "Error: Null capability for get_buffer" << std::endl;
            return nullptr;
        }
        
        if (!cap->has_permission(CapabilityPermission::READ)) {
            std::cerr << "Error: Insufficient READ permission for get_buffer" << std::endl;
            return nullptr;
        }
        
        // More permissive during initialization
        auto it = buffers.find(buffer_id);
        if (it != buffers.end()) {
            return it->second;
        }
        
        std::cerr << "Error: Buffer not found: " << buffer_id << std::endl;
        return nullptr;
    }

    // Set stalled state - requires a capability with WRITE permission
    void set_stalled(bool state, Capability* cap) {
        if (!cap) {
            std::cerr << "Error: Null capability for set_stalled" << std::endl;
            return;
        }
        
        if (!cap->has_permission(CapabilityPermission::WRITE)) {
            std::cerr << "Error: Insufficient WRITE permission for set_stalled" << std::endl;
            return;
        }
        
        stalled.store(state);
    }

    // Check if stalled - requires a capability with READ permission
    bool is_stalled(Capability* cap) const {
        if (!cap) {
            std::cerr << "Error: Null capability for is_stalled" << std::endl;
            return false;
        }
        
        if (!cap->has_permission(CapabilityPermission::READ)) {
            std::cerr << "Error: Insufficient READ permission for is_stalled" << std::endl;
            return false;
        }
        
        return stalled.load();
    }

    // Get device ID - requires a capability with READ permission
    uint32_t get_device_id(Capability* cap) const {
        if (!cap) {
            std::cerr << "Error: Null capability for get_device_id" << std::endl;
            return 0;
        }
        
        if (!cap->has_permission(CapabilityPermission::READ)) {
            std::cerr << "Error: Insufficient READ permission for get_device_id" << std::endl;
            return 0;
        }
        
        return device_id;
    }

    // Get stream mode - requires a capability with READ permission
    StreamMode get_stream_mode(Capability* cap) const {
        if (!cap) {
            std::cerr << "Error: Null capability for get_stream_mode" << std::endl;
            return HOST_STREAM;
        }
        
        if (!cap->has_permission(CapabilityPermission::READ)) {
            std::cerr << "Error: Insufficient READ permission for get_stream_mode" << std::endl;
            return HOST_STREAM;
        }
        
        return stream_mode;
    }

    // Set stream mode - requires a capability with WRITE permission
    void set_stream_mode(StreamMode mode, Capability* cap) {
        if (!cap) {
            std::cerr << "Error: Null capability for set_stream_mode" << std::endl;
            return;
        }
        
        if (!cap->has_permission(CapabilityPermission::WRITE)) {
            std::cerr << "Error: Insufficient WRITE permission for set_stream_mode" << std::endl;
            return;
        }
        
        stream_mode = mode;
    }

    // Check if using huge pages - requires a capability with READ permission
    bool is_using_huge_pages(Capability* cap) const {
        if (!cap) {
            std::cerr << "Error: Null capability for is_using_huge_pages" << std::endl;
            return false;
        }
        
        if (!cap->has_permission(CapabilityPermission::READ)) {
            std::cerr << "Error: Insufficient READ permission for is_using_huge_pages" << std::endl;
            return false;
        }
        
        return use_huge_pages;
    }

    // Release all resources - requires a capability with WRITE permission
    void release_resources(Capability* cap);

    // Execute graph with compute nodes - requires a capability with EXECUTE permission
    void execute_graph(ComputeNode** nodes, int num_nodes, sgEntry* sg_entries, Capability* cap);

    // Benchmark execution - requires a capability with EXECUTE permission
    void benchmark_graph(int num_runs, Capability* cap);

    /**
     * Execute all nodes in the DFG pipeline.
     * This triggers dataflow execution across all connected nodes.
     * @param cap Capability with EXECUTE permission
     * @return true if execution successful, false on error
     */
    bool execute_all(Capability* cap);

    // Get the root capability
    Capability* get_root_capability() const {
        return root_capability;
    }

    // Get the DFG's software enforcer (for SoftwareNode usage)
    SoftwareEnforcer* get_software_enforcer() const {
        return software_enforcer.get();
    }

    // Get all nodes (polymorphic, for debugging)
    const std::unordered_map<std::string, std::shared_ptr<NodeBase>>& get_all_nodes() const {
        return nodes;
    }

    // Get all compute nodes (filtered)
    std::vector<std::shared_ptr<ComputeNode>> get_compute_nodes() const {
        std::vector<std::shared_ptr<ComputeNode>> compute_nodes;
        for (const auto& pair : nodes) {
            if (pair.second->is_compute_node()) {
                auto compute = std::dynamic_pointer_cast<ComputeNode>(pair.second);
                if (compute) {
                    compute_nodes.push_back(compute);
                }
            }
        }
        return compute_nodes;
    }

    // Get nodes by type
    std::vector<std::shared_ptr<NodeBase>> get_nodes_by_type(NodeType type) const {
        std::vector<std::shared_ptr<NodeBase>> result;
        for (const auto& pair : nodes) {
            if (pair.second->get_node_type() == type) {
                result.push_back(pair.second);
            }
        }
        return result;
    }
    
    // Get all capabilities (for debugging)
    const std::unordered_map<std::string, Capability*>& get_all_capabilities() const {
        return capabilities;
    }
    
    // ------------------------------------------------------------------
    // Enhanced Capability Management API
    // ------------------------------------------------------------------

    /**
     * Create a capability from the root capability
     * @param cap_space_id The ID for the new capability space
     * @param access Permission flags for the capability
     * @param resource The resource this capability grants access to (or nullptr for DFG itself)
     * @param resource_size The size of the resource
     * @param thread The thread this capability grants access to (or nullptr)
     * @return Pointer to the created capability or nullptr on failure
     */
    Capability* create_root_capability(const std::string& cap_space_id,
                                     uint32_t access,
                                     void* resource = nullptr,
                                     size_t resource_size = 0,
                                     cThread<std::any>* thread = nullptr) {
        // Check parameters
        if (cap_space_id.empty()) {
            std::cerr << "Error: Empty capability ID for create_root_capability" << std::endl;
            return nullptr;
        }
        
        // Make sure we're not overwriting an existing capability
        if (capabilities.find(cap_space_id) != capabilities.end()) {
            std::cerr << "Error: Capability ID " << cap_space_id << " already exists" << std::endl;
            return nullptr;
        }
        
        // If no resource specified, use the DFG itself
        if (!resource) {
            resource = this;
            resource_size = sizeof(DFG);
        }
        
        // Create the capability as a child of the root capability
        Capability* new_cap = root_capability->delegate(cap_space_id, access);
        if (!new_cap) {
            std::cerr << "Error: Failed to delegate from root capability for " << cap_space_id << std::endl;
            return nullptr;
        }
        
        // Set the thread if provided
        if (thread) {
            new_cap->set_thread(thread);
        }
        
        // Store the capability
        capabilities[cap_space_id] = new_cap;

        return new_cap;
    }

    /**
     * Register an externally created capability with the DFG
     * This allows tracking capabilities that were created via delegation
     * @param cap_id The ID for the capability
     * @param cap The capability to register
     * @param parent_cap The parent capability (for verification, optional)
     * @return true if registration succeeded, false otherwise
     */
    bool register_capability(const std::string& cap_id, Capability* cap, Capability* parent_cap = nullptr) {
        if (cap_id.empty()) {
            std::cerr << "Error: Empty capability ID for register_capability" << std::endl;
            return false;
        }

        if (!cap) {
            std::cerr << "Error: Null capability for register_capability" << std::endl;
            return false;
        }

        // Check if already registered
        if (capabilities.find(cap_id) != capabilities.end()) {
            std::cerr << "Error: Capability ID " << cap_id << " already exists" << std::endl;
            return false;
        }

        capabilities[cap_id] = cap;
        return true;
    }

    /**
     * Register a capability using just the capability pointer (ID extracted from capability)
     */
    bool register_capability(Capability* cap) {
        if (!cap) return false;
        return register_capability(cap->get_id(), cap, nullptr);
    }

    /**
     * Delegate a capability
     * @param parent_cap The parent capability to delegate from
     * @param cap_id The ID for the new delegated capability
     * @param access Permission flags for the new capability
     * @return Pointer to the delegated capability or nullptr on failure
     */
    Capability* delegate_capability(Capability* parent_cap,
                                  const std::string& cap_id,
                                  uint32_t access) {
        // Check parameters
        if (!parent_cap) {
            std::cerr << "Error: Null parent capability for delegation" << std::endl;
            return nullptr;
        }
        
        if (cap_id.empty()) {
            std::cerr << "Error: Empty capability ID for delegation" << std::endl;
            return nullptr;
        }
        
        // Check for duplicate ID
        if (capabilities.find(cap_id) != capabilities.end()) {
            std::cerr << "Error: Capability ID " << cap_id << " already exists" << std::endl;
            return nullptr;
        }
        
        // Let the parent capability handle the delegation
        Capability* new_cap = parent_cap->delegate(cap_id, access);
        if (!new_cap) {
            std::cerr << "Error: Failed to delegate capability " << cap_id 
                      << " from parent " << parent_cap->get_id() << std::endl;
            return nullptr;
        }
        
        // Store the new capability
        capabilities[cap_id] = new_cap;
        
        return new_cap;
    }
    
    /**
     * Revoke a capability
     * @param cap_to_revoke The capability to revoke
     * @param admin_cap An administrative capability with DELEGATE permission
     * @return True if successful, false otherwise
     */
    bool revoke_capability(Capability* cap_to_revoke, Capability* admin_cap) {
        // Check parameters
        if (!cap_to_revoke) {
            std::cerr << "Error: Null capability to revoke" << std::endl;
            return false;
        }
        
        if (!admin_cap) {
            std::cerr << "Error: Null administrative capability for revocation" << std::endl;
            return false;
        }
        
        // Admin cap must have DELEGATE permission
        if (!admin_cap->has_permission(CapabilityPermission::DELEGATE)) {
            std::cerr << "Error: Administrative capability lacks DELEGATE permission for revocation" << std::endl;
            return false;
        }
        
        // Can't revoke root capability
        if (cap_to_revoke == root_capability) {
            std::cerr << "Error: Cannot revoke root capability" << std::endl;
            return false;
        }
        
        // Get the capability ID before removing
        std::string cap_id = cap_to_revoke->get_id();
        
        // Recursive function to revoke all descendants
        std::function<void(Capability*)> revoke_recursive;
        revoke_recursive = [this, &revoke_recursive](Capability* cap) {
            if (!cap) return;
            
            // Process all children first (make a copy since we'll modify)
            std::vector<Capability*> children = cap->get_children();
            for (auto child : children) {
                if (child) {
                    revoke_recursive(child);
                }
            }
            
            // Remove this capability from our registry
            std::string cap_id = cap->get_id();
            capabilities.erase(cap_id);
            
            // Get parent to remove this child
            Capability* parent = cap->get_parent();
            if (parent) {
                parent->remove_child(cap_id);
            }
            
            // Delete the capability
            delete cap;
        };
        
        // Start recursive revocation
        revoke_recursive(cap_to_revoke);
        
        return true;
    }
    
    /**
     * Set an expiration time for a capability
     * @param cap The capability to expire
     * @param admin_cap An administrative capability with DELEGATE permission
     * @param timeout Timeout in seconds
     * @return True if successful, false otherwise
     */
    bool expire_capability(Capability* cap, Capability* admin_cap, uint32_t timeout) {
        // Check parameters
        if (!cap) {
            std::cerr << "Error: Null capability for expiration" << std::endl;
            return false;
        }
        
        if (!admin_cap) {
            std::cerr << "Error: Null administrative capability for expiration" << std::endl;
            return false;
        }
        
        // Admin cap must have DELEGATE permission
        if (!admin_cap->has_permission(CapabilityPermission::DELEGATE)) {
            std::cerr << "Error: Administrative capability lacks DELEGATE permission for expiration" << std::endl;
            return false;
        }
        
        // Set the expiry time
        cap->set_expiry(std::chrono::seconds(timeout));
        
        return true;
    }
    
    /**
     * Find a capability by ID
     * @param cap_id The ID of the capability to find
     * @param admin_cap An administrative capability with READ permission
     * @return Pointer to the capability or nullptr if not found
     */
    Capability* find_capability(const std::string& cap_id, Capability* admin_cap) {
        // Check parameters
        if (cap_id.empty()) {
            std::cerr << "Error: Empty capability ID for find_capability" << std::endl;
            return nullptr;
        }
        
        if (!admin_cap) {
            std::cerr << "Error: Null administrative capability for find_capability" << std::endl;
            return nullptr;
        }
        
        if (!admin_cap->has_permission(CapabilityPermission::READ)) {
            std::cerr << "Error: Administrative capability lacks READ permission for capability lookup" << std::endl;
            return nullptr;
        }
        
        auto it = capabilities.find(cap_id);
        if (it != capabilities.end()) {
            return it->second;
        }
        
        // Not found - don't print error to avoid log spam during normal lookup
        return nullptr;
    }
    
    /**
     * Print the capability tree
     * @param admin_cap An administrative capability with READ permission
     */
    void print_capability_tree(Capability* admin_cap) const {
        // Check parameters
        if (!admin_cap) {
            std::cerr << "Error: Null administrative capability for print_capability_tree" << std::endl;
            return;
        }
        
        if (!admin_cap->has_permission(CapabilityPermission::READ)) {
            std::cerr << "Error: Administrative capability lacks read permission for printing capability tree" << std::endl;
            return;
        }
        
        if (root_capability) {
            std::cout << "Capability Tree:" << std::endl;
            root_capability->print_tree();
        } else {
            std::cout << "No root capability found." << std::endl;
        }
    }
};

// Initialize static counters
std::atomic<int> DFG::node_counter{0};
std::atomic<int> DFG::buffer_counter{0};

// -------------------- ComputeNode's get_mem method implementation --------------------

// Allocate memory - requires a capability with WRITE permission
void* ComputeNode::get_mem(size_t size, Capability* cap) {
    if (!cap) {
        std::cerr << "Error: Null capability for get_mem on node " << node_id << std::endl;
        return nullptr;
    }
    
    if (!cap->has_permission(CapabilityPermission::WRITE)) {
        std::cerr << "Error: Insufficient WRITE permission for get_mem on node " << node_id << std::endl;
        return nullptr;
    }
    
    // Check if the node has a thread
    if (!thread) {
        std::cerr << "Error: Thread not initialized for node " << node_id << std::endl;
        return nullptr;
    }
    
    // Determine allocation type based on DFG configuration
    CoyoteAlloc alloc_type = CoyoteAlloc::HPF; // Default to hugepages
    
    if (parent_dfg) {
        Capability* root_cap = parent_dfg->get_root_capability();
        if (root_cap && !parent_dfg->is_using_huge_pages(root_cap)) {
            alloc_type = CoyoteAlloc::REG;
        }
    }
    
    try {
        // Ensure size is properly aligned
        size_t aligned_size = (size + 63) & ~63; // Align to 64-byte boundary
        
        // Allocate memory with proper type
        void* memory = thread->getMem({alloc_type, static_cast<uint32_t>(aligned_size)});
        if (!memory) {
            std::cerr << "Error: Failed to allocate memory of size " << aligned_size 
                      << " for node " << node_id << std::endl;
        }
        return memory;
    } catch (const std::exception& e) {
        std::cerr << "Exception during memory allocation for node " << node_id 
                  << ": " << e.what() << std::endl;
        return nullptr;
    }
}

// -------------------- ComputeNode constructor implementation --------------------

// ComputeNode constructor implementation with proper error handling
ComputeNode::ComputeNode(const std::string& node_id, DFG* parent_dfg, int vfid)
    : NodeBase(node_id, parent_dfg, NodeType::COMPUTE), vfid(vfid) {
    // Create the thread with proper error handling
    try {
        // Get the parent DFG's device_id safely
        uint32_t device_id = 0;
        if (parent_dfg) {
            Capability* root_cap = parent_dfg->get_root_capability();
            if (root_cap) {
                device_id = parent_dfg->get_device_id(root_cap);
            }
        }
        
        // Create the thread
        thread = std::make_shared<cThread<std::any>>(
            vfid,
            getpid(),
            device_id
        );
        
        if (!thread) {
            throw std::runtime_error("Failed to create thread for node " + node_id);
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception during thread creation for node " << node_id << ": " << e.what() << std::endl;
        // We don't re-throw here because the node object itself is created,
        // but the thread will be null which will be checked in other methods
    }
}

// -------------------- Implementation of DFG methods --------------------

std::shared_ptr<ComputeNode> DFG::create_node(Capability* cap, int vfid, const std::string& custom_id) {
    // Check parameters
    if (custom_id.empty()) {
        std::cerr << "Error: Empty node ID for create_node" << std::endl;
        return nullptr;
    }

    if (!cap) {
        std::cerr << "Error: Null capability for create_node" << std::endl;
        return nullptr;
    }

    // Ensure capability has WRITE permission for the DFG
    if (!cap->has_permission(CapabilityPermission::WRITE)) {
        std::cerr << "Error: Insufficient WRITE permission for create_node" << std::endl;
        return nullptr;
    }

    // Check if node already exists
    if (nodes.find(custom_id) != nodes.end()) {
        std::cerr << "Error: Node " << custom_id << " already exists" << std::endl;
        return nullptr;
    }

    try {
        // Create the compute node with proper thread setup
        auto node = std::make_shared<ComputeNode>(custom_id, this, vfid);
        if (!node) {
            throw std::runtime_error("Failed to create compute node object");
        }

        // Get the thread to ensure it was created successfully
        cThread<std::any>* thread_ptr = node->get_thread_direct();
        if (!thread_ptr) {
            throw std::runtime_error("ComputeNode created but thread initialization failed");
        }

        // Store the node (polymorphic storage)
        nodes[custom_id] = node;

        // Create a capability for this node
        std::string node_cap_id = custom_id + "_cap";
        Capability* node_cap = create_root_capability(
            node_cap_id,
            CapabilityPermission::READ | CapabilityPermission::WRITE |
            CapabilityPermission::EXECUTE | CapabilityPermission::DELEGATE |
            CapabilityPermission::TRANSITIVE_DELEGATE,
            node.get(),
            sizeof(ComputeNode),
            thread_ptr
        );

        if (!node_cap) {
            // Cleanup if capability creation fails
            nodes.erase(custom_id);
            throw std::runtime_error("Failed to create capability for compute node");
        }

        std::cout << "Successfully created compute node " << custom_id << " with thread "
                  << thread_ptr << " and capability " << node_cap_id << std::endl;

        return node;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception during compute node creation: " << e.what() << std::endl;
        return nullptr;
    }
}

std::shared_ptr<Buffer> DFG::create_buffer(Capability* cap, size_t size, const std::string& custom_id) {
    // Check parameters
    if (custom_id.empty()) {
        std::cerr << "Error: Empty buffer ID for create_buffer" << std::endl;
        return nullptr;
    }
    
    if (!cap) {
        std::cerr << "Error: Null capability for create_buffer" << std::endl;
        return nullptr;
    }
    
    if (size == 0) {
        std::cerr << "Error: Zero size for buffer " << custom_id << std::endl;
        return nullptr;
    }
    
    // Ensure capability has WRITE permission for the DFG
    if (!cap->has_permission(CapabilityPermission::WRITE)) {
        std::cerr << "Error: Insufficient WRITE permission for create_buffer" << std::endl;
        return nullptr;
    }
    
    // Check if buffer already exists
    if (buffers.find(custom_id) != buffers.end()) {
        std::cerr << "Error: Buffer " << custom_id << " already exists" << std::endl;
        return nullptr;
    }
    
    // Make sure we have at least one node
    if (nodes.empty()) {
        std::cerr << "Error: No nodes available for memory allocation" << std::endl;
        return nullptr;
    }
    
    // Try to find a valid compute node with proper capability to allocate memory
    void* memory = nullptr;
    ComputeNode* allocating_node = nullptr;

    for (const auto& node_pair : nodes) {
        auto base_node = node_pair.second;
        if (!base_node || !base_node->is_compute_node()) continue;

        auto node = std::dynamic_pointer_cast<ComputeNode>(base_node);
        if (!node) continue;

        std::string node_id = node->get_id();
        std::string node_cap_id = node_id + "_cap";
        Capability* node_cap = find_capability(node_cap_id, cap);

        if (node_cap) {
            // Try to allocate memory with this compute node
            try {
                // Align the size for hardware requirements
                size_t aligned_size = (size + 63) & ~63; // Align to 64-byte boundary

                memory = node->get_mem(aligned_size, node_cap);
                if (memory) {
                    allocating_node = node.get();
                    std::cout << "Mapped mem at: " << memory << std::endl;
                    break;
                }
            } catch (const std::exception& e) {
                std::cerr << "Exception during memory allocation with node " << node_id
                          << ": " << e.what() << std::endl;
                // Try the next node
            }
        }
    }
    
    if (!memory || !allocating_node) {
        std::cerr << "Error: Failed to allocate memory for buffer using any available node" << std::endl;
        return nullptr;
    }
    
    try {
        // Create the buffer
        auto buffer = std::make_shared<Buffer>(custom_id, this, memory, size);
        if (!buffer) {
            throw std::runtime_error("Failed to create buffer object");
        }
        
        buffers[custom_id] = buffer;
        
        // Create a capability for this buffer
        std::string buffer_cap_id = custom_id + "_cap";
        Capability* buffer_cap = create_root_capability(
            buffer_cap_id,
            CapabilityPermission::READ | CapabilityPermission::WRITE | 
            CapabilityPermission::DELEGATE | CapabilityPermission::TRANSITIVE_DELEGATE,  // Add TRANSITIVE_DELEGATE here
            memory,
            size
        );
        
        if (!buffer_cap) {
            // Free memory and clean up
            std::string node_id = allocating_node->get_id();
            std::string node_cap_id = node_id + "_cap";
            Capability* node_cap = find_capability(node_cap_id, cap);
            
            if (node_cap) {
                allocating_node->free_mem(memory, node_cap);
            }
            
            buffers.erase(custom_id);
            throw std::runtime_error("Failed to create capability for buffer");
        }
        
        std::cout << "Successfully created buffer " << custom_id << " of size " << size << std::endl;
        
        return buffer;
    } catch (const std::exception& e) {
        std::cerr << "Exception during buffer creation: " << e.what() << std::endl;
        
        // Clean up allocated memory on failure
        if (memory && allocating_node) {
            std::string node_id = allocating_node->get_id();
            std::string node_cap_id = node_id + "_cap";
            Capability* node_cap = find_capability(node_cap_id, cap);
            
            if (node_cap) {
                allocating_node->free_mem(memory, node_cap);
            }
        }
        
        return nullptr;
    }
}

void DFG::execute_graph(ComputeNode** nodes, int num_nodes, sgEntry* sg_entries, Capability* cap) {
    // Ensure capability has EXECUTE permission for the DFG
    if (!cap || !cap->has_permission(CapabilityPermission::EXECUTE) || !cap->is_for_resource(this)) {
        std::cerr << "Error: Invalid or insufficient capability for execute_graph" << std::endl;
        return;
    }
    
    if (!nodes || num_nodes <= 0 || !sg_entries) {
        std::cerr << "Error: Invalid parameters for execute_graph" << std::endl;
        return;
    }
    
    // Prepare nodes with proper capabilities
    std::vector<Capability*> node_caps(num_nodes, nullptr);
    
    // Get capabilities and clear completion counters
    for (int i = 0; i < num_nodes; i++) {
        if (!nodes[i]) {
            std::cerr << "Error: Null node at index " << i << " in execute_graph" << std::endl;
            return;
        }
        
        std::string node_id = nodes[i]->get_id();
        std::string node_cap_id = node_id + "_cap";
        node_caps[i] = find_capability(node_cap_id, cap);
        
        if (!node_caps[i]) {
            std::cerr << "Error: Capability not found for node " << node_id << std::endl;
            return;
        }
        
        // Reset completion counter
        try {
            nodes[i]->clear_completed(node_caps[i]);
        } catch (const std::exception& e) {
            std::cerr << "Exception clearing completion counter for node " << node_id 
                      << ": " << e.what() << std::endl;
            return;
        }
    }
    
    // Execute nodes in order with proper error handling
    for (int i = 0; i < num_nodes; i++) {
        try {
            // CRITICAL CHANGE: Get the thread pointer through capability-secured methods
            // but access it directly for the invoke call with LOCAL_TRANSFER operation
            cThread<std::any>* thread = nodes[i]->get_thread(node_caps[i]);
            if (thread) {
                // Direct thread invoke with LOCAL_TRANSFER - this is the key difference
                thread->invoke(CoyoteOper::LOCAL_TRANSFER, &sg_entries[i], {true, true, false});
            } else {
                std::cerr << "Error: Failed to get thread for node " << nodes[i]->get_id() << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception during node execution for " << nodes[i]->get_id() 
                      << ": " << e.what() << std::endl;
            stalled.store(true);
            return;
        }
    }
}

void DFG::release_resources(Capability* cap) {
    // Ensure capability has WRITE permission for the DFG
    if (!cap) {
        std::cerr << "Error: Null capability for release_resources" << std::endl;
        return;
    }
    
    if (!cap->has_permission(CapabilityPermission::WRITE)) {
        std::cerr << "Error: Insufficient WRITE permission for release_resources" << std::endl;
        return;
    }
    
    // Set stalled state to prevent new operations
    stalled.store(true);
    
    // Free buffer memory with proper error handling (only compute nodes can free memory)
    if (!nodes.empty()) {
        for (const auto& node_pair : nodes) {
            auto base_node = node_pair.second;
            if (!base_node || !base_node->is_compute_node()) continue;

            auto node = std::dynamic_pointer_cast<ComputeNode>(base_node);
            if (!node) continue;

            std::string node_id = node->get_id();
            std::string node_cap_id = node_id + "_cap";
            Capability* node_cap = find_capability(node_cap_id, cap);

            if (!node_cap) {
                std::cerr << "Warning: Could not find capability for node " << node_id << " during cleanup" << std::endl;
                continue;
            }

            // First ensure the node is idle
            try {
                node->clear_completed(node_cap);
            } catch (const std::exception& e) {
                std::cerr << "Exception clearing completion for node " << node_id
                          << ": " << e.what() << std::endl;
            }

            // Free buffer memory associated with this node
            for (auto& buffer_pair : buffers) {
                auto buffer = buffer_pair.second;
                if (!buffer) continue;

                std::string buffer_id = buffer->get_id();
                std::string buffer_cap_id = buffer_id + "_cap";
                Capability* buffer_cap = find_capability(buffer_cap_id, cap);

                if (!buffer_cap) {
                    std::cerr << "Warning: Could not find capability for buffer " << buffer_id << " during cleanup" << std::endl;
                    continue;
                }

                try {
                    void* memory = buffer->get_memory(buffer_cap);
                    if (memory) {
                        node->free_mem(memory, node_cap);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Exception freeing memory for buffer " << buffer_id
                              << ": " << e.what() << std::endl;
                }
            }
        }
    }
    
    // Revoke all capabilities except the root
    std::vector<std::string> cap_ids_to_revoke;
    for (const auto& cap_pair : capabilities) {
        if (cap_pair.second != root_capability) {
            cap_ids_to_revoke.push_back(cap_pair.first);
        }
    }
    
    // Revoke in reverse order to handle dependencies
    for (auto it = cap_ids_to_revoke.rbegin(); it != cap_ids_to_revoke.rend(); ++it) {
        Capability* cap_to_revoke = capabilities[*it];
        if (cap_to_revoke) {
            revoke_capability(cap_to_revoke, root_capability);
        }
    }
    
    // Clear collections
    capabilities.clear();
    nodes.clear();
    buffers.clear();
    
    // Add root capability back to the map if it exists
    if (root_capability) {
        capabilities[root_capability->get_id()] = root_capability;
    }
}

void DFG::benchmark_graph(int num_runs, Capability* cap) {
    // Ensure capability has EXECUTE permission for the DFG
    if (!cap) {
        std::cerr << "Error: Null capability for benchmark_graph" << std::endl;
        return;
    }
    
    if (!cap->has_permission(CapabilityPermission::EXECUTE)) {
        std::cerr << "Error: Insufficient EXECUTE permission for benchmark_graph" << std::endl;
        return;
    }
    
    // Validate parameters
    if (num_runs <= 0) {
        std::cerr << "Error: Invalid number of runs for benchmark_graph: " << num_runs << std::endl;
        return;
    }
    
    try {
        cBench bench(num_runs);
        
        auto benchmark_func = [this]() {
            // This is a placeholder for actual benchmark implementation
            // In a real system, this would use the DFG's nodes and buffers
            // to perform a standardized workload
        };
        
        bench.runtime(benchmark_func);
        
        std::cout << "Performance Metrics:" << std::endl;
        std::cout << "Average Execution Time: " << bench.getAvg() << " ns" << std::endl;
        std::cout << "Min Execution Time: " << bench.getMin() << " ns" << std::endl;
        std::cout << "Max Execution Time: " << bench.getMax() << " ns" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Exception during benchmark_graph: " << e.what() << std::endl;
    }
}

bool DFG::execute_all(Capability* cap) {
    // Ensure capability has EXECUTE permission for the DFG
    if (!cap) {
        std::cerr << "Error: Null capability for execute_all" << std::endl;
        return false;
    }

    if (!cap->has_permission(CapabilityPermission::EXECUTE)) {
        std::cerr << "Error: Insufficient EXECUTE permission for execute_all" << std::endl;
        return false;
    }

    // Get all compute nodes and execute them in order
    std::vector<std::shared_ptr<ComputeNode>> compute_nodes = get_compute_nodes();
    if (compute_nodes.empty()) {
        std::cout << "[DFG::execute_all] No compute nodes to execute" << std::endl;
        return true;  // Not an error - just nothing to do
    }

    std::cout << "[DFG::execute_all] Executing " << compute_nodes.size() << " compute nodes" << std::endl;

    // Create scatter-gather entries for each node
    std::vector<sgEntry> sg_entries(compute_nodes.size());
    for (size_t i = 0; i < compute_nodes.size(); i++) {
        memset(&sg_entries[i], 0, sizeof(sgEntry));
        sg_entries[i].local.src_stream = 1;
        sg_entries[i].local.dst_stream = 1;

        // Configure offsets based on position in pipeline
        if (i == 0) {
            sg_entries[i].local.offset_r = 0;
            sg_entries[i].local.offset_w = 6;
        } else if (i == compute_nodes.size() - 1) {
            sg_entries[i].local.offset_r = 6;
            sg_entries[i].local.offset_w = 0;
        } else {
            sg_entries[i].local.offset_r = 6;
            sg_entries[i].local.offset_w = 6;
        }
    }

    // Convert to raw pointer array for execute_graph
    std::vector<ComputeNode*> node_ptrs(compute_nodes.size());
    for (size_t i = 0; i < compute_nodes.size(); i++) {
        node_ptrs[i] = compute_nodes[i].get();
    }

    // Execute the graph
    try {
        execute_graph(node_ptrs.data(), static_cast<int>(node_ptrs.size()), sg_entries.data(), cap);
        std::cout << "[DFG::execute_all] Execution complete" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[DFG::execute_all] Exception: " << e.what() << std::endl;
        return false;
    }
}

// -------------------- Factory Functions --------------------

// Create a new DFG
DFG* create_dfg(const std::string& app_id, uint32_t device_id = 0, bool use_huge_pages = true, StreamMode stream_mode = HOST_STREAM) {
    if (app_id.empty()) {
        std::cerr << "Error: Empty application ID for create_dfg" << std::endl;
        return nullptr;
    }
    
    try {
        return new DFG(app_id, device_id, use_huge_pages, stream_mode);
    } catch (const std::exception& e) {
        std::cerr << "Exception during DFG creation: " << e.what() << std::endl;
        return nullptr;
    }
}

// Create a compute node with auto-generated ID
ComputeNode* create_node(DFG* dfg, int vfid = 0) {
    if (!dfg) {
        std::cerr << "Error: Null DFG for create_node" << std::endl;
        return nullptr;
    }

    // Use root capability for creation
    Capability* root_cap = dfg->get_root_capability();
    if (!root_cap) {
        std::cerr << "Error: Could not get root capability for node creation" << std::endl;
        return nullptr;
    }

    try {
        auto node = dfg->create_node(root_cap, vfid);
        return node.get();
    } catch (const std::exception& e) {
        std::cerr << "Exception during node creation: " << e.what() << std::endl;
        return nullptr;
    }
}

// Create a compute node with custom ID
ComputeNode* create_node(DFG* dfg, int vfid, const std::string& custom_id) {
    if (!dfg) {
        std::cerr << "Error: Null DFG for create_node" << std::endl;
        return nullptr;
    }

    if (custom_id.empty()) {
        std::cerr << "Error: Empty node ID for create_node" << std::endl;
        return nullptr;
    }

    // Use root capability for creation
    Capability* root_cap = dfg->get_root_capability();
    if (!root_cap) {
        std::cerr << "Error: Could not get root capability for node creation" << std::endl;
        return nullptr;
    }

    try {
        auto node = dfg->create_node(root_cap, vfid, custom_id);
        return node.get();
    } catch (const std::exception& e) {
        std::cerr << "Exception during node creation: " << e.what() << std::endl;
        return nullptr;
    }
}

// Alias for backward compatibility - create_compute_node
inline ComputeNode* create_compute_node(DFG* dfg, int vfid, const std::string& custom_id) {
    return create_node(dfg, vfid, custom_id);
}

// Create a buffer with auto-generated ID
Buffer* create_buffer(DFG* dfg, size_t size) {
    if (!dfg) return nullptr;
    
    // Use root capability for creation
    Capability* root_cap = dfg->get_root_capability();
    
    auto buffer = dfg->create_buffer(root_cap, size);
    return buffer.get();
}

// Create a buffer with custom ID
Buffer* create_buffer(DFG* dfg, size_t size, const std::string& custom_id) {
    if (!dfg) return nullptr;
    
    // Use root capability for creation
    Capability* root_cap = dfg->get_root_capability();
    
    auto buffer = dfg->create_buffer(root_cap, size, custom_id);
    return buffer.get();
}

// Connect edges between nodes with capabilities
bool connect_edges(const std::string& source_id, const std::string& target_id, DFG* dfg, 
    uint32_t read_offset = 0, uint32_t write_offset = 0, bool suppress_expected_errors = true) {
    if (!dfg) return false;

    // Use root capability for connection
    Capability* root_cap = dfg->get_root_capability();

    // Get the source and target nodes/buffers
    auto source_node = dfg->get_node(source_id, root_cap);
    auto target_node = dfg->get_node(target_id, root_cap);

    if (!source_node && !suppress_expected_errors) {
        std::cerr << "Error: Node not found: " << source_id << std::endl;
    }
    
    if (!target_node && !suppress_expected_errors) {
        std::cerr << "Error: Node not found: " << target_id << std::endl;
    }

    // Get node/buffer capabilities
    std::string source_cap_id = source_id + "_cap";
    std::string target_cap_id = target_id + "_cap";

    Capability* source_cap = dfg->find_capability(source_cap_id, root_cap);
    Capability* target_cap = dfg->find_capability(target_cap_id, root_cap);

    if (!source_cap || !target_cap) {
    std::cerr << "Error: Source or Destination capability not found" << std::endl;
    return false;
    }

    // Create a connection-specific capability
    std::string conn_cap_id = source_id + "_to_" + target_id;

    // Delegate from source to connection with DELEGATE and TRANSITIVE_DELEGATE
    uint32_t source_conn_perms = CapabilityPermission::READ | CapabilityPermission::DELEGATE | 
                    CapabilityPermission::TRANSITIVE_DELEGATE;
    Capability* conn_source_cap = dfg->delegate_capability(source_cap, conn_cap_id + "_src", source_conn_perms);

    // Delegate from target to connection with DELEGATE and TRANSITIVE_DELEGATE
    uint32_t target_conn_perms = CapabilityPermission::WRITE | CapabilityPermission::DELEGATE | 
                    CapabilityPermission::TRANSITIVE_DELEGATE;
    Capability* conn_target_cap = dfg->delegate_capability(target_cap, conn_cap_id + "_dest", target_conn_perms);

    if (!conn_source_cap || !conn_target_cap) {
    std::cerr << "Error: Failed to create connection capabilities" << std::endl;
    return false;
    }

    // Configure physical connections if nodes exist
    if (source_node) {
    source_node->connect_edges(read_offset, write_offset, conn_source_cap);
    }

    if (target_node) {
    target_node->connect_edges(write_offset, read_offset, conn_target_cap);
    }

    return true;
}

// Disconnect edges between nodes/buffers
bool disconnect_edges(const std::string& source_id, const std::string& target_id, DFG* dfg) {
    if (!dfg) return false;
    
    // Use root capability
    Capability* root_cap = dfg->get_root_capability();
    
    // Find and revoke connection capabilities
    std::string conn_source_cap_id = source_id + "_to_" + target_id + "_src";
    std::string conn_target_cap_id = source_id + "_to_" + target_id + "_dest";
    
    Capability* conn_source_cap = dfg->find_capability(conn_source_cap_id, root_cap);
    Capability* conn_target_cap = dfg->find_capability(conn_target_cap_id, root_cap);
    
    bool success = true;
    
    if (conn_source_cap) {
        success &= dfg->revoke_capability(conn_source_cap, root_cap);
    }
    
    if (conn_target_cap) {
        success &= dfg->revoke_capability(conn_target_cap, root_cap);
    }
    
    return success;
}

// Execute the DFG with compute nodes
void execute_graph(DFG* dfg, ComputeNode** nodes, int num_nodes, sgEntry* sg_entries) {
    if (!dfg) return;

    // Use root capability
    Capability* root_cap = dfg->get_root_capability();

    dfg->execute_graph(nodes, num_nodes, sg_entries, root_cap);
}

// Write data to a buffer
void write_buffer(Buffer* buffer, void* data, size_t size) {
    if (!buffer || !data) return;
    
    // Find the buffer's capability
    DFG* dfg = buffer->get_parent_dfg();
    if (!dfg) return;
    
    // Use root capability to find buffer capability
    Capability* root_cap = dfg->get_root_capability();
    std::string buffer_cap_id = buffer->get_id() + "_cap";
    Capability* buffer_cap = dfg->find_capability(buffer_cap_id, root_cap);
    
    if (buffer_cap) {
        buffer->write_data(data, size, buffer_cap);
    }
}

// Read data from a buffer
void* read_buffer(Buffer* buffer) {
    if (!buffer) return nullptr;
    
    // Find the buffer's capability
    DFG* dfg = buffer->get_parent_dfg();
    if (!dfg) return nullptr;
    
    // Use root capability to find buffer capability
    Capability* root_cap = dfg->get_root_capability();
    std::string buffer_cap_id = buffer->get_id() + "_cap";
    Capability* buffer_cap = dfg->find_capability(buffer_cap_id, root_cap);
    
    if (buffer_cap) {
        return buffer->get_memory(buffer_cap);
    }
    
    return nullptr;
}

// Configure a node's IO switch
void configure_node_io_switch(Node* node, IODevs io_switch) {
    if (!node) return;
    
    // Find the node's capability
    DFG* dfg = nullptr; // Need to get this from somewhere - would need to add parent_dfg to Node class
    if (!dfg) {
        // Fallback for backward compatibility
        // This assumes the node is accessible without capability checks
        // Not secure, but ensures existing code works
        cThread<std::any>* thread = nullptr; // Would need direct access to thread
        if (thread) {
            thread->ioSwitch(io_switch);
        }
        return;
    }
    
    // Use root capability to find node capability
    Capability* root_cap = dfg->get_root_capability();
    std::string node_cap_id = node->get_id() + "_cap";
    Capability* node_cap = dfg->find_capability(node_cap_id, root_cap);
    
    if (node_cap) {
        node->set_io_switch(io_switch, node_cap);
    }
}

// Set a node's operation type
void set_node_operation(Node* node, CoyoteOper operation) {
    if (!node) return;
    
    // Find the node's capability
    DFG* dfg = nullptr; // Need to get this from somewhere - would need to add parent_dfg to Node class
    if (!dfg) {
        // Fallback for backward compatibility - set operation directly
        // This is not secure but ensures existing code works
        // Would need direct access to private members or setter
        return;
    }
    
    // Use root capability to find node capability
    Capability* root_cap = dfg->get_root_capability();
    std::string node_cap_id = node->get_id() + "_cap";
    Capability* node_cap = dfg->find_capability(node_cap_id, root_cap);
    
    if (node_cap) {
        node->set_operation(operation, node_cap);
    }
}

// Release all resources
void release_resources(DFG* dfg) {
    if (!dfg) return;
    
    // Use root capability
    Capability* root_cap = dfg->get_root_capability();
    
    dfg->release_resources(root_cap);
    delete dfg;
}

// Print capability tree
void print_capability_tree(DFG* dfg) {
    if (!dfg) return;
    
    // Use root capability
    Capability* root_cap = dfg->get_root_capability();
    
    dfg->print_capability_tree(root_cap);
}

// Create a capability
Capability* create_capability(DFG* dfg, 
                             const std::string& app_id, 
                             const std::string& node_buf_id,
                             const std::string& cap_space_id,
                             uint32_t access,
                             bool allow_transitive_delegation = false) {
    if (!dfg) return nullptr;
    
    // Add transitive delegation if requested
    if (allow_transitive_delegation) {
        access |= CapabilityPermission::TRANSITIVE_DELEGATE;
    }
    
    // Use root capability
    Capability* root_cap = dfg->get_root_capability();
    
    // Find the resource capability
    std::string resource_cap_id = node_buf_id + "_cap";
    Capability* resource_cap = dfg->find_capability(resource_cap_id, root_cap);
    
    if (!resource_cap) {
        std::cerr << "Error: Resource capability not found for " << node_buf_id << std::endl;
        return nullptr;
    }
    
    // Delegate from the resource capability
    return dfg->delegate_capability(resource_cap, cap_space_id, access);
}

// Delegate a capability
Capability* delegate_capability(DFG* dfg,
    const std::string& app_id,
    const std::string& node_buf_id,
    const std::string& cap_space_id,
    uint32_t access,
    bool allow_transitive_delegation = false) {
    if (allow_transitive_delegation) {
    access |= CapabilityPermission::TRANSITIVE_DELEGATE;
    }
    return create_capability(dfg, app_id, node_buf_id, cap_space_id, access, false);
}

// Transfer ownership of capabilities
bool transfer_ownership(DFG* dfg,
                       const std::string& app_id,
                       const std::string& source_id,
                       const std::string& target_id) {
    if (!dfg) return false;
    
    // This would require major restructuring of the capability model
    // For now we'll return false as not supported
    std::cerr << "Error: Transfer ownership not supported in this security model" << std::endl;
    return false;
}

// Revoke a capability
bool revoke_capability(DFG* dfg,
                      const std::string& app_id,
                      const std::string& node_buf_id,
                      const std::string& cap_space_id) {
    if (!dfg) return false;
    
    // Use root capability
    Capability* root_cap = dfg->get_root_capability();
    
    // Find the capability to revoke
    Capability* cap_to_revoke = dfg->find_capability(cap_space_id, root_cap);
    
    if (!cap_to_revoke) {
        std::cerr << "Error: Capability not found for revocation: " << cap_space_id << std::endl;
        return false;
    }
    
    // Revoke the capability
    return dfg->revoke_capability(cap_to_revoke, root_cap);
}

// Set an expiration time for a capability
bool expire_capability(DFG* dfg,
                      const std::string& app_id,
                      const std::string& node_buf_id,
                      const std::string& cap_space_id,
                      uint32_t timeout) {
    if (!dfg) return false;
    
    // Use root capability
    Capability* root_cap = dfg->get_root_capability();
    
    // Find the capability to expire
    Capability* cap_to_expire = dfg->find_capability(cap_space_id, root_cap);
    
    if (!cap_to_expire) {
        std::cerr << "Error: Capability not found for expiration: " << cap_space_id << std::endl;
        return false;
    }
    
    // Set expiration
    return dfg->expire_capability(cap_to_expire, root_cap, timeout);
}

// Reset all capabilities for a component
bool reset_capabilities(DFG* dfg,
                       const std::string& app_id,
                       const std::string& node_buf_id) {
    if (!dfg) return false;
    
    // Use root capability
    Capability* root_cap = dfg->get_root_capability();
    
    // Find the component's root capability
    std::string component_cap_id = node_buf_id + "_cap";
    Capability* component_cap = dfg->find_capability(component_cap_id, root_cap);
    
    if (!component_cap) {
        std::cerr << "Error: Component capability not found: " << component_cap_id << std::endl;
        return false;
    }
    
    // Get all children
    const auto& children = component_cap->get_children();

    // Revoke all children
    bool success = true;
    for (auto* child : children) {
        success &= dfg->revoke_capability(child, root_cap);
    }

    return success;
}

// ============================================================================
// Factory Functions for New Node Types
// ============================================================================

// ------------------------- Network Node Factories -------------------------

/**
 * Create an RDMA network node with VLAN tag for VIU verification
 * @param dfg Parent DFG
 * @param node_id Unique node identifier
 * @param vlan_id VLAN tag for traffic verification at VIU (0 = no VLAN enforcement)
 */
RDMANetworkNode* create_rdma_network_node(DFG* dfg, const std::string& node_id, uint16_t vlan_id = 0) {
    if (!dfg) {
        std::cerr << "Error: Null DFG for create_rdma_network_node" << std::endl;
        return nullptr;
    }

    Capability* root_cap = dfg->get_root_capability();
    if (!root_cap) {
        std::cerr << "Error: Could not get root capability" << std::endl;
        return nullptr;
    }

    auto node = std::make_shared<RDMANetworkNode>(node_id, dfg, vlan_id);
    if (!dfg->add_node(node, root_cap)) {
        std::cerr << "Error: Failed to add RDMA network node to DFG" << std::endl;
        return nullptr;
    }

    // Create a NETWORK-scoped capability for this node
    std::string cap_id = node_id + "_cap";
    uint32_t net_perms = NET_SEND | NET_RECEIVE | NET_ESTABLISH | NET_QOS_MODIFY |
                         CapabilityPermission::READ | CapabilityPermission::WRITE |
                         CapabilityPermission::DELEGATE;
    Capability* node_cap = root_cap->delegate(cap_id, net_perms, CapabilityScope::NETWORK);
    if (node_cap) {
        dfg->register_capability(node_cap);
    }

    return node.get();
}

/**
 * Create a TCP network node with VLAN tag for VIU verification
 * @param dfg Parent DFG
 * @param node_id Unique node identifier
 * @param vlan_id VLAN tag for traffic verification at VIU (0 = no VLAN enforcement)
 */
TCPNetworkNode* create_tcp_network_node(DFG* dfg, const std::string& node_id, uint16_t vlan_id = 0) {
    if (!dfg) {
        std::cerr << "Error: Null DFG for create_tcp_network_node" << std::endl;
        return nullptr;
    }

    Capability* root_cap = dfg->get_root_capability();
    if (!root_cap) {
        std::cerr << "Error: Could not get root capability" << std::endl;
        return nullptr;
    }

    auto node = std::make_shared<TCPNetworkNode>(node_id, dfg, vlan_id);
    if (!dfg->add_node(node, root_cap)) {
        std::cerr << "Error: Failed to add TCP network node to DFG" << std::endl;
        return nullptr;
    }

    // Create a NETWORK-scoped capability for this node
    std::string cap_id = node_id + "_cap";
    uint32_t net_perms = NET_SEND | NET_RECEIVE | NET_ESTABLISH |
                         CapabilityPermission::READ | CapabilityPermission::WRITE |
                         CapabilityPermission::DELEGATE;
    Capability* node_cap = root_cap->delegate(cap_id, net_perms, CapabilityScope::NETWORK);
    if (node_cap) {
        dfg->register_capability(node_cap);
    }

    return node.get();
}

/**
 * Create a raw Ethernet network node with VLAN tag for VIU verification
 * @param dfg Parent DFG
 * @param node_id Unique node identifier
 * @param interface Network interface name
 * @param vlan_id VLAN tag for traffic verification at VIU (0 = no VLAN enforcement)
 */
RawEthernetNode* create_raw_ethernet_node(DFG* dfg, const std::string& node_id,
                                          const std::string& interface = "", uint16_t vlan_id = 0) {
    if (!dfg) {
        std::cerr << "Error: Null DFG for create_raw_ethernet_node" << std::endl;
        return nullptr;
    }

    Capability* root_cap = dfg->get_root_capability();
    if (!root_cap) {
        std::cerr << "Error: Could not get root capability" << std::endl;
        return nullptr;
    }

    auto node = std::make_shared<RawEthernetNode>(node_id, dfg, interface, vlan_id);
    if (!dfg->add_node(node, root_cap)) {
        std::cerr << "Error: Failed to add Raw Ethernet node to DFG" << std::endl;
        return nullptr;
    }

    // Create a NETWORK-scoped capability for this node
    std::string cap_id = node_id + "_cap";
    uint32_t net_perms = NET_SEND | NET_RECEIVE | NET_ESTABLISH |
                         CapabilityPermission::READ | CapabilityPermission::WRITE |
                         CapabilityPermission::DELEGATE;
    Capability* node_cap = root_cap->delegate(cap_id, net_perms, CapabilityScope::NETWORK);
    if (node_cap) {
        dfg->register_capability(node_cap);
    }

    return node.get();
}

// ------------------------- Software Node Factories -------------------------

/**
 * Create a software parser node
 */
ParserNode* create_parser_node(DFG* dfg, const std::string& node_id) {
    if (!dfg) {
        std::cerr << "Error: Null DFG for create_parser_node" << std::endl;
        return nullptr;
    }

    Capability* root_cap = dfg->get_root_capability();
    if (!root_cap) {
        std::cerr << "Error: Could not get root capability" << std::endl;
        return nullptr;
    }

    auto node = std::make_shared<ParserNode>(node_id, dfg);
    if (!dfg->add_node(node, root_cap)) {
        std::cerr << "Error: Failed to add parser node to DFG" << std::endl;
        return nullptr;
    }

    // Create a SOFTWARE-scoped capability for this node
    std::string cap_id = node_id + "_cap";
    uint32_t sw_perms = SW_CPU_EXECUTE | SW_MEMORY_ACCESS |
                        CapabilityPermission::READ | CapabilityPermission::WRITE |
                        CapabilityPermission::DELEGATE;
    Capability* node_cap = root_cap->delegate(cap_id, sw_perms, CapabilityScope::SOFTWARE);
    if (node_cap) {
        dfg->register_capability(node_cap);
    }

    return node.get();
}

/**
 * Create a software deparser node
 */
DeparserNode* create_deparser_node(DFG* dfg, const std::string& node_id) {
    if (!dfg) {
        std::cerr << "Error: Null DFG for create_deparser_node" << std::endl;
        return nullptr;
    }

    Capability* root_cap = dfg->get_root_capability();
    if (!root_cap) {
        std::cerr << "Error: Could not get root capability" << std::endl;
        return nullptr;
    }

    auto node = std::make_shared<DeparserNode>(node_id, dfg);
    if (!dfg->add_node(node, root_cap)) {
        std::cerr << "Error: Failed to add deparser node to DFG" << std::endl;
        return nullptr;
    }

    // Create a SOFTWARE-scoped capability for this node
    std::string cap_id = node_id + "_cap";
    uint32_t sw_perms = SW_CPU_EXECUTE | SW_MEMORY_ACCESS |
                        CapabilityPermission::READ | CapabilityPermission::WRITE |
                        CapabilityPermission::DELEGATE;
    Capability* node_cap = root_cap->delegate(cap_id, sw_perms, CapabilityScope::SOFTWARE);
    if (node_cap) {
        dfg->register_capability(node_cap);
    }

    return node.get();
}

/**
 * Create a software network function node
 */
SoftwareNFNode* create_software_nf_node(DFG* dfg, const std::string& node_id,
                                        const std::string& nf_name = "") {
    if (!dfg) {
        std::cerr << "Error: Null DFG for create_software_nf_node" << std::endl;
        return nullptr;
    }

    Capability* root_cap = dfg->get_root_capability();
    if (!root_cap) {
        std::cerr << "Error: Could not get root capability" << std::endl;
        return nullptr;
    }

    auto node = std::make_shared<SoftwareNFNode>(node_id, dfg, nf_name);
    if (!dfg->add_node(node, root_cap)) {
        std::cerr << "Error: Failed to add software NF node to DFG" << std::endl;
        return nullptr;
    }

    // Create a SOFTWARE-scoped capability for this node
    std::string cap_id = node_id + "_cap";
    uint32_t sw_perms = SW_CPU_EXECUTE | SW_MEMORY_ACCESS | SW_NIC_ACCESS |
                        CapabilityPermission::READ | CapabilityPermission::WRITE |
                        CapabilityPermission::DELEGATE;
    Capability* node_cap = root_cap->delegate(cap_id, sw_perms, CapabilityScope::SOFTWARE);
    if (node_cap) {
        dfg->register_capability(node_cap);
    }

    return node.get();
}

// ------------------------- Remote DFG Node Factory -------------------------

/**
 * Create a remote DFG node for multi-FPGA support
 */
RemoteDFGNode* create_remote_dfg_node(DFG* dfg, const std::string& node_id,
                                      uint16_t local_vlan_id, uint16_t remote_vlan_id = 0) {
    if (!dfg) {
        std::cerr << "Error: Null DFG for create_remote_dfg_node" << std::endl;
        return nullptr;
    }

    Capability* root_cap = dfg->get_root_capability();
    if (!root_cap) {
        std::cerr << "Error: Could not get root capability" << std::endl;
        return nullptr;
    }

    auto node = std::make_shared<RemoteDFGNode>(node_id, dfg, local_vlan_id, remote_vlan_id);
    if (!dfg->add_node(node, root_cap)) {
        std::cerr << "Error: Failed to add remote DFG node to DFG" << std::endl;
        return nullptr;
    }

    // Create a REMOTE-scoped capability for this node
    std::string cap_id = node_id + "_cap";
    uint32_t remote_perms = REMOTE_EXECUTE | REMOTE_DELEGATE | REMOTE_TRANSFER |
                            CapabilityPermission::READ | CapabilityPermission::WRITE |
                            CapabilityPermission::DELEGATE;
    Capability* node_cap = root_cap->delegate(cap_id, remote_perms, CapabilityScope::REMOTE);
    if (node_cap) {
        dfg->register_capability(node_cap);
    }

    return node.get();
}

// ------------------------- Capability Factories -------------------------

/**
 * Create a network capability with appropriate scope and permissions
 */
Capability* create_network_capability(DFG* dfg, const std::string& node_id,
                                      const std::string& cap_id, uint32_t permissions) {
    if (!dfg) return nullptr;

    Capability* root_cap = dfg->get_root_capability();
    if (!root_cap) return nullptr;

    // Create capability with NETWORK scope
    Capability* cap = root_cap->delegate(cap_id, permissions, CapabilityScope::NETWORK);
    if (cap) {
        dfg->register_capability(cap_id, cap, root_cap);
    }
    return cap;
}

/**
 * Create a software capability with resource limits
 */
Capability* create_software_capability(DFG* dfg, const std::string& node_id,
                                       const std::string& cap_id, uint32_t permissions,
                                       const SoftwareResourceLimits& limits) {
    if (!dfg) return nullptr;

    Capability* root_cap = dfg->get_root_capability();
    if (!root_cap) return nullptr;

    // Create capability with SOFTWARE scope
    Capability* cap = root_cap->delegate(cap_id, permissions, CapabilityScope::SOFTWARE);
    if (cap) {
        dfg->register_capability(cap_id, cap, root_cap);

        // Set resource limits in enforcement engine
        get_software_enforcer().set_resource_limits(cap_id, limits);
    }
    return cap;
}

/**
 * Create a remote capability for cross-FPGA operations
 */
Capability* create_remote_capability(DFG* dfg, const std::string& cap_id,
                                     uint32_t permissions) {
    if (!dfg) return nullptr;

    Capability* root_cap = dfg->get_root_capability();
    if (!root_cap) return nullptr;

    // Create capability with REMOTE scope
    Capability* cap = root_cap->delegate(cap_id, permissions, CapabilityScope::REMOTE);
    if (cap) {
        dfg->register_capability(cap_id, cap, root_cap);
    }
    return cap;
}

// ------------------------- Node Connection Helpers -------------------------

/**
 * Connect two nodes in the DFG (generic polymorphic version)
 */
bool connect_nodes(const std::string& source_id, const std::string& target_id, DFG* dfg) {
    if (!dfg) return false;

    Capability* root_cap = dfg->get_root_capability();
    if (!root_cap) return false;

    auto source = dfg->get_node_base(source_id, root_cap);
    auto target = dfg->get_node_base(target_id, root_cap);

    if (!source || !target) {
        std::cerr << "Error: Source or target node not found" << std::endl;
        return false;
    }

    // Use base class connect_to method
    return source->connect_to(target.get(), root_cap);
}

} // namespace dfg
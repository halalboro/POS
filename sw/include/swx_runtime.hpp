/*
 * POS SWX Runtime
 *
 * DPDK SWX runtime for the FPGA middlebox deployment model.
 * Provides P4-DPDK pipeline execution, host NIC I/O, and DMA buffer management.
 *
 * This is the sole runtime component for SOFTWARE tasks. The API layer
 * (pipeline.hpp) calls into this runtime when SOFTWARE tasks are present.
 *
 * SECURITY NOTE (Future Work):
 * ============================
 * Currently, software capabilities are NOT enforced between SOFTWARE tasks.
 * Security isolation is provided at the DMA buffer boundary (memory capabilities).
 *
 * This means:
 *   - SOFTWARE task can only access the DMA buffers it's connected to
 *   - Multiple SOFTWARE tasks in the same pipeline share address space
 *   - We assume at most ONE SOFTWARE task between endpoint and DMA buffer
 *
 * Edge case NOT handled (future work):
 *   HOST_RX → [SW1] → [SW2] → [SW3] → [DMA Buffer] → FPGA
 *   SW1, SW2, SW3 can access each other's memory - no isolation.
 *
 * To address this in future:
 *   - Implement software capability tokens
 *   - Run each SOFTWARE task in separate process/container
 *   - Use DPDK's multi-process support for isolation
 *
 * Copyright (c) 2025, Systems Group, ETH Zurich
 * MIT License
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <functional>
#include <cstdint>

// Forward declarations for DPDK types
struct rte_mempool;
struct rte_mbuf;
struct rte_swx_pipeline;
struct rte_swx_ctl_pipeline;

namespace pos {

// ============================================================================
// SWX Runtime - Singleton (All-in-One)
// ============================================================================

/**
 * SWX Runtime
 *
 * All-in-one DPDK SWX runtime for middlebox deployment model.
 * Manages: EAL init, pipelines, endpoints, DMA buffers, lcores, poll loops.
 */
class SWXRuntime {
public:
    // ========================================================================
    // Singleton
    // ========================================================================

    static SWXRuntime& instance();

    SWXRuntime(const SWXRuntime&) = delete;
    SWXRuntime& operator=(const SWXRuntime&) = delete;

    // ========================================================================
    // Initialization
    // ========================================================================

    /**
     * Initialize DPDK EAL (lazy - called automatically on first use)
     */
    bool initialize(const std::vector<std::string>& eal_args = {});
    bool isInitialized() const { return initialized_.load(); }
    void shutdown();

    // ========================================================================
    // Pipeline Management
    // ========================================================================

    /**
     * Load a SWX pipeline from .spec file
     * @return Pipeline handle, or -1 on error
     */
    int loadPipeline(const std::string& name, const std::string& spec_path);

    /**
     * Unload a pipeline
     */
    void unloadPipeline(int handle);

    /**
     * Run pipeline on packets
     */
    int runPipeline(int handle, struct rte_mbuf** pkts, int n_pkts);

    // ========================================================================
    // Host Endpoint Management (DPDK NIC ports)
    // ========================================================================

    /**
     * Create a host endpoint
     * @param name Endpoint name
     * @param iface Interface (port ID, PCI address, or name)
     * @param is_rx true for RX, false for TX
     * @return Endpoint handle, or -1 on error
     */
    int createEndpoint(const std::string& name, const std::string& iface, bool is_rx);

    /**
     * Start/stop endpoint
     */
    bool startEndpoint(int handle);
    void stopEndpoint(int handle);

    /**
     * Receive packets from RX endpoint
     */
    int receive(int handle, struct rte_mbuf** pkts, int max_pkts);

    /**
     * Transmit packets to TX endpoint
     */
    int transmit(int handle, struct rte_mbuf** pkts, int n_pkts);

    // ========================================================================
    // DMA Buffer Management (Hugepage-backed shared memory)
    // ========================================================================

    /**
     * Create a DMA buffer
     * @return Buffer handle, or -1 on error
     */
    int createBuffer(const std::string& name, size_t size);

    /**
     * Get buffer addresses
     */
    void* getBufferAddr(int handle);
    uint64_t getBufferPhysAddr(int handle);
    size_t getBufferSize(int handle);

    /**
     * Write/read buffer
     */
    ssize_t writeBuffer(int handle, const void* data, size_t len, size_t offset = 0);
    ssize_t readBuffer(int handle, void* data, size_t len, size_t offset = 0);

    /**
     * Destroy buffer
     */
    void destroyBuffer(int handle);

    // ========================================================================
    // Lcore Management
    // ========================================================================

    unsigned allocateLcore();
    void freeLcore(unsigned lcore_id);
    unsigned getAvailableLcoreCount() const;

    // ========================================================================
    // Software Task Execution (Parser/Deparser poll loops)
    // ========================================================================

    /**
     * Create and start a software task
     * @param name Task name
     * @param spec_path Path to .spec file
     * @param is_parser true = parser, false = deparser
     * @param endpoint_handle RX endpoint (parser) or TX endpoint (deparser)
     * @param buffer_handle DMA buffer handle
     * @param burst_size Packets per burst
     * @return Task handle, or -1 on error
     */
    int createTask(const std::string& name,
                   const std::string& spec_path,
                   bool is_parser,
                   int endpoint_handle,
                   int buffer_handle,
                   uint32_t burst_size = 32);

    /**
     * Stop a task
     */
    void stopTask(int handle);

    /**
     * Check if task is running
     */
    bool isTaskRunning(int handle) const;

    // ========================================================================
    // Packet Allocation
    // ========================================================================

    struct rte_mbuf* allocatePacket();
    void freePacket(struct rte_mbuf* pkt);
    void freePackets(struct rte_mbuf** pkts, int n_pkts);
    struct rte_mempool* getMempool() { return mempool_; }

    // ========================================================================
    // Error Handling
    // ========================================================================

    const std::string& getLastError() const { return last_error_; }

private:
    SWXRuntime();
    ~SWXRuntime();

    // ========================================================================
    // Internal Structures
    // ========================================================================

    struct Pipeline {
        std::string name;
        std::string spec_path;
        struct rte_swx_pipeline* swx = nullptr;
        struct rte_swx_ctl_pipeline* ctl = nullptr;
        bool valid = false;
    };

    struct Endpoint {
        std::string name;
        std::string iface;
        uint16_t port_id = UINT16_MAX;
        bool is_rx = true;
        bool running = false;
        bool valid = false;
    };

    struct Buffer {
        std::string name;
        void* addr = nullptr;
        uint64_t phys_addr = 0;
        size_t size = 0;
        bool valid = false;
    };

    struct Task {
        std::string name;
        int pipeline = -1;
        int endpoint = -1;
        int buffer = -1;
        unsigned lcore_id = UINT_MAX;
        std::atomic<bool> running{false};
        std::atomic<bool> should_stop{false};
        bool is_parser = true;
        uint32_t burst_size = 32;
        bool valid = false;
    };

    // ========================================================================
    // State
    // ========================================================================

    std::atomic<bool> initialized_{false};
    std::mutex init_mutex_;

    struct rte_mempool* mempool_ = nullptr;
    std::vector<Pipeline> pipelines_;
    std::vector<Endpoint> endpoints_;
    std::vector<Buffer> buffers_;
    std::vector<Task> tasks_;
    mutable std::mutex resource_mutex_;

    std::vector<bool> lcore_allocated_;
    unsigned main_lcore_ = 0;
    mutable std::mutex lcore_mutex_;

    std::string last_error_;
    void setError(const std::string& msg);

    // ========================================================================
    // Internal Helpers
    // ========================================================================

    std::vector<std::string> getDefaultEalArgs();
    bool initEal(const std::vector<std::string>& args);
    bool initMempool();
    bool initLcores();
    bool findPortByName(const std::string& iface, uint16_t& port_id);
    bool configurePort(uint16_t port_id, bool is_rx);

    // Poll loops
    static int parserLoopWrapper(void* arg);
    static int deparserLoopWrapper(void* arg);
    void parserLoop(Task* task);
    void deparserLoop(Task* task);
};

} // namespace pos

/*
 * POS SWX Runtime Implementation
 *
 * All-in-one DPDK SWX runtime for the middlebox deployment model.
 *
 * Copyright (c) 2025, Systems Group, ETH Zurich
 * MIT License
 */

#include "swx_runtime.hpp"

#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_memzone.h>
#include <rte_malloc.h>
#include <rte_ethdev.h>
#include <rte_swx_pipeline.h>
#include <rte_swx_ctl.h>
#include <rte_errno.h>

#include <iostream>
#include <sstream>
#include <cstring>
#include <climits>

namespace pos {

// Configuration constants
static constexpr unsigned MEMPOOL_CACHE_SIZE = 256;
static constexpr unsigned NUM_MBUFS = 8191;
static constexpr uint16_t RX_RING_SIZE = 1024;
static constexpr uint16_t TX_RING_SIZE = 1024;

// ============================================================================
// Singleton
// ============================================================================

SWXRuntime& SWXRuntime::instance() {
    static SWXRuntime inst;
    return inst;
}

SWXRuntime::SWXRuntime() {}

SWXRuntime::~SWXRuntime() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool SWXRuntime::initialize(const std::vector<std::string>& eal_args) {
    std::lock_guard<std::mutex> lock(init_mutex_);

    if (initialized_.load()) return true;

    auto args = eal_args.empty() ? getDefaultEalArgs() : eal_args;

    if (!initEal(args)) return false;
    if (!initMempool()) return false;
    if (!initLcores()) return false;

    initialized_.store(true);
    std::cout << "[SWXRuntime] Initialized with " << getAvailableLcoreCount() << " lcores" << std::endl;
    return true;
}

std::vector<std::string> SWXRuntime::getDefaultEalArgs() {
    return {"pos_swx", "-l", "0-3", "-n", "4", "--proc-type=auto", "--log-level=5"};
}

bool SWXRuntime::initEal(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    int ret = rte_eal_init(static_cast<int>(args.size()), argv.data());
    if (ret < 0) {
        setError("rte_eal_init failed: " + std::string(rte_strerror(rte_errno)));
        return false;
    }
    return true;
}

bool SWXRuntime::initMempool() {
    mempool_ = rte_pktmbuf_pool_create("pos_swx_pool", NUM_MBUFS, MEMPOOL_CACHE_SIZE,
                                        0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (!mempool_) {
        setError("rte_pktmbuf_pool_create failed: " + std::string(rte_strerror(rte_errno)));
        return false;
    }
    return true;
}

bool SWXRuntime::initLcores() {
    std::lock_guard<std::mutex> lock(lcore_mutex_);
    main_lcore_ = rte_get_main_lcore();
    lcore_allocated_.resize(RTE_MAX_LCORE, false);
    lcore_allocated_[main_lcore_] = true;

    for (unsigned i = 0; i < RTE_MAX_LCORE; i++) {
        if (!rte_lcore_is_enabled(i)) lcore_allocated_[i] = true;
    }
    return true;
}

void SWXRuntime::shutdown() {
    if (!initialized_.load()) return;

    std::cout << "[SWXRuntime] Shutting down..." << std::endl;

    // Stop all tasks
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        for (auto& task : tasks_) {
            if (task.valid && task.running.load()) {
                task.should_stop.store(true);
                rte_eal_wait_lcore(task.lcore_id);
            }
        }
        tasks_.clear();
    }

    // Free pipelines
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        for (auto& p : pipelines_) {
            if (p.valid) {
                if (p.ctl) rte_swx_ctl_pipeline_free(p.ctl);
                if (p.swx) rte_swx_pipeline_free(p.swx);
            }
        }
        pipelines_.clear();
    }

    // Stop endpoints
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        for (auto& ep : endpoints_) {
            if (ep.valid && ep.running) {
                rte_eth_dev_stop(ep.port_id);
            }
        }
        endpoints_.clear();
    }

    // Free buffers
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        for (auto& buf : buffers_) {
            if (buf.valid && buf.addr) {
                const struct rte_memzone* mz = rte_memzone_lookup(buf.name.c_str());
                if (mz) rte_memzone_free(mz);
                else rte_free(buf.addr);
            }
        }
        buffers_.clear();
    }

    if (mempool_) {
        rte_mempool_free(mempool_);
        mempool_ = nullptr;
    }

    rte_eal_cleanup();
    initialized_.store(false);
    std::cout << "[SWXRuntime] Shutdown complete" << std::endl;
}

// ============================================================================
// Pipeline Management
// ============================================================================

int SWXRuntime::loadPipeline(const std::string& name, const std::string& spec_path) {
    if (!initialized_.load() && !initialize()) return -1;

    std::lock_guard<std::mutex> lock(resource_mutex_);

    // Find free slot or add new
    int handle = -1;
    for (size_t i = 0; i < pipelines_.size(); i++) {
        if (!pipelines_[i].valid) { handle = static_cast<int>(i); break; }
    }
    if (handle < 0) {
        handle = static_cast<int>(pipelines_.size());
        pipelines_.push_back({});
    }

    Pipeline& p = pipelines_[handle];
    p.name = name;
    p.spec_path = spec_path;

    // Create pipeline
    int ret = rte_swx_pipeline_config(&p.swx, 0);
    if (ret) {
        setError("rte_swx_pipeline_config failed");
        return -1;
    }

    // Build from spec
    FILE* f = fopen(spec_path.c_str(), "r");
    if (!f) {
        setError("Cannot open spec file: " + spec_path);
        rte_swx_pipeline_free(p.swx);
        return -1;
    }

    ret = rte_swx_pipeline_build_from_spec(p.swx, f, nullptr, nullptr);
    fclose(f);

    if (ret) {
        setError("rte_swx_pipeline_build_from_spec failed");
        rte_swx_pipeline_free(p.swx);
        return -1;
    }

    // Create control interface
    p.ctl = rte_swx_ctl_pipeline_create(p.swx);
    if (!p.ctl) {
        setError("rte_swx_ctl_pipeline_create failed");
        rte_swx_pipeline_free(p.swx);
        return -1;
    }

    p.valid = true;
    std::cout << "[SWXRuntime] Loaded pipeline '" << name << "'" << std::endl;
    return handle;
}

void SWXRuntime::unloadPipeline(int handle) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    if (handle < 0 || handle >= static_cast<int>(pipelines_.size())) return;

    Pipeline& p = pipelines_[handle];
    if (!p.valid) return;

    if (p.ctl) rte_swx_ctl_pipeline_free(p.ctl);
    if (p.swx) rte_swx_pipeline_free(p.swx);
    p.valid = false;
}

int SWXRuntime::runPipeline(int handle, struct rte_mbuf** pkts, int n_pkts) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    if (handle < 0 || handle >= static_cast<int>(pipelines_.size())) return 0;
    if (!pipelines_[handle].valid) return 0;

    rte_swx_pipeline_run(pipelines_[handle].swx, static_cast<uint32_t>(n_pkts));
    return n_pkts;
}

// ============================================================================
// Host Endpoint Management
// ============================================================================

int SWXRuntime::createEndpoint(const std::string& name, const std::string& iface, bool is_rx) {
    if (!initialized_.load() && !initialize()) return -1;

    std::lock_guard<std::mutex> lock(resource_mutex_);

    int handle = -1;
    for (size_t i = 0; i < endpoints_.size(); i++) {
        if (!endpoints_[i].valid) { handle = static_cast<int>(i); break; }
    }
    if (handle < 0) {
        handle = static_cast<int>(endpoints_.size());
        endpoints_.push_back({});
    }

    Endpoint& ep = endpoints_[handle];
    ep.name = name;
    ep.iface = iface;
    ep.is_rx = is_rx;

    if (!findPortByName(iface, ep.port_id)) return -1;
    if (!configurePort(ep.port_id, is_rx)) return -1;

    ep.valid = true;
    std::cout << "[SWXRuntime] Created endpoint '" << name << "' on port " << ep.port_id << std::endl;
    return handle;
}

bool SWXRuntime::findPortByName(const std::string& iface, uint16_t& port_id) {
    uint16_t nb_ports = rte_eth_dev_count_avail();
    if (nb_ports == 0) {
        setError("No Ethernet ports available");
        return false;
    }

    // Try as port ID
    try {
        unsigned long id = std::stoul(iface);
        if (id < nb_ports) {
            port_id = static_cast<uint16_t>(id);
            return true;
        }
    } catch (...) {}

    // Try to find by name
    uint16_t pid;
    RTE_ETH_FOREACH_DEV(pid) {
        struct rte_eth_dev_info info;
        if (rte_eth_dev_info_get(pid, &info) == 0) {
            const char* dev_name = rte_dev_name(info.device);
            if (dev_name && (iface == dev_name || strstr(dev_name, iface.c_str()))) {
                port_id = pid;
                return true;
            }
        }
    }

    // Default to first port
    if (nb_ports == 1) {
        port_id = 0;
        return true;
    }

    setError("Port not found: " + iface);
    return false;
}

bool SWXRuntime::configurePort(uint16_t port_id, bool is_rx) {
    struct rte_eth_conf conf = {};
    conf.rxmode.mq_mode = RTE_ETH_MQ_RX_NONE;
    conf.txmode.mq_mode = RTE_ETH_MQ_TX_NONE;

    int ret = rte_eth_dev_configure(port_id, 1, 1, &conf);
    if (ret) {
        setError("rte_eth_dev_configure failed");
        return false;
    }

    int socket_id = rte_eth_dev_socket_id(port_id);

    ret = rte_eth_rx_queue_setup(port_id, 0, RX_RING_SIZE, socket_id, nullptr, mempool_);
    if (ret) {
        setError("rte_eth_rx_queue_setup failed");
        return false;
    }

    ret = rte_eth_tx_queue_setup(port_id, 0, TX_RING_SIZE, socket_id, nullptr);
    if (ret) {
        setError("rte_eth_tx_queue_setup failed");
        return false;
    }

    return true;
}

bool SWXRuntime::startEndpoint(int handle) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    if (handle < 0 || handle >= static_cast<int>(endpoints_.size())) return false;

    Endpoint& ep = endpoints_[handle];
    if (!ep.valid || ep.running) return ep.running;

    int ret = rte_eth_dev_start(ep.port_id);
    if (ret) {
        setError("rte_eth_dev_start failed");
        return false;
    }

    if (ep.is_rx) rte_eth_promiscuous_enable(ep.port_id);

    ep.running = true;
    return true;
}

void SWXRuntime::stopEndpoint(int handle) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    if (handle < 0 || handle >= static_cast<int>(endpoints_.size())) return;

    Endpoint& ep = endpoints_[handle];
    if (!ep.valid || !ep.running) return;

    rte_eth_dev_stop(ep.port_id);
    ep.running = false;
}

int SWXRuntime::receive(int handle, struct rte_mbuf** pkts, int max_pkts) {
    if (handle < 0 || handle >= static_cast<int>(endpoints_.size())) return 0;
    Endpoint& ep = endpoints_[handle];
    if (!ep.valid || !ep.running || !ep.is_rx) return 0;

    return rte_eth_rx_burst(ep.port_id, 0, pkts, static_cast<uint16_t>(max_pkts));
}

int SWXRuntime::transmit(int handle, struct rte_mbuf** pkts, int n_pkts) {
    if (handle < 0 || handle >= static_cast<int>(endpoints_.size())) return 0;
    Endpoint& ep = endpoints_[handle];
    if (!ep.valid || !ep.running || ep.is_rx) return 0;

    uint16_t sent = rte_eth_tx_burst(ep.port_id, 0, pkts, static_cast<uint16_t>(n_pkts));

    // Free unsent
    for (uint16_t i = sent; i < n_pkts; i++) rte_pktmbuf_free(pkts[i]);

    return sent;
}

// ============================================================================
// DMA Buffer Management
// ============================================================================

int SWXRuntime::createBuffer(const std::string& name, size_t size) {
    if (!initialized_.load() && !initialize()) return -1;

    std::lock_guard<std::mutex> lock(resource_mutex_);

    int handle = -1;
    for (size_t i = 0; i < buffers_.size(); i++) {
        if (!buffers_[i].valid) { handle = static_cast<int>(i); break; }
    }
    if (handle < 0) {
        handle = static_cast<int>(buffers_.size());
        buffers_.push_back({});
    }

    Buffer& buf = buffers_[handle];
    buf.name = name;
    buf.size = size;

    // Try memzone (hugepage, contiguous)
    const struct rte_memzone* mz = rte_memzone_reserve_aligned(
        name.c_str(), size, 0, RTE_MEMZONE_IOVA_CONTIG, 4096);

    if (mz) {
        buf.addr = mz->addr;
        buf.phys_addr = mz->iova;
    } else {
        // Fallback
        buf.addr = rte_malloc_socket(name.c_str(), size, 4096, 0);
        if (!buf.addr) {
            setError("Buffer allocation failed");
            return -1;
        }
        buf.phys_addr = rte_malloc_virt2iova(buf.addr);
    }

    memset(buf.addr, 0, size);
    buf.valid = true;

    std::cout << "[SWXRuntime] Created buffer '" << name << "' size=" << size << std::endl;
    return handle;
}

void* SWXRuntime::getBufferAddr(int handle) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    if (handle < 0 || handle >= static_cast<int>(buffers_.size())) return nullptr;
    return buffers_[handle].valid ? buffers_[handle].addr : nullptr;
}

uint64_t SWXRuntime::getBufferPhysAddr(int handle) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    if (handle < 0 || handle >= static_cast<int>(buffers_.size())) return 0;
    return buffers_[handle].valid ? buffers_[handle].phys_addr : 0;
}

size_t SWXRuntime::getBufferSize(int handle) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    if (handle < 0 || handle >= static_cast<int>(buffers_.size())) return 0;
    return buffers_[handle].valid ? buffers_[handle].size : 0;
}

ssize_t SWXRuntime::writeBuffer(int handle, const void* data, size_t len, size_t offset) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    if (handle < 0 || handle >= static_cast<int>(buffers_.size())) return -1;

    Buffer& buf = buffers_[handle];
    if (!buf.valid || offset + len > buf.size) return -1;

    memcpy(static_cast<uint8_t*>(buf.addr) + offset, data, len);
    return static_cast<ssize_t>(len);
}

ssize_t SWXRuntime::readBuffer(int handle, void* data, size_t len, size_t offset) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    if (handle < 0 || handle >= static_cast<int>(buffers_.size())) return -1;

    Buffer& buf = buffers_[handle];
    if (!buf.valid || offset + len > buf.size) return -1;

    memcpy(data, static_cast<uint8_t*>(buf.addr) + offset, len);
    return static_cast<ssize_t>(len);
}

void SWXRuntime::destroyBuffer(int handle) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    if (handle < 0 || handle >= static_cast<int>(buffers_.size())) return;

    Buffer& buf = buffers_[handle];
    if (!buf.valid) return;

    const struct rte_memzone* mz = rte_memzone_lookup(buf.name.c_str());
    if (mz) rte_memzone_free(mz);
    else if (buf.addr) rte_free(buf.addr);

    buf.valid = false;
}

// ============================================================================
// Lcore Management
// ============================================================================

unsigned SWXRuntime::allocateLcore() {
    std::lock_guard<std::mutex> lock(lcore_mutex_);

    unsigned lcore_id;
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        if (!lcore_allocated_[lcore_id]) {
            lcore_allocated_[lcore_id] = true;
            return lcore_id;
        }
    }

    setError("No available lcores");
    return UINT_MAX;
}

void SWXRuntime::freeLcore(unsigned lcore_id) {
    std::lock_guard<std::mutex> lock(lcore_mutex_);
    if (lcore_id < RTE_MAX_LCORE && lcore_id != main_lcore_) {
        lcore_allocated_[lcore_id] = false;
    }
}

unsigned SWXRuntime::getAvailableLcoreCount() const {
    std::lock_guard<std::mutex> lock(lcore_mutex_);
    unsigned count = 0;
    unsigned lcore_id;
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        if (!lcore_allocated_[lcore_id]) count++;
    }
    return count;
}

// ============================================================================
// Software Task Execution
// ============================================================================

int SWXRuntime::createTask(const std::string& name, const std::string& spec_path,
                           bool is_parser, int endpoint_handle, int buffer_handle,
                           uint32_t burst_size)
{
    if (!initialized_.load() && !initialize()) return -1;

    // Load pipeline
    int pipeline = loadPipeline(name + "_pipeline", spec_path);
    if (pipeline < 0) return -1;

    // Allocate lcore
    unsigned lcore = allocateLcore();
    if (lcore == UINT_MAX) {
        unloadPipeline(pipeline);
        return -1;
    }

    std::lock_guard<std::mutex> lock(resource_mutex_);

    int handle = -1;
    for (size_t i = 0; i < tasks_.size(); i++) {
        if (!tasks_[i].valid) { handle = static_cast<int>(i); break; }
    }
    if (handle < 0) {
        handle = static_cast<int>(tasks_.size());
        tasks_.push_back({});
    }

    Task& task = tasks_[handle];
    task.name = name;
    task.pipeline = pipeline;
    task.endpoint = endpoint_handle;
    task.buffer = buffer_handle;
    task.lcore_id = lcore;
    task.is_parser = is_parser;
    task.burst_size = burst_size;
    task.should_stop.store(false);
    task.running.store(false);
    task.valid = true;

    // Start endpoint
    startEndpoint(endpoint_handle);

    // Launch poll loop
    int (*loop_fn)(void*) = is_parser ? parserLoopWrapper : deparserLoopWrapper;
    int ret = rte_eal_remote_launch(loop_fn, &task, lcore);
    if (ret) {
        setError("rte_eal_remote_launch failed");
        task.valid = false;
        freeLcore(lcore);
        return -1;
    }

    task.running.store(true);
    std::cout << "[SWXRuntime] Started " << (is_parser ? "parser" : "deparser")
              << " task '" << name << "' on lcore " << lcore << std::endl;

    return handle;
}

void SWXRuntime::stopTask(int handle) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    if (handle < 0 || handle >= static_cast<int>(tasks_.size())) return;

    Task& task = tasks_[handle];
    if (!task.valid || !task.running.load()) return;

    task.should_stop.store(true);
    rte_eal_wait_lcore(task.lcore_id);
    task.running.store(false);

    freeLcore(task.lcore_id);
    std::cout << "[SWXRuntime] Stopped task '" << task.name << "'" << std::endl;
}

bool SWXRuntime::isTaskRunning(int handle) const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    if (handle < 0 || handle >= static_cast<int>(tasks_.size())) return false;
    return tasks_[handle].valid && tasks_[handle].running.load();
}

// ============================================================================
// Poll Loops
// ============================================================================

int SWXRuntime::parserLoopWrapper(void* arg) {
    Task* task = static_cast<Task*>(arg);
    SWXRuntime::instance().parserLoop(task);
    return 0;
}

int SWXRuntime::deparserLoopWrapper(void* arg) {
    Task* task = static_cast<Task*>(arg);
    SWXRuntime::instance().deparserLoop(task);
    return 0;
}

void SWXRuntime::parserLoop(Task* task) {
    struct rte_mbuf* pkts[64];
    const uint32_t burst = task->burst_size;

    std::cout << "[SWXRuntime] Parser loop started" << std::endl;

    while (!task->should_stop.load()) {
        // RX from endpoint
        int n = receive(task->endpoint, pkts, burst);
        if (n == 0) {
            _mm_pause();
            continue;
        }

        // Run parser pipeline
        runPipeline(task->pipeline, pkts, n);

        // Write to DMA buffer
        void* buf_addr = getBufferAddr(task->buffer);
        if (buf_addr) {
            size_t offset = 0;
            for (int i = 0; i < n; i++) {
                uint8_t* pkt_data = rte_pktmbuf_mtod(pkts[i], uint8_t*);
                uint32_t pkt_len = rte_pktmbuf_pkt_len(pkts[i]);

                // Write length + data
                memcpy(static_cast<uint8_t*>(buf_addr) + offset, &pkt_len, sizeof(pkt_len));
                offset += sizeof(pkt_len);
                memcpy(static_cast<uint8_t*>(buf_addr) + offset, pkt_data, pkt_len);
                offset += pkt_len;
            }
        }

        // Free mbufs
        freePackets(pkts, n);
    }

    std::cout << "[SWXRuntime] Parser loop exited" << std::endl;
}

void SWXRuntime::deparserLoop(Task* task) {
    const uint32_t burst = task->burst_size;

    std::cout << "[SWXRuntime] Deparser loop started" << std::endl;

    while (!task->should_stop.load()) {
        // Read from DMA buffer
        void* buf_addr = getBufferAddr(task->buffer);
        if (!buf_addr) {
            _mm_pause();
            continue;
        }

        // Simple implementation: read packets, run deparser, TX
        // In real impl, need proper sync with FPGA

        _mm_pause();  // Placeholder - needs FPGA sync
    }

    std::cout << "[SWXRuntime] Deparser loop exited" << std::endl;
}

// ============================================================================
// Packet Allocation
// ============================================================================

struct rte_mbuf* SWXRuntime::allocatePacket() {
    return mempool_ ? rte_pktmbuf_alloc(mempool_) : nullptr;
}

void SWXRuntime::freePacket(struct rte_mbuf* pkt) {
    if (pkt) rte_pktmbuf_free(pkt);
}

void SWXRuntime::freePackets(struct rte_mbuf** pkts, int n_pkts) {
    for (int i = 0; i < n_pkts; i++) {
        if (pkts[i]) rte_pktmbuf_free(pkts[i]);
    }
}

// ============================================================================
// Error Handling
// ============================================================================

void SWXRuntime::setError(const std::string& msg) {
    last_error_ = msg;
    std::cerr << "[SWXRuntime] Error: " << msg << std::endl;
}

} // namespace pos

#pragma once

#include "cDefs.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <functional>
#include <unordered_map>
#include <unordered_set> 
#include <boost/functional/hash.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#ifdef EN_AVX
#include <x86intrin.h>
#include <smmintrin.h>
#include <immintrin.h>
#endif
#include <unistd.h>
#include <errno.h>
#include <byteswap.h>
#include <iostream>
#include <fcntl.h>
#include <inttypes.h>
#include <mutex>
#include <atomic>
#include <sys/mman.h>
#include <sys/types.h>
#include <thread>
#include <sys/ioctl.h>
#include <fstream>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <thread>

#include "cSched.hpp"
#include "cTask.hpp"
#include "bThread.hpp"

using namespace std;
using namespace boost::interprocess;
namespace fpga {

/**
 * @brief Coyote thread, a single thread of execution within vFPGAs
 * 
 * General notion: One cThread deals with one vFPGA and has both the memory mapping and control function to control all operations of this vFPGA in interaction with the services of the dynamic layer
 * cThreads inherit from bThreads -> Check these later as well 
 */

template<typename Cmpl>
class cThread : public bThread {
protected: 

    /* Task queue */
    mutex mtx_task;
    condition_variable cv_task; // Condition to wait for / on with the mutex declared before 
    queue<std::unique_ptr<bTask<Cmpl>>> task_queue; // Queue with pointers to bTasks that can be assigned to cThreads for execution 

    /* Completion queue */
    mutex mtx_cmpl; // Condition to wait for / on with the mutex declared before.
    queue<std::pair<int32_t, Cmpl>> cmpl_queue; // Every entry has two components: A 32-Bit task-ID and the completion-element
    std::atomic<int32_t> cnt_cmpl = { 0 }; // Counter for completed operations. Declared as atomic to make it threadsafe in this context. 

public:

	/**
	 * @brief Ctor, Dtor
	 * The cThread inherits from the bThread. Thus, the constructor of the bThread is called with the arguments for vfid, hpid, dev, scheduler and uisr
	 */
	cThread(int32_t vfid, pid_t hpid, uint32_t dev, cSched *csched = nullptr, void (*uisr)(int) = nullptr) :
        bThread(vfid, hpid, dev, csched, uisr) { 
            # ifdef VERBOSE
                std::cout << "cThread: Created an instance with vfid " << vfid << ", hpid " << hpid << ", device " << dev << std::endl; 
            # endif 
        }
	
    // Destructor of the cThread to kill it at the end of its lifetime 
    ~cThread() {
        // If the thread is running at the time of destruction, stop it running, write debugging message and let it join (wait for completion before final destruction)
        if(run) {
            run = false;

            # ifdef VERBOSE
                std::cout << "cThread: Called the destructor." << std::endl; 
            # endif 

            DBG3("cThread:  joining");
            c_thread.join();
        }
    }

    /**
     * @brief Task execution
     * 
     * Interestingly, this cThread uses a regular thread in the background 
     * 
     * @param ctask - lambda to be scheduled
     * 
     */
    void start() {
        # ifdef VERBOSE
            std::cout << "cThread: Called the start()-function" << std::endl; 
        # endif 

        // Set run to true to indicate that the thread is now running 
        run = true;

        // Lock the mutex in the first place to secure the thread 
        unique_lock<mutex> lck(mtx_task);
        DBG3("cThread:  initial lock");

        // Create a new thread-object, give it the process-function defined here and a pointer to itself for execution 
        # ifdef VERBOSE
            std::cout << "cThread: Kicked off the cThread for processing tasks." << std::endl; 
        # endif
        c_thread = thread(&cThread::processTasks, this);
        DBG3("cThread:  thread started");

        // Wait on the condition-variable (until completion of the task?)
        cv_task.wait(lck);
    }

    // Takes a smart pointer to a bTask (which holds reference for completion) and places it in the task queue for later execution
    void scheduleTask(std::unique_ptr<bTask<Cmpl>> ctask) {
        # ifdef VERBOSE
            std::cout << "cThread: Called the scheduleTask() to place a new bTask in the execution queue." << std::endl; 
        # endif 

        lock_guard<mutex> lck2(mtx_task); // Lock the mutex for the duration of the execution of this function to be thread-safe
        task_queue.emplace(std::move(ctask)); // Places the ctask in the task-queue. Uses move to hand over the object itself rather than a copy (important for smart pointers)
    }

    // Checks if there's an entry in the completion queue that shows a completed task 
    bool getTaskCompletedNext(int32_t tid, Cmpl &cmpl) {
        // Check if there's a completion event available in the queue
        # ifdef VERBOSE
            std::cout << "cThread: Called the getTaskCompletedNext() to check if there's a completion event available in the queue." << std::endl; 
        # endif
        if(!cmpl_queue.empty()) {
            lock_guard<mutex> lck(mtx_cmpl); // Lock before interacting with the queue for thread-safety 

            // Get the task ID and the completion element from the front of the completion queue 
            tid = std::get<0>(cmpl_queue.front());
            cmpl = std::get<1>(cmpl_queue.front());
            # ifdef VERBOSE
                std::cout << "cThread: Got the tid from the completion queue: " << tid << std::endl; 
            # endif

            // Pop the first element in the queue 
            cmpl_queue.pop();
            return true;
        } else {
            return false;
        }
    }

    // Returns the current count of completed functions 
    inline auto getTaskCompletedCnt() { return cnt_cmpl.load(); }

    // Returns the current size of the task-queue 
    inline auto getTaskQueueSize() { return task_queue.size(); }


protected:
    /* Task execution */
    void processTasks() {
        # ifdef VERBOSE
            std::cout << "cThread: Called the processTasks()-function in the executor-thread." << std::endl; 
        # endif

        // Create a completion code and a lock as starting conditions for task-processing 
        Cmpl cmpl_code;
        unique_lock<mutex> lck(mtx_task);
        run = true;

        // Unlock the lock as long as nothing is happening
        lck.unlock();

        // Notify a waiting thread to start processing 
        cv_task.notify_one();

        // Processing continues as long as run is true or there are items left for processing in the task queue 
        while(run || !task_queue.empty()) {
            // Lock the lock for safe access to the task queue 
            lck.lock();

            // Check for the first element in the task queue that can be fetched and then processed 
            if(!task_queue.empty()) {
                if(task_queue.front() != nullptr) {
                    
                    // Remove next task from the queue
                    auto curr_task = std::move(const_cast<std::unique_ptr<bTask<Cmpl>>&>(task_queue.front()));
                    task_queue.pop();
                    lck.unlock(); // Unlock the lock since the thread-sensitive interaction with the task-queue is done 

                    # ifdef VERBOSE
                        std::cout << "cThread: Pulled a task from the task_queue with vfid " << getVfid() << ", task ID " << curr_task->getTid() << ", oid " << curr_task->getOid() << " and priority " << curr_task->getPriority() << std::endl; 
                    # endif

                    DBG3("Process task: vfid: " <<  getVfid() << ", tid: " << curr_task->getTid() 
                        << ", oid: " << curr_task->getOid() << ", prio: " << curr_task->getPriority());

                    // Run the task and safe the generated completion code for this
                    # ifdef VERBOSE
                        std::cout << "cThread: Called the run()-function on the current task." << std::endl; 
                    # endif            
                    cmpl_code = curr_task->run(this);

                    // Completion
                    cnt_cmpl++; // Count up the completion counter 
                    # ifdef VERBOSE
                        std::cout << "cThread: Current completion counter: " << cnt_cmpl << std::endl; 
                    # endif

                    // Close the lock for thread-safety, enqueue the completion in the queue and open the lock again after this critical operation 
                    mtx_cmpl.lock();
                    cmpl_queue.push({curr_task->getTid(), cmpl_code});
                    mtx_cmpl.unlock();
                    
                } else {
                    task_queue.pop();
                    lck.unlock();
                }
            } else {
                lck.unlock();
            }

            // Wait for a short period in time (not sure why though)
            nanosleep(&PAUSE, NULL);
        }
    }

	const ibvQp* getQpair() const { return qpair.get(); }

};

} /* namespace fpga */

// Include cOps for CoyoteOper, syncSg, localSg, rdmaSg, rdmaSgConn, tcpSg, CoyoteAlloc
#include "cOps.hpp"

namespace coyote {

/**
 * @brief The cThread class is the core component of Coyote for interacting with vFPGAs
 *
 * This class provides methods for memory management, data transfer operations, and synchronization
 * with the vFPGA device. It also handles user interrupts and out-of-band set-up for RDMA operations.
 * It abstracts the interaction with the char vfpga_device in the driver, providing
 * a high-level interface for Coyote operations.
 */
class cThread {
protected:
    /// vFPGA device file descriptor
    int32_t fd = { 0 };

    /// vFPGA virtual ID
    int32_t vfid = { -1 };

    /// Coyote thread ID
    int32_t ctid = { -1 };

    /// Host process ID
    pid_t hpid = { 0 };

    /// Shell configuration, as set by the user in CMake config
    fpga::fpgaCnfg fcnfg;

    /// RDMA queue pair (legacy single-QP)
    std::unique_ptr<fpga::ibvQp> qpair;

    /// Multiple RDMA connections for multi-FPGA scenarios
    std::unordered_map<std::string, std::unique_ptr<fpga::ibvConnection>> connections;

    /// Next available QPN for new connections
    uint16_t next_qpn = 0;

    /// Number data transfer commands sent to the vFPGA
    uint32_t cmd_cnt = { 0 };

    /// User interrupt file descriptor
    int32_t efd = { -1 };

    /// Termination event file descriptor for stopping the user interrupt thread
    int32_t terminate_efd = { -1 };

    /// Dedicated thread for handling user interrupts
    std::thread event_thread;

    /// vFPGA config registers, if AVX is enabled
    #ifdef EN_AVX
    volatile __m256i *cnfg_reg_avx = { 0 };
    #endif

    /// vFPGA config registers, if AVX is disabled
    volatile uint64_t *cnfg_reg = { 0 };

    /// User-defined control registers
    volatile uint64_t *ctrl_reg = { 0 };

    /// Pointer to writeback region, if enabled
    volatile uint32_t *wback = { 0 };

    /// A map of all the pages that have been allocated and mapped for this thread
    std::unordered_map<void*, CoyoteAlloc> mapped_pages;

    /// Out-of-band connection file descriptor (legacy single connection)
    int connfd = { -1 };

    /// Out-of-band socket file descriptor
    int sockfd = { -1 };

    /// Set to true if there is an active out-of-band connection
    bool is_connected;

    /// Inter-process vFPGA lock
    boost::interprocess::named_mutex vlock;

    /// Set to true if the vFPGA lock is acquired by this cThread
    bool lock_acquired = { false };

    /// Utility function, memory mapping all the vFPGA control registers
    void mmapFpga();

    /// Utility function, unmapping all the vFPGA control registers
    void munmapFpga();

    /// Posts a DMA command to the vFPGA
    void postCmd(uint64_t offs_3, uint64_t offs_2, uint64_t offs_1, uint64_t offs_0);

    /// Sends an ack to the connected remote node
    void sendAck(uint32_t ack);

    /// Reads an ack from the connected remote node
    uint32_t readAck();

    /// Helper: Sends an ack on a specific file descriptor (for multi-connection support)
    void sendAckOnFd(int fd, uint32_t ack);

    /// Helper: Reads an ack from a specific file descriptor (for multi-connection support)
    uint32_t readAckOnFd(int fd);

public:
    /// Writes an IP address to a config register for ARP lookup
    void doArpLookup(uint32_t ip_addr);

    /// Writes the exchanged QP information to the vFPGA config registers (legacy single-QP)
    void writeQpContext(uint32_t port);

    /**
     * @brief Default constructor for the cThread
     * @param vfid Virtual FPGA ID
     * @param hpid Host process ID
     * @param device Device number
     * @param uisr User interrupt service routine
     */
    cThread(int32_t vfid, pid_t hpid, uint32_t device = 0, std::function<void(int)> uisr = nullptr);

    /// Default destructor
    ~cThread();

    /// Maps a buffer to the vFPGAs TLB
    void userMap(void *vaddr, uint32_t len);

    /// Unmaps a buffer from the vFPGAs TLB
    void userUnmap(void *vaddr);

    /// Allocates memory and maps it into the vFPGA's TLB
    void* getMem(CoyoteAlloc&& alloc);

    /// Frees and unmaps previously allocated memory
    void freeMem(void* vaddr);

    /// Sets a control register in the vFPGA
    void setCSR(uint64_t val, uint32_t offs);

    /// Reads from a register in the vFPGA
    uint64_t getCSR(uint32_t offs) const;

    /// Invokes a sync or offload operation
    void invoke(CoyoteOper oper, syncSg sg);

    /// Invokes a one-sided local operation
    void invoke(CoyoteOper oper, localSg sg, bool last = true);

    /// Invokes a two-sided local operation
    void invoke(CoyoteOper oper, localSg src_sg, localSg dst_sg, bool last = true);

    /// Invokes an RDMA operation (legacy single-QP)
    void invoke(CoyoteOper oper, rdmaSg sg, bool last = true);

    /**
     * @brief Invokes an RDMA operation on a specific named connection
     *
     * This is used for multi-FPGA scenarios where a single vFPGA needs to communicate
     * with multiple remote FPGAs. The connection name in sg specifies which QP to use.
     *
     * @param oper Operation to invoke (REMOTE_RDMA_READ, REMOTE_RDMA_WRITE, REMOTE_RDMA_SEND)
     * @param sg Scatter-gather entry with connection name and RDMA parameters
     * @param last Indicates whether this is the last operation in a sequence
     */
    void invoke(CoyoteOper oper, rdmaSgConn sg, bool last = true);

    /// Invokes a TCP operation
    void invoke(CoyoteOper oper, tcpSg sg, bool last = true);

    /// Returns the number of completed operations for a given operation type
    uint32_t checkCompleted(CoyoteOper oper) const;

    /// Clears all the completion counters
    void clearCompleted();

    /// Synchronizes the connection between client and server (legacy single connection)
    void connSync(bool client);

    /// Sets up the cThread for RDMA operations (legacy single-QP)
    void* initRDMA(uint32_t buffer_size, uint16_t port, const char* server_address = nullptr);

    /// Releases the out-of-band connection
    void closeConn();

    /**
     * @brief Initializes an RDMA connection with a specific name for multi-FPGA scenarios
     *
     * This function creates a named RDMA connection that can be used independently of other
     * connections. This allows a single vFPGA to communicate with multiple remote FPGAs.
     *
     * @param conn_name Unique name for this connection (e.g., "to_rose", "from_amy")
     * @param buffer_size Size of the buffer to be allocated for RDMA operations
     * @param port Port number to be used for the out-of-band connection
     * @param server_address Optional server address; if nullptr, this cThread acts as server
     * @return Pointer to the allocated buffer, or nullptr on failure
     */
    void* initRDMAConnection(const std::string& conn_name, uint32_t buffer_size,
                             uint16_t port, const char* server_address = nullptr);

    /**
     * @brief Closes a specific named RDMA connection
     *
     * @param conn_name Name of the connection to close
     */
    void closeRDMAConnection(const std::string& conn_name);

    /**
     * @brief Synchronizes on a specific named connection
     *
     * @param conn_name Name of the connection
     * @param client If true, this cThread acts as a client
     */
    void connSync(const std::string& conn_name, bool client);

    /**
     * @brief Check if a named connection exists
     *
     * @param conn_name Name of the connection
     * @return true if connection exists
     */
    bool hasConnection(const std::string& conn_name) const;

    /**
     * @brief Get number of active connections
     *
     * @return Number of connections
     */
    size_t getConnectionCount() const { return connections.size(); }

    /**
     * @brief Get a connection by name
     *
     * @param conn_name Name of the connection
     * @return Pointer to the connection, or nullptr if not found
     */
    fpga::ibvConnection* getConnection(const std::string& conn_name);

    /// Locks the vFPGA for exclusive access
    void lock();

    /// Unlocks the vFPGA
    void unlock();

    /// Getter: vFPGA ID
    int32_t getVfid() const;

    /// Getter: Coyote thread ID
    int32_t getCtid() const;

    /// Getter: Host process ID
    pid_t getHpid() const;

    /// Getter: queue pair (legacy single-QP)
    fpga::ibvQp* getQpair() const;

    /// Getter: queue pair for a named connection
    fpga::ibvQp* getQpair(const std::string& conn_name);

    /// Utility function, prints stats about this cThread
    void printDebug() const;

private:
    // Pointer to implementation pattern for simulation support
    class AdditionalState;
    std::unique_ptr<AdditionalState> additional_state;
};

} /* namespace coyote */


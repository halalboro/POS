#pragma once
#include <string>
#include <memory>
#include <unordered_map>
#include "cSched.hpp"
#include "bFunc.hpp"
#include "cThread.hpp"

namespace fpga {

/**
 * @brief Capability Control Library
 * 
 * Provides register access and capability management for vFPGA
 * Works alongside cService for infrastructure needs
 */
class cCapability : public cSched {
protected:
    // Singleton instance
    static cCapability* ccapability;
    
    // Function map for capability operations
    std::unordered_map<int32_t, std::unique_ptr<bFunc>> cap_functions;
    
    // vFPGA identifiers
    int32_t vfid = { -1 };
    uint32_t dev;
    string service_id;
    
    // Socket connection
    string socket_name;
    int sockfd;
    
    // Register map for capability control
    struct RegInfo {
        uint32_t addr;      // Register address
        uint32_t value;     // Current register value
        bool readable;      // Read permission
        bool writable;      // Write permission
    };
    std::unordered_map<string, RegInfo> registers;

    /**
     * @brief Protected constructor for singleton pattern
     */
    cCapability(string name, int32_t vfid, uint32_t dev, bool priority = true);

    /**
     * @brief Low-level register access
     */
    void registerWrite(uint32_t addr, uint32_t value);
    uint32_t registerRead(uint32_t addr);
    
public:
    /**
     * @brief Get singleton instance
     */
    static cCapability* getInstance(string name, int32_t vfid, uint32_t dev, bool priority = true) {
        if(ccapability == nullptr) {
            ccapability = new cCapability(name, vfid, dev, priority);
        }
        return ccapability;
    }

    /**
     * @brief Register management
     */
    void defineRegister(const string& name, uint32_t addr, bool readable = true, bool writable = true);
    void writeRegister(const string& name, uint32_t value);
    uint32_t readRegister(const string& name);
    
    /**
     * @brief Capability control functions
     */
    void setCapability(uint32_t cap_id, uint32_t value);
    uint32_t getCapability(uint32_t cap_id);
    void enableCapability(uint32_t cap_id);
    void disableCapability(uint32_t cap_id);
    
    /**
     * @brief Service integration
     */
    void start();
    void addCapabilityFunction(int32_t fid, std::unique_ptr<bFunc> f);

    // Destructor
    ~cCapability();
};

/**
 * @brief Client interface for capability control
 */
class cCapLib {
private:
    int sockfd;
    int32_t vfid;
    uint32_t dev;
    
public:
    cCapLib(const string& socket_name, int32_t fid);
    ~cCapLib();
    
    template<typename... Args>
    double task(uint32_t priority, Args... args) {
        // Execute capability task
        return 0.0;
    }
    
    // Register access
    void writeReg(const string& name, uint32_t value);
    uint32_t readReg(const string& name);
    
    // Capability control
    void setCapability(uint32_t cap_id, uint32_t value);
    uint32_t getCapability(uint32_t cap_id);
};

} // namespace fpga

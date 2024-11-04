#include "cCapability.hpp"
#include <stdexcept>
#include <syslog.h>

namespace fpga {

// Initialize static member
cCapability* cCapability::ccapability = nullptr;

cCapability::cCapability(string name, int32_t vfid, uint32_t dev, bool priority) 
    : cSched(vfid, dev, priority, false) {
    this->vfid = vfid;
    this->dev = dev;
    this->service_id = "capability-service-vfid-" + std::to_string(vfid) + "-" + name;
    this->socket_name = "/tmp/capability-service-vfid-" + std::to_string(vfid) + "-" + name;
}

cCapability::~cCapability() {
    // Cleanup registered functions
    cap_functions.clear();
    
    // Cleanup socket if needed
    if(sockfd > 0) {
        close(sockfd);
    }
}

void cCapability::registerWrite(uint32_t addr, uint32_t value) {
    // Get current thread for register access
    cThread<double>* cthread = getCurrentThread();
    if (!cthread) {
        throw std::runtime_error("No active thread for register write");
    }

    // Lock vFPGA for register access
    cthread->pLock(operatorCAP, opPriority);
    
    // Write to register
    cthread->writeReg(addr, value);
    
    // Unlock vFPGA
    cthread->pUnlock();
}

uint32_t cCapability::registerRead(uint32_t addr) {
    // Get current thread for register access
    cThread<double>* cthread = getCurrentThread();
    if (!cthread) {
        throw std::runtime_error("No active thread for register read");
    }

    // Lock vFPGA for register access
    cthread->pLock(operatorCAP, opPriority);
    
    // Read from register
    uint32_t value = cthread->readReg(addr);
    
    // Unlock vFPGA
    cthread->pUnlock();
    
    return value;
}

void cCapability::defineRegister(const string& name, uint32_t addr, bool readable, bool writable) {
    if (registers.find(name) != registers.end()) {
        throw std::runtime_error("Register " + name + " already defined");
    }
    
    registers[name] = RegInfo{addr, 0, readable, writable};
    syslog(LOG_NOTICE, "Defined register %s at address 0x%x", name.c_str(), addr);
}

void cCapability::writeRegister(const string& name, uint32_t value) {
    auto it = registers.find(name);
    if (it == registers.end()) {
        throw std::runtime_error("Register " + name + " not defined");
    }
    
    if (!it->second.writable) {
        throw std::runtime_error("Register " + name + " is not writable");
    }
    
    registerWrite(it->second.addr, value);
    it->second.value = value;
    
    syslog(LOG_NOTICE, "Wrote 0x%x to register %s", value, name.c_str());
}

uint32_t cCapability::readRegister(const string& name) {
    auto it = registers.find(name);
    if (it == registers.end()) {
        throw std::runtime_error("Register " + name + " not defined");
    }
    
    if (!it->second.readable) {
        throw std::runtime_error("Register " + name + " is not readable");
    }
    
    uint32_t value = registerRead(it->second.addr);
    it->second.value = value;
    
    syslog(LOG_NOTICE, "Read 0x%x from register %s", value, name.c_str());
    return value;
}

void cCapability::setCapability(uint32_t cap_id, uint32_t value) {
    string reg_name = "CAP_" + std::to_string(cap_id);
    writeRegister(reg_name, value);
}

uint32_t cCapability::getCapability(uint32_t cap_id) {
    string reg_name = "CAP_" + std::to_string(cap_id);
    return readRegister(reg_name);
}

void cCapability::enableCapability(uint32_t cap_id) {
    setCapability(cap_id, 1);
}

void cCapability::disableCapability(uint32_t cap_id) {
    setCapability(cap_id, 0);
}

void cCapability::start() {
    // Initialize capability functions
    for (auto& func : cap_functions) {
        func.second->start();
    }
}

void cCapability::addCapabilityFunction(int32_t fid, std::unique_ptr<bFunc> f) {
    if(cap_functions.find(fid) == cap_functions.end()) {
        cap_functions.emplace(fid, std::move(f));
        syslog(LOG_NOTICE, "Added capability function %d", fid);
    }
}

// cCapLib implementation
cCapLib::cCapLib(const string& socket_name, int32_t fid) {
    // Connect to capability service through cService
    // This would use existing cService infrastructure
}

cCapLib::~cCapLib() {
    if(sockfd > 0) {
        close(sockfd);
    }
}

void cCapLib::writeReg(const string& name, uint32_t value) {
    // Execute register write task through cService
}

uint32_t cCapLib::readReg(const string& name) {
    // Execute register read task through cService
    return 0;
}

void cCapLib::setCapability(uint32_t cap_id, uint32_t value) {
    // Execute capability set task through cService
}

uint32_t cCapLib::getCapability(uint32_t cap_id) {
    // Execute capability get task through cService
    return 0;
}

} // namespace fpga

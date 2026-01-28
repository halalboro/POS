/*
 * POS Server Implementation
 *
 * gRPC server running on worker nodes that receives deployment requests
 * from client nodes and manages DFG instances on the local FPGA.
 *
 * Copyright (c) 2025, Systems Group, ETH Zurich
 * MIT License
 */

#include "pos_server.hpp"
#include "pos_service.grpc.pb.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <random>

namespace pos {

// ============================================================================
// POSServiceImpl Implementation
// ============================================================================

POSServiceImpl::POSServiceImpl()
    : start_time_(std::chrono::system_clock::now()) {
    std::cout << "POS Service initialized" << std::endl;
}

POSServiceImpl::~POSServiceImpl() {
    // Clean up all instances
    std::lock_guard<std::mutex> lock(instances_mutex_);
    instances_.clear();
}

std::string POSServiceImpl::generateInstanceId() {
    uint64_t id = next_instance_id_.fetch_add(1);
    std::ostringstream oss;
    oss << "dfg_instance_" << std::setfill('0') << std::setw(8) << id;
    return oss.str();
}

std::shared_ptr<DeployedDFGInstance> POSServiceImpl::getInstance(const std::string& instance_id) {
    std::lock_guard<std::mutex> lock(instances_mutex_);
    auto it = instances_.find(instance_id);
    if (it != instances_.end()) {
        return it->second;
    }
    return nullptr;
}

std::string POSServiceImpl::extractClientId(grpc::ServerContext* context,
                                             const std::string& request_client_id) {
    // Try to get client ID from request first
    if (!request_client_id.empty()) {
        return request_client_id;
    }

    // Fall back to gRPC metadata
    auto client_id_meta = context->client_metadata().find("x-client-id");
    if (client_id_meta != context->client_metadata().end()) {
        return std::string(client_id_meta->second.data(), client_id_meta->second.size());
    }

    // Fall back to peer address
    return context->peer();
}

// Helper to convert proto NodeType to dfg::NodeType
::dfg::NodeType protoToNodeType(pos::NodeType proto_type) {
    switch (proto_type) {
        case pos::NODE_TYPE_COMPUTE: return ::dfg::NodeType::COMPUTE;
        case pos::NODE_TYPE_MEMORY: return ::dfg::NodeType::MEMORY;
        case pos::NODE_TYPE_NETWORK_RDMA: return ::dfg::NodeType::NETWORK_RDMA;
        case pos::NODE_TYPE_NETWORK_TCP: return ::dfg::NodeType::NETWORK_TCP;
        case pos::NODE_TYPE_NETWORK_RAW: return ::dfg::NodeType::NETWORK_RAW;
        case pos::NODE_TYPE_SOFTWARE_PARSER: return ::dfg::NodeType::SOFTWARE_PARSER;
        case pos::NODE_TYPE_SOFTWARE_DEPARSER: return ::dfg::NodeType::SOFTWARE_DEPARSER;
        case pos::NODE_TYPE_SOFTWARE_NF: return ::dfg::NodeType::SOFTWARE_NF;
        case pos::NODE_TYPE_REMOTE_DFG: return ::dfg::NodeType::REMOTE_DFG;
        default: return ::dfg::NodeType::COMPUTE;
    }
}

// Helper to convert proto CapabilityScope to dfg::CapabilityScope
::dfg::CapabilityScope protoToCapabilityScope(pos::CapabilityScope proto_scope) {
    switch (proto_scope) {
        case pos::SCOPE_LOCAL: return ::dfg::CapabilityScope::LOCAL;
        case pos::SCOPE_NETWORK: return ::dfg::CapabilityScope::NETWORK;
        case pos::SCOPE_SOFTWARE: return ::dfg::CapabilityScope::SOFTWARE;
        case pos::SCOPE_REMOTE: return ::dfg::CapabilityScope::REMOTE;
        case pos::SCOPE_GLOBAL: return ::dfg::CapabilityScope::GLOBAL;
        default: return ::dfg::CapabilityScope::LOCAL;
    }
}

// Helper to convert dfg::CapabilityScope to proto
pos::CapabilityScope capabilityScopeToProto(::dfg::CapabilityScope scope) {
    switch (scope) {
        case ::dfg::CapabilityScope::LOCAL: return pos::SCOPE_LOCAL;
        case ::dfg::CapabilityScope::NETWORK: return pos::SCOPE_NETWORK;
        case ::dfg::CapabilityScope::SOFTWARE: return pos::SCOPE_SOFTWARE;
        case ::dfg::CapabilityScope::REMOTE: return pos::SCOPE_REMOTE;
        case ::dfg::CapabilityScope::GLOBAL: return pos::SCOPE_GLOBAL;
        default: return pos::SCOPE_LOCAL;
    }
}

std::shared_ptr<::dfg::DFG> POSServiceImpl::buildDFGFromSpec(
    const pos::DFGSpec& spec,
    std::string& error_message) {

    try {
        // Determine stream mode
        ::dfg::StreamMode stream_mode = (spec.stream_mode() == pos::STREAM_HOST)
            ? ::dfg::HOST_STREAM : ::dfg::CARD_STREAM;

        // Create the DFG
        auto dfg = std::make_shared<::dfg::DFG>(
            spec.app_id(),
            spec.device_id(),
            spec.use_huge_pages(),
            stream_mode
        );

        // Get root capability for construction
        auto root_cap = dfg->get_root_capability();
        if (!root_cap) {
            error_message = "Failed to get root capability";
            return nullptr;
        }

        // Create buffers first (nodes may reference them)
        for (const auto& buf_spec : spec.buffers()) {
            auto buffer = dfg->create_buffer(root_cap, buf_spec.size(), buf_spec.buffer_id());
            if (!buffer) {
                error_message = "Failed to create buffer: " + buf_spec.buffer_id();
                return nullptr;
            }

            // Write initial data if provided
            if (!buf_spec.initial_data().empty()) {
                void* ptr = buffer->get_pointer(root_cap);
                if (ptr && buf_spec.initial_data().size() <= buf_spec.size()) {
                    memcpy(ptr, buf_spec.initial_data().data(), buf_spec.initial_data().size());
                }
            }
        }

        // Create nodes based on their type
        for (const auto& node_spec : spec.nodes()) {
            std::shared_ptr<::dfg::NodeBase> node;

            switch (node_spec.node_type()) {
                case pos::NODE_TYPE_COMPUTE: {
                    if (node_spec.has_compute_config()) {
                        const auto& cfg = node_spec.compute_config();
                        auto compute_node = dfg->create_node(root_cap, cfg.vfid(), node_spec.node_id());
                        if (compute_node) {
                            // Set operation type if specified
                            if (cfg.operation_type() != 0) {
                                compute_node->set_operation(static_cast<CoyoteOper>(cfg.operation_type()), root_cap);
                            }
                        }
                        node = compute_node;
                    }
                    break;
                }

                case pos::NODE_TYPE_NETWORK_RDMA: {
                    if (node_spec.has_rdma_config()) {
                        const auto& cfg = node_spec.rdma_config();
                        auto rdma_node = std::make_shared<::dfg::RDMANetworkNode>(node_spec.node_id());
                        rdma_node->set_vlan_id(cfg.vlan_id());
                        // Additional RDMA configuration would go here
                        node = rdma_node;
                        dfg->add_node(node, root_cap);
                    }
                    break;
                }

                case pos::NODE_TYPE_NETWORK_TCP: {
                    if (node_spec.has_tcp_config()) {
                        const auto& cfg = node_spec.tcp_config();
                        auto tcp_node = std::make_shared<::dfg::TCPNetworkNode>(
                            node_spec.node_id(), cfg.is_server());
                        // Additional TCP configuration would go here
                        node = tcp_node;
                        dfg->add_node(node, root_cap);
                    }
                    break;
                }

                case pos::NODE_TYPE_NETWORK_RAW: {
                    if (node_spec.has_raw_config()) {
                        const auto& cfg = node_spec.raw_config();
                        auto raw_node = std::make_shared<::dfg::RawEthernetNode>(
                            node_spec.node_id(), cfg.interface_name());
                        if (cfg.promiscuous()) {
                            raw_node->set_promiscuous(true, root_cap);
                        }
                        if (cfg.ethertype() != 0) {
                            raw_node->set_ethertype(cfg.ethertype(), root_cap);
                        }
                        node = raw_node;
                        dfg->add_node(node, root_cap);
                    }
                    break;
                }

                case pos::NODE_TYPE_SOFTWARE_PARSER: {
                    if (node_spec.has_software_config()) {
                        const auto& cfg = node_spec.software_config();
                        ::dfg::SoftwareResourceLimits limits;
                        limits.max_memory_bytes = cfg.max_memory_bytes();
                        limits.max_cpu_percent = cfg.max_cpu_percent();
                        limits.max_threads = cfg.max_threads();
                        limits.max_bandwidth_bps = cfg.max_bandwidth_bps();

                        auto parser_node = std::make_shared<::dfg::ParserNode>(
                            node_spec.node_id(), limits);
                        node = parser_node;
                        dfg->add_node(node, root_cap);
                    }
                    break;
                }

                case pos::NODE_TYPE_SOFTWARE_DEPARSER: {
                    if (node_spec.has_software_config()) {
                        const auto& cfg = node_spec.software_config();
                        ::dfg::SoftwareResourceLimits limits;
                        limits.max_memory_bytes = cfg.max_memory_bytes();
                        limits.max_cpu_percent = cfg.max_cpu_percent();
                        limits.max_threads = cfg.max_threads();
                        limits.max_bandwidth_bps = cfg.max_bandwidth_bps();

                        auto deparser_node = std::make_shared<::dfg::DeparserNode>(
                            node_spec.node_id(), limits);
                        node = deparser_node;
                        dfg->add_node(node, root_cap);
                    }
                    break;
                }

                case pos::NODE_TYPE_SOFTWARE_NF: {
                    if (node_spec.has_software_config()) {
                        const auto& cfg = node_spec.software_config();
                        ::dfg::SoftwareResourceLimits limits;
                        limits.max_memory_bytes = cfg.max_memory_bytes();
                        limits.max_cpu_percent = cfg.max_cpu_percent();
                        limits.max_threads = cfg.max_threads();
                        limits.max_bandwidth_bps = cfg.max_bandwidth_bps();

                        auto nf_node = std::make_shared<::dfg::SoftwareNFNode>(
                            node_spec.node_id(), limits);
                        node = nf_node;
                        dfg->add_node(node, root_cap);
                    }
                    break;
                }

                case pos::NODE_TYPE_REMOTE_DFG: {
                    if (node_spec.has_remote_config()) {
                        const auto& cfg = node_spec.remote_config();
                        auto remote_node = std::make_shared<::dfg::RemoteDFGNode>(
                            node_spec.node_id(),
                            cfg.local_vlan_id(),
                            cfg.remote_vlan_id()
                        );
                        node = remote_node;
                        dfg->add_node(node, root_cap);
                    }
                    break;
                }

                default:
                    error_message = "Unknown node type for node: " + node_spec.node_id();
                    return nullptr;
            }

            if (!node) {
                error_message = "Failed to create node: " + node_spec.node_id();
                return nullptr;
            }
        }

        // Create edges (connections between nodes)
        for (const auto& edge_spec : spec.edges()) {
            std::cout << "Creating edge: " << edge_spec.source_id()
                      << " -> " << edge_spec.target_id() << std::endl;

            // Use dfg::connect_edges to establish the connection
            // Default offsets: read_offset=0, write_offset=6 for typical pipeline
            uint32_t read_offset = 0;
            uint32_t write_offset = 6;

            // Check if edge spec has custom offsets
            if (edge_spec.has_read_offset()) {
                read_offset = edge_spec.read_offset();
            }
            if (edge_spec.has_write_offset()) {
                write_offset = edge_spec.write_offset();
            }

            bool edge_created = ::dfg::connect_edges(
                edge_spec.source_id(),
                edge_spec.target_id(),
                dfg.get(),
                read_offset,
                write_offset,
                true  // suppress permission errors during construction
            );

            if (!edge_created) {
                std::cerr << "Warning: Failed to create edge from "
                          << edge_spec.source_id() << " to " << edge_spec.target_id()
                          << " (nodes may not be compute nodes)" << std::endl;
                // Don't fail - edge creation may fail for non-compute nodes
                // which is expected for network/software nodes
            }
        }

        return dfg;

    } catch (const std::exception& e) {
        error_message = std::string("Exception building DFG: ") + e.what();
        return nullptr;
    }
}

::dfg::Capability* POSServiceImpl::findCapability(::dfg::DFG* dfg, const std::string& cap_id) {
    // Use root capability to search
    auto root_cap = dfg->get_root_capability();
    return dfg->find_capability(cap_id, root_cap);
}

// ============================================================================
// gRPC Service Methods
// ============================================================================

grpc::Status POSServiceImpl::DeployDFG(
    grpc::ServerContext* context,
    const pos::DeployDFGRequest* request,
    pos::DeployDFGResponse* response) {

    std::string client_id = extractClientId(context, request->client_id());
    std::cout << "DeployDFG request from client: " << client_id << std::endl;

    // Validate request
    if (!request->has_dfg_spec()) {
        response->set_success(false);
        response->set_error_message("Missing DFG specification");
        return grpc::Status::OK;
    }

    const auto& spec = request->dfg_spec();

    // Build the DFG from spec
    std::string error_message;
    auto dfg = buildDFGFromSpec(spec, error_message);
    if (!dfg) {
        response->set_success(false);
        response->set_error_message(error_message);
        return grpc::Status::OK;
    }

    // Create instance record
    auto instance = std::make_shared<DeployedDFGInstance>();
    instance->instance_id = generateInstanceId();
    instance->dfg_id = spec.dfg_id();
    instance->client_id = client_id;
    instance->dfg = dfg;
    instance->deploy_time = std::chrono::system_clock::now();
    instance->state.store(DeployedDFGInstance::State::RUNNING);

    // Store instance
    {
        std::lock_guard<std::mutex> lock(instances_mutex_);
        instances_[instance->instance_id] = instance;
    }

    // Build response
    response->set_success(true);
    auto* handle = response->mutable_handle();
    handle->set_dfg_id(spec.dfg_id());
    handle->set_instance_id(instance->instance_id);
    handle->set_deployment_timestamp(
        std::chrono::duration_cast<std::chrono::seconds>(
            instance->deploy_time.time_since_epoch()).count());

    std::cout << "DFG deployed successfully: " << instance->instance_id << std::endl;
    return grpc::Status::OK;
}

grpc::Status POSServiceImpl::UndeployDFG(
    grpc::ServerContext* context,
    const pos::UndeployDFGRequest* request,
    pos::UndeployDFGResponse* response) {

    std::string client_id = extractClientId(context, request->client_id());
    std::cout << "UndeployDFG request for instance: " << request->instance_id()
              << " from client: " << client_id << std::endl;

    std::shared_ptr<DeployedDFGInstance> instance;
    {
        std::lock_guard<std::mutex> lock(instances_mutex_);
        auto it = instances_.find(request->instance_id());
        if (it == instances_.end()) {
            response->set_success(false);
            response->set_error_message("Instance not found: " + request->instance_id());
            return grpc::Status::OK;
        }

        // Check ownership
        if (it->second->client_id != client_id) {
            response->set_success(false);
            response->set_error_message("Not authorized to undeploy this instance");
            return grpc::Status::OK;
        }

        instance = it->second;
        instances_.erase(it);
    }

    // Mark as stopped
    instance->state.store(DeployedDFGInstance::State::STOPPED);

    response->set_success(true);
    std::cout << "DFG undeployed: " << request->instance_id() << std::endl;
    return grpc::Status::OK;
}

grpc::Status POSServiceImpl::GetDFGStatus(
    grpc::ServerContext* context,
    const pos::GetDFGStatusRequest* request,
    pos::GetDFGStatusResponse* response) {

    auto instance = getInstance(request->instance_id());
    if (!instance) {
        response->set_success(false);
        response->set_error_message("Instance not found: " + request->instance_id());
        return grpc::Status::OK;
    }

    response->set_success(true);
    auto* status = response->mutable_status();
    status->set_instance_id(instance->instance_id);
    status->set_dfg_id(instance->dfg_id);

    // Convert state
    switch (instance->state.load()) {
        case DeployedDFGInstance::State::DEPLOYING:
            status->set_state(pos::DFGStatus::STATE_DEPLOYING);
            break;
        case DeployedDFGInstance::State::RUNNING:
            status->set_state(pos::DFGStatus::STATE_RUNNING);
            break;
        case DeployedDFGInstance::State::STALLED:
            status->set_state(pos::DFGStatus::STATE_STALLED);
            break;
        case DeployedDFGInstance::State::ERROR:
            status->set_state(pos::DFGStatus::STATE_ERROR);
            status->set_error_message(instance->error_message);
            break;
        case DeployedDFGInstance::State::STOPPED:
            status->set_state(pos::DFGStatus::STATE_STOPPED);
            break;
    }

    // Calculate uptime
    auto now = std::chrono::system_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        now - instance->deploy_time).count();
    status->set_uptime_seconds(uptime);

    status->set_bytes_processed(instance->bytes_processed.load());
    status->set_operations_completed(instance->operations_completed.load());

    return grpc::Status::OK;
}

grpc::Status POSServiceImpl::ListDFGs(
    grpc::ServerContext* context,
    const pos::ListDFGsRequest* request,
    pos::ListDFGsResponse* response) {

    std::string client_id = extractClientId(context, request->client_id());

    std::lock_guard<std::mutex> lock(instances_mutex_);
    response->set_success(true);

    for (const auto& [id, instance] : instances_) {
        // Only show instances owned by this client (or all if admin)
        if (instance->client_id == client_id || client_id == "admin") {
            auto* status = response->add_dfgs();
            status->set_instance_id(instance->instance_id);
            status->set_dfg_id(instance->dfg_id);

            switch (instance->state.load()) {
                case DeployedDFGInstance::State::DEPLOYING:
                    status->set_state(pos::DFGStatus::STATE_DEPLOYING);
                    break;
                case DeployedDFGInstance::State::RUNNING:
                    status->set_state(pos::DFGStatus::STATE_RUNNING);
                    break;
                case DeployedDFGInstance::State::STALLED:
                    status->set_state(pos::DFGStatus::STATE_STALLED);
                    break;
                case DeployedDFGInstance::State::ERROR:
                    status->set_state(pos::DFGStatus::STATE_ERROR);
                    break;
                case DeployedDFGInstance::State::STOPPED:
                    status->set_state(pos::DFGStatus::STATE_STOPPED);
                    break;
            }
        }
    }

    return grpc::Status::OK;
}

grpc::Status POSServiceImpl::ExecuteNode(
    grpc::ServerContext* context,
    const pos::ExecuteNodeRequest* request,
    pos::ExecuteNodeResponse* response) {

    auto instance = getInstance(request->instance_id());
    if (!instance) {
        response->set_success(false);
        response->set_error_message("Instance not found");
        return grpc::Status::OK;
    }

    // Find the capability
    auto cap = findCapability(instance->dfg.get(), request->cap_id());
    if (!cap) {
        response->set_success(false);
        response->set_error_message("Capability not found: " + request->cap_id());
        return grpc::Status::OK;
    }

    // Get the node
    auto node = instance->dfg->get_node(request->node_id(), cap);
    if (!node) {
        response->set_success(false);
        response->set_error_message("Node not found or not a compute node: " + request->node_id());
        return grpc::Status::OK;
    }

    // Build scatter-gather entry
    sgEntry sg;
    sg.local.src_addr = reinterpret_cast<void*>(request->src_addr());
    sg.local.src_len = request->src_len();
    sg.local.dst_addr = reinterpret_cast<void*>(request->dst_addr());
    sg.local.dst_len = request->dst_len();

    // Execute the operation
    bool success;
    if (request->blocking()) {
        success = node->execute_with_sg(&sg, cap);
    } else {
        success = node->start_with_sg(&sg, cap);
    }

    if (success) {
        instance->operations_completed.fetch_add(1);
        instance->bytes_processed.fetch_add(request->src_len() + request->dst_len());
    }

    response->set_success(success);
    if (!success) {
        response->set_error_message("Execution failed");
    }

    return grpc::Status::OK;
}

grpc::Status POSServiceImpl::ReadBuffer(
    grpc::ServerContext* context,
    const pos::ReadBufferRequest* request,
    pos::ReadBufferResponse* response) {

    auto instance = getInstance(request->instance_id());
    if (!instance) {
        response->set_success(false);
        response->set_error_message("Instance not found");
        return grpc::Status::OK;
    }

    auto cap = findCapability(instance->dfg.get(), request->cap_id());
    if (!cap) {
        response->set_success(false);
        response->set_error_message("Capability not found");
        return grpc::Status::OK;
    }

    auto buffer = instance->dfg->get_buffer(request->buffer_id(), cap);
    if (!buffer) {
        response->set_success(false);
        response->set_error_message("Buffer not found");
        return grpc::Status::OK;
    }

    void* ptr = buffer->get_pointer(cap);
    if (!ptr) {
        response->set_success(false);
        response->set_error_message("Cannot access buffer");
        return grpc::Status::OK;
    }

    uint64_t offset = request->offset();
    uint64_t length = request->length();
    uint64_t buf_size = buffer->get_size(cap);

    if (offset + length > buf_size) {
        response->set_success(false);
        response->set_error_message("Read exceeds buffer bounds");
        return grpc::Status::OK;
    }

    response->set_success(true);
    response->set_data(static_cast<char*>(ptr) + offset, length);

    return grpc::Status::OK;
}

grpc::Status POSServiceImpl::WriteBuffer(
    grpc::ServerContext* context,
    const pos::WriteBufferRequest* request,
    pos::WriteBufferResponse* response) {

    auto instance = getInstance(request->instance_id());
    if (!instance) {
        response->set_success(false);
        response->set_error_message("Instance not found");
        return grpc::Status::OK;
    }

    auto cap = findCapability(instance->dfg.get(), request->cap_id());
    if (!cap) {
        response->set_success(false);
        response->set_error_message("Capability not found");
        return grpc::Status::OK;
    }

    auto buffer = instance->dfg->get_buffer(request->buffer_id(), cap);
    if (!buffer) {
        response->set_success(false);
        response->set_error_message("Buffer not found");
        return grpc::Status::OK;
    }

    void* ptr = buffer->get_pointer(cap);
    if (!ptr) {
        response->set_success(false);
        response->set_error_message("Cannot access buffer");
        return grpc::Status::OK;
    }

    uint64_t offset = request->offset();
    uint64_t buf_size = buffer->get_size(cap);
    const std::string& data = request->data();

    if (offset + data.size() > buf_size) {
        response->set_success(false);
        response->set_error_message("Write exceeds buffer bounds");
        return grpc::Status::OK;
    }

    memcpy(static_cast<char*>(ptr) + offset, data.data(), data.size());
    response->set_success(true);

    return grpc::Status::OK;
}

grpc::Status POSServiceImpl::DelegateCapability(
    grpc::ServerContext* context,
    const pos::DelegateCapabilityRequest* request,
    pos::DelegateCapabilityResponse* response) {

    auto instance = getInstance(request->instance_id());
    if (!instance) {
        response->set_success(false);
        response->set_error_message("Instance not found");
        return grpc::Status::OK;
    }

    auto source_cap = findCapability(instance->dfg.get(), request->source_cap_id());
    if (!source_cap) {
        response->set_success(false);
        response->set_error_message("Source capability not found");
        return grpc::Status::OK;
    }

    // Delegate using DFG's delegation method
    auto root_cap = instance->dfg->get_root_capability();
    auto new_cap = instance->dfg->delegate_capability(
        root_cap,
        request->source_cap_id(),
        request->new_cap_id(),
        request->permissions(),
        protoToCapabilityScope(request->scope())
    );

    if (!new_cap) {
        response->set_success(false);
        response->set_error_message("Delegation failed - check permissions");
        return grpc::Status::OK;
    }

    // Set expiry if specified
    if (request->expiry_timestamp() > 0) {
        auto expiry = std::chrono::system_clock::from_time_t(request->expiry_timestamp());
        instance->dfg->expire_capability(request->new_cap_id(), expiry, root_cap);
    }

    response->set_success(true);
    auto* delegated = response->mutable_delegated_cap();
    delegated->set_cap_id(new_cap->get_id());
    delegated->set_permissions(new_cap->get_permissions());
    delegated->set_scope(capabilityScopeToProto(new_cap->get_scope()));
    delegated->set_parent_cap_id(request->source_cap_id());

    return grpc::Status::OK;
}

grpc::Status POSServiceImpl::RevokeCapability(
    grpc::ServerContext* context,
    const pos::RevokeCapabilityRequest* request,
    pos::RevokeCapabilityResponse* response) {

    auto instance = getInstance(request->instance_id());
    if (!instance) {
        response->set_success(false);
        response->set_error_message("Instance not found");
        return grpc::Status::OK;
    }

    auto admin_cap = findCapability(instance->dfg.get(), request->admin_cap_id());
    if (!admin_cap) {
        response->set_success(false);
        response->set_error_message("Admin capability not found");
        return grpc::Status::OK;
    }

    uint32_t revoked_count = 0;
    if (request->recursive()) {
        revoked_count = instance->dfg->revoke_capability_recursive(request->cap_id(), admin_cap);
    } else {
        bool success = instance->dfg->revoke_capability(request->cap_id(), admin_cap);
        revoked_count = success ? 1 : 0;
    }

    response->set_success(revoked_count > 0);
    response->set_revoked_count(revoked_count);
    if (revoked_count == 0) {
        response->set_error_message("No capabilities revoked - check permissions or capability ID");
    }

    return grpc::Status::OK;
}

grpc::Status POSServiceImpl::HealthCheck(
    grpc::ServerContext* context,
    const pos::HealthCheckRequest* request,
    pos::HealthCheckResponse* response) {

    response->set_healthy(true);
    response->set_version("1.0.0");
    response->set_active_dfgs(getActiveInstanceCount());
    response->set_uptime_seconds(getUptimeSeconds());

    // TODO: Get actual available memory and vFPGAs from system
    response->set_available_memory(1024 * 1024 * 1024);  // 1GB placeholder
    response->set_available_vfpgas(4);  // Placeholder

    return grpc::Status::OK;
}

grpc::Status POSServiceImpl::SetupRDMA(
    grpc::ServerContext* context,
    const pos::SetupRDMARequest* request,
    pos::SetupRDMAResponse* response) {

    std::string client_id = extractClientId(context, request->client_id());
    std::cout << "SetupRDMA request from client: " << client_id
              << " for instance: " << request->instance_id()
              << " node: " << request->node_id() << std::endl;

    // Get the DFG instance
    auto instance = getInstance(request->instance_id());
    if (!instance) {
        response->set_success(false);
        response->set_error_message("Instance not found: " + request->instance_id());
        return grpc::Status::OK;
    }

    // Get root capability for RDMA setup
    auto root_cap = instance->dfg->get_root_capability();
    if (!root_cap) {
        response->set_success(false);
        response->set_error_message("Failed to get root capability");
        return grpc::Status::OK;
    }

    // Find the RDMA node (could be a REMOTE endpoint or explicit RDMA node)
    auto node = instance->dfg->get_node_base(request->node_id(), root_cap);
    if (!node) {
        response->set_success(false);
        response->set_error_message("Node not found: " + request->node_id());
        return grpc::Status::OK;
    }

    // Check if it's an RDMA-capable node
    auto rdma_node = std::dynamic_pointer_cast<::dfg::RDMANetworkNode>(node);
    auto remote_node = std::dynamic_pointer_cast<::dfg::RemoteDFGNode>(node);

    if (!rdma_node && !remote_node) {
        response->set_success(false);
        response->set_error_message("Node is not RDMA-capable: " + request->node_id());
        return grpc::Status::OK;
    }

    // RDMA connection setup:
    // 1. If is_initiator=true: This worker creates the QP and waits for remote QP info
    // 2. If is_initiator=false: This worker receives remote QP info and completes connection
    //
    // The actual RDMA setup involves:
    // - Creating a Queue Pair (QP) on the local FPGA
    // - Exchanging QP numbers with the remote worker
    // - Programming the QP with remote info to establish connection

    uint32_t local_qpn = 0;
    uint32_t remote_qpn = 0;
    std::string local_ip;
    std::string remote_ip = request->remote_ip();

    if (rdma_node) {
        // Setup RDMA using RDMANetworkNode
        bool setup_success = rdma_node->setup_qp(
            request->buffer_size(),
            request->is_initiator(),
            root_cap
        );

        if (!setup_success) {
            response->set_success(false);
            response->set_error_message("Failed to setup RDMA QP");
            return grpc::Status::OK;
        }

        local_qpn = rdma_node->get_local_qpn();
        local_ip = rdma_node->get_local_ip();

        // If we have remote info, complete the connection
        if (request->remote_qpn() != 0) {
            bool connect_success = rdma_node->connect_to_remote(
                request->remote_ip(),
                request->remote_qpn(),
                root_cap
            );
            if (!connect_success) {
                response->set_success(false);
                response->set_error_message("Failed to connect to remote QP");
                return grpc::Status::OK;
            }
            remote_qpn = request->remote_qpn();
        }
    } else if (remote_node) {
        // Setup RDMA using RemoteDFGNode's internal RDMA connection
        bool connect_success = remote_node->connect_remote(
            request->remote_ip(),
            request->remote_rdma_port(),
            remote_node->get_remote_vlan_id(),
            root_cap
        );

        if (!connect_success) {
            response->set_success(false);
            response->set_error_message("Failed to establish remote RDMA connection");
            return grpc::Status::OK;
        }

        local_qpn = remote_node->get_local_qpn();
        remote_qpn = remote_node->get_remote_qpn();
        local_ip = remote_node->get_local_ip();
    }

    // Store the RDMA connection info for this instance
    {
        std::lock_guard<std::mutex> lock(instances_mutex_);
        instance->rdma_connections[request->node_id()] = {
            local_qpn, remote_qpn, local_ip, remote_ip
        };
    }

    response->set_success(true);
    response->set_local_qpn(local_qpn);
    response->set_remote_qpn(remote_qpn);
    response->set_local_ip(local_ip);
    response->set_remote_ip(remote_ip);

    std::cout << "RDMA setup complete: local_qpn=" << local_qpn
              << " remote_qpn=" << remote_qpn << std::endl;

    return grpc::Status::OK;
}

grpc::Status POSServiceImpl::ExecuteDFG(
    grpc::ServerContext* context,
    const pos::ExecuteDFGRequest* request,
    pos::ExecuteDFGResponse* response) {

    std::string client_id = extractClientId(context, request->client_id());
    std::cout << "ExecuteDFG request from client: " << client_id
              << " for instance: " << request->instance_id() << std::endl;

    // Get the DFG instance
    auto instance = getInstance(request->instance_id());
    if (!instance) {
        response->set_success(false);
        response->set_error_message("Instance not found: " + request->instance_id());
        return grpc::Status::OK;
    }

    // Find capability (use root if not specified)
    ::dfg::Capability* cap = nullptr;
    if (!request->cap_id().empty()) {
        cap = findCapability(instance->dfg.get(), request->cap_id());
        if (!cap) {
            response->set_success(false);
            response->set_error_message("Capability not found: " + request->cap_id());
            return grpc::Status::OK;
        }
    } else {
        cap = instance->dfg->get_root_capability();
    }

    // Execute the entire DFG pipeline
    // This triggers the dataflow execution on all nodes in the graph
    bool success = instance->dfg->execute_all(cap);

    if (success) {
        instance->operations_completed.fetch_add(1);
        response->set_success(true);
        std::cout << "DFG execution completed successfully" << std::endl;
    } else {
        response->set_success(false);
        response->set_error_message("DFG execution failed");
        std::cout << "DFG execution failed" << std::endl;
    }

    return grpc::Status::OK;
}

size_t POSServiceImpl::getActiveInstanceCount() const {
    std::lock_guard<std::mutex> lock(instances_mutex_);
    size_t count = 0;
    for (const auto& [id, instance] : instances_) {
        if (instance->state.load() == DeployedDFGInstance::State::RUNNING) {
            count++;
        }
    }
    return count;
}

uint64_t POSServiceImpl::getUptimeSeconds() const {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(
        now - start_time_).count();
}

// ============================================================================
// POSServer Implementation
// ============================================================================

POSServer::POSServer(const std::string& address, int max_message_size)
    : server_address_(address), max_message_size_(max_message_size), running_(false) {
}

POSServer::~POSServer() {
    stop();
}

void POSServer::buildServer() {
    service_ = std::make_unique<POSServiceImpl>();

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials());
    builder.RegisterService(service_.get());
    builder.SetMaxReceiveMessageSize(max_message_size_);
    builder.SetMaxSendMessageSize(max_message_size_);

    // Enable health check service
    grpc::EnableDefaultHealthCheckService(true);

    server_ = builder.BuildAndStart();
}

bool POSServer::start() {
    if (running_) {
        return false;
    }

    buildServer();
    if (!server_) {
        std::cerr << "Failed to build POS server" << std::endl;
        return false;
    }

    running_ = true;
    server_thread_ = std::thread([this]() {
        std::cout << "POS Server listening on " << server_address_ << std::endl;
        server_->Wait();
    });

    return true;
}

void POSServer::run() {
    buildServer();
    if (server_) {
        running_ = true;
        std::cout << "POS Server listening on " << server_address_ << std::endl;
        server_->Wait();
        running_ = false;
    }
}

void POSServer::stop() {
    if (running_ && server_) {
        server_->Shutdown();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        running_ = false;
        std::cout << "POS Server stopped" << std::endl;
    }
}

} // namespace pos

/*
 * POS Client (gRPC client) Implementation
 *
 * Client running on client nodes for deploying DFGs to POS worker nodes.
 *
 * Copyright (c) 2025, Systems Group, ETH Zurich
 * MIT License
 */

#include "pos_client.hpp"
#include "pos_service.grpc.pb.h"

#include <iostream>

namespace pos {

POSClient::POSClient(const std::string& server_address, const std::string& client_id)
    : server_address_(server_address), client_id_(client_id) {

    // Create channel with default options
    grpc::ChannelArguments args;
    args.SetMaxReceiveMessageSize(64 * 1024 * 1024);  // 64MB
    args.SetMaxSendMessageSize(64 * 1024 * 1024);

    channel_ = grpc::CreateCustomChannel(
        server_address,
        grpc::InsecureChannelCredentials(),
        args
    );

    stub_ = pos::POSService::NewStub(channel_);
}

POSClient::~POSClient() = default;

bool POSClient::isConnected() const {
    if (!channel_) return false;
    auto state = channel_->GetState(false);
    return state == GRPC_CHANNEL_READY || state == GRPC_CHANNEL_IDLE;
}

std::unique_ptr<grpc::ClientContext> POSClient::createContext() {
    auto context = std::make_unique<grpc::ClientContext>();

    // Set deadline
    auto deadline = std::chrono::system_clock::now() + timeout_;
    context->set_deadline(deadline);

    // Add client ID metadata
    context->AddMetadata("x-client-id", client_id_);

    // Add auth token if set
    if (!auth_token_.empty()) {
        context->AddMetadata("authorization", "Bearer " + auth_token_);
    }

    return context;
}

// ============================================================================
// DFG Lifecycle Management
// ============================================================================

ClientResult<DFGInstanceHandle> POSClient::deployDFG(const pos::DFGSpec& spec) {
    pos::DeployDFGRequest request;
    *request.mutable_dfg_spec() = spec;
    request.set_client_id(client_id_);
    request.set_auth_token(auth_token_);

    pos::DeployDFGResponse response;
    auto context = createContext();

    grpc::Status status = stub_->DeployDFG(context.get(), request, &response);

    if (!status.ok()) {
        return ClientResult<DFGInstanceHandle>("gRPC error: " + status.error_message());
    }

    if (!response.success()) {
        return ClientResult<DFGInstanceHandle>(response.error_message());
    }

    DFGInstanceHandle handle;
    handle.dfg_id = response.handle().dfg_id();
    handle.instance_id = response.handle().instance_id();
    handle.deployment_timestamp = response.handle().deployment_timestamp();

    return ClientResult<DFGInstanceHandle>(std::move(handle));
}

ClientResult<void> POSClient::undeployDFG(const std::string& instance_id) {
    pos::UndeployDFGRequest request;
    request.set_instance_id(instance_id);
    request.set_client_id(client_id_);
    request.set_auth_token(auth_token_);

    pos::UndeployDFGResponse response;
    auto context = createContext();

    grpc::Status status = stub_->UndeployDFG(context.get(), request, &response);

    if (!status.ok()) {
        return ClientResult<void>("gRPC error: " + status.error_message());
    }

    if (!response.success()) {
        return ClientResult<void>(response.error_message());
    }

    return ClientResult<void>(true);
}

ClientResult<DFGInstanceStatus> POSClient::getDFGStatus(const std::string& instance_id) {
    pos::GetDFGStatusRequest request;
    request.set_instance_id(instance_id);
    request.set_client_id(client_id_);

    pos::GetDFGStatusResponse response;
    auto context = createContext();

    grpc::Status status = stub_->GetDFGStatus(context.get(), request, &response);

    if (!status.ok()) {
        return ClientResult<DFGInstanceStatus>("gRPC error: " + status.error_message());
    }

    if (!response.success()) {
        return ClientResult<DFGInstanceStatus>(response.error_message());
    }

    DFGInstanceStatus result;
    result.instance_id = response.status().instance_id();
    result.dfg_id = response.status().dfg_id();
    result.uptime_seconds = response.status().uptime_seconds();
    result.bytes_processed = response.status().bytes_processed();
    result.operations_completed = response.status().operations_completed();
    result.error_message = response.status().error_message();

    switch (response.status().state()) {
        case pos::DFGStatus::STATE_DEPLOYING:
            result.state = DFGInstanceStatus::State::DEPLOYING;
            break;
        case pos::DFGStatus::STATE_RUNNING:
            result.state = DFGInstanceStatus::State::RUNNING;
            break;
        case pos::DFGStatus::STATE_STALLED:
            result.state = DFGInstanceStatus::State::STALLED;
            break;
        case pos::DFGStatus::STATE_ERROR:
            result.state = DFGInstanceStatus::State::ERROR;
            break;
        case pos::DFGStatus::STATE_STOPPED:
            result.state = DFGInstanceStatus::State::STOPPED;
            break;
        default:
            result.state = DFGInstanceStatus::State::UNKNOWN;
            break;
    }

    return ClientResult<DFGInstanceStatus>(std::move(result));
}

ClientResult<std::vector<DFGInstanceStatus>> POSClient::listDFGs() {
    pos::ListDFGsRequest request;
    request.set_client_id(client_id_);

    pos::ListDFGsResponse response;
    auto context = createContext();

    grpc::Status status = stub_->ListDFGs(context.get(), request, &response);

    if (!status.ok()) {
        return ClientResult<std::vector<DFGInstanceStatus>>("gRPC error: " + status.error_message());
    }

    if (!response.success()) {
        return ClientResult<std::vector<DFGInstanceStatus>>(response.error_message());
    }

    std::vector<DFGInstanceStatus> results;
    for (const auto& proto_status : response.dfgs()) {
        DFGInstanceStatus result;
        result.instance_id = proto_status.instance_id();
        result.dfg_id = proto_status.dfg_id();
        result.uptime_seconds = proto_status.uptime_seconds();
        result.bytes_processed = proto_status.bytes_processed();
        result.operations_completed = proto_status.operations_completed();

        switch (proto_status.state()) {
            case pos::DFGStatus::STATE_DEPLOYING:
                result.state = DFGInstanceStatus::State::DEPLOYING;
                break;
            case pos::DFGStatus::STATE_RUNNING:
                result.state = DFGInstanceStatus::State::RUNNING;
                break;
            case pos::DFGStatus::STATE_STALLED:
                result.state = DFGInstanceStatus::State::STALLED;
                break;
            case pos::DFGStatus::STATE_ERROR:
                result.state = DFGInstanceStatus::State::ERROR;
                break;
            case pos::DFGStatus::STATE_STOPPED:
                result.state = DFGInstanceStatus::State::STOPPED;
                break;
            default:
                result.state = DFGInstanceStatus::State::UNKNOWN;
                break;
        }

        results.push_back(std::move(result));
    }

    return ClientResult<std::vector<DFGInstanceStatus>>(std::move(results));
}

// ============================================================================
// Node Operations
// ============================================================================

ClientResult<uint64_t> POSClient::executeNode(
    const std::string& instance_id,
    const std::string& node_id,
    const std::string& cap_id,
    uint64_t src_addr, uint32_t src_len,
    uint64_t dst_addr, uint32_t dst_len,
    bool blocking) {

    pos::ExecuteNodeRequest request;
    request.set_instance_id(instance_id);
    request.set_node_id(node_id);
    request.set_client_id(client_id_);
    request.set_cap_id(cap_id);
    request.set_src_addr(src_addr);
    request.set_src_len(src_len);
    request.set_dst_addr(dst_addr);
    request.set_dst_len(dst_len);
    request.set_blocking(blocking);

    pos::ExecuteNodeResponse response;
    auto context = createContext();

    grpc::Status status = stub_->ExecuteNode(context.get(), request, &response);

    if (!status.ok()) {
        return ClientResult<uint64_t>("gRPC error: " + status.error_message());
    }

    if (!response.success()) {
        return ClientResult<uint64_t>(response.error_message());
    }

    return ClientResult<uint64_t>(response.completion_id());
}

// ============================================================================
// Buffer Operations
// ============================================================================

ClientResult<std::string> POSClient::readBuffer(
    const std::string& instance_id,
    const std::string& buffer_id,
    const std::string& cap_id,
    uint64_t offset, uint64_t length) {

    pos::ReadBufferRequest request;
    request.set_instance_id(instance_id);
    request.set_buffer_id(buffer_id);
    request.set_client_id(client_id_);
    request.set_cap_id(cap_id);
    request.set_offset(offset);
    request.set_length(length);

    pos::ReadBufferResponse response;
    auto context = createContext();

    grpc::Status status = stub_->ReadBuffer(context.get(), request, &response);

    if (!status.ok()) {
        return ClientResult<std::string>("gRPC error: " + status.error_message());
    }

    if (!response.success()) {
        return ClientResult<std::string>(response.error_message());
    }

    return ClientResult<std::string>(response.data());
}

ClientResult<void> POSClient::writeBuffer(
    const std::string& instance_id,
    const std::string& buffer_id,
    const std::string& cap_id,
    uint64_t offset,
    const std::string& data) {

    pos::WriteBufferRequest request;
    request.set_instance_id(instance_id);
    request.set_buffer_id(buffer_id);
    request.set_client_id(client_id_);
    request.set_cap_id(cap_id);
    request.set_offset(offset);
    request.set_data(data);

    pos::WriteBufferResponse response;
    auto context = createContext();

    grpc::Status status = stub_->WriteBuffer(context.get(), request, &response);

    if (!status.ok()) {
        return ClientResult<void>("gRPC error: " + status.error_message());
    }

    if (!response.success()) {
        return ClientResult<void>(response.error_message());
    }

    return ClientResult<void>(true);
}

// ============================================================================
// Capability Management
// ============================================================================

ClientResult<CapabilityInfo> POSClient::delegateCapability(
    const std::string& instance_id,
    const std::string& source_cap_id,
    const std::string& new_cap_id,
    uint32_t permissions,
    int scope,
    uint64_t expiry_timestamp) {

    pos::DelegateCapabilityRequest request;
    request.set_instance_id(instance_id);
    request.set_source_cap_id(source_cap_id);
    request.set_new_cap_id(new_cap_id);
    request.set_permissions(permissions);
    request.set_scope(static_cast<pos::CapabilityScope>(scope));
    request.set_expiry_timestamp(expiry_timestamp);
    request.set_client_id(client_id_);

    pos::DelegateCapabilityResponse response;
    auto context = createContext();

    grpc::Status status = stub_->DelegateCapability(context.get(), request, &response);

    if (!status.ok()) {
        return ClientResult<CapabilityInfo>("gRPC error: " + status.error_message());
    }

    if (!response.success()) {
        return ClientResult<CapabilityInfo>(response.error_message());
    }

    CapabilityInfo info;
    info.cap_id = response.delegated_cap().cap_id();
    info.permissions = response.delegated_cap().permissions();
    info.scope = static_cast<int>(response.delegated_cap().scope());
    info.parent_cap_id = response.delegated_cap().parent_cap_id();
    info.expiry_timestamp = response.delegated_cap().expiry_timestamp();

    return ClientResult<CapabilityInfo>(std::move(info));
}

ClientResult<uint32_t> POSClient::revokeCapability(
    const std::string& instance_id,
    const std::string& cap_id,
    const std::string& admin_cap_id,
    bool recursive) {

    pos::RevokeCapabilityRequest request;
    request.set_instance_id(instance_id);
    request.set_cap_id(cap_id);
    request.set_admin_cap_id(admin_cap_id);
    request.set_recursive(recursive);
    request.set_client_id(client_id_);

    pos::RevokeCapabilityResponse response;
    auto context = createContext();

    grpc::Status status = stub_->RevokeCapability(context.get(), request, &response);

    if (!status.ok()) {
        return ClientResult<uint32_t>("gRPC error: " + status.error_message());
    }

    if (!response.success()) {
        return ClientResult<uint32_t>(response.error_message());
    }

    return ClientResult<uint32_t>(response.revoked_count());
}

// ============================================================================
// Health and Monitoring
// ============================================================================

ClientResult<ServerHealth> POSClient::healthCheck() {
    pos::HealthCheckRequest request;
    request.set_client_id(client_id_);

    pos::HealthCheckResponse response;
    auto context = createContext();

    grpc::Status status = stub_->HealthCheck(context.get(), request, &response);

    if (!status.ok()) {
        return ClientResult<ServerHealth>("gRPC error: " + status.error_message());
    }

    ServerHealth health;
    health.healthy = response.healthy();
    health.version = response.version();
    health.active_dfgs = response.active_dfgs();
    health.uptime_seconds = response.uptime_seconds();
    health.available_memory = response.available_memory();
    health.available_vfpgas = response.available_vfpgas();

    return ClientResult<ServerHealth>(std::move(health));
}

// ============================================================================
// Multi-FPGA Support
// ============================================================================

ClientResult<POSClient::RDMAConnectionInfo> POSClient::setupRDMA(
    const std::string& instance_id,
    const std::string& node_id,
    const std::string& remote_ip,
    uint32_t remote_rdma_port,
    uint32_t buffer_size,
    bool is_initiator) {

    pos::SetupRDMARequest request;
    request.set_instance_id(instance_id);
    request.set_node_id(node_id);
    request.set_client_id(client_id_);
    request.set_remote_ip(remote_ip);
    request.set_remote_rdma_port(remote_rdma_port);
    request.set_buffer_size(buffer_size);
    request.set_is_initiator(is_initiator);

    pos::SetupRDMAResponse response;
    auto context = createContext();

    grpc::Status status = stub_->SetupRDMA(context.get(), request, &response);

    if (!status.ok()) {
        return ClientResult<RDMAConnectionInfo>("gRPC error: " + status.error_message());
    }

    if (!response.success()) {
        return ClientResult<RDMAConnectionInfo>(response.error_message());
    }

    RDMAConnectionInfo info;
    info.local_qpn = response.local_qpn();
    info.remote_qpn = response.remote_qpn();
    info.local_ip = response.local_ip();
    info.remote_ip = response.remote_ip();

    return ClientResult<RDMAConnectionInfo>(std::move(info));
}

ClientResult<void> POSClient::executeDFG(const std::string& instance_id,
                                         const std::string& cap_id) {
    pos::ExecuteDFGRequest request;
    request.set_instance_id(instance_id);
    request.set_client_id(client_id_);
    request.set_cap_id(cap_id);

    pos::ExecuteDFGResponse response;
    auto context = createContext();

    grpc::Status status = stub_->ExecuteDFG(context.get(), request, &response);

    if (!status.ok()) {
        return ClientResult<void>("gRPC error: " + status.error_message());
    }

    if (!response.success()) {
        return ClientResult<void>(response.error_message());
    }

    return ClientResult<void>(true);
}

// ============================================================================
// DFG Specification Builder Helpers
// ============================================================================

std::unique_ptr<pos::DFGSpec> POSClient::createDFGSpec(
    const std::string& dfg_id,
    const std::string& app_id,
    uint32_t device_id,
    bool use_huge_pages) {

    auto spec = std::make_unique<pos::DFGSpec>();
    spec->set_dfg_id(dfg_id);
    spec->set_app_id(app_id);
    spec->set_device_id(device_id);
    spec->set_use_huge_pages(use_huge_pages);
    spec->set_stream_mode(pos::STREAM_HOST);

    return spec;
}

void POSClient::addComputeNode(pos::DFGSpec* spec,
                               const std::string& node_id,
                               int vfid,
                               uint32_t operation_type) {
    auto* node = spec->add_nodes();
    node->set_node_id(node_id);
    node->set_node_type(pos::NODE_TYPE_COMPUTE);

    auto* config = node->mutable_compute_config();
    config->set_vfid(vfid);
    config->set_operation_type(operation_type);
}

void POSClient::addRDMANode(pos::DFGSpec* spec,
                            const std::string& node_id,
                            uint16_t vlan_id,
                            const std::string& remote_host,
                            uint32_t remote_port) {
    auto* node = spec->add_nodes();
    node->set_node_id(node_id);
    node->set_node_type(pos::NODE_TYPE_NETWORK_RDMA);

    auto* config = node->mutable_rdma_config();
    config->set_vlan_id(vlan_id);
    if (!remote_host.empty()) {
        config->set_remote_host(remote_host);
        config->set_remote_port(remote_port);
    }
}

void POSClient::addTCPNode(pos::DFGSpec* spec,
                           const std::string& node_id,
                           bool is_server,
                           uint32_t port,
                           const std::string& remote_host) {
    auto* node = spec->add_nodes();
    node->set_node_id(node_id);
    node->set_node_type(pos::NODE_TYPE_NETWORK_TCP);

    auto* config = node->mutable_tcp_config();
    config->set_is_server(is_server);
    config->set_port(port);
    if (!remote_host.empty()) {
        config->set_remote_host(remote_host);
    }
}

void POSClient::addParserNode(pos::DFGSpec* spec,
                              const std::string& node_id,
                              uint64_t max_memory,
                              double max_cpu,
                              uint32_t max_threads) {
    auto* node = spec->add_nodes();
    node->set_node_id(node_id);
    node->set_node_type(pos::NODE_TYPE_SOFTWARE_PARSER);

    auto* config = node->mutable_software_config();
    config->set_max_memory_bytes(max_memory);
    config->set_max_cpu_percent(max_cpu);
    config->set_max_threads(max_threads);
}

void POSClient::addSoftwareNFNode(pos::DFGSpec* spec,
                                  const std::string& node_id,
                                  uint64_t max_memory,
                                  double max_cpu,
                                  uint32_t max_threads) {
    auto* node = spec->add_nodes();
    node->set_node_id(node_id);
    node->set_node_type(pos::NODE_TYPE_SOFTWARE_NF);

    auto* config = node->mutable_software_config();
    config->set_max_memory_bytes(max_memory);
    config->set_max_cpu_percent(max_cpu);
    config->set_max_threads(max_threads);
}

void POSClient::addBuffer(pos::DFGSpec* spec,
                          const std::string& buffer_id,
                          uint64_t size,
                          bool use_huge_pages,
                          const std::string& initial_data) {
    auto* buffer = spec->add_buffers();
    buffer->set_buffer_id(buffer_id);
    buffer->set_size(size);
    buffer->set_use_huge_pages(use_huge_pages);
    if (!initial_data.empty()) {
        buffer->set_initial_data(initial_data);
    }
}

void POSClient::addEdge(pos::DFGSpec* spec,
                        const std::string& source_id,
                        const std::string& target_id,
                        const std::string& edge_id) {
    auto* edge = spec->add_edges();
    edge->set_source_id(source_id);
    edge->set_target_id(target_id);
    if (!edge_id.empty()) {
        edge->set_edge_id(edge_id);
    }
}

} // namespace pos

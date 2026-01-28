/**
  * Copyright (c) 2021, Systems Group, ETH Zurich
  * All rights reserved.
  *
  * Redistribution and use in source and binary forms, with or without modification,
  * are permitted provided that the following conditions are met:
  *
  * 1. Redistributions of source code must retain the above copyright notice,
  * this list of conditions and the following disclaimer.
  * 2. Redistributions in binary form must reproduce the above copyright notice,
  * this list of conditions and the following disclaimer in the documentation
  * and/or other materials provided with the distribution.
  * 3. Neither the name of the copyright holder nor the names of its contributors
  * may be used to endorse or promote products derived from this software
  * without specific prior written permission.
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
  * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
  * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  */

`timescale 1ns / 1ps

import lynxTypes::*;

`include "axi_macros.svh"
`include "lynx_macros.svh"

/**
 * Memory Gateway Module (gate_mem)
 *
 * Filters DMA requests based on memory endpoint bounds checking.
 * This module enforces memory isolation by validating that all
 * read/write requests fall within configured endpoint regions.
 *
 * Features:
 * - Configurable number of memory endpoints
 * - Overflow-safe bounds checking
 * - Immediate drop for unauthorized requests
 * - Violation counting for security monitoring
 *
 * Endpoint configuration format (99 bits per endpoint):
 *   [98]      valid (1 bit)
 *   [97:96]   access_rights (2 bits) - [1]=write, [0]=read
 *   [95:48]   vaddr_bound (48 bits)
 *   [47:0]    vaddr_base (48 bits)
 */
module gate_mem #(
    parameter integer N_ENDPOINTS = 1
) (
    input  logic                                aclk,
    input  logic                                aresetn,

    // Memory endpoint control interface (99 bits per endpoint)
    input  logic [(99*N_ENDPOINTS)-1:0]         ep_ctrl,

  
    // Format: [13:10]=reserved, [9:6]=sender_id, [5:2]=receiver_id, [1:0]=flags
    // This ID is injected into outgoing DMA requests and used by the DMA controller
    // to route responses back through the vIO Switch to the correct vFPGA
    input  logic [13:0]                         route_id,

    // Original DMA interfaces (unfiltered inputs)
    metaIntf.s                                  s_rd_req,
    metaIntf.s                                  s_wr_req,

    // Filtered DMA interfaces (only authorized requests)
    metaIntf.m                                  m_rd_req,
    metaIntf.m                                  m_wr_req
);

    // ========================================================================================
    // ENDPOINT CONFIGURATION
    // ========================================================================================

    // Bit field definitions for each endpoint configuration
    localparam integer EP_BASE_ADDR_BITS = 48;
    localparam integer EP_BOUND_ADDR_BITS = 48;
    localparam integer EP_ACCESS_BITS = 2;
    localparam integer EP_VALID_BITS = 1;
    localparam integer EP_TOTAL_BITS = EP_BASE_ADDR_BITS + EP_BOUND_ADDR_BITS + EP_ACCESS_BITS + EP_VALID_BITS; // 99

    // Bit field offsets within each endpoint configuration
    localparam integer EP_BASE_ADDR_OFFSET = 0;
    localparam integer EP_BOUND_ADDR_OFFSET = EP_BASE_ADDR_OFFSET + EP_BASE_ADDR_BITS;
    localparam integer EP_ACCESS_OFFSET = EP_BOUND_ADDR_OFFSET + EP_BOUND_ADDR_BITS;
    localparam integer EP_VALID_OFFSET = EP_ACCESS_OFFSET + EP_ACCESS_BITS;

    // Endpoint registers
    endpoint_reg_t [N_ENDPOINTS-1:0] endpoint_regs;

    // Extract configuration for each endpoint and update registers
    always_ff @(posedge aclk) begin
        if (~aresetn) begin
            // Initialize all endpoints as invalid
            for (int i = 0; i < N_ENDPOINTS; i++) begin
                endpoint_regs[i].valid <= 1'b0;
                endpoint_regs[i].vaddr_base <= '0;
                endpoint_regs[i].vaddr_bound <= '0;
                endpoint_regs[i].access_rights <= 2'b00;
            end
        end
        else begin
            // Update endpoint configurations from packed control signal
            for (int i = 0; i < N_ENDPOINTS; i++) begin
                endpoint_regs[i].vaddr_base <= ep_ctrl[(i * EP_TOTAL_BITS) + EP_BASE_ADDR_OFFSET +: EP_BASE_ADDR_BITS];
                endpoint_regs[i].vaddr_bound <= ep_ctrl[(i * EP_TOTAL_BITS) + EP_BOUND_ADDR_OFFSET +: EP_BOUND_ADDR_BITS];
                endpoint_regs[i].access_rights <= ep_ctrl[(i * EP_TOTAL_BITS) + EP_ACCESS_OFFSET +: EP_ACCESS_BITS];
                endpoint_regs[i].valid <= ep_ctrl[(i * EP_TOTAL_BITS) + EP_VALID_OFFSET +: EP_VALID_BITS];

                // VALIDATION: Check that base <= bound for valid endpoints
                if (endpoint_regs[i].valid &&
                    (endpoint_regs[i].vaddr_base > endpoint_regs[i].vaddr_bound)) begin
                    // Invalid range - disable endpoint
                    endpoint_regs[i].valid <= 1'b0;
                end
            end
        end
    end

    // ========================================================================================
    // SECURITY VALIDATION - Overflow-Safe Bounds Checking
    // ========================================================================================

    logic rd_access_allowed, wr_access_allowed;
    logic violation_detected;

    always_comb begin
        // Default to deny access
        rd_access_allowed = 1'b0;
        wr_access_allowed = 1'b0;

        // Read request validation
        if (s_rd_req.valid && s_rd_req.data.len > 0) begin
            // Check against all endpoints
            for (int i = 0; i < N_ENDPOINTS; i++) begin
                if (endpoint_regs[i].valid && endpoint_regs[i].access_rights[0]) begin
                    // Calculate endpoint size to detect malicious huge lengths
                    logic [VADDR_BITS-1:0] endpoint_size;
                    endpoint_size = endpoint_regs[i].vaddr_bound - endpoint_regs[i].vaddr_base + 1;

                    // Security check: Length cannot exceed endpoint size (prevents underflow attack)
                    if (s_rd_req.data.len <= endpoint_size) begin
                        // Overflow-safe bounds check: both start and end must be within bounds
                        if (s_rd_req.data.vaddr >= endpoint_regs[i].vaddr_base &&
                            s_rd_req.data.vaddr <= (endpoint_regs[i].vaddr_bound - s_rd_req.data.len + 1)) begin
                            rd_access_allowed = 1'b1;
                            break; // Found valid endpoint, allow access
                        end
                    end
                    // If len > endpoint_size, skip this endpoint (implicit denial)
                end
            end
        end

        // Write request validation
        if (s_wr_req.valid && s_wr_req.data.len > 0) begin
            for (int i = 0; i < N_ENDPOINTS; i++) begin
                if (endpoint_regs[i].valid && endpoint_regs[i].access_rights[1]) begin
                    // Calculate endpoint size to detect malicious huge lengths
                    logic [VADDR_BITS-1:0] endpoint_size;
                    endpoint_size = endpoint_regs[i].vaddr_bound - endpoint_regs[i].vaddr_base + 1;

                    // Security check: Length cannot exceed endpoint size (prevents underflow attack)
                    if (s_wr_req.data.len <= endpoint_size) begin
                        // Overflow-safe bounds check: both start and end must be within bounds
                        if (s_wr_req.data.vaddr >= endpoint_regs[i].vaddr_base &&
                            s_wr_req.data.vaddr <= (endpoint_regs[i].vaddr_bound - s_wr_req.data.len + 1)) begin
                            wr_access_allowed = 1'b1;
                            break; // Found valid endpoint, allow access
                        end
                    end
                    // If len > endpoint_size, skip this endpoint (implicit denial)
                end
            end
        end
    end

    // ========================================================================================
    // Immediate Drop for Unauthorized Requests
    // ========================================================================================

    always_comb begin
        // AUTHORIZED REQUESTS: Forward to downstream with route_id injected
        m_rd_req.valid = s_rd_req.valid && rd_access_allowed;
        m_rd_req.data = s_rd_req.data;
        m_rd_req.data.route_id = route_id;  // Inject route_id for vIO Switch routing

        m_wr_req.valid = s_wr_req.valid && wr_access_allowed;
        m_wr_req.data = s_wr_req.data;
        m_wr_req.data.route_id = route_id;  // Inject route_id for vIO Switch routing

        // AUTHORIZED: Wait for downstream ready
        // UNAUTHORIZED: Immediately drop (assert ready to consume/reject request)
        s_rd_req.ready = rd_access_allowed ? m_rd_req.ready : 1'b1;
        s_wr_req.ready = wr_access_allowed ? m_wr_req.ready : 1'b1;
    end

    // ========================================================================================
    // Access Violation Detection
    // ========================================================================================

    // Detect violation occurrence (combinational)
    always_comb begin
        violation_detected =
            (s_rd_req.valid && !rd_access_allowed) ||
            (s_wr_req.valid && !wr_access_allowed);
    end

    logic access_violation_detected;
    logic [31:0] violation_count;

    always_ff @(posedge aclk) begin
        if (~aresetn) begin
            violation_count <= 0;
            access_violation_detected <= 1'b0;
        end
        else begin
            access_violation_detected <= violation_detected;
            if (violation_detected) begin
                violation_count <= violation_count + 1;
            end
        end
    end

endmodule

# Create Data Width Converter IPs
create_ip -name axis_dwidth_converter -vendor xilinx.com -library ip -version 1.1 -module_name dwidth_converter_512_256
set_property -dict [list \
    CONFIG.S_TDATA_NUM_BYTES {64} \
    CONFIG.M_TDATA_NUM_BYTES {32} \
    CONFIG.TID_WIDTH {4} \
    CONFIG.HAS_TLAST {1} \
] [get_ips dwidth_converter_512_256]

create_ip -name axis_dwidth_converter -vendor xilinx.com -library ip -version 1.1 -module_name dwidth_converter_256_512
set_property -dict [list \
    CONFIG.S_TDATA_NUM_BYTES {32} \
    CONFIG.M_TDATA_NUM_BYTES {64} \
    CONFIG.TID_WIDTH {4} \
    CONFIG.HAS_TLAST {1} \
] [get_ips dwidth_converter_256_512]

create_ip -name ila -vendor xilinx.com -library ip -version 6.2 -module_name ila_ctrl_fpga
set_property -dict [list \
    CONFIG.C_NUM_OF_PROBES {7} \
    CONFIG.C_PROBE0_WIDTH {2} \
    CONFIG.C_PROBE1_WIDTH {32} \
    CONFIG.C_PROBE2_WIDTH {64} \
    CONFIG.C_PROBE5_WIDTH {32} \
    CONFIG.C_EN_STRG_QUAL {1} \
    CONFIG.ALL_PROBE_SAME_MU_CNT {2} \
] [get_ips ila_ctrl_fpga]

# Generate all IP targets
generate_target all [get_ips]
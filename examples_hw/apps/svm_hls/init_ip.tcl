# Create Data Width Converter IPs
create_ip -name axis_dwidth_converter -vendor xilinx.com -library ip -version 1.1 -module_name dwidth_converter_512_32_3
set_property -dict [list \
    CONFIG.S_TDATA_NUM_BYTES {64} \
    CONFIG.M_TDATA_NUM_BYTES {4} \
    CONFIG.TID_WIDTH {4} \
    CONFIG.HAS_TLAST {1} \
] [get_ips dwidth_converter_512_32_3]

create_ip -name axis_dwidth_converter -vendor xilinx.com -library ip -version 1.1 -module_name dwidth_converter_32_512_3
set_property -dict [list \
    CONFIG.S_TDATA_NUM_BYTES {4} \
    CONFIG.M_TDATA_NUM_BYTES {64} \
    CONFIG.TID_WIDTH {4} \
    CONFIG.HAS_TLAST {1} \
] [get_ips dwidth_converter_32_512_3]

# Generate all IP targets
generate_target all [get_ips]

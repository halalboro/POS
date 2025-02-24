# Create Data Width Converter IPs
create_ip -name axis_dwidth_converter -vendor xilinx.com -library ip -version 1.1 -module_name dwidth_converter_512_64_2
set_property -dict [list \
    CONFIG.S_TDATA_NUM_BYTES {64} \
    CONFIG.M_TDATA_NUM_BYTES {8} \
    CONFIG.TID_WIDTH {4} \
    CONFIG.HAS_TLAST {1} \
] [get_ips dwidth_converter_512_64_2]

create_ip -name axis_dwidth_converter -vendor xilinx.com -library ip -version 1.1 -module_name dwidth_converter_32_512_2
set_property -dict [list \
    CONFIG.S_TDATA_NUM_BYTES {4} \
    CONFIG.M_TDATA_NUM_BYTES {64} \
    CONFIG.TID_WIDTH {4} \
    CONFIG.HAS_TLAST {1} \
] [get_ips dwidth_converter_32_512_2]

create_ip -name ila -vendor xilinx.com -library ip -version 6.2 -module_name ila_fft2svm

set_property -dict { 
    CONFIG.C_NUM_OF_PROBES {15} 
    CONFIG.C_EN_STRG_QUAL {1} 
    CONFIG.ALL_PROBE_SAME_MU_CNT {2}
} [get_ips ila_fft2svm]

# Generate all IP targets
generate_target all [get_ips]

# Create Data Width Converter IPs
create_ip -name axis_dwidth_converter -vendor xilinx.com -library ip -version 1.1 -module_name dwidth_converter_512_64
set_property -dict [list \
    CONFIG.S_TDATA_NUM_BYTES {64} \
    CONFIG.M_TDATA_NUM_BYTES {8} \
    CONFIG.TID_WIDTH {4} \
    CONFIG.HAS_TLAST {1} \
] [get_ips dwidth_converter_512_64]

create_ip -name axis_dwidth_converter -vendor xilinx.com -library ip -version 1.1 -module_name dwidth_converter_32_512
set_property -dict [list \
    CONFIG.S_TDATA_NUM_BYTES {4} \
    CONFIG.M_TDATA_NUM_BYTES {64} \
    CONFIG.TID_WIDTH {4} \
    CONFIG.HAS_TLAST {1} \
] [get_ips dwidth_converter_32_512]

# Create IP repository directory
file mkdir $build_dir/iprepo

# Unzip the IP to the repository
set cmd "exec unzip $hw_dir/ip/user_org_hls_fft2svm_0_c0_1_1_0.zip -d $build_dir/iprepo/fft2svm_0_c0_1"
eval $cmd

# Update IP catalog
update_ip_catalog -rebuild

# Try this instead
set fft2svm_0 [create_ip -name fft2svm_0_c0_1 -vendor user.org -library hls -version 1.0 -module_name fft2svm_0_c0_1]

# Configure IP properties
set_property -dict {
    GENERATE_SYNTH_CHECKPOINT {1}
} $fft2svm_0

# Generate IP targets
generate_target all [get_ips]

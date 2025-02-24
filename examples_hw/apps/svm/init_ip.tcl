# Create Data Width Converter IPs
create_ip -name axis_dwidth_converter -vendor xilinx.com -library ip -version 1.1 -module_name dwidth_converter_512_32
set_property -dict [list \
    CONFIG.S_TDATA_NUM_BYTES {64} \
    CONFIG.M_TDATA_NUM_BYTES {4} \
    CONFIG.TID_WIDTH {4} \
    CONFIG.HAS_TLAST {1} \
] [get_ips dwidth_converter_512_32]

create_ip -name axis_dwidth_converter -vendor xilinx.com -library ip -version 1.1 -module_name dwidth_converter_32_512
set_property -dict [list \
    CONFIG.S_TDATA_NUM_BYTES {4} \
    CONFIG.M_TDATA_NUM_BYTES {64} \
    CONFIG.TID_WIDTH {4} \
    CONFIG.HAS_TLAST {1} \
] [get_ips dwidth_converter_32_512]

file mkdir $build_dir/iprepo
set cmd "exec unzip $hw_dir/ip/user_org_hls_svm_speech_30_0_c0_0_1_0.zip -d $build_dir/iprepo/svm_speech_30_0_c0_0"
eval $cmd

# Update IP catalog
update_ip_catalog -rebuild

# Create IP instance with correct name and version
set svm_speech_30_0 [create_ip -name svm_speech_30_0_c0_0 -vendor user.org -library hls -version 1.0 -module_name svm_speech_30_0_c0_0]

# Configure IP properties
set_property -dict {
    GENERATE_SYNTH_CHECKPOINT {1}
} $svm_speech_30_0

# Generate all IP targets
generate_target all [get_ips]
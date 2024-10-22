# Create AXI Interconnect
create_ip -name axi_interconnect -vendor xilinx.com -library ip -version 2.1 -module_name axi_interconnect_0
# Configure AXI Interconnect
set_property -dict [list \
    CONFIG.NUM_SI {1} \
    CONFIG.NUM_MI {1} \
    CONFIG.S00_HAS_REGSLICE {4} \
    CONFIG.M00_HAS_REGSLICE {4} \
    CONFIG.ENABLE_ADVANCED_OPTIONS {1} \
    CONFIG.SYNCHRONIZATION_STAGES {3} \
    CONFIG.S00_HAS_DATA_FIFO {2} \
    CONFIG.M00_HAS_DATA_FIFO {2} \
    CONFIG.ENABLE_CLOCK_DOMAINS {1} \
    CONFIG.NUM_CLKS {2} \
] [get_ips axi_interconnect_0]
generate_target all [get_ips axi_interconnect_0]

# Create Clock Wizard
create_ip -name clk_wiz -vendor xilinx.com -library ip -version 6.0 -module_name clk_wiz_0
set_property -dict [list \
    CONFIG.PRIM_SOURCE {Differential_clock_capable_pin} \
    CONFIG.PRIM_IN_FREQ {100.000} \
    CONFIG.CLKOUT1_REQUESTED_OUT_FREQ {100.000} \
    CONFIG.CLKOUT2_REQUESTED_OUT_FREQ {10.000} \
    CONFIG.USE_SAFE_CLOCK_STARTUP {true} \
    CONFIG.USE_LOCKED {true} \
    CONFIG.RESET_TYPE {ACTIVE_HIGH} \
    CONFIG.NUM_OUT_CLKS {2} \
    CONFIG.CLKIN1_JITTER_PS {100.0} \
    CONFIG.FEEDBACK_SOURCE {FDBK_AUTO} \
    CONFIG.RESET_PORT {reset} \
] [get_ips clk_wiz_0]
generate_target all [get_ips clk_wiz_0]

# Create Processor System Reset modules
# For 100MHz clock domain
create_ip -name proc_sys_reset -vendor xilinx.com -library ip -version 5.0 -module_name proc_sys_reset_1
set_property -dict [list \
    CONFIG.USE_BOARD_FLOW {false} \
    CONFIG.RESET_BOARD_INTERFACE {Custom} \
    CONFIG.C_AUX_RESET_HIGH {0} \
    CONFIG.C_EXT_RESET_HIGH {1} \
] [get_ips proc_sys_reset_1]

# For 10MHz clock domain
create_ip -name proc_sys_reset -vendor xilinx.com -library ip -version 5.0 -module_name proc_sys_reset_0
set_property -dict [list \
    CONFIG.USE_BOARD_FLOW {false} \
    CONFIG.RESET_BOARD_INTERFACE {Custom} \
    CONFIG.C_AUX_RESET_HIGH {0} \
    CONFIG.C_EXT_RESET_HIGH {1} \
] [get_ips proc_sys_reset_0]

generate_target all [get_ips proc_sys_reset_1]
generate_target all [get_ips proc_sys_reset_0]

# Create AXI-Stream Clock Converter
create_ip -name axis_clock_converter -vendor xilinx.com -library ip -version 1.1 -module_name axis_clock_converter_0
set_property -dict [list \
    CONFIG.TDATA_NUM_BYTES {4} \
    CONFIG.HAS_TLAST {0} \
    CONFIG.HAS_TKEEP {1} \
    CONFIG.SYNCHRONIZATION_STAGES {3} \
] [get_ips axis_clock_converter_0]
generate_target all [get_ips axis_clock_converter_0]

# Import SVM IP
set dcp_path "$hw_dir/ip/svm_speech_30.dcp"
read_checkpoint $dcp_path
add_files $dcp_path
import_files -force -norecurse
create_ip_run [get_files $dcp_path]
set_property IS_LOCKED true [get_files $dcp_path]
set_property USED_IN {out_of_context synthesis implementation} [get_files $dcp_path]
set_property USED_IN_SYNTHESIS 1 [get_files $dcp_path]
set_property USED_IN_IMPLEMENTATION 1 [get_files $dcp_path]

# Create ILA for Debug
create_ip -name ila -vendor xilinx.com -library ip -version 6.2 -module_name ila_0
set_property -dict [list \
    CONFIG.C_NUM_OF_PROBES {17} \
    CONFIG.C_PROBE0_WIDTH {1} \     # tvalid
    CONFIG.C_PROBE1_WIDTH {1} \     # tready
    CONFIG.C_PROBE2_WIDTH {32} \    # tdata
    CONFIG.C_PROBE3_WIDTH {1} \     # tlast
    CONFIG.C_PROBE4_WIDTH {1} \     # tvalid
    CONFIG.C_PROBE5_WIDTH {1} \     # tready
    CONFIG.C_PROBE6_WIDTH {32} \    # tdata
    CONFIG.C_PROBE7_WIDTH {1} \     # tvalid
    CONFIG.C_PROBE8_WIDTH {1} \     # tready
    CONFIG.C_PROBE9_WIDTH {32} \    # tdata
    CONFIG.C_PROBE10_WIDTH {1} \    # tvalid
    CONFIG.C_PROBE11_WIDTH {1} \    # tready
    CONFIG.C_PROBE12_WIDTH {32} \   # tdata
    CONFIG.C_PROBE13_WIDTH {1} \    # tlast
    CONFIG.C_PROBE14_WIDTH {1} \    # clk_locked
    CONFIG.C_PROBE15_WIDTH {1} \    # rst_n_100M
    CONFIG.C_PROBE16_WIDTH {1} \    # rst_n_10M
    CONFIG.C_DATA_DEPTH {1024} \
    CONFIG.C_EN_STRG_QUAL {1} \
    CONFIG.C_ADV_TRIGGER {true} \
    CONFIG.ALL_PROBE_SAME_MU {true} \
    CONFIG.ALL_PROBE_SAME_MU_CNT {2} \
] [get_ips ila_0]

# Generate all IP targets
generate_target all [get_ips]
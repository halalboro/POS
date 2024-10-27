# Create Data Width Converter IPs
create_ip -name axis_dwidth_converter -vendor xilinx.com -library ip -version 1.1 -module_name dwidth_converter_512_32
set_property -dict [list \
    CONFIG.S_TDATA_NUM_BYTES {64} \
    CONFIG.M_TDATA_NUM_BYTES {4} \
    CONFIG.TID_WIDTH {6} \
    CONFIG.HAS_TLAST {1} \
    CONFIG.HAS_TKEEP {1} \
    CONFIG.Component_Name {dwidth_converter_512_32} \
] [get_ips dwidth_converter_512_32]

create_ip -name axis_dwidth_converter -vendor xilinx.com -library ip -version 1.1 -module_name dwidth_converter_32_512
set_property -dict [list \
    CONFIG.S_TDATA_NUM_BYTES {4} \
    CONFIG.M_TDATA_NUM_BYTES {64} \
    CONFIG.TID_WIDTH {6} \ 
    CONFIG.HAS_TLAST {1} \ 
    CONFIG.HAS_TKEEP {1} \ 
    CONFIG.HAS_MI_TKEEP {1} \ 
    CONFIG.Component_Name {dwidth_converter_32_512}\
] [get_ips dwidth_converter_32_512]

# Create AXI Crossbar (for SVM control)
create_ip -name axi_crossbar -vendor xilinx.com -library ip -version 2.1 -module_name axi_crossbar_0
set_property -dict [list \
    CONFIG.NUM_SI {1} \
    CONFIG.NUM_MI {1} \
    CONFIG.PROTOCOL {AXI4LITE} \
    CONFIG.CONNECTIVITY_MODE {SASD} \
    CONFIG.Component_Name {axi_crossbar_0} \
] [get_ips axi_crossbar_0]

# Create Clock Wizard
create_ip -name clk_wiz -vendor xilinx.com -library ip -version 6.0 -module_name clk_wiz_0
set_property -dict [list \
    CONFIG.PRIMITIVE {MMCM} \
    CONFIG.USE_LOCKED {true} \
    CONFIG.PRIM_SOURCE {Single_ended_clock_capable_pin} \
    CONFIG.PRIM_IN_FREQ {100.000} \
    CONFIG.CLKOUT1_USED {true} \
    CONFIG.NUM_OUT_CLKS {2} \
    CONFIG.CLKOUT2_USED {true} \
    CONFIG.CLKOUT1_REQUESTED_OUT_FREQ {100.000} \
    CONFIG.CLKOUT2_REQUESTED_OUT_FREQ {10.000} \
    CONFIG.MMCM_CLKOUT1_DIVIDE {120} \
    CONFIG.Component_Name {clk_wiz_0} \
] [get_ips clk_wiz_0]

# Create Processor System Reset modules
# For 100MHz clock domain
create_ip -name proc_sys_reset -vendor xilinx.com -library ip -version 5.0 -module_name proc_sys_reset_1
set_property -dict [list \
    CONFIG.USE_BOARD_FLOW {false} \
    CONFIG.RESET_BOARD_INTERFACE {Custom} \
    CONFIG.C_AUX_RESET_HIGH {0} \
    CONFIG.C_EXT_RESET_HIGH {1} \
    CONFIG.Component_Name {proc_sys_reset_1} \
] [get_ips proc_sys_reset_1]

# For 10MHz clock domain
create_ip -name proc_sys_reset -vendor xilinx.com -library ip -version 5.0 -module_name proc_sys_reset_0
set_property -dict [list \
    CONFIG.USE_BOARD_FLOW {false} \
    CONFIG.RESET_BOARD_INTERFACE {Custom} \
    CONFIG.C_AUX_RESET_HIGH {0} \
    CONFIG.C_EXT_RESET_HIGH {1} \
    CONFIG.Component_Name {proc_sys_reset_0} \
] [get_ips proc_sys_reset_0]

# Create AXI-Stream Clock Converter for FFT2SVM to SVM (100MHz -> 10MHz)
create_ip -name axis_clock_converter -vendor xilinx.com -library ip -version 1.1 -module_name axis_clock_converter_1
set_property -dict [list \
    CONFIG.TDATA_NUM_BYTES {4} \
    CONFIG.HAS_TKEEP {1} \
    CONFIG.SYNCHRONIZATION_STAGES {3} \
    CONFIG.HAS_TLAST {1} \
    CONFIG.Component_Name {axis_clock_converter_1} \
] [get_ips axis_clock_converter_1]

# Create AXI-Stream Clock Converter for SVM to Host (10MHz -> 100MHz)
create_ip -name axis_clock_converter -vendor xilinx.com -library ip -version 1.1 -module_name axis_clock_converter_0
set_property -dict [list \
    CONFIG.TDATA_NUM_BYTES {4} \
    CONFIG.HAS_TKEEP {1} \
    CONFIG.SYNCHRONIZATION_STAGES {3} \
    CONFIG.HAS_TLAST {1} \
    CONFIG.Component_Name {axis_clock_converter_0} \
] [get_ips axis_clock_converter_0]

# Create FFT IP
create_ip -name xfft -vendor xilinx.com -library ip -version 9.1 -module_name xfft_0
set_property -dict [list \
    CONFIG.transform_length {16384} \
    CONFIG.data_format {floating_point} \
    CONFIG.implementation_options {automatically_select} \
    CONFIG.input_width {32} \
    CONFIG.output_ordering {natural_order} \
    CONFIG.aresetn {true} \
    CONFIG.Component_Name {xfft_0} \
] [get_ips xfft_0]

# Create Constant blocks for FFT configuration
create_ip -name xlconstant -vendor xilinx.com -library ip -version 1.1 -module_name xlconstant_0
set_property -dict [list \
    CONFIG.CONST_WIDTH {16} \
    CONFIG.CONST_VAL {1} \
    CONFIG.Component_Name {xlconstant_0} \
] [get_ips xlconstant_0]

create_ip -name xlconstant -vendor xilinx.com -library ip -version 1.1 -module_name xlconstant_1
set_property -dict [list \
    CONFIG.CONST_WIDTH {1} \
    CONFIG.CONST_VAL {1} \
    CONFIG.Component_Name {xlconstant_1} \
] [get_ips xlconstant_1]

# Create ILA for Debug
create_ip -name ila -vendor xilinx.com -library ip -version 6.2 -module_name ila_0
set_property -dict [list \
    CONFIG.C_NUM_OF_PROBES {27} \
    CONFIG.C_DATA_DEPTH {1024} \
    CONFIG.C_ENABLE_ILA_AXI_MON {false} \
    CONFIG.C_MONITOR_TYPE {Native} \
    CONFIG.C_TRIGIN_EN {false} \
    CONFIG.Component_Name {ila_0} \
    CONFIG.C_EN_STRG_QUAL {1} \
    CONFIG.ALL_PROBE_SAME_MU_CNT {2} \
] [get_ips ila_0]

# Set probe widths after initial configuration
set_property -dict [list \
    CONFIG.C_PROBE0_WIDTH {1} \
    CONFIG.C_PROBE1_WIDTH {1} \
    CONFIG.C_PROBE2_WIDTH {32} \
    CONFIG.C_PROBE3_WIDTH {1} \
    CONFIG.C_PROBE4_WIDTH {1} \
    CONFIG.C_PROBE5_WIDTH {1} \
    CONFIG.C_PROBE6_WIDTH {64} \
    CONFIG.C_PROBE7_WIDTH {1} \
    CONFIG.C_PROBE8_WIDTH {1} \
    CONFIG.C_PROBE9_WIDTH {1} \
    CONFIG.C_PROBE10_WIDTH {32} \
    CONFIG.C_PROBE11_WIDTH {1} \
    CONFIG.C_PROBE12_WIDTH {1} \
    CONFIG.C_PROBE13_WIDTH {1} \
    CONFIG.C_PROBE14_WIDTH {32} \
    CONFIG.C_PROBE15_WIDTH {1} \
    CONFIG.C_PROBE16_WIDTH {1} \
    CONFIG.C_PROBE17_WIDTH {1} \
    CONFIG.C_PROBE18_WIDTH {32} \
    CONFIG.C_PROBE19_WIDTH {1} \
    CONFIG.C_PROBE20_WIDTH {1} \
    CONFIG.C_PROBE21_WIDTH {1} \
    CONFIG.C_PROBE22_WIDTH {32} \
    CONFIG.C_PROBE23_WIDTH {1} \
    CONFIG.C_PROBE24_WIDTH {1} \
    CONFIG.C_PROBE25_WIDTH {1} \
    CONFIG.C_PROBE26_WIDTH {1} \
] [get_ips ila_0]

# Generate all IP targets
generate_target all [get_ips]

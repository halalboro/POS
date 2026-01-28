#
# Register slices
#

# Common
create_ip -name axi_register_slice -vendor xilinx.com -library ip -version 2.1 -module_name axi_register_slice_512
set_property -dict [list CONFIG.ADDR_WIDTH {64} CONFIG.DATA_WIDTH {512} CONFIG.REG_AW {1} CONFIG.REG_AR {1} CONFIG.REG_B {1} CONFIG.ID_WIDTH {6} CONFIG.MAX_BURST_LENGTH {14} CONFIG.NUM_READ_OUTSTANDING {32} CONFIG.NUM_WRITE_OUTSTANDING {32}] [get_ips axi_register_slice_512]

create_ip -name axi_register_slice -vendor xilinx.com -library ip -version 2.1 -module_name axil_register_slice
set_property -dict [list CONFIG.PROTOCOL {AXI4LITE} CONFIG.ADDR_WIDTH {64} CONFIG.DATA_WIDTH {64} CONFIG.REG_AW {1} CONFIG.REG_AR {1} CONFIG.REG_W {1} CONFIG.REG_R {1} CONFIG.REG_B {1} ] [get_ips axil_register_slice]

create_ip -name axi_register_slice -vendor xilinx.com -library ip -version 2.1 -module_name axim_register_slice
set_property -dict [list CONFIG.ADDR_WIDTH {64} CONFIG.DATA_WIDTH {256} CONFIG.REG_AW {1} CONFIG.REG_AR {1} CONFIG.REG_B {1} CONFIG.ID_WIDTH {6} CONFIG.MAX_BURST_LENGTH {14} CONFIG.NUM_READ_OUTSTANDING {32} CONFIG.NUM_WRITE_OUTSTANDING {32}] [get_ips axim_register_slice]

create_ip -name axis_register_slice -vendor xilinx.com -library ip -version 1.1 -module_name axis_register_slice_512
set_property -dict [list CONFIG.TDATA_NUM_BYTES {64} CONFIG.REG_CONFIG {8} CONFIG.HAS_TKEEP {1} CONFIG.HAS_TLAST {1}] [get_ips axis_register_slice_512]

create_ip -name axis_register_slice -vendor xilinx.com -library ip -version 1.1 -module_name axisr_register_slice_512
set_property -dict [list CONFIG.TDATA_NUM_BYTES {64} CONFIG.REG_CONFIG {8} CONFIG.HAS_TKEEP {1} CONFIG.HAS_TLAST {1} CONFIG.TID_WIDTH {6}] [get_ips axisr_register_slice_512]

# Requests
create_ip -name axis_register_slice -vendor xilinx.com -library ip -version 1.1 -module_name axis_register_slice_req_96
set_property -dict [list CONFIG.TDATA_NUM_BYTES {12} CONFIG.REG_CONFIG {8} ] [get_ips axis_register_slice_req_96]

create_ip -name axis_register_slice -vendor xilinx.com -library ip -version 1.1 -module_name axis_register_slice_req_128
set_property -dict [list CONFIG.TDATA_NUM_BYTES {16} CONFIG.REG_CONFIG {8} ] [get_ips axis_register_slice_req_128]

create_ip -name axis_register_slice -vendor xilinx.com -library ip -version 1.1 -module_name axis_register_slice_req_256
set_property -dict [list CONFIG.TDATA_NUM_BYTES {32} CONFIG.REG_CONFIG {8} ] [get_ips axis_register_slice_req_256]

# Meta
create_ip -name axis_register_slice -vendor xilinx.com -library ip -version 1.1 -module_name axis_register_slice_meta_8
set_property -dict [list CONFIG.TDATA_NUM_BYTES {1} CONFIG.REG_CONFIG {8} ] [get_ips axis_register_slice_meta_8]

create_ip -name axis_register_slice -vendor xilinx.com -library ip -version 1.1 -module_name axis_register_slice_meta_16
set_property -dict [list CONFIG.TDATA_NUM_BYTES {2} CONFIG.REG_CONFIG {8} ] [get_ips axis_register_slice_meta_16]

create_ip -name axis_register_slice -vendor xilinx.com -library ip -version 1.1 -module_name axis_register_slice_meta_32
set_property -dict [list CONFIG.TDATA_NUM_BYTES {4} CONFIG.REG_CONFIG {8} ] [get_ips axis_register_slice_meta_32]

create_ip -name axis_register_slice -vendor xilinx.com -library ip -version 1.1 -module_name axis_register_slice_meta_56
set_property -dict [list CONFIG.TDATA_NUM_BYTES {7} CONFIG.REG_CONFIG {8} ] [get_ips axis_register_slice_meta_56]

create_ip -name axis_register_slice -vendor xilinx.com -library ip -version 1.1 -module_name axis_register_slice_meta_64
set_property -dict [list CONFIG.TDATA_NUM_BYTES {8} CONFIG.REG_CONFIG {8} ] [get_ips axis_register_slice_meta_64]

create_ip -name axis_register_slice -vendor xilinx.com -library ip -version 1.1 -module_name axis_register_slice_meta_72
set_property -dict [list CONFIG.TDATA_NUM_BYTES {9} CONFIG.REG_CONFIG {8} ] [get_ips axis_register_slice_meta_72]

create_ip -name axis_register_slice -vendor xilinx.com -library ip -version 1.1 -module_name axis_register_slice_meta_96
set_property -dict [list CONFIG.TDATA_NUM_BYTES {12} CONFIG.REG_CONFIG {8} ] [get_ips axis_register_slice_meta_96]

create_ip -name axis_register_slice -vendor xilinx.com -library ip -version 1.1 -module_name axis_register_slice_meta_128
set_property -dict [list CONFIG.TDATA_NUM_BYTES {16} CONFIG.REG_CONFIG {8} ] [get_ips axis_register_slice_meta_128]

create_ip -name axis_register_slice -vendor xilinx.com -library ip -version 1.1 -module_name axis_register_slice_meta_256
set_property -dict [list CONFIG.TDATA_NUM_BYTES {32} CONFIG.REG_CONFIG {8} ] [get_ips axis_register_slice_meta_256]

create_ip -name axis_register_slice -vendor xilinx.com -library ip -version 1.1 -module_name axis_register_slice_meta_512
set_property -dict [list CONFIG.TDATA_NUM_BYTES {64} CONFIG.REG_CONFIG {8} ] [get_ips axis_register_slice_meta_512]

#
# CCross
#

# Common
create_ip -name axi_clock_converter -vendor xilinx.com -library ip -version 2.1 -module_name axi_clock_converter_512
set_property -dict [list CONFIG.ADDR_WIDTH {64} CONFIG.DATA_WIDTH {512} CONFIG.ID_WIDTH {6}] [get_ips axi_clock_converter_512]

create_ip -name axi_clock_converter -vendor xilinx.com -library ip -version 2.1 -module_name axi_clock_converter_256
set_property -dict [list CONFIG.ADDR_WIDTH {64} CONFIG.DATA_WIDTH {256} CONFIG.ID_WIDTH {6}] [get_ips axi_clock_converter_256]

create_ip -name axi_clock_converter -vendor xilinx.com -library ip -version 2.1 -module_name axil_clock_converter
set_property -dict [list CONFIG.PROTOCOL {AXI4LITE} CONFIG.ADDR_WIDTH {64} CONFIG.DATA_WIDTH {64} CONFIG.ID_WIDTH {0} CONFIG.AWUSER_WIDTH {0} CONFIG.ARUSER_WIDTH {0} CONFIG.RUSER_WIDTH {0} CONFIG.WUSER_WIDTH {0} CONFIG.BUSER_WIDTH {0}] [get_ips axil_clock_converter]

create_ip -name axis_clock_converter -vendor xilinx.com -library ip -version 1.1 -module_name axis_clock_converter_512
set_property -dict [list CONFIG.TDATA_NUM_BYTES {64} CONFIG.HAS_TKEEP {1} CONFIG.HAS_TLAST {1} ] [get_ips axis_clock_converter_512]

create_ip -name axis_clock_converter -vendor xilinx.com -library ip -version 1.1 -module_name axisr_clock_converter_512
set_property -dict [list CONFIG.TDATA_NUM_BYTES {64} CONFIG.HAS_TKEEP {1} CONFIG.HAS_TLAST {1} CONFIG.TID_WIDTH {6}] [get_ips axisr_clock_converter_512]

create_ip -name axis_clock_converter -vendor xilinx.com -library ip -version 1.1 -module_name axis_clock_converter_96
set_property -dict [list CONFIG.TDATA_NUM_BYTES {12} ] [get_ips axis_clock_converter_96]

create_ip -name axis_clock_converter -vendor xilinx.com -library ip -version 1.1 -module_name axis_clock_converter_dma_rsp
set_property -dict [list CONFIG.TDATA_NUM_BYTES {0} CONFIG.TUSER_WIDTH {6} CONFIG.SYNCHRONIZATION_STAGES {2} ] [get_ips axis_clock_converter_dma_rsp]

# Meta
create_ip -name axis_clock_converter -vendor xilinx.com -library ip -version 1.1 -module_name meta_clock_converter_8
set_property -dict [list CONFIG.TDATA_NUM_BYTES {1} ] [get_ips meta_clock_converter_8]

create_ip -name axis_clock_converter -vendor xilinx.com -library ip -version 1.1 -module_name meta_clock_converter_16
set_property -dict [list CONFIG.TDATA_NUM_BYTES {2} ] [get_ips meta_clock_converter_16]

create_ip -name axis_clock_converter -vendor xilinx.com -library ip -version 1.1 -module_name meta_clock_converter_32
set_property -dict [list CONFIG.TDATA_NUM_BYTES {4} ] [get_ips meta_clock_converter_32]

create_ip -name axis_clock_converter -vendor xilinx.com -library ip -version 1.1 -module_name meta_clock_converter_64
set_property -dict [list CONFIG.TDATA_NUM_BYTES {8} ] [get_ips meta_clock_converter_64]

create_ip -name axis_clock_converter -vendor xilinx.com -library ip -version 1.1 -module_name meta_clock_converter_72
set_property -dict [list CONFIG.TDATA_NUM_BYTES {9} ] [get_ips meta_clock_converter_72]

create_ip -name axis_clock_converter -vendor xilinx.com -library ip -version 1.1 -module_name meta_clock_converter_96
set_property -dict [list CONFIG.TDATA_NUM_BYTES {12} ] [get_ips meta_clock_converter_96]

create_ip -name axis_clock_converter -vendor xilinx.com -library ip -version 1.1 -module_name meta_clock_converter_128
set_property -dict [list CONFIG.TDATA_NUM_BYTES {16} ] [get_ips meta_clock_converter_128]

create_ip -name axis_clock_converter -vendor xilinx.com -library ip -version 1.1 -module_name meta_clock_converter_256
set_property -dict [list CONFIG.TDATA_NUM_BYTES {32} ] [get_ips meta_clock_converter_256]

create_ip -name axis_clock_converter -vendor xilinx.com -library ip -version 1.1 -module_name meta_clock_converter_512
set_property -dict [list CONFIG.TDATA_NUM_BYTES {64} ] [get_ips meta_clock_converter_512]

#
# FIFOs
#

# Requests
create_ip -name axis_data_fifo -vendor xilinx.com -library ip -version 2.0 -module_name axis_data_fifo_req_96_used
set_property -dict [list CONFIG.TDATA_NUM_BYTES {12} CONFIG.FIFO_DEPTH {32} CONFIG.HAS_WR_DATA_COUNT {1} ] [get_ips axis_data_fifo_req_96_used]

create_ip -name axis_data_fifo -vendor xilinx.com -library ip -version 2.0 -module_name axis_data_fifo_req_128_used
set_property -dict [list CONFIG.TDATA_NUM_BYTES {16} CONFIG.FIFO_DEPTH {32} CONFIG.HAS_WR_DATA_COUNT {1} ] [get_ips axis_data_fifo_req_128_used]

create_ip -name axis_data_fifo -vendor xilinx.com -library ip -version 2.0 -module_name axis_data_fifo_req_256_used
set_property -dict [list CONFIG.TDATA_NUM_BYTES {32} CONFIG.FIFO_DEPTH {32} CONFIG.HAS_WR_DATA_COUNT {1} ] [get_ips axis_data_fifo_req_256_used]

create_ip -name axis_data_fifo -vendor xilinx.com -library ip -version 2.0 -module_name axis_data_fifo_req_96
set_property -dict [list CONFIG.TDATA_NUM_BYTES {12} CONFIG.FIFO_DEPTH {32} ] [get_ips axis_data_fifo_req_96]

create_ip -name axis_data_fifo -vendor xilinx.com -library ip -version 2.0 -module_name axis_data_fifo_req_128
set_property -dict [list CONFIG.TDATA_NUM_BYTES {16} CONFIG.FIFO_DEPTH {32} ] [get_ips axis_data_fifo_req_128]

create_ip -name axis_data_fifo -vendor xilinx.com -library ip -version 2.0 -module_name axis_data_fifo_req_256
set_property -dict [list CONFIG.TDATA_NUM_BYTES {32} CONFIG.FIFO_DEPTH {32} ] [get_ips axis_data_fifo_req_256]

# Meta
create_ip -name axis_data_fifo -vendor xilinx.com -library ip -version 2.0 -module_name axis_data_fifo_meta_8
set_property -dict [list CONFIG.TDATA_NUM_BYTES {1} CONFIG.FIFO_DEPTH {32} ] [get_ips axis_data_fifo_meta_8]

create_ip -name axis_data_fifo -vendor xilinx.com -library ip -version 2.0 -module_name axis_data_fifo_meta_16
set_property -dict [list CONFIG.TDATA_NUM_BYTES {2} CONFIG.FIFO_DEPTH {32} ] [get_ips axis_data_fifo_meta_16]

create_ip -name axis_data_fifo -vendor xilinx.com -library ip -version 2.0 -module_name axis_data_fifo_meta_32
set_property -dict [list CONFIG.TDATA_NUM_BYTES {4} CONFIG.FIFO_DEPTH {32} ] [get_ips axis_data_fifo_meta_32]

create_ip -name axis_data_fifo -vendor xilinx.com -library ip -version 2.0 -module_name axis_data_fifo_meta_64
set_property -dict [list CONFIG.TDATA_NUM_BYTES {8} CONFIG.FIFO_DEPTH {32} ] [get_ips axis_data_fifo_meta_64]

create_ip -name axis_data_fifo -vendor xilinx.com -library ip -version 2.0 -module_name axis_data_fifo_meta_96
set_property -dict [list CONFIG.TDATA_NUM_BYTES {12} CONFIG.FIFO_DEPTH {32} ] [get_ips axis_data_fifo_meta_96]

create_ip -name axis_data_fifo -vendor xilinx.com -library ip -version 2.0 -module_name axis_data_fifo_meta_128
set_property -dict [list CONFIG.TDATA_NUM_BYTES {16} CONFIG.FIFO_DEPTH {32} ] [get_ips axis_data_fifo_meta_128]

create_ip -name axis_data_fifo -vendor xilinx.com -library ip -version 2.0 -module_name axis_data_fifo_meta_256
set_property -dict [list CONFIG.TDATA_NUM_BYTES {32} CONFIG.FIFO_DEPTH {32} ] [get_ips axis_data_fifo_meta_256]

create_ip -name axis_data_fifo -vendor xilinx.com -library ip -version 2.0 -module_name axis_data_fifo_meta_512
set_property -dict [list CONFIG.TDATA_NUM_BYTES {64} CONFIG.FIFO_DEPTH {32} ] [get_ips axis_data_fifo_meta_512]

#
# Data queues
#

set nn 1024
if {$cfg(pmtu) > 1024} {
    set nn 2048
}
if {$cfg(pmtu) > 2048} {
    set nn 4096
}
if {$cfg(pmtu) > 4096} {
    set nn 8192
}
if {$cfg(pmtu) > 8192} {
    set nn 16384
}
if {$cfg(pmtu) > 16384} {
    set nn 32768
}
set nn512 [expr {$cfg(n_outs) * ($nn / 64)}]

create_ip -name axis_data_fifo -vendor xilinx.com -library ip -version 2.0 -module_name axis_data_fifo_512
set cmd "set_property -dict \[list CONFIG.TDATA_NUM_BYTES {64} CONFIG.FIFO_DEPTH {$nn512} CONFIG.HAS_TKEEP {1} CONFIG.HAS_TLAST {1} ] \[get_ips axis_data_fifo_512]"
eval $cmd

create_ip -name axis_data_fifo -vendor xilinx.com -library ip -version 2.0 -module_name axisr_data_fifo_512
set cmd "set_property -dict \[list CONFIG.TDATA_NUM_BYTES {64} CONFIG.FIFO_DEPTH {$nn512} CONFIG.HAS_TKEEP {1} CONFIG.HAS_TLAST {1}  CONFIG.TID_WIDTH {6}] \[get_ips axisr_data_fifo_512]"
eval $cmd

# ILA
create_ip -name ila -vendor xilinx.com -library ip -version 6.2 -module_name ila_mem_gate_signal
set_property -dict [list \
    CONFIG.C_NUM_OF_PROBES {1} \
    CONFIG.C_PROBE0_WIDTH {130} \
    CONFIG.C_EN_STRG_QUAL {1} \
    CONFIG.ALL_PROBE_SAME_MU_CNT {2} \
] [get_ips ila_mem_gate_signal]

create_ip -name ila -vendor xilinx.com -library ip -version 6.2 -module_name ila_mem_region_top_req_t
set_property -dict [list \
    CONFIG.C_NUM_OF_PROBES {32} \
    CONFIG.C_PROBE0_WIDTH {5} \
    CONFIG.C_PROBE1_WIDTH {2} \
    CONFIG.C_PROBE2_WIDTH {1} \
    CONFIG.C_PROBE3_WIDTH {1} \
    CONFIG.C_PROBE4_WIDTH {1} \
    CONFIG.C_PROBE5_WIDTH {4} \
    CONFIG.C_PROBE6_WIDTH {6} \
    CONFIG.C_PROBE7_WIDTH {4} \
    CONFIG.C_PROBE8_WIDTH {1} \
    CONFIG.C_PROBE9_WIDTH {48} \
    CONFIG.C_PROBE10_WIDTH {28} \
    CONFIG.C_PROBE11_WIDTH {1} \
    CONFIG.C_PROBE12_WIDTH {1} \
    CONFIG.C_PROBE13_WIDTH {6} \
    CONFIG.C_PROBE14_WIDTH {1} \
    CONFIG.C_PROBE15_WIDTH {1} \
    CONFIG.C_PROBE16_WIDTH {5} \
    CONFIG.C_PROBE17_WIDTH {2} \
    CONFIG.C_PROBE18_WIDTH {1} \
    CONFIG.C_PROBE19_WIDTH {1} \
    CONFIG.C_PROBE20_WIDTH {1} \
    CONFIG.C_PROBE21_WIDTH {4} \
    CONFIG.C_PROBE22_WIDTH {6} \
    CONFIG.C_PROBE23_WIDTH {4} \
    CONFIG.C_PROBE24_WIDTH {1} \
    CONFIG.C_PROBE25_WIDTH {48} \
    CONFIG.C_PROBE26_WIDTH {28} \
    CONFIG.C_PROBE27_WIDTH {1} \
    CONFIG.C_PROBE28_WIDTH {1} \
    CONFIG.C_PROBE29_WIDTH {6} \
    CONFIG.C_PROBE30_WIDTH {1} \
    CONFIG.C_PROBE31_WIDTH {1} \
    CONFIG.C_EN_STRG_QUAL {1} \
    CONFIG.ALL_PROBE_SAME_MU_CNT {2} \
] [get_ips ila_mem_region_top_req_t]

# # Interconnect
# create_ip -name axi_interconnect -vendor xilinx.com -library ip -version 2.1 -module_name axi_stream_interconnect_0
# # Configure AXI Interconnect
# set_property -dict [list \
#     CONFIG.NUM_SI {1} \
#     CONFIG.NUM_MI {1} \
#     CONFIG.S00_HAS_REGSLICE {4} \
#     CONFIG.M00_HAS_REGSLICE {4} \
#     CONFIG.ENABLE_ADVANCED_OPTIONS {1} \
#     CONFIG.SYNCHRONIZATION_STAGES {3} \
#     CONFIG.S00_HAS_DATA_FIFO {2} \
#     CONFIG.M00_HAS_DATA_FIFO {2} \
#     CONFIG.ENABLE_CLOCK_DOMAINS {1} \
#     CONFIG.NUM_CLKS {2} \
# ] [get_ips axi_stream_interconnect_0]
# generate_target all [get_ips axi_stream_interconnect_0]

create_ip -name axis_switch -vendor xilinx.com -library ip -version 1.1 -module_name axis_switch_0
set_property -dict [list CONFIG.NUM_MI {4} CONFIG.NUM_SI {4} CONFIG.HAS_TLAST {1} CONFIG.TDATA_NUM_BYTES {64} CONFIG.TDEST_WIDTH {2} CONFIG.DECODER_REG {1}] [get_ips axis_switch_0]

create_ip -name axis_switch -vendor xilinx.com -library ip -version 1.1 -module_name axis_switch_2_0
set_property -dict [list CONFIG.NUM_MI {2} CONFIG.NUM_SI {2} CONFIG.HAS_TLAST {1} CONFIG.TDATA_NUM_BYTES {64} CONFIG.TDEST_WIDTH {2} CONFIG.TID_WIDTH {6} CONFIG.DECODER_REG {1}] [get_ips axis_switch_2_0]

create_ip -name axis_switch -vendor xilinx.com -library ip -version 1.1 -module_name axis_switch_4_0
set_property -dict [list CONFIG.NUM_MI {4} CONFIG.NUM_SI {4} CONFIG.HAS_TLAST {1} CONFIG.TDATA_NUM_BYTES {64} CONFIG.TDEST_WIDTH {8} CONFIG.TID_WIDTH {6} CONFIG.DECODER_REG {1}] [get_ips axis_switch_4_0]
set_property -dict [list CONFIG.M00_AXIS_BASETDEST {0x00} CONFIG.M01_AXIS_BASETDEST {0x20} CONFIG.M02_AXIS_BASETDEST {0x40} CONFIG.M03_AXIS_BASETDEST {0x60} \
    CONFIG.M00_AXIS_HIGHTDEST {0x0000001F} CONFIG.M01_AXIS_HIGHTDEST {0x0000003F} CONFIG.M02_AXIS_HIGHTDEST {0x5F} CONFIG.M03_AXIS_HIGHTDEST {0x7f}] [get_ips axis_switch_4_0]

create_ip -name axis_switch -vendor xilinx.com -library ip -version 1.1 -module_name vio_switch_ip_4
set_property -dict [list CONFIG.NUM_MI {4} CONFIG.NUM_SI {4} CONFIG.HAS_TLAST {1} CONFIG.TDATA_NUM_BYTES {64} CONFIG.TDEST_WIDTH {14} CONFIG.TID_WIDTH {6} CONFIG.DECODER_REG {1}] [get_ips vio_switch_ip_4]
set_property -dict [list CONFIG.M00_AXIS_BASETDEST {0x00} CONFIG.M01_AXIS_BASETDEST {0x400} CONFIG.M02_AXIS_BASETDEST {0x800} CONFIG.M03_AXIS_BASETDEST {0xC00} \
    CONFIG.M00_AXIS_HIGHTDEST {0x3FF} CONFIG.M01_AXIS_HIGHTDEST {0x7FF} CONFIG.M02_AXIS_HIGHTDEST {0xBFF} CONFIG.M03_AXIS_HIGHTDEST {0xFFF}] [get_ips vio_switch_ip_4]

create_ip -name axis_switch -vendor xilinx.com -library ip -version 1.1 -module_name axis_switch_6_0
set_property -dict [list CONFIG.NUM_MI {6} CONFIG.NUM_SI {6} CONFIG.HAS_TLAST {1} CONFIG.TDATA_NUM_BYTES {64} CONFIG.TDEST_WIDTH {8} CONFIG.TID_WIDTH {6} CONFIG.DECODER_REG {1}] [get_ips axis_switch_6_0]
set_property -dict [list CONFIG.M00_AXIS_BASETDEST {0x00} CONFIG.M01_AXIS_BASETDEST {0x20} \
    CONFIG.M02_AXIS_BASETDEST {0x40} CONFIG.M03_AXIS_BASETDEST {0x60} \
    CONFIG.M04_AXIS_BASETDEST {0x80} CONFIG.M05_AXIS_BASETDEST {0xA0} \
    CONFIG.M00_AXIS_HIGHTDEST {0x1F} CONFIG.M01_AXIS_HIGHTDEST {0x3F} \
    CONFIG.M02_AXIS_HIGHTDEST {0x5F} CONFIG.M03_AXIS_HIGHTDEST {0x7F} \
    CONFIG.M04_AXIS_HIGHTDEST {0x9F} CONFIG.M05_AXIS_HIGHTDEST {0xBF}] [get_ips axis_switch_6_0]

create_ip -name axis_switch -vendor xilinx.com -library ip -version 1.1 -module_name vio_switch_ip_6
set_property -dict [list CONFIG.NUM_MI {6} CONFIG.NUM_SI {6} CONFIG.HAS_TLAST {1} CONFIG.TDATA_NUM_BYTES {64} CONFIG.TDEST_WIDTH {14} CONFIG.TID_WIDTH {6} CONFIG.DECODER_REG {1}] [get_ips vio_switch_ip_6]
set_property -dict [list CONFIG.M00_AXIS_BASETDEST {0x000} CONFIG.M01_AXIS_BASETDEST {0x400} \
    CONFIG.M02_AXIS_BASETDEST {0x800} CONFIG.M03_AXIS_BASETDEST {0xC00} \
    CONFIG.M04_AXIS_BASETDEST {0x1000} CONFIG.M05_AXIS_BASETDEST {0x1400} \
    CONFIG.M00_AXIS_HIGHTDEST {0x3FF} CONFIG.M01_AXIS_HIGHTDEST {0x7FF} \
    CONFIG.M02_AXIS_HIGHTDEST {0xBFF} CONFIG.M03_AXIS_HIGHTDEST {0xFFF} \
    CONFIG.M04_AXIS_HIGHTDEST {0x13FF} CONFIG.M05_AXIS_HIGHTDEST {0x17FF}] [get_ips vio_switch_ip_6]


create_ip -name axis_switch -vendor xilinx.com -library ip -version 1.1 -module_name vio_switch_ip_8
set_property -dict [list CONFIG.NUM_MI {8} CONFIG.NUM_SI {8} CONFIG.HAS_TLAST {1} CONFIG.TDATA_NUM_BYTES {64} CONFIG.TDEST_WIDTH {14} CONFIG.TID_WIDTH {6} CONFIG.DECODER_REG {1}] [get_ips vio_switch_ip_8]
set_property -dict [list CONFIG.M00_AXIS_BASETDEST {0x000} CONFIG.M01_AXIS_BASETDEST {0x400} \
    CONFIG.M02_AXIS_BASETDEST {0x800} CONFIG.M03_AXIS_BASETDEST {0xC00} \
    CONFIG.M04_AXIS_BASETDEST {0x1000} CONFIG.M05_AXIS_BASETDEST {0x1400} \
    CONFIG.M06_AXIS_BASETDEST {0x1800} CONFIG.M07_AXIS_BASETDEST {0x1C00} \
    CONFIG.M00_AXIS_HIGHTDEST {0x3FF} CONFIG.M01_AXIS_HIGHTDEST {0x7FF} \
    CONFIG.M02_AXIS_HIGHTDEST {0xBFF} CONFIG.M03_AXIS_HIGHTDEST {0xFFF} \
    CONFIG.M04_AXIS_HIGHTDEST {0x13FF} CONFIG.M05_AXIS_HIGHTDEST {0x17FF} \
    CONFIG.M06_AXIS_HIGHTDEST {0x1BFF} CONFIG.M07_AXIS_HIGHTDEST {0x1FFF}] [get_ips vio_switch_ip_8]

create_ip -name axis_switch -vendor xilinx.com -library ip -version 1.1 -module_name vio_switch_ip_12
set_property -dict [list CONFIG.NUM_MI {12} CONFIG.NUM_SI {12} CONFIG.HAS_TLAST {1} CONFIG.TDATA_NUM_BYTES {64} CONFIG.TDEST_WIDTH {14} CONFIG.TID_WIDTH {6} CONFIG.DECODER_REG {1}] [get_ips vio_switch_ip_12]
set_property -dict [list CONFIG.M00_AXIS_BASETDEST {0x000} CONFIG.M01_AXIS_BASETDEST {0x400} \
    CONFIG.M02_AXIS_BASETDEST {0x800} CONFIG.M03_AXIS_BASETDEST {0xC00} \
    CONFIG.M04_AXIS_BASETDEST {0x1000} CONFIG.M05_AXIS_BASETDEST {0x1400} \
    CONFIG.M06_AXIS_BASETDEST {0x1800} CONFIG.M07_AXIS_BASETDEST {0x1C00} \
    CONFIG.M08_AXIS_BASETDEST {0x2000} CONFIG.M09_AXIS_BASETDEST {0x2400} \
    CONFIG.M10_AXIS_BASETDEST {0x2800} CONFIG.M11_AXIS_BASETDEST {0x2C00} \
    CONFIG.M00_AXIS_HIGHTDEST {0x3FF} CONFIG.M01_AXIS_HIGHTDEST {0x7FF} \
    CONFIG.M02_AXIS_HIGHTDEST {0xBFF} CONFIG.M03_AXIS_HIGHTDEST {0xFFF} \
    CONFIG.M04_AXIS_HIGHTDEST {0x13FF} CONFIG.M05_AXIS_HIGHTDEST {0x17FF} \
    CONFIG.M06_AXIS_HIGHTDEST {0x1BFF} CONFIG.M07_AXIS_HIGHTDEST {0x1FFF} \
    CONFIG.M08_AXIS_HIGHTDEST {0x23FF} CONFIG.M09_AXIS_HIGHTDEST {0x27FF} \
    CONFIG.M10_AXIS_HIGHTDEST {0x2BFF} CONFIG.M11_AXIS_HIGHTDEST {0x2FFF}] [get_ips vio_switch_ip_12]


create_ip -name axis_switch -vendor xilinx.com -library ip -version 1.1 -module_name vio_switch_ip_16
set_property -dict [list CONFIG.NUM_MI {16} CONFIG.NUM_SI {16} CONFIG.HAS_TLAST {1} CONFIG.TDATA_NUM_BYTES {64} CONFIG.TDEST_WIDTH {14} CONFIG.TID_WIDTH {6} CONFIG.DECODER_REG {1}] [get_ips vio_switch_ip_16]
set_property -dict [list CONFIG.M00_AXIS_BASETDEST {0x000} CONFIG.M01_AXIS_BASETDEST {0x400} \
    CONFIG.M02_AXIS_BASETDEST {0x800} CONFIG.M03_AXIS_BASETDEST {0xC00} \
    CONFIG.M04_AXIS_BASETDEST {0x1000} CONFIG.M05_AXIS_BASETDEST {0x1400} \
    CONFIG.M06_AXIS_BASETDEST {0x1800} CONFIG.M07_AXIS_BASETDEST {0x1C00} \
    CONFIG.M08_AXIS_BASETDEST {0x2000} CONFIG.M09_AXIS_BASETDEST {0x2400} \
    CONFIG.M10_AXIS_BASETDEST {0x2800} CONFIG.M11_AXIS_BASETDEST {0x2C00} \
    CONFIG.M12_AXIS_BASETDEST {0x3000} CONFIG.M13_AXIS_BASETDEST {0x3400} \
    CONFIG.M14_AXIS_BASETDEST {0x3800} CONFIG.M15_AXIS_BASETDEST {0x3C00} \
    CONFIG.M00_AXIS_HIGHTDEST {0x3FF} CONFIG.M01_AXIS_HIGHTDEST {0x7FF} \
    CONFIG.M02_AXIS_HIGHTDEST {0xBFF} CONFIG.M03_AXIS_HIGHTDEST {0xFFF} \
    CONFIG.M04_AXIS_HIGHTDEST {0x13FF} CONFIG.M05_AXIS_HIGHTDEST {0x17FF} \
    CONFIG.M06_AXIS_HIGHTDEST {0x1BFF} CONFIG.M07_AXIS_HIGHTDEST {0x1FFF} \
    CONFIG.M08_AXIS_HIGHTDEST {0x23FF} CONFIG.M09_AXIS_HIGHTDEST {0x27FF} \
    CONFIG.M10_AXIS_HIGHTDEST {0x2BFF} CONFIG.M11_AXIS_HIGHTDEST {0x2FFF} \
    CONFIG.M12_AXIS_HIGHTDEST {0x33FF} CONFIG.M13_AXIS_HIGHTDEST {0x37FF} \
    CONFIG.M14_AXIS_HIGHTDEST {0x3BFF} CONFIG.M15_AXIS_HIGHTDEST {0x3FFF}] [get_ips vio_switch_ip_16]

# ============================================================================
# POS Extended vIO Switch IPs (with RDMA/TCP/Bypass ports)
# New size = 2*N + 5 (N vFIU + N Host + RDMA_RX + RDMA_TX_REQ + RDMA_TX_RSP + TCP + Bypass)
# ============================================================================

# vio_switch_ip_9: For vio_switch_2 (2 vFIU + 2 Host + 5 network ports)
create_ip -name axis_switch -vendor xilinx.com -library ip -version 1.1 -module_name vio_switch_ip_9
set_property -dict [list CONFIG.NUM_MI {9} CONFIG.NUM_SI {9} CONFIG.HAS_TLAST {1} CONFIG.TDATA_NUM_BYTES {64} CONFIG.TDEST_WIDTH {14} CONFIG.TID_WIDTH {6} CONFIG.DECODER_REG {1}] [get_ips vio_switch_ip_9]
set_property -dict [list CONFIG.M00_AXIS_BASETDEST {0x000} CONFIG.M01_AXIS_BASETDEST {0x400} \
    CONFIG.M02_AXIS_BASETDEST {0x800} CONFIG.M03_AXIS_BASETDEST {0xC00} \
    CONFIG.M04_AXIS_BASETDEST {0x1000} CONFIG.M05_AXIS_BASETDEST {0x1400} \
    CONFIG.M06_AXIS_BASETDEST {0x1800} CONFIG.M07_AXIS_BASETDEST {0x1C00} \
    CONFIG.M08_AXIS_BASETDEST {0x2000} \
    CONFIG.M00_AXIS_HIGHTDEST {0x3FF} CONFIG.M01_AXIS_HIGHTDEST {0x7FF} \
    CONFIG.M02_AXIS_HIGHTDEST {0xBFF} CONFIG.M03_AXIS_HIGHTDEST {0xFFF} \
    CONFIG.M04_AXIS_HIGHTDEST {0x13FF} CONFIG.M05_AXIS_HIGHTDEST {0x17FF} \
    CONFIG.M06_AXIS_HIGHTDEST {0x1BFF} CONFIG.M07_AXIS_HIGHTDEST {0x1FFF} \
    CONFIG.M08_AXIS_HIGHTDEST {0x23FF}] [get_ips vio_switch_ip_9]

# vio_switch_ip_11: For vio_switch_3 (3 vFIU + 3 Host + 5 network ports)
create_ip -name axis_switch -vendor xilinx.com -library ip -version 1.1 -module_name vio_switch_ip_11
set_property -dict [list CONFIG.NUM_MI {11} CONFIG.NUM_SI {11} CONFIG.HAS_TLAST {1} CONFIG.TDATA_NUM_BYTES {64} CONFIG.TDEST_WIDTH {14} CONFIG.TID_WIDTH {6} CONFIG.DECODER_REG {1}] [get_ips vio_switch_ip_11]
set_property -dict [list CONFIG.M00_AXIS_BASETDEST {0x000} CONFIG.M01_AXIS_BASETDEST {0x400} \
    CONFIG.M02_AXIS_BASETDEST {0x800} CONFIG.M03_AXIS_BASETDEST {0xC00} \
    CONFIG.M04_AXIS_BASETDEST {0x1000} CONFIG.M05_AXIS_BASETDEST {0x1400} \
    CONFIG.M06_AXIS_BASETDEST {0x1800} CONFIG.M07_AXIS_BASETDEST {0x1C00} \
    CONFIG.M08_AXIS_BASETDEST {0x2000} CONFIG.M09_AXIS_BASETDEST {0x2400} \
    CONFIG.M10_AXIS_BASETDEST {0x2800} \
    CONFIG.M00_AXIS_HIGHTDEST {0x3FF} CONFIG.M01_AXIS_HIGHTDEST {0x7FF} \
    CONFIG.M02_AXIS_HIGHTDEST {0xBFF} CONFIG.M03_AXIS_HIGHTDEST {0xFFF} \
    CONFIG.M04_AXIS_HIGHTDEST {0x13FF} CONFIG.M05_AXIS_HIGHTDEST {0x17FF} \
    CONFIG.M06_AXIS_HIGHTDEST {0x1BFF} CONFIG.M07_AXIS_HIGHTDEST {0x1FFF} \
    CONFIG.M08_AXIS_HIGHTDEST {0x23FF} CONFIG.M09_AXIS_HIGHTDEST {0x27FF} \
    CONFIG.M10_AXIS_HIGHTDEST {0x2BFF}] [get_ips vio_switch_ip_11]

# vio_switch_ip_13: For vio_switch_4 (4 vFIU + 4 Host + 5 network ports)
create_ip -name axis_switch -vendor xilinx.com -library ip -version 1.1 -module_name vio_switch_ip_13
set_property -dict [list CONFIG.NUM_MI {13} CONFIG.NUM_SI {13} CONFIG.HAS_TLAST {1} CONFIG.TDATA_NUM_BYTES {64} CONFIG.TDEST_WIDTH {14} CONFIG.TID_WIDTH {6} CONFIG.DECODER_REG {1}] [get_ips vio_switch_ip_13]
set_property -dict [list CONFIG.M00_AXIS_BASETDEST {0x000} CONFIG.M01_AXIS_BASETDEST {0x400} \
    CONFIG.M02_AXIS_BASETDEST {0x800} CONFIG.M03_AXIS_BASETDEST {0xC00} \
    CONFIG.M04_AXIS_BASETDEST {0x1000} CONFIG.M05_AXIS_BASETDEST {0x1400} \
    CONFIG.M06_AXIS_BASETDEST {0x1800} CONFIG.M07_AXIS_BASETDEST {0x1C00} \
    CONFIG.M08_AXIS_BASETDEST {0x2000} CONFIG.M09_AXIS_BASETDEST {0x2400} \
    CONFIG.M10_AXIS_BASETDEST {0x2800} CONFIG.M11_AXIS_BASETDEST {0x2C00} \
    CONFIG.M12_AXIS_BASETDEST {0x3000} \
    CONFIG.M00_AXIS_HIGHTDEST {0x3FF} CONFIG.M01_AXIS_HIGHTDEST {0x7FF} \
    CONFIG.M02_AXIS_HIGHTDEST {0xBFF} CONFIG.M03_AXIS_HIGHTDEST {0xFFF} \
    CONFIG.M04_AXIS_HIGHTDEST {0x13FF} CONFIG.M05_AXIS_HIGHTDEST {0x17FF} \
    CONFIG.M06_AXIS_HIGHTDEST {0x1BFF} CONFIG.M07_AXIS_HIGHTDEST {0x1FFF} \
    CONFIG.M08_AXIS_HIGHTDEST {0x23FF} CONFIG.M09_AXIS_HIGHTDEST {0x27FF} \
    CONFIG.M10_AXIS_HIGHTDEST {0x2BFF} CONFIG.M11_AXIS_HIGHTDEST {0x2FFF} \
    CONFIG.M12_AXIS_HIGHTDEST {0x33FF}] [get_ips vio_switch_ip_13]

# vio_switch_ip_17: For vio_switch_6 (6 vFIU + 6 Host + 5 network ports)
create_ip -name axis_switch -vendor xilinx.com -library ip -version 1.1 -module_name vio_switch_ip_17
set_property -dict [list CONFIG.NUM_MI {17} CONFIG.NUM_SI {17} CONFIG.HAS_TLAST {1} CONFIG.TDATA_NUM_BYTES {64} CONFIG.TDEST_WIDTH {14} CONFIG.TID_WIDTH {6} CONFIG.DECODER_REG {1}] [get_ips vio_switch_ip_17]
set_property -dict [list CONFIG.M00_AXIS_BASETDEST {0x000} CONFIG.M01_AXIS_BASETDEST {0x400} \
    CONFIG.M02_AXIS_BASETDEST {0x800} CONFIG.M03_AXIS_BASETDEST {0xC00} \
    CONFIG.M04_AXIS_BASETDEST {0x1000} CONFIG.M05_AXIS_BASETDEST {0x1400} \
    CONFIG.M06_AXIS_BASETDEST {0x1800} CONFIG.M07_AXIS_BASETDEST {0x1C00} \
    CONFIG.M08_AXIS_BASETDEST {0x2000} CONFIG.M09_AXIS_BASETDEST {0x2400} \
    CONFIG.M10_AXIS_BASETDEST {0x2800} CONFIG.M11_AXIS_BASETDEST {0x2C00} \
    CONFIG.M12_AXIS_BASETDEST {0x3000} CONFIG.M13_AXIS_BASETDEST {0x3400} \
    CONFIG.M14_AXIS_BASETDEST {0x3800} CONFIG.M15_AXIS_BASETDEST {0x3C00} \
    CONFIG.M16_AXIS_BASETDEST {0x4000} \
    CONFIG.M00_AXIS_HIGHTDEST {0x3FF} CONFIG.M01_AXIS_HIGHTDEST {0x7FF} \
    CONFIG.M02_AXIS_HIGHTDEST {0xBFF} CONFIG.M03_AXIS_HIGHTDEST {0xFFF} \
    CONFIG.M04_AXIS_HIGHTDEST {0x13FF} CONFIG.M05_AXIS_HIGHTDEST {0x17FF} \
    CONFIG.M06_AXIS_HIGHTDEST {0x1BFF} CONFIG.M07_AXIS_HIGHTDEST {0x1FFF} \
    CONFIG.M08_AXIS_HIGHTDEST {0x23FF} CONFIG.M09_AXIS_HIGHTDEST {0x27FF} \
    CONFIG.M10_AXIS_HIGHTDEST {0x2BFF} CONFIG.M11_AXIS_HIGHTDEST {0x2FFF} \
    CONFIG.M12_AXIS_HIGHTDEST {0x33FF} CONFIG.M13_AXIS_HIGHTDEST {0x37FF} \
    CONFIG.M14_AXIS_HIGHTDEST {0x3BFF} CONFIG.M15_AXIS_HIGHTDEST {0x3FFF} \
    CONFIG.M16_AXIS_HIGHTDEST {0x43FF}] [get_ips vio_switch_ip_17]

# vio_switch_ip_21: For vio_switch_8 (8 vFIU + 8 Host + 5 network ports)
create_ip -name axis_switch -vendor xilinx.com -library ip -version 1.1 -module_name vio_switch_ip_21
set_property -dict [list CONFIG.NUM_MI {21} CONFIG.NUM_SI {21} CONFIG.HAS_TLAST {1} CONFIG.TDATA_NUM_BYTES {64} CONFIG.TDEST_WIDTH {14} CONFIG.TID_WIDTH {6} CONFIG.DECODER_REG {1}] [get_ips vio_switch_ip_21]
set_property -dict [list CONFIG.M00_AXIS_BASETDEST {0x000} CONFIG.M01_AXIS_BASETDEST {0x400} \
    CONFIG.M02_AXIS_BASETDEST {0x800} CONFIG.M03_AXIS_BASETDEST {0xC00} \
    CONFIG.M04_AXIS_BASETDEST {0x1000} CONFIG.M05_AXIS_BASETDEST {0x1400} \
    CONFIG.M06_AXIS_BASETDEST {0x1800} CONFIG.M07_AXIS_BASETDEST {0x1C00} \
    CONFIG.M08_AXIS_BASETDEST {0x2000} CONFIG.M09_AXIS_BASETDEST {0x2400} \
    CONFIG.M10_AXIS_BASETDEST {0x2800} CONFIG.M11_AXIS_BASETDEST {0x2C00} \
    CONFIG.M12_AXIS_BASETDEST {0x3000} CONFIG.M13_AXIS_BASETDEST {0x3400} \
    CONFIG.M14_AXIS_BASETDEST {0x3800} CONFIG.M15_AXIS_BASETDEST {0x3C00} \
    CONFIG.M16_AXIS_BASETDEST {0x4000} CONFIG.M17_AXIS_BASETDEST {0x4400} \
    CONFIG.M18_AXIS_BASETDEST {0x4800} CONFIG.M19_AXIS_BASETDEST {0x4C00} \
    CONFIG.M20_AXIS_BASETDEST {0x5000} \
    CONFIG.M00_AXIS_HIGHTDEST {0x3FF} CONFIG.M01_AXIS_HIGHTDEST {0x7FF} \
    CONFIG.M02_AXIS_HIGHTDEST {0xBFF} CONFIG.M03_AXIS_HIGHTDEST {0xFFF} \
    CONFIG.M04_AXIS_HIGHTDEST {0x13FF} CONFIG.M05_AXIS_HIGHTDEST {0x17FF} \
    CONFIG.M06_AXIS_HIGHTDEST {0x1BFF} CONFIG.M07_AXIS_HIGHTDEST {0x1FFF} \
    CONFIG.M08_AXIS_HIGHTDEST {0x23FF} CONFIG.M09_AXIS_HIGHTDEST {0x27FF} \
    CONFIG.M10_AXIS_HIGHTDEST {0x2BFF} CONFIG.M11_AXIS_HIGHTDEST {0x2FFF} \
    CONFIG.M12_AXIS_HIGHTDEST {0x33FF} CONFIG.M13_AXIS_HIGHTDEST {0x37FF} \
    CONFIG.M14_AXIS_HIGHTDEST {0x3BFF} CONFIG.M15_AXIS_HIGHTDEST {0x3FFF} \
    CONFIG.M16_AXIS_HIGHTDEST {0x43FF} CONFIG.M17_AXIS_HIGHTDEST {0x47FF} \
    CONFIG.M18_AXIS_HIGHTDEST {0x4BFF} CONFIG.M19_AXIS_HIGHTDEST {0x4FFF} \
    CONFIG.M20_AXIS_HIGHTDEST {0x53FF}] [get_ips vio_switch_ip_21]

# vio_switch_ip_25: For vio_switch_10 (10 vFIU + 10 Host + 5 network ports)
create_ip -name axis_switch -vendor xilinx.com -library ip -version 1.1 -module_name vio_switch_ip_25
set_property -dict [list CONFIG.NUM_MI {25} CONFIG.NUM_SI {25} CONFIG.HAS_TLAST {1} CONFIG.TDATA_NUM_BYTES {64} CONFIG.TDEST_WIDTH {14} CONFIG.TID_WIDTH {6} CONFIG.DECODER_REG {1}] [get_ips vio_switch_ip_25]
set_property -dict [list CONFIG.M00_AXIS_BASETDEST {0x000} CONFIG.M01_AXIS_BASETDEST {0x400} \
    CONFIG.M02_AXIS_BASETDEST {0x800} CONFIG.M03_AXIS_BASETDEST {0xC00} \
    CONFIG.M04_AXIS_BASETDEST {0x1000} CONFIG.M05_AXIS_BASETDEST {0x1400} \
    CONFIG.M06_AXIS_BASETDEST {0x1800} CONFIG.M07_AXIS_BASETDEST {0x1C00} \
    CONFIG.M08_AXIS_BASETDEST {0x2000} CONFIG.M09_AXIS_BASETDEST {0x2400} \
    CONFIG.M10_AXIS_BASETDEST {0x2800} CONFIG.M11_AXIS_BASETDEST {0x2C00} \
    CONFIG.M12_AXIS_BASETDEST {0x3000} CONFIG.M13_AXIS_BASETDEST {0x3400} \
    CONFIG.M14_AXIS_BASETDEST {0x3800} CONFIG.M15_AXIS_BASETDEST {0x3C00} \
    CONFIG.M16_AXIS_BASETDEST {0x4000} CONFIG.M17_AXIS_BASETDEST {0x4400} \
    CONFIG.M18_AXIS_BASETDEST {0x4800} CONFIG.M19_AXIS_BASETDEST {0x4C00} \
    CONFIG.M20_AXIS_BASETDEST {0x5000} CONFIG.M21_AXIS_BASETDEST {0x5400} \
    CONFIG.M22_AXIS_BASETDEST {0x5800} CONFIG.M23_AXIS_BASETDEST {0x5C00} \
    CONFIG.M24_AXIS_BASETDEST {0x6000} \
    CONFIG.M00_AXIS_HIGHTDEST {0x3FF} CONFIG.M01_AXIS_HIGHTDEST {0x7FF} \
    CONFIG.M02_AXIS_HIGHTDEST {0xBFF} CONFIG.M03_AXIS_HIGHTDEST {0xFFF} \
    CONFIG.M04_AXIS_HIGHTDEST {0x13FF} CONFIG.M05_AXIS_HIGHTDEST {0x17FF} \
    CONFIG.M06_AXIS_HIGHTDEST {0x1BFF} CONFIG.M07_AXIS_HIGHTDEST {0x1FFF} \
    CONFIG.M08_AXIS_HIGHTDEST {0x23FF} CONFIG.M09_AXIS_HIGHTDEST {0x27FF} \
    CONFIG.M10_AXIS_HIGHTDEST {0x2BFF} CONFIG.M11_AXIS_HIGHTDEST {0x2FFF} \
    CONFIG.M12_AXIS_HIGHTDEST {0x33FF} CONFIG.M13_AXIS_HIGHTDEST {0x37FF} \
    CONFIG.M14_AXIS_HIGHTDEST {0x3BFF} CONFIG.M15_AXIS_HIGHTDEST {0x3FFF} \
    CONFIG.M16_AXIS_HIGHTDEST {0x43FF} CONFIG.M17_AXIS_HIGHTDEST {0x47FF} \
    CONFIG.M18_AXIS_HIGHTDEST {0x4BFF} CONFIG.M19_AXIS_HIGHTDEST {0x4FFF} \
    CONFIG.M20_AXIS_HIGHTDEST {0x53FF} CONFIG.M21_AXIS_HIGHTDEST {0x57FF} \
    CONFIG.M22_AXIS_HIGHTDEST {0x5BFF} CONFIG.M23_AXIS_HIGHTDEST {0x5FFF} \
    CONFIG.M24_AXIS_HIGHTDEST {0x63FF}] [get_ips vio_switch_ip_25]

# vio_switch_ip_29: For vio_switch_12 (12 vFIU + 12 Host + 5 network ports)
create_ip -name axis_switch -vendor xilinx.com -library ip -version 1.1 -module_name vio_switch_ip_29
set_property -dict [list CONFIG.NUM_MI {29} CONFIG.NUM_SI {29} CONFIG.HAS_TLAST {1} CONFIG.TDATA_NUM_BYTES {64} CONFIG.TDEST_WIDTH {14} CONFIG.TID_WIDTH {6} CONFIG.DECODER_REG {1}] [get_ips vio_switch_ip_29]
set_property -dict [list CONFIG.M00_AXIS_BASETDEST {0x000} CONFIG.M01_AXIS_BASETDEST {0x400} \
    CONFIG.M02_AXIS_BASETDEST {0x800} CONFIG.M03_AXIS_BASETDEST {0xC00} \
    CONFIG.M04_AXIS_BASETDEST {0x1000} CONFIG.M05_AXIS_BASETDEST {0x1400} \
    CONFIG.M06_AXIS_BASETDEST {0x1800} CONFIG.M07_AXIS_BASETDEST {0x1C00} \
    CONFIG.M08_AXIS_BASETDEST {0x2000} CONFIG.M09_AXIS_BASETDEST {0x2400} \
    CONFIG.M10_AXIS_BASETDEST {0x2800} CONFIG.M11_AXIS_BASETDEST {0x2C00} \
    CONFIG.M12_AXIS_BASETDEST {0x3000} CONFIG.M13_AXIS_BASETDEST {0x3400} \
    CONFIG.M14_AXIS_BASETDEST {0x3800} CONFIG.M15_AXIS_BASETDEST {0x3C00} \
    CONFIG.M16_AXIS_BASETDEST {0x4000} CONFIG.M17_AXIS_BASETDEST {0x4400} \
    CONFIG.M18_AXIS_BASETDEST {0x4800} CONFIG.M19_AXIS_BASETDEST {0x4C00} \
    CONFIG.M20_AXIS_BASETDEST {0x5000} CONFIG.M21_AXIS_BASETDEST {0x5400} \
    CONFIG.M22_AXIS_BASETDEST {0x5800} CONFIG.M23_AXIS_BASETDEST {0x5C00} \
    CONFIG.M24_AXIS_BASETDEST {0x6000} CONFIG.M25_AXIS_BASETDEST {0x6400} \
    CONFIG.M26_AXIS_BASETDEST {0x6800} CONFIG.M27_AXIS_BASETDEST {0x6C00} \
    CONFIG.M28_AXIS_BASETDEST {0x7000} \
    CONFIG.M00_AXIS_HIGHTDEST {0x3FF} CONFIG.M01_AXIS_HIGHTDEST {0x7FF} \
    CONFIG.M02_AXIS_HIGHTDEST {0xBFF} CONFIG.M03_AXIS_HIGHTDEST {0xFFF} \
    CONFIG.M04_AXIS_HIGHTDEST {0x13FF} CONFIG.M05_AXIS_HIGHTDEST {0x17FF} \
    CONFIG.M06_AXIS_HIGHTDEST {0x1BFF} CONFIG.M07_AXIS_HIGHTDEST {0x1FFF} \
    CONFIG.M08_AXIS_HIGHTDEST {0x23FF} CONFIG.M09_AXIS_HIGHTDEST {0x27FF} \
    CONFIG.M10_AXIS_HIGHTDEST {0x2BFF} CONFIG.M11_AXIS_HIGHTDEST {0x2FFF} \
    CONFIG.M12_AXIS_HIGHTDEST {0x33FF} CONFIG.M13_AXIS_HIGHTDEST {0x37FF} \
    CONFIG.M14_AXIS_HIGHTDEST {0x3BFF} CONFIG.M15_AXIS_HIGHTDEST {0x3FFF} \
    CONFIG.M16_AXIS_HIGHTDEST {0x43FF} CONFIG.M17_AXIS_HIGHTDEST {0x47FF} \
    CONFIG.M18_AXIS_HIGHTDEST {0x4BFF} CONFIG.M19_AXIS_HIGHTDEST {0x4FFF} \
    CONFIG.M20_AXIS_HIGHTDEST {0x53FF} CONFIG.M21_AXIS_HIGHTDEST {0x57FF} \
    CONFIG.M22_AXIS_HIGHTDEST {0x5BFF} CONFIG.M23_AXIS_HIGHTDEST {0x5FFF} \
    CONFIG.M24_AXIS_HIGHTDEST {0x63FF} CONFIG.M25_AXIS_HIGHTDEST {0x67FF} \
    CONFIG.M26_AXIS_HIGHTDEST {0x6BFF} CONFIG.M27_AXIS_HIGHTDEST {0x6FFF} \
    CONFIG.M28_AXIS_HIGHTDEST {0x73FF}] [get_ips vio_switch_ip_29]

# vio_switch_ip_33: For vio_switch_14 (14 vFIU + 14 Host + 5 network ports)
create_ip -name axis_switch -vendor xilinx.com -library ip -version 1.1 -module_name vio_switch_ip_33
set_property -dict [list CONFIG.NUM_MI {33} CONFIG.NUM_SI {33} CONFIG.HAS_TLAST {1} CONFIG.TDATA_NUM_BYTES {64} CONFIG.TDEST_WIDTH {14} CONFIG.TID_WIDTH {6} CONFIG.DECODER_REG {1}] [get_ips vio_switch_ip_33]
set_property -dict [list CONFIG.M00_AXIS_BASETDEST {0x000} CONFIG.M01_AXIS_BASETDEST {0x400} \
    CONFIG.M02_AXIS_BASETDEST {0x800} CONFIG.M03_AXIS_BASETDEST {0xC00} \
    CONFIG.M04_AXIS_BASETDEST {0x1000} CONFIG.M05_AXIS_BASETDEST {0x1400} \
    CONFIG.M06_AXIS_BASETDEST {0x1800} CONFIG.M07_AXIS_BASETDEST {0x1C00} \
    CONFIG.M08_AXIS_BASETDEST {0x2000} CONFIG.M09_AXIS_BASETDEST {0x2400} \
    CONFIG.M10_AXIS_BASETDEST {0x2800} CONFIG.M11_AXIS_BASETDEST {0x2C00} \
    CONFIG.M12_AXIS_BASETDEST {0x3000} CONFIG.M13_AXIS_BASETDEST {0x3400} \
    CONFIG.M14_AXIS_BASETDEST {0x3800} CONFIG.M15_AXIS_BASETDEST {0x3C00} \
    CONFIG.M16_AXIS_BASETDEST {0x4000} CONFIG.M17_AXIS_BASETDEST {0x4400} \
    CONFIG.M18_AXIS_BASETDEST {0x4800} CONFIG.M19_AXIS_BASETDEST {0x4C00} \
    CONFIG.M20_AXIS_BASETDEST {0x5000} CONFIG.M21_AXIS_BASETDEST {0x5400} \
    CONFIG.M22_AXIS_BASETDEST {0x5800} CONFIG.M23_AXIS_BASETDEST {0x5C00} \
    CONFIG.M24_AXIS_BASETDEST {0x6000} CONFIG.M25_AXIS_BASETDEST {0x6400} \
    CONFIG.M26_AXIS_BASETDEST {0x6800} CONFIG.M27_AXIS_BASETDEST {0x6C00} \
    CONFIG.M28_AXIS_BASETDEST {0x7000} CONFIG.M29_AXIS_BASETDEST {0x7400} \
    CONFIG.M30_AXIS_BASETDEST {0x7800} CONFIG.M31_AXIS_BASETDEST {0x7C00} \
    CONFIG.M32_AXIS_BASETDEST {0x8000} \
    CONFIG.M00_AXIS_HIGHTDEST {0x3FF} CONFIG.M01_AXIS_HIGHTDEST {0x7FF} \
    CONFIG.M02_AXIS_HIGHTDEST {0xBFF} CONFIG.M03_AXIS_HIGHTDEST {0xFFF} \
    CONFIG.M04_AXIS_HIGHTDEST {0x13FF} CONFIG.M05_AXIS_HIGHTDEST {0x17FF} \
    CONFIG.M06_AXIS_HIGHTDEST {0x1BFF} CONFIG.M07_AXIS_HIGHTDEST {0x1FFF} \
    CONFIG.M08_AXIS_HIGHTDEST {0x23FF} CONFIG.M09_AXIS_HIGHTDEST {0x27FF} \
    CONFIG.M10_AXIS_HIGHTDEST {0x2BFF} CONFIG.M11_AXIS_HIGHTDEST {0x2FFF} \
    CONFIG.M12_AXIS_HIGHTDEST {0x33FF} CONFIG.M13_AXIS_HIGHTDEST {0x37FF} \
    CONFIG.M14_AXIS_HIGHTDEST {0x3BFF} CONFIG.M15_AXIS_HIGHTDEST {0x3FFF} \
    CONFIG.M16_AXIS_HIGHTDEST {0x43FF} CONFIG.M17_AXIS_HIGHTDEST {0x47FF} \
    CONFIG.M18_AXIS_HIGHTDEST {0x4BFF} CONFIG.M19_AXIS_HIGHTDEST {0x4FFF} \
    CONFIG.M20_AXIS_HIGHTDEST {0x53FF} CONFIG.M21_AXIS_HIGHTDEST {0x57FF} \
    CONFIG.M22_AXIS_HIGHTDEST {0x5BFF} CONFIG.M23_AXIS_HIGHTDEST {0x5FFF} \
    CONFIG.M24_AXIS_HIGHTDEST {0x63FF} CONFIG.M25_AXIS_HIGHTDEST {0x67FF} \
    CONFIG.M26_AXIS_HIGHTDEST {0x6BFF} CONFIG.M27_AXIS_HIGHTDEST {0x6FFF} \
    CONFIG.M28_AXIS_HIGHTDEST {0x73FF} CONFIG.M29_AXIS_HIGHTDEST {0x77FF} \
    CONFIG.M30_AXIS_HIGHTDEST {0x7BFF} CONFIG.M31_AXIS_HIGHTDEST {0x7FFF} \
    CONFIG.M32_AXIS_HIGHTDEST {0x83FF}] [get_ips vio_switch_ip_33]

# vio_switch_ip_37: For vio_switch_16 (16 vFIU + 16 Host + 5 network ports)
create_ip -name axis_switch -vendor xilinx.com -library ip -version 1.1 -module_name vio_switch_ip_37
set_property -dict [list CONFIG.NUM_MI {37} CONFIG.NUM_SI {37} CONFIG.HAS_TLAST {1} CONFIG.TDATA_NUM_BYTES {64} CONFIG.TDEST_WIDTH {14} CONFIG.TID_WIDTH {6} CONFIG.DECODER_REG {1}] [get_ips vio_switch_ip_37]
set_property -dict [list CONFIG.M00_AXIS_BASETDEST {0x000} CONFIG.M01_AXIS_BASETDEST {0x400} \
    CONFIG.M02_AXIS_BASETDEST {0x800} CONFIG.M03_AXIS_BASETDEST {0xC00} \
    CONFIG.M04_AXIS_BASETDEST {0x1000} CONFIG.M05_AXIS_BASETDEST {0x1400} \
    CONFIG.M06_AXIS_BASETDEST {0x1800} CONFIG.M07_AXIS_BASETDEST {0x1C00} \
    CONFIG.M08_AXIS_BASETDEST {0x2000} CONFIG.M09_AXIS_BASETDEST {0x2400} \
    CONFIG.M10_AXIS_BASETDEST {0x2800} CONFIG.M11_AXIS_BASETDEST {0x2C00} \
    CONFIG.M12_AXIS_BASETDEST {0x3000} CONFIG.M13_AXIS_BASETDEST {0x3400} \
    CONFIG.M14_AXIS_BASETDEST {0x3800} CONFIG.M15_AXIS_BASETDEST {0x3C00} \
    CONFIG.M16_AXIS_BASETDEST {0x4000} CONFIG.M17_AXIS_BASETDEST {0x4400} \
    CONFIG.M18_AXIS_BASETDEST {0x4800} CONFIG.M19_AXIS_BASETDEST {0x4C00} \
    CONFIG.M20_AXIS_BASETDEST {0x5000} CONFIG.M21_AXIS_BASETDEST {0x5400} \
    CONFIG.M22_AXIS_BASETDEST {0x5800} CONFIG.M23_AXIS_BASETDEST {0x5C00} \
    CONFIG.M24_AXIS_BASETDEST {0x6000} CONFIG.M25_AXIS_BASETDEST {0x6400} \
    CONFIG.M26_AXIS_BASETDEST {0x6800} CONFIG.M27_AXIS_BASETDEST {0x6C00} \
    CONFIG.M28_AXIS_BASETDEST {0x7000} CONFIG.M29_AXIS_BASETDEST {0x7400} \
    CONFIG.M30_AXIS_BASETDEST {0x7800} CONFIG.M31_AXIS_BASETDEST {0x7C00} \
    CONFIG.M32_AXIS_BASETDEST {0x8000} CONFIG.M33_AXIS_BASETDEST {0x8400} \
    CONFIG.M34_AXIS_BASETDEST {0x8800} CONFIG.M35_AXIS_BASETDEST {0x8C00} \
    CONFIG.M36_AXIS_BASETDEST {0x9000} \
    CONFIG.M00_AXIS_HIGHTDEST {0x3FF} CONFIG.M01_AXIS_HIGHTDEST {0x7FF} \
    CONFIG.M02_AXIS_HIGHTDEST {0xBFF} CONFIG.M03_AXIS_HIGHTDEST {0xFFF} \
    CONFIG.M04_AXIS_HIGHTDEST {0x13FF} CONFIG.M05_AXIS_HIGHTDEST {0x17FF} \
    CONFIG.M06_AXIS_HIGHTDEST {0x1BFF} CONFIG.M07_AXIS_HIGHTDEST {0x1FFF} \
    CONFIG.M08_AXIS_HIGHTDEST {0x23FF} CONFIG.M09_AXIS_HIGHTDEST {0x27FF} \
    CONFIG.M10_AXIS_HIGHTDEST {0x2BFF} CONFIG.M11_AXIS_HIGHTDEST {0x2FFF} \
    CONFIG.M12_AXIS_HIGHTDEST {0x33FF} CONFIG.M13_AXIS_HIGHTDEST {0x37FF} \
    CONFIG.M14_AXIS_HIGHTDEST {0x3BFF} CONFIG.M15_AXIS_HIGHTDEST {0x3FFF} \
    CONFIG.M16_AXIS_HIGHTDEST {0x43FF} CONFIG.M17_AXIS_HIGHTDEST {0x47FF} \
    CONFIG.M18_AXIS_HIGHTDEST {0x4BFF} CONFIG.M19_AXIS_HIGHTDEST {0x4FFF} \
    CONFIG.M20_AXIS_HIGHTDEST {0x53FF} CONFIG.M21_AXIS_HIGHTDEST {0x57FF} \
    CONFIG.M22_AXIS_HIGHTDEST {0x5BFF} CONFIG.M23_AXIS_HIGHTDEST {0x5FFF} \
    CONFIG.M24_AXIS_HIGHTDEST {0x63FF} CONFIG.M25_AXIS_HIGHTDEST {0x67FF} \
    CONFIG.M26_AXIS_HIGHTDEST {0x6BFF} CONFIG.M27_AXIS_HIGHTDEST {0x6FFF} \
    CONFIG.M28_AXIS_HIGHTDEST {0x73FF} CONFIG.M29_AXIS_HIGHTDEST {0x77FF} \
    CONFIG.M30_AXIS_HIGHTDEST {0x7BFF} CONFIG.M31_AXIS_HIGHTDEST {0x7FFF} \
    CONFIG.M32_AXIS_HIGHTDEST {0x83FF} CONFIG.M33_AXIS_HIGHTDEST {0x87FF} \
    CONFIG.M34_AXIS_HIGHTDEST {0x8BFF} CONFIG.M35_AXIS_HIGHTDEST {0x8FFF} \
    CONFIG.M36_AXIS_HIGHTDEST {0x93FF}] [get_ips vio_switch_ip_37]

# ILA for vIO Switch
create_ip -name ila -vendor xilinx.com -library ip -version 6.2 -module_name ila_vio_switch_4
set_property -dict [list \
    CONFIG.C_NUM_OF_PROBES {22} \
    CONFIG.C_EN_STRG_QUAL {1} \
    CONFIG.ALL_PROBE_SAME_MU_CNT {2} \
    CONFIG.C_PROBE0_WIDTH {1} \
    CONFIG.C_PROBE1_WIDTH {1} \
    CONFIG.C_PROBE2_WIDTH {1} \
    CONFIG.C_PROBE3_WIDTH {1} \
    CONFIG.C_PROBE4_WIDTH {1} \
    CONFIG.C_PROBE5_WIDTH {1} \
    CONFIG.C_PROBE6_WIDTH {1} \
    CONFIG.C_PROBE7_WIDTH {1} \
    CONFIG.C_PROBE8_WIDTH {2} \
    CONFIG.C_PROBE9_WIDTH {2} \
    CONFIG.C_PROBE10_WIDTH {2} \
    CONFIG.C_PROBE11_WIDTH {2} \
    CONFIG.C_PROBE12_WIDTH {2} \
    CONFIG.C_PROBE13_WIDTH {2} \
    CONFIG.C_PROBE14_WIDTH {14} \
    CONFIG.C_PROBE15_WIDTH {14} \
    CONFIG.C_PROBE16_WIDTH {14} \
    CONFIG.C_PROBE17_WIDTH {14} \
    CONFIG.C_PROBE18_WIDTH {64} \
    CONFIG.C_PROBE19_WIDTH {64} \
    CONFIG.C_PROBE20_WIDTH {64} \
    CONFIG.C_PROBE21_WIDTH {64} \
] [get_ips ila_vio_switch_4]

create_ip -name ila -vendor xilinx.com -library ip -version 6.2 -module_name ila_vio_switch_16
set_property -dict [list \
    CONFIG.C_NUM_OF_PROBES {31} \
    CONFIG.C_EN_STRG_QUAL {1} \
    CONFIG.ALL_PROBE_SAME_MU_CNT {2} \
    CONFIG.C_PROBE3_WIDTH {64} \
    CONFIG.C_PROBE4_WIDTH {14} \
    CONFIG.C_PROBE8_WIDTH {64} \
    CONFIG.C_PROBE9_WIDTH {14} \
    CONFIG.C_PROBE10_WIDTH {16} \
    CONFIG.C_PROBE11_WIDTH {16} \
    CONFIG.C_PROBE12_WIDTH {16} \
    CONFIG.C_PROBE13_WIDTH {16} \
    CONFIG.C_PROBE14_WIDTH {16} \
    CONFIG.C_PROBE15_WIDTH {16} \
    CONFIG.C_PROBE16_WIDTH {16} \
    CONFIG.C_PROBE17_WIDTH {16} \
    CONFIG.C_PROBE18_WIDTH {32} \
    CONFIG.C_PROBE19_WIDTH {32} \
    CONFIG.C_PROBE20_WIDTH {32} \
    CONFIG.C_PROBE21_WIDTH {32} \
    CONFIG.C_PROBE22_WIDTH {32} \
    CONFIG.C_PROBE23_WIDTH {32} \
    CONFIG.C_PROBE24_WIDTH {32} \
    CONFIG.C_PROBE25_WIDTH {32} \
    CONFIG.C_PROBE26_WIDTH {32} \
    CONFIG.C_PROBE27_WIDTH {32} \
    CONFIG.C_PROBE28_WIDTH {32} \
    CONFIG.C_PROBE29_WIDTH {32} \
    CONFIG.C_PROBE30_WIDTH {32} \
] [get_ips ila_vio_switch_16]
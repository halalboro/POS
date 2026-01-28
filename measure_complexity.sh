# Host code
echo "POS Host application Complexity Measurement"
###############################################################

echo "13_basic"
scc  examples/13_basic/sw/src

echo "14_calc"
scc  examples/14_calc/sw/src

echo "15_ecn"
scc  examples/15_ecn/sw/src

echo "16_firewall"
scc  examples/16_firewall/sw/src

echo "17_linkmonitor"
scc  examples/17_linkmonitor/sw/src

echo "18_loadbalance"
scc  examples/18_loadbalance/sw/src

echo "19_mri"
scc  examples/19_mri/sw/src

echo "20_multicast"
scc  examples/20_multicast/sw/src

echo "21_qos"
scc  examples/21_qos/sw/src

echo "22_sourcerouting"
scc  examples/22_sourcerouting/sw/src

echo "23_tunnel"
scc  examples/23_tunnel/sw/src

###############################################################

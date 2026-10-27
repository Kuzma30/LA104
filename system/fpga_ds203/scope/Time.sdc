#Clock
create_clock [get_ports {MCI} ] -name {MCI} -period 13 
create_clock [get_ports {nRD} ] -name {nRD} -period 100 
create_clock [get_ports {SCK} ] -name {SCK} -period 100 
create_clock [get_ports {SDA} ] -name {SDA} -period 100 
#Generated Clock
#Input Delay
#Output Delay
#Max-delay
#False Path
#Multi-cycle
#LA104: edge counters are clocked by the digital inputs CH_C and CH_D
create_clock [get_ports {Din[16]} ] -name {CH_C} -period 13 
create_clock [get_ports {Din[17]} ] -name {CH_D} -period 13 

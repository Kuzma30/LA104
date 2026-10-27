# 72 MHz from PA8 (MCO = SYSCLK)
create_clock -period 13.89 -name {clk} [get_ports {clk}]

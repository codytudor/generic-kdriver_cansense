# CAN bus termination hardware monitor

This is a generic driver that utilizes a GPIO for every CAN bus on a board. Currently
only two CAN busses are supported (can0 & can1). This driver requires specific hardware
to actually determine the state of the given CAN bus's termination status. The driver
was written such that a GPIO HIGH value would indicate that the CAN bus termination 
resistor(s) on this board are connected to the nets CAN_HIGH && CAN_LOW. Two sysfs
entries for each CAN bus are given: a canx_status will give a human readable response
and canx_value will give a strig representation of the returned value from gpio_get_value(); 

Each GPIO must be defined in the board's device tree and identified as can[1..0]. 

An example device tree entry would be:

    board_name {
        compatible = "can-hwmon";
        gpios = <&gpio7 11 GPIO_ACTIVE_HIGH>, <&gpio1 3 GPIO_ACTIVE_HIGH>;
        gpio-names = "can0", "can1";
    }; 

* @board_name: this is the actual dev name that will be given to this sysfs entry in the hwmon class
* @gpios: you must define triple's as shown above. The ACTIVE_HIGH/ACTIVE_LOW is ignored
* @gpio-names: a string list as shown above that must match the order of the gpios property


In the example above our board will register with the hwmon class in sysfs and be given 
a dev name of 'board_name'. The termination resistor for can0 is connected to nets 
CAN0_HI && CAN0_LO if the value of gpio203 is 1. If the value is 0 then the resistor is
not in circuit. Similarly, can1 termination resistor can be monitored on gpio3.

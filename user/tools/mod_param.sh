#!/bin/bash

DA_DEBUG_ALERT_FLAG=0x00000001
DA_DEBUG_WARNING_FLAG=0x00000002
DA_DEBUG_ERROR_FLAG=0x00000004
DA_DEBUG_INFO_FLAG=0x00000008
DA_DEBUG_ENTRYEXIT_FLAG=0x00000010
DA_DEBUG_DEBUG=0x00000020

get_params() {
	local_start=`cat /sys/module/da_kmodule/parameters/local_start`
	local_end=`cat /sys/module/da_kmodule/parameters/local_end`
	bandwidth_bps=`cat /sys/module/da_kmodule/parameters/bandwidth_bps`
	latency_ns=`cat /sys/module/da_kmodule/parameters/latency_ns`
	da_debug_flag=`cat /sys/module/da_kmodule/parameters/da_debug_flag`
}

update_params() {
	echo $local_start > /sys/module/da_kmodule/parameters/local_start
	echo $local_end > /sys/module/da_kmodule/parameters/local_end
	echo $bandwidth_bps > /sys/module/da_kmodule/parameters/bandwidth_bps
	echo $latency_ns > /sys/module/da_kmodule/parameters/latency_ns
	echo $da_debug_flag > /sys/module/da_kmodule/parameters/da_debug_flag
}

print_params() {
	echo -e "    Local address start      : $local_start"
	echo -e "    Local address end        : $local_end"
	echo -e "    Bandwidth (bits-per-sec) : $bandwidth_bps"
	echo -e "    Latency (nano-sec)       : $latency_ns"
	printf  "    Debug flags              : %s\n" $(printf "%8s" $(echo "obase=2;$da_debug_flag" | bc) | tr ' ' '0')
}


echo "Getting params from module.."
get_params
echo "Initial param values :"
print_params

if [ "$1" == "local_start" ]
then
	local_start=$2
elif [ "$1" == "local_end" ]
then
	local_end=$2
elif [ "$1" == "bandwidth_bps" ]
then
	bandwidth_bps=$2
elif [ "$1" == "latency_ns" ]
then
	latency_ns=$2
elif [ "$1" == "set-debug" ]
then
	da_debug_flag=$((da_debug_flag | DA_DEBUG_DEBUG))
fi

echo "Updating new param values.."
update_params
echo "Getting params from module to verify.."
get_params
echo "Updated param values :"
print_params
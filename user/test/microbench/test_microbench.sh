#!/bin/bash

# Change pwd to script path
SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"


# Module parameters :
pid=100
latency_ns=2500
local_npages=1000
bandwidth_bps=100000000000000000


function resetup {
	echo never > /sys/kernel/mm/transparent_hugepage/enabled

	if [ `lsmod | grep kmodule | wc -l` -gt 0 ] 
	then 
	    echo "Removing module.."
	    rmmod kmodule || exit 1
	fi

	echo "Inserting module.."
	insmod $SCRIPT_PATH/../kernel/kmodule.ko pid=$pid latency_ns=$latency_ns local_npages=$local_npages bandwidth_bps=$bandwidth_bps printk.synchronous=1
#	dmesg -c > /dev/null ##########################################
}

function run_test {
	$SCRIPT_PATH/test_microbench 3000 &
	pid=$!
	echo "PID : $pid"

	echo "[NOTE] Press enter to send the test program a signal to start testing..."
	#read r
	sleep 3

	if [ "$pid" == "" ]
	then
		echo "Could not find test program"
		exit 1
	fi

	cat /proc/$pid/maps

	resetup

	#sudo pkill -USR1 test_microbench
	kill -USR1 $pid

	wait $pid

	expected_delay_per_fault_ms=$(bc -l <<< "scale=2; 2 * $latency_ns / 1000 + 4096 * 8 * 1000000 / $bandwidth_bps")
	echo "Expected delay per page fault     : $expected_delay_per_fault_ms ms"
}


for test_case in {1..3}
do
	for i in {0..100}
	do
		latency_ns=$((i*100))
		run_test 2>&1 > test_${test_case}_user_loop_latency_${latency_ns}.log
		cat /proc/pf_list > test_${test_case}_kernel_loop_latency_${latency_ns}.log
	done
done


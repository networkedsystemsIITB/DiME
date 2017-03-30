#!/bin/bash

# Module parameters :
pid=100
latency_ns=20000
local_npages=10
bandwidth_bps=10000000000000000

echo never > /sys/kernel/mm/transparent_hugepage/enabled

if [ "$#" -lt "1" ]
then
	echo "Number of pages required"
	exit 1
fi

./test_prog $1 &

echo "[NOTE] Press enter to send the test program a signal to start testing..."
read r

pid=$!
#pid=$(ps | grep test_prog | awk '{print $1;}')
if [ "$pid" == "" ]
then
	echo "Could not find test program"
	exit 1
fi

if [ `lsmod | grep kmodule | wc -l` -gt 0 ] 
then 
	echo "Removing module.."
	rmmod kmodule
fi

echo "Inserting module.."
expected_delay_per_fault_ms=$(bc -l <<< "scale=2; 2 * $latency_ns / 1000 + 4096 * 8 * 1000000 / $bandwidth_bps")
echo "Expected delay per page fault : $expected_delay_per_fault_ms ms"
insmod ../../kernel/kmodule.ko pid=$pid latency_ns=$latency_ns local_npages=$local_npages bandwidth_bps=$bandwidth_bps

sudo pkill -USR1 test_prog

wait $pid


#!/bin/bash

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
insmod ../../kernel/kmodule.ko pid=$pid latency_ns=20000 local_npages=100 bandwidth_bps=1000000000

sudo pkill -USR1 test_prog

wait $pid


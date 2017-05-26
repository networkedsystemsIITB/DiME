#!/bin/bash

# Change pwd to script path
SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

if [ `lsmod | grep kmodule | wc -l` -gt 0 ] 
then 
    echo "Removing module.."
    rmmod kmodule || exit 1
fi

# Module parameters :
pid=100
latency_ns=10000
local_npages=1000
bandwidth_bps=10000000000000000

echo never > /sys/kernel/mm/transparent_hugepage/enabled

if [ "$#" -lt "1" ]
then
    echo "Please provide command as '<command>'"
    exit 1
fi


start_time_ns=`date +%s%N`

# start executing the command in background
eval $1 &

pid=$!
#ps aux | grep test_prog 
pstree -ps `ps aux | grep "test_prog" | grep -v "/bin/bash" | awk '{print $2;}' | head -n1`
#pid=$(ps | grep test_prog | awk '{print $1;}')
if [ "$pid" == "" ]
then
    echo "Could not find test program"
    exit 1
fi

echo "Inserting module.."
insmod $SCRIPT_PATH/../kernel/kmodule.ko pid=$pid latency_ns=$latency_ns local_npages=$local_npages bandwidth_bps=$bandwidth_bps

wait $pid

end_time_ns=`date +%s%N`
diff_time_ns=$(bc -l <<< "scale=2; $end_time_ns - $start_time_ns")
diff_time_ms=$(bc -l <<< "scale=2; $diff_time_ns / 1000")
page_fault_count=`cat /sys/module/kmodule/parameters/page_fault_count`
if [ $page_fault_count -eq 0 ]
then
    page_fault_count=1
fi
actual_delay_per_fault_ms=$(bc -l <<< "scale=2; $diff_time_ns / 1000 / $page_fault_count")
expected_delay_per_fault_ms=$(bc -l <<< "scale=2; 2 * $latency_ns / 1000 + 4096 * 8 * 1000000 / $bandwidth_bps")

#echo "Total execution time(ns)         : $diff_time_ns ns"
echo -e "\n\n\nCommand execution completed, statistics :"
echo "Total execution time              : $diff_time_ms ms"
echo "Number of page faults             : $page_fault_count"
echo "Actual delay per page fault       : $actual_delay_per_fault_ms ms"
echo "Expected delay per page fault     : $expected_delay_per_fault_ms ms"

#!/bin/bash

function remove_dime_module {
	if [ `lsmod | grep kmodule | wc -l` -gt 0 ] 
	then 
	    echo "[SH]:	Removing DiME module.."
	    rmmod kmodule || exit 1
	fi
}

function remove_fifo_policy_module {
	if [ `lsmod | grep prp_fifo_module | wc -l` -gt 0 ] 
	then 
	    echo "[SH]:	Removing FIFO policy module.."
	    rmmod prp_fifo_module || exit 1
	fi
}

function initialize {
	echo never > /sys/kernel/mm/transparent_hugepage/enabled

	remove_fifo_policy_module
	remove_dime_module
}

# Module parameters :
pid=100
if [ "$latency_ns" == "" ]; then
	latency_ns=10000
fi
if [ "$local_npages" == "" ]; then
	local_npages=1000
fi
if [ "$bandwidth_bps" == "" ]; then
	bandwidth_bps=10000000000000000
fi

# Change pwd to script path
SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

if [ "$#" -lt "1" ]
then
	echo "[SH]:	Number of pages required"
	exit 1
fi

for testcase in {2..2}; ############################################################# test case 1..2
do
	for last_npages in 0 10 20 30 40 50 60 70 80 90 100;
	do
		# Remove DiME module
		echo "[SH]:	Initialing modules..."
		initialize

		$SCRIPT_PATH/test_prog $1 $testcase $last_npages &
		pid1=$!
		$SCRIPT_PATH/test_prog $1 $testcase $last_npages &
		pid2=$!
		if [ "$pid1" == "" -o "$pid2" == ""]
		then
		    echo "[SH]:	Could not find test program"
		    exit 1
		fi
		pstree -ps `ps aux | grep "test_prog" | grep -v "/bin/bash" | awk '{print $2;}' | head -n1`
		pstree -ps `ps aux | grep "test_prog" | grep -v "/bin/bash" | awk '{print $2;}' | head -n2 | tail -n1`

		#echo "[SH]:	PID of background process : $pid"

		# Insert DiME module with this PID
		echo "[SH]:	Inserting DiME module.."
		insmod $SCRIPT_PATH/../../kernel/kmodule.ko
		echo "instance_id=0 pid=$pid1 latency_ns=$latency_ns local_npages=$local_npages bandwidth_bps=$bandwidth_bps" > /proc/dime_config
		echo "instance_id=1 pid=$pid2 latency_ns=$latency_ns local_npages=$local_npages bandwidth_bps=$bandwidth_bps" > /proc/dime_config
		echo "[SH]:	Inserting FIFO policy module.."
		insmod $SCRIPT_PATH/../../kernel/prp_fifo_module.ko
		echo "[SH]:	FIFO policy module inserted, ready to send signal.."
		#cat /proc/$pid/maps
		read -r

		# Start testcase
		start_time_ns=`date +%s%N`
		sudo pkill -USR1 test_prog
		wait $pid1
		wait $pid2
		end_time_ns=`date +%s%N`

		# Calculate execution time
#		diff_time_ns=$(bc -l <<< "scale=2; $end_time_ns - $start_time_ns")
#		diff_time_ms=$(bc -l <<< "scale=2; $diff_time_ns / 1000")
#		page_fault_count=`cat /proc/dime_config | grep "$pid," | awk '{print $5}'`
#		if [ $page_fault_count -eq 0 ]
#		then
#		    page_fault_count=1
#		fi
#		actual_delay_per_fault_ms=$(bc -l <<< "scale=2; $diff_time_ns / 1000 / $page_fault_count")
#		expected_delay_per_fault_ms=$(bc -l <<< "scale=2; 2 * $latency_ns / 1000 + 4096 * 8 * 1000000 / $bandwidth_bps")

#		echo "Total execution time(ns)         : $diff_time_ns ns"
#		echo -e "\n\n\nCommand execution completed, statistics :"
#		echo "Total execution time              : $diff_time_ms ms"
#		echo "Number of page faults             : $page_fault_count"
#		echo "Actual delay per page fault       : $actual_delay_per_fault_ms ms"
#		echo "Expected delay per page fault     : $expected_delay_per_fault_ms ms"

		# Print dime config stats
		echo "[SH]:	DiME config stats:"
		cat /proc/dime_config
	done;
done;


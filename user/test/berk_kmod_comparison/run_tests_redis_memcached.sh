#!/bin/bash

function reboot_server {
	#ssh root@$server_ip "reboot" #########################################################################
	sleep 2
	while ! ssh root@$server_ip "echo hello > /dev/null"
	do
		echo "waiting for server to reboot"
		sleep 10
		while ! ssh root@$server_ip "echo hello > /dev/null"
		do
			echo "waiting for server to reboot"
			sleep 10
		done
	done
	ssh root@$server_ip "dmesg -c" > /dev/null
}

function resetup_everything {
	reboot_server

	ssh root@$server_ip "
		service redis stop;
		service memcached stop;
		service memcached_server1 stop;
		service memcached_server2 stop;
		service redis1 stop;
		service redis2 stop;
		echo never > /sys/kernel/mm/transparent_hugepage/enabled;
		sync;
		echo 3 > /proc/sys/vm/drop_caches;
		sleep 2;
		service redis1 start;
		service redis2 start;
		service memcached_server1 start;
		service memcached_server2 start || exit 1
	" || exit 1
	kmod_remove_module
	berk_remove_module
	sleep 2
}

function kmod_remove_module {
		ssh root@$server_ip '
			for mod in $(lsmod | sed "s/[ \t]\+/\t/g" | cut -f1 | grep prp_); do
				echo "[SH]:	Removing $mod policy module.."
				rmmod $mod || exit 1
			done

			if [ `lsmod | grep kmodule | wc -l` -gt 0 ];
			then
				echo "Removing module..";
				rmmod kmodule || exit 1;
			fi;

		' || exit 1
}

function kmod_insert_module {
	kmod_remove_module

	if [ $process1 = redis ]; then
		instance1_pid=`ssh root@$server_ip "ps aux | grep redis | grep $redis_instance1_port | head -n1 | sed 's/[ \t]\+/\t/g' | cut -f 2"`
	elif [ $process1 = memcached ]; then
		instance1_pid=`ssh root@$server_ip "ps aux | grep memcached | grep $memcached_instance1_port | head -n1 | sed 's/[ \t]\+/\t/g' | cut -f 2"`
	fi
	if [ $process2 = redis ]; then
		instance2_pid=`ssh root@$server_ip "ps aux | grep redis | grep $redis_instance2_port | head -n1 | sed 's/[ \t]\+/\t/g' | cut -f 2"`
	elif [ $process2 = memcached ]; then
		instance2_pid=`ssh root@$server_ip "ps aux | grep memcached | grep $memcached_instance2_port | head -n1 | sed 's/[ \t]\+/\t/g' | cut -f 2"`
	fi
	
	if [ $test_mode = single -o $test_mode = multisingle ]; then
		pid=$instance1_pid
	elif [ $test_mode = shared ]; then
		pid=$instance1_pid
		pid+=","
		pid+=$instance2_pid
	elif [ $test_mode = separate ]; then
		pid=$instance1_pid
		pid1=$instance2_pid
	fi &&

	if [ "$enable_module" == "kmod_fifo" ]; then
		kmod_prp_path_on_server=$kmod_prp_fifo_path_on_server
	elif [ "$enable_module" == "kmod_lru" ]; then
		kmod_prp_path_on_server=$kmod_prp_lru_path_on_server
	fi

	ssh root@$server_ip "
		echo \"Inserting module with pid=$pid local_npages=$kmod_local_npages latency_ns=$kmod_latency_ns bandwidth_bps=$kmod_bandwidth_bps\";
		insmod $kmod_path_on_server  &&
		echo  'instance_id=0 pid=$pid local_npages=$kmod_local_npages latency_ns=$kmod_latency_ns bandwidth_bps=$kmod_bandwidth_bps' > /proc/dime_config &&
		if [ \"$test_mode\" == \"separate\" ]; then
			echo  'instance_id=1 pid=$pid1 local_npages=$kmod_local_npages latency_ns=$kmod_latency_ns bandwidth_bps=$kmod_bandwidth_bps' > /proc/dime_config; 
		fi &&
		insmod $kmod_prp_path_on_server || exit 2;
	" || exit 1
}


function berk_remove_module {
	ssh root@$server_ip '
		if [ `lsmod | grep rmem | wc -l` -gt 0 ];
		then
			echo "Removing module..";
			swapon /dev/dm-1 &&
			cd /home/server2/rmem &&
			./exit_rmem.sh || exit 1;
		fi;
	' || exit 1
}

function berk_insert_module {
	berk_remove_module

	ssh root@$server_ip "
		echo \"Inserting module with localmem=$berk_remote_memory_gb latency_ns=$berk_latency_us bandwidth_bps=$berk_bandwith_gbps\";
		cd /home/server2/rmem;
		./init_rmem.sh $berk_remote_memory_gb $berk_inject_latency $berk_bandwith_gbps $berk_latency_us;
		swapoff /dev/dm-1;
	" || exit 1
}


# Params:
#	$1 = redis instance id
#
function redis_load {
	if [ "$1" == "1" ]; then
		redis_port=$redis_instance1_port
		redis_workload_config=$redis_short_workload_config
	elif [ "$1" == "2" ]; then
		redis_port=$redis_instance2_port
		redis_workload_config=$redis_short_workload_config
	fi

	pushd $ycsb_home > /dev/null
		./bin/ycsb load redis -s \
			-P $redis_workload_config \
			-p "redis.host=$server_ip" \
			-p "redis.port=$redis_port" \
			-threads 10
	popd > /dev/null
}

# Params:
#	$1 = redis instance id
#
function memcached_load {
	if [ "$1" == "1" ]; then
		server_port=$memcached_instance1_port
		workload_config=$redis_short_workload_config
	elif [ "$1" == "2" ]; then
		server_port=$memcached_instance2_port
		workload_config=$redis_short_workload_config
	fi

	pushd $ycsb_home > /dev/null
		./bin/ycsb load memcached -s \
			-P $workload_config \
			-p "memcached.hosts=${server_ip}:${server_port}" \
			-threads 10
	popd > /dev/null
}

# Params:
#	$1 = redis instance id
#
function redis_run {
	if [ "$1" == "1" ]; then
		redis_port=$redis_instance1_port
		redis_workload_config=$redis_short_workload_config
	elif [ "$1" == "2" ]; then
		redis_port=$redis_instance2_port
		redis_workload_config=$redis_short_workload_config
	fi

	pushd $ycsb_home > /dev/null
		./bin/ycsb run redis -s \
			-P $redis_workload_config \
			-p "redis.host=$server_ip" \
			-p "redis.port=$redis_port" \
			-threads 10
	popd > /dev/null
}

# Params:
#	$1 = memcached instance id
#
function memcached_run {
	if [ "$1" == "1" ]; then
		server_port=$memcached_instance1_port
		workload_config=$redis_short_workload_config
	elif [ "$1" == "2" ]; then
		server_port=$memcached_instance2_port
		workload_config=$redis_short_workload_config
	fi

	pushd $ycsb_home > /dev/null
		./bin/ycsb run memcached -s \
			-P $workload_config \
			-p "memcached.hosts=${server_ip}:${server_port}" \
			-threads 10
	popd > /dev/null
}

function kmod_get_module_stats {
	ssh $server_ip "cat /proc/dime_config"
}


# Params:
#	$1 = process 1 or 2
#
function load_process {
	proc=$1
	if [ $proc = 1 ]; then
		if [ $process1 = redis ]; then
			redis_load ${proc} | tee ${testfile_prefix}-instance-${proc}-load.log
		else
			memcached_load ${proc} | tee ${testfile_prefix}-instance-${proc}-load.log
		fi
	elif [ $proc = 2 ]; then
		if [ $process2 = redis ]; then
			redis_load ${proc} | tee ${testfile_prefix}-instance-${proc}-load.log
		else
			memcached_load ${proc} | tee ${testfile_prefix}-instance-${proc}-load.log
		fi
	fi
}

# Params:
#	$1 = process 1 or 2
#
function run_process {
	proc=$1
	if [ $proc = 1 ]; then
		if [ $process1 = redis ]; then
			redis_run ${proc} | tee ${testfile_prefix}-instance-${proc}-run.log
		else
			memcached_run ${proc} | tee ${testfile_prefix}-instance-${proc}-run.log
		fi
	elif [ $proc = 2 ]; then
		if [ $process2 = redis ]; then
			redis_run ${proc} | tee ${testfile_prefix}-instance-${proc}-run.log
		else
			memcached_run ${proc} | tee ${testfile_prefix}-instance-${proc}-run.log
		fi
	fi
}

function run_processes {
	pushd $ycsb_home > /dev/null
		# load data to both redis and memcached
		if [ ! $test_mode = single ]
		then
			load_process 2 &
			load_process 1
		else
			load_process 1
		fi

		wait	# wait for loading

		kmod_get_module_stats >> ${testfile_prefix}-instance-1-load.log

		if [ ! $test_mode = single ]
		then
			run_process 2 &
			run_process 1
			# kill other instance client
			#kill $(ps aux | grep 'ycsb' | grep -v "grep" | awk '{print $2}')
		else
			run_process 1
		fi

		wait

		kmod_get_module_stats >> ${testfile_prefix}-instance-1-run.log

		ssh root@$server_ip "dmesg -c" > ${testfile_prefix}-dmesg.log
	popd > /dev/null
}

function run_test {

	resetup_everything

	kmod_insert_module	# insert kmod to count pagefaults

	if [ "$enable_module" == "no" ]; then
		berk_remove_module
		testname="test-${testcase}-insert_module-${enable_module}-test_mode-${test_mode}-process1-${process1}"
		testfile_prefix=$curdir/$testname
	elif [ "$enable_module" == "kmod_lru" ]; then
		percent_local_mem=$(echo "4 * $kmod_local_npages * 100 / 1000000" | bc);
		if [ $test_mode = shared ]; then
			percent_local_mem=$(($percent_local_mem / 2))
		fi
		berk_remove_module
		testname="test-${testcase}-kmod-insert_module-${enable_module}-test_mode-${test_mode}-process1-${process1}-process2-${process2}-percent_local_mem-${percent_local_mem}-local_npages-${kmod_local_npages}-latency-${kmod_latency_ns}-bandwidth-${kmod_bandwidth_bps}"
		testfile_prefix=$curdir/$testname
	elif [ "$enable_module" == "kmod_fifo" ]; then
		percent_local_mem=$(echo "4 * $kmod_local_npages * 100 / 1000000" | bc);
		if [ $test_mode = shared ]; then
			percent_local_mem=$(($percent_local_mem / 2))
		fi
		berk_remove_module
		testname="test-${testcase}-kmod-insert_module-${enable_module}-test_mode-${test_mode}-process1-${process1}-process2-${process2}-percent_local_mem-${percent_local_mem}-local_npages-${kmod_local_npages}-latency-${kmod_latency_ns}-bandwidth-${kmod_bandwidth_bps}"
		testfile_prefix=$curdir/$testname
	elif [ "$enable_module" == "berk" ]; then
		percent_local_mem=$(echo "(7.6 - $berk_remote_memory_gb)*100" | bc | cut -d. -f1);
		if [ $test_mode = shared ]; then
			percent_local_mem=$(($percent_local_mem / 2))
		fi
		berk_insert_module
		testname="test-${testcase}-berk-insert_module-${enable_module}-test_mode-${test_mode}-process1-${process1}-process2-${process2}-percent_local_mem-${percent_local_mem}-remote_mem-${berk_remote_memory_gb}-latency-${berk_latency_us}-bandwidth-${berk_bandwith_gbps}"
		testfile_prefix=$curdir/$testname
	fi

	run_processes
}





# Initialize variables
curdir=`pwd -P`
pushd `dirname $0` > /dev/null
scriptdir=`pwd -P`
popd > /dev/null



# Config
server_ip=10.129.2.141
ycsb_home=/root/ycsb
kmod_path_on_server="/root/DiME/kernel/kmodule.ko"
kmod_prp_fifo_path_on_server="/root/DiME/kernel/prp_fifo_module.ko"
kmod_prp_lru_path_on_server="/root/DiME/kernel/prp_lru_module.ko"
redis_short_workload_config="${ycsb_home}/workloads/workloada_r"
redis_long_workload_config="${ycsb_home}/workloads/workloada_r"
#kmod_process_in_module="redis1"	# shared/separate/redis1/redis2
redis_instance1_port=6381
redis_instance2_port=6382
memcached_instance1_port=11211
memcached_instance2_port=11212
process1="memcached"
process2="memcached"
test_mode="single"	# shared/separate/single/multisingle
enable_module="no"	# kmod_lru kmod_fifo


# 7.6G available memory
function run_single_test {

	# kmodule parameterss
	kmod_latency_ns=2500
	kmod_bandwidth_bps=100000000000
	kmod_local_npages=1000000000
	for enable_module in "kmod_lru" "kmod_fifo";
	do
		for kmod_latency_ns in 2500 5000 7500 10000 12500 15000 20000 40000 60000 100000 150000 200000; # 10 20 30 40 50 60
		do
			for kmod_local_npages in 25000 50000 75000 100000 125000 150000; # 10 20 30 40 50 60
			do
				temp_local_npages=$kmod_local_npages;
				for test_mode in "single" ; #"separate" "shared"; ###############################################"separate" "shared" "multisingle";
				do
					if [ "$test_mode" == "shared" ];
					then
						kmod_local_npages=$((kmod_local_npages*2));
					else
						kmod_local_npages=$temp_local_npages;
					fi
					run_test
				done
			done
		done
	done
	


	# berk module parameters
#	berk_remote_memory_gb=1000
#	berk_bandwith_gbps=100
#	berk_latency_us=5
#	berk_inject_latency=1
#	enable_module="berk"
#	test_mode="single"
#	kmod_local_npages=0					# to make kmod as a pagefault counter
#	kmod_latency_ns=0					# to make kmod as a pagefault counter
#	kmod_bandwidth_bps=10000000000000	# to make kmod as a pagefault counter
#	for berk_remote_memory_gb in 7.5 7.4 7.3 7.2 7.1 7; # 10 20 30 40 50 60
#	do
#		echo "doing nothing"
#		run_test
#	done

	# berk module parameters
#	berk_remote_memory_gb=1000
#	berk_bandwith_gbps=100
#	berk_latency_us=5
#	berk_inject_latency=1
#	enable_module="berk"
#	test_mode="shared"
#	kmod_local_npages=0					# to make kmod as a pagefault counter
#	kmod_latency_ns=0					# to make kmod as a pagefault counter
#	kmod_bandwidth_bps=10000000000000	# to make kmod as a pagefault counter
#	for berk_remote_memory_gb in 7.4 7.2 7 6.8 6.6 6.4; # 10 20 30 40 50 60
#	do
#		echo "doing nothing"
#		run_test
#	done

}


# run tests:
for testcase in {1..5};
do
	#exec 3>"$HOSTDIR/$HOST.comb" 2> >(tee "$HOSTDIR/$HOST.err" >&3) 1> >(tee "$HOSTDIR/$HOST.out" >&3);
	run_single_test #3>"test-${testcase}.comb" 2> >(tee "test-${testcase}.err" >&3) 1> >(tee "test-${testcase}.out" >&3);
done;





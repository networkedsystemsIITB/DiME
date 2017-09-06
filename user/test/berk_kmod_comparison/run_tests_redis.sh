#!/bin/bash

function reboot_server {
	ssh root@$server_ip "reboot"
	sleep 2
	while ! ssh root@$server_ip "echo hello > /dev/null"
	do
		sleep 10
		while ! ssh root@$server_ip "echo hello > /dev/null"
		do
			sleep 10
		done
	done
	ssh root@$server_ip "dmesg -c" > /dev/null
}

function resetup_everything {
	reboot_server

	ssh root@$server_ip "
		service redis stop;
		echo never > /sys/kernel/mm/transparent_hugepage/enabled &&
		sync &&
		echo 3 > /proc/sys/vm/drop_caches &&
		service redis1 stop &&
		sleep 2 &&
		service redis1 start &&
		service redis2 stop &&
		sleep 2 &&
		service redis2 start || exit 1
	" || exit 1
	kmod_remove_module
	berk_remove_module
	sleep 2
}

function kmod_remove_module {
		ssh root@$server_ip '
			if [ `lsmod | grep prp_fifo_module | wc -l` -gt 0 ];
			then
				echo "Removing page replacement policy module..";
				rmmod prp_fifo_module || exit 1;
			fi;
			if [ `lsmod | grep kmodule | wc -l` -gt 0 ];
			then
				echo "Removing module..";
				rmmod kmodule || exit 1;
			fi;
		' || exit 1
}

function kmod_insert_module {
	kmod_remove_module

	instance1_pid=`ssh root@$server_ip "ps aux | grep redis | grep $redis_instance1_port | head -n1 | sed 's/[ \t]\+/\t/g' | cut -f 2"`
	instance2_pid=`ssh root@$server_ip "ps aux | grep redis | grep $redis_instance2_port | head -n1 | sed 's/[ \t]\+/\t/g' | cut -f 2"`
	
	if [ "$kmod_process_in_module" == "redis1" ]; then
		pid=$instance1_pid
	elif [ "$kmod_process_in_module" == "redis2" ]; then
		pid=$instance2_pid
	elif [ "$kmod_process_in_module" == "shared" ]; then
		pid=$instance1_pid
		pid+=","
		pid+=$instance2_pid
	elif [ "$kmod_process_in_module" == "separate" ]; then
		pid=$instance1_pid
		pid1=$instance2_pid
	fi &&
	ssh root@$server_ip "
		echo \"Inserting module with pid=$pid local_npages=$kmod_local_npages latency_ns=$kmod_latency_ns bandwidth_bps=$kmod_bandwidth_bps\";
		insmod $kmod_path_on_server  &&
		echo  'instance_id=0 pid=$pid local_npages=$kmod_local_npages latency_ns=$kmod_latency_ns bandwidth_bps=$kmod_bandwidth_bps' > /proc/dime_config &&
		if [ \"$kmod_process_in_module\" == \"separate\" ]; then 
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
		redis_workload_config=$redis_long_workload_config
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
function redis_run {
	if [ "$1" == "1" ]; then
		redis_port=$redis_instance1_port
		redis_workload_config=$redis_short_workload_config
	elif [ "$1" == "2" ]; then
		redis_port=$redis_instance2_port
		redis_workload_config=$redis_long_workload_config
	fi

	pushd $ycsb_home > /dev/null
		./bin/ycsb run redis -s \
			-P $redis_workload_config \
			-p "redis.host=$server_ip" \
			-p "redis.port=$redis_port" \
			-threads 10
	popd > /dev/null
}

function run_processes {
	pushd $ycsb_home
		# load data to both redis and memcached
		if [ "$execute_multiple" == "yes" ]
		then
			redis_load 2 | tee ${testfile_prefix}-redis-instance-2-load.log &
			redis_load 1 | tee ${testfile_prefix}-redis-instance-1-load.log
		else
			redis_load 1 | tee ${testfile_prefix}-redis-instance-1-load.log
		fi

		wait	# wait for loading

		if [ "$enable_module" == "kmod" ]; then
			pagefaults=$(ssh $server_ip "cat /proc/dime_config | head -n2 | tail -n1 | sed 's/[ \t\r\n]\+/ /g' | cut -d' ' -f6");
			echo "[OVERALL], pagefault_count, $pagefaults" >>  ${testfile_prefix}-redis-instance-1-load.log
		fi

		if [ "$execute_multiple" == "yes" ]
		then
			redis_run 2 | tee ${testfile_prefix}-redis-instance-2-run.log &
			redis_run 1 | tee ${testfile_prefix}-redis-instance-1-run.log
			# kill other instance client
			kill $(ps aux | grep 'ycsb' | grep -v "grep" | awk '{print $2}')
		else
			redis_run 1 | tee ${testfile_prefix}-redis-instance-1-run.log
		fi

		if [ "$enable_module" == "kmod" ]; then
			pagefaults_new=$(ssh $server_ip "cat /proc/dime_config | head -n2 | tail -n1 | sed 's/[ \t\r\n]\+/ /g' | cut -d' ' -f6");
			pagefaults_new=$((pagefaults_new-pagefaults));
			echo "[OVERALL], pagefault_count, $pagefaults_new" >>  ${testfile_prefix}-redis-instance-1-run.log
		fi

		ssh root@$server_ip "dmesg -c" > ${testfile_prefix}-dmesg.log
	popd
}

function run_test {

	resetup_everything
	
	if [ "$enable_module" == "no" ]; then
		kmod_remove_module
		berk_remove_module
		testname="test-${testcase}-insert_module-${enable_module}-execute_multiple-${execute_multiple}"
		testfile_prefix=$curdir/$testname
	elif [ "$enable_module" == "kmod" ]; then
		berk_remove_module
		kmod_insert_module
		testname="test-${testcase}-kmod-insert_module-${enable_module}-execute_multiple-${execute_multiple}-local_npages-${kmod_local_npages}-latency-${kmod_latency_ns}-bandwidth-${kmod_bandwidth_bps}-kmod_process_in_module-${kmod_process_in_module}"
		testfile_prefix=$curdir/$testname
	elif [ "$enable_module" == "berk" ]; then
		kmod_remove_module
		berk_insert_module
		testname="test-${testcase}-berk-insert_module-${enable_module}-execute_multiple-${execute_multiple}-remote_mem-${berk_remote_memory_gb}-latency-${berk_latency_us}"
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
server_ip=192.168.122.97
ycsb_home=/home/u/YCSB_new
kmod_path_on_server="/opt/DiME/kernel/kmodule.ko"
kmod_prp_path_on_server="/opt/DiME/kernel/prp_fifo_module.ko"
redis_short_workload_config="${ycsb_home}/workloads/workloada_r"
redis_long_workload_config="${ycsb_home}/workloads/workloada_m"
kmod_process_in_module="redis1"	# shared/separate/redis1/redis2
redis_instance1_port=6381
redis_instance2_port=6382
#TODO::
process_in_consideration="redis"
#TODO::


# 7.6G available memory
function run_single_test {

        # kmodule parameters
        kmod_latency_ns=2500
        kmod_bandwidth_bps=100000000000
        kmod_local_npages=1000000000
        enable_module="kmod"
        execute_multiple="no"
        kmod_process_in_module="redis1"
        for kmod_local_npages in 25000 50000 75000 100000 125000 150000; # 10 20 30 40 50 60
        do
                run_test
        done


	# kmodule parameters
	kmod_latency_ns=2500
	kmod_bandwidth_bps=100000000000
	kmod_local_npages=1000000000
	enable_module="kmod"
	execute_multiple="yes"
	for kmod_local_npages in 25000 50000 75000 100000 125000 150000; # 10 20 30 40 50 60
	do
		temp_local_npages=$kmod_local_npages;
		for kmod_process_in_module in "shared" "separate" "redis1";
		do
			if [ "$kmod_process_in_module" == "shared" ];
			then
				kmod_local_npages=$((kmod_local_npages*2));
			else
				kmod_local_npages=$temp_local_npages;
			fi
			run_test
		done
	done

	# berk module parameters
	berk_remote_memory_gb=1000
	berk_bandwith_gbps=100
	berk_latency_us=5
	berk_inject_latency=1
	enable_module="berk"
	execute_multiple="yes"
	for berk_remote_memory_gb in 7.4 7.2 7 6.8 6.6 6.4; # 10 20 30 40 50 60
	do
		run_test
	done


	# berk module parameters
	berk_remote_memory_gb=1000
	berk_bandwith_gbps=100
	berk_latency_us=5
	berk_inject_latency=1
	enable_module="berk"
	execute_multiple="no"
	for berk_remote_memory_gb in 7.5 7.4 7.3 7.2 7.1 7; # 10 20 30 40 50 60
	do
		run_test
	done

}


# run tests:
for testcase in {1..10};
do
	#exec 3>"$HOSTDIR/$HOST.comb" 2> >(tee "$HOSTDIR/$HOST.err" >&3) 1> >(tee "$HOSTDIR/$HOST.out" >&3);
	run_single_test #3>"test-${testcase}.comb" 2> >(tee "test-${testcase}.err" >&3) 1> >(tee "test-${testcase}.out" >&3);
done;





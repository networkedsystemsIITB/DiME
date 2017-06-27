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
		echo never > /sys/kernel/mm/transparent_hugepage/enabled &&
		sync &&
		echo 3 > /proc/sys/vm/drop_caches &&
		service memcached stop &&
		sleep 2 &&
		service memcached start &&
		service redis stop &&
		sleep 2 &&
		service redis start || exit 1
	" || exit 1
	kmod_remove_module
	berk_remove_module
	sleep 2
}

function kmod_remove_module {
		ssh root@$server_ip '
			if [ `lsmod | grep kmodule | wc -l` -gt 0 ];
			then
				echo "Removing module..";
				rmmod kmodule || exit 1;
			fi;
		' || exit 1
}

function kmod_insert_module {
	kmod_remove_module
	
	if [ "$process_in_module" == "memcached" ]; then
		pid=`ssh root@$server_ip "ps aux | grep '/usr/bin/memcached' | head -n1 | sed 's/[ \t]\+/\t/g' | cut -f 2"`
	elif [ "$process_in_module" == "redis" ]; then
		pid=`ssh root@$server_ip "ps aux | grep '/usr/local/bin/redis-server' | head -n1 | sed 's/[ \t]\+/\t/g' | cut -f 2"`
	fi &&
	ssh root@$server_ip "
		echo \"Inserting module with pid=$pid local_npages=$kmod_local_npages latency_ns=$kmod_latency_ns bandwidth_bps=$kmod_bandwidth_bps\";
		insmod $kmod_path_on_server  pid=$pid local_npages=$kmod_local_npages latency_ns=$kmod_latency_ns bandwidth_bps=$kmod_bandwidth_bps || exit 2;
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


function redis_load {
	pushd $ycsb_home > /dev/null
		./bin/ycsb load redis -s \
			-P $redis_workload_config \
			-p "redis.host=$server_ip" \
			-threads 10
	popd > /dev/null
}
function redis_run {
	pid_redis=`ssh root@$server_ip "pgrep redis | head -n1"`

	pushd $ycsb_home > /dev/null
		./bin/ycsb run redis -s \
			-P $redis_workload_config \
			-p "redis.host=$server_ip" \
			-threads 10 &

	pid_redis_ycsb=$!

	while [ -d "/proc/${pid_redis_ycsb}" ]
	do
		rss_redis=`ssh root@$server_ip "cat /proc/${pid_redis}/status | grep -i vmrss | awk '{print $2}'"`
		swap_redis=`ssh root@$server_ip "cat /proc/${pid_redis}/status | grep -i vmswap | awk '{print $2}'"`
		echo -e "${sleep_counter}\t${rss_redis}" >> ${testfile_prefix}-run-redis-rss.log
		echo -e "${sleep_counter}\t${swap_redis}" >> ${testfile_prefix}-run-redis-swap.log
		sleep 2;
	done;

	wait;

	popd > /dev/null
}

function memcached_load {
	pushd $ycsb_home > /dev/null
		./bin/ycsb load memcached -s \
			-P $memcached_workload_config \
			-p "memcached.hosts=$server_ip" \
			-threads 10
	popd > /dev/null
}
function memcached_run {
	pid_memcached=`ssh root@$server_ip "pgrep memcached | head -n1"`

	pushd $ycsb_home > /dev/null
	
		./bin/ycsb run memcached -s \
			-P $memcached_workload_config \
			-p "memcached.hosts=$server_ip" \
			-threads 10 &

		pid_memcached_ycsb=$!

		while [ -d "/proc/${pid_memcached_ycsb}" ]
		do
			rss_memcached=`ssh root@$server_ip "cat /proc/${pid_memcached}/status | grep -i vmrss | awk '{print $2}'"`
			swap_memcached=`ssh root@$server_ip "cat /proc/${pid_memcached}/status | grep -i vmswap | awk '{print $2}'"`
			echo -e "${sleep_counter}\t${rss_memcached}" >> ${testfile_prefix}-run-memcached-rss.log
			echo -e "${sleep_counter}\t${swap_memcached}" >> ${testfile_prefix}-run-memcached-swap.log
			sleep 2;
		done;

		wait;

	popd > /dev/null
}

function run_processes {
	pushd $ycsb_home
		# load data to both redis and memcached
		if [ "$execute_multiple" == "yes" ]
		then
			redis_load | tee ${testfile_prefix}-redis-load.log &
			memcached_load | tee ${testfile_prefix}-memcached-load.log
		elif [ "$process_in_module" == "redis" ]
		then
			redis_load | tee ${testfile_prefix}-redis-load.log
		elif [ "$process_in_module" == "memcached" ]
		then
			memcached_load | tee ${testfile_prefix}-memcached-load.log
		fi

		wait	# wait for loading

		if [ "$enable_module" == "kmod" ]; then
			pagefaults=$(ssh $server_ip "cat /sys/module/kmodule/parameters/page_fault_count");
			echo "[OVERALL], pagefault_count, $pagefaults" >>  ${testfile_prefix}-redis-load.log
			echo "[OVERALL], pagefault_count, $pagefaults" >>  ${testfile_prefix}-memcached-load.log
		fi

		if [ "$execute_multiple" == "yes" ]
		then
			redis_run | tee ${testfile_prefix}-redis-run.log &
			memcached_run | tee ${testfile_prefix}-memcached-run.log
		elif [ "$process_in_module" == "redis" ]
		then
			redis_run | tee ${testfile_prefix}-redis-run.log
		elif [ "$process_in_module" == "memcached" ]
		then
			memcached_run | tee ${testfile_prefix}-memcached-run.log
		fi

		# kill memcached client
		if [ "$execute_multiple" == "yes" ]
		then
			kill $(ps aux | grep 'ycsb' | grep -v "grep" | awk '{print $2}')
		fi

		if [ "$enable_module" == "kmod" ]; then
			pagefaults_new=$(ssh $server_ip "cat /sys/module/kmodule/parameters/page_fault_count");
			pagefaults_new=$((pagefaults_new-pagefaults));
			echo "[OVERALL], pagefault_count, $pagefaults_new" >>  ${testfile_prefix}-redis-run.log
			echo "[OVERALL], pagefault_count, $pagefaults_new" >>  ${testfile_prefix}-memcached-run.log
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
		testname="test-${testcase}-kmod-insert_module-${enable_module}-execute_multiple-${execute_multiple}-local_npages-${kmod_local_npages}-latency-${kmod_latency_ns}-bandwidth-${kmod_bandwidth_bps}"
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
kmod_path_on_server="/home/server2/disaggregation/kernel/kmodule.ko"
memcached_workload_config="${ycsb_home}/workloads/workloada_m"
redis_workload_config="${ycsb_home}/workloads/workloada_r"
process_in_module="memcached"



function run_single_test {

	# berk module parameters
	berk_remote_memory_gb=1000
	berk_bandwith_gbps=100
	berk_latency_us=5
	berk_inject_latency=1
	enable_module="berk"
	execute_multiple="no"
	for berk_remote_memory_gb in 7.55 7.46 7.36 7.27 7.17 7; # 10 20 30 40 50 60
	do
		run_test
	done

	# kmodule parameters
	kmod_latency_ns=2500
	kmod_bandwidth_bps=100000000000
	kmod_local_npages=1000000000
	enable_module="kmod"
	execute_multiple="no"
	for kmod_local_npages in 25000 50000 75000 100000 125000 150000; # 10 20 30 40 50 60
	do
		run_test
	done

	# berk module parameters
	berk_remote_memory_gb=1000
	berk_bandwith_gbps=100
	berk_latency_us=5
	berk_inject_latency=1
	enable_module="berk"
	execute_multiple="yes"
	for berk_remote_memory_gb in 7.46 7.27 7 6.887 6.696 6.5; # 10 20 30 40 50 60
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
		run_test
	done
}


# run tests:
for testcase in {1..10};
do
	#exec 3>"$HOSTDIR/$HOST.comb" 2> >(tee "$HOSTDIR/$HOST.err" >&3) 1> >(tee "$HOSTDIR/$HOST.out" >&3);
	run_single_test #3>"test-${testcase}.comb" 2> >(tee "test-${testcase}.err" >&3) 1> >(tee "test-${testcase}.out" >&3);
done;





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
	#ssh root@$server_ip "mount -a" #########################################################################
	ssh root@$server_ip "dmesg -c" > /dev/null
}

function stop_services {
	ssh root@$server_ip "
		service memcached stop;
		service memcached_server1 stop;
		service memcached_server2 stop;
		service redis1 stop;
		service redis2 stop;
		service redis stop;"
}

function start_services {
	ssh root@$server_ip "
		echo never > /sys/kernel/mm/transparent_hugepage/enabled;
		service redis1 start;
		service redis2 start;
		service memcached_server1 start;
		service memcached_server2 start
		sync;
		echo 3 > /proc/sys/vm/drop_caches;"
}

function resetup_everything {
	reboot_server

	stop_services
	start_services
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
	fi


	# pin process threads to CPU cores
	cpus=`ssh root@$server_ip "nproc --all"`;
	cpuid=0;
	for pinpid in `ssh root@$server_ip "ps -p $instance1_pid -o tid= -L | sort -n"`; do
		echo "Pinning process1 $process1 : $instance1_pid thread $pinpid to CPU $cpuid";
		ssh root@$server_ip "taskset -pc $cpuid $pinpid";
		#cpuid=$((cpuid+1));			######### pin all process threads to single cpu
		#cpuid=$((cpuid%cpus));
	done;
	cpuid=$((cpuid+1));
	cpuid=$((cpuid%cpus));


	if [ "$enable_module" == "kmod_fifo" ]; then
		kmod_prp_path_on_server=$kmod_prp_fifo_path_on_server
	elif [ "$enable_module" == "kmod_lru" ]; then
		kmod_prp_path_on_server=$kmod_prp_lru_path_on_server
	elif [ "$enable_module" == "kmod_random" ]; then
		kmod_prp_path_on_server=$kmod_prp_random_path_on_server
	fi




	ssh root@$server_ip "
		echo \"Inserting module with pid=$pid local_npages=$kmod_local_npages latency_ns=$kmod_latency_ns bandwidth_bps=$kmod_bandwidth_bps\";
		insmod $kmod_path_on_server  &&
		echo  'instance_id=0 pid=$pid local_npages=$kmod_local_npages latency_ns=$kmod_latency_ns bandwidth_bps=$kmod_bandwidth_bps' > /proc/dime_config &&
		if [ \"$test_mode\" == \"separate\" ]; then
			echo  'instance_id=1 pid=$pid1 local_npages=$kmod_local_npages latency_ns=$kmod_latency_ns bandwidth_bps=$kmod_bandwidth_bps' > /proc/dime_config; 
		fi || exit 2;
	" || exit 1


	echo "Inserting module $enable_module : kswapd_sleep_ms=$kmod_kswapd_sleep_ms free_list_max_size=$kmod_free_list_max_size"
	if [ "$enable_module" == "kmod_lru" ]; then
		ssh root@$server_ip "insmod $kmod_prp_path_on_server kswapd_sleep_ms=$kmod_kswapd_sleep_ms free_list_max_size=$kmod_free_list_max_size || exit 2;" || exit 1
		# pin kswapd thread to next CPU
		dime_kswapd_pid=`ssh root@$server_ip "ps aux | grep dime_kswapd | grep -v grep | head -n1 | sed 's/[ \t]\+/\t/g' | cut -f 2"`
		echo "Pinning kswapd thread $dime_kswapd_pid to CPU $cpuid";
		ssh root@$server_ip "taskset -pc $cpuid $dime_kswapd_pid";
	elif [ "$enable_module" == "kmod_fifo" -o "$enable_module" == "kmod_random" ]; then
		ssh root@$server_ip "insmod $kmod_prp_path_on_server || exit 2;" || exit 1
	fi
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
		server_port=$redis_instance1_port
	elif [ "$1" == "2" ]; then
		server_port=$redis_instance2_port
	fi

	if [ $bench_tool = ycsb ]; then
		pushd $ycsb_home > /dev/null
			./bin/ycsb load redis -s \
				-P $redis_workload_config \
				-p "redis.host=$server_ip" \
				-p "redis.port=$server_port" \
				-threads $client_threads
		popd > /dev/null
	elif [ $bench_tool = memtier ]; then
		memtier_benchmark -s ${server_ip} -p ${server_port} -P redis -n $(($workload_b / $client_threads / $clients_per_thread)) -c $clients_per_thread -t $client_threads --ratio 100:0 -d 1000 --key-maximum=$workload_b --key-pattern=P:P --hide-histogram
	fi
}

# Params:
#	$1 = redis instance id
#
function memcached_load {
	if [ "$1" == "1" ]; then
		server_port=$memcached_instance1_port
	elif [ "$1" == "2" ]; then
		server_port=$memcached_instance2_port
	fi

	if [ $bench_tool = ycsb ]; then
		pushd $ycsb_home > /dev/null
			./bin/ycsb load memcached -s \
				-P $memcached_workload_config \
				-p "memcached.hosts=${server_ip}:${server_port}" \
				-threads $client_threads
		popd > /dev/null
	elif [ $bench_tool = memtier ]; then
		memtier_benchmark -s ${server_ip} -p ${server_port} -P memcache_binary -n $(($workload_b / $client_threads / $clients_per_thread)) -c $clients_per_thread -t $client_threads --ratio 100:0 -d 1000 --key-maximum=$workload_b --key-pattern=P:P --hide-histogram
	fi
}

# Params:
#	$1 = redis instance id
#
function redis_run {
	if [ "$1" == "1" ]; then
		server_port=$redis_instance1_port
	elif [ "$1" == "2" ]; then
		server_port=$redis_instance2_port
	fi

	if [ $bench_tool = ycsb ]; then
		pushd $ycsb_home > /dev/null
			./bin/ycsb run redis -s \
				-P $redis_workload_config \
				-p "redis.host=$server_ip" \
				-p "redis.port=$server_port" \
				-threads $client_threads
		popd > /dev/null
	elif [ $bench_tool = memtier ]; then
		memtier_benchmark -s ${server_ip} -p ${server_port} -P redis -n $(($workload_b_run  / $client_threads / $clients_per_thread)) -c $clients_per_thread -t $client_threads --ratio 50:50 -d 1000 --key-maximum=$workload_b --key-pattern=G:G --hide-histogram # --key-stddev=$(($workload_b))
	fi
}

# Params:
#	$1 = memcached instance id
#
function memcached_run {
	if [ "$1" == "1" ]; then
		server_port=$memcached_instance1_port
	elif [ "$1" == "2" ]; then
		server_port=$memcached_instance2_port
	fi

	if [ $bench_tool = ycsb ]; then
		pushd $ycsb_home > /dev/null
			./bin/ycsb run memcached -s \
				-P $memcached_workload_config \
				-p "memcached.hosts=${server_ip}:${server_port}" \
				-threads $client_threads
		popd > /dev/null
	elif [ $bench_tool = memtier ]; then
		memtier_benchmark -s ${server_ip} -p ${server_port} -P memcache_binary -n $(($workload_b_run / $client_threads / $clients_per_thread)) -c $clients_per_thread -t $client_threads --ratio 50:50 -d 1000 --key-maximum=$workload_b --key-pattern=G:G --hide-histogram #--key-stddev=$(($workload_b)) 
	fi
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
	kmod_remove_module
	berk_remove_module
	stop_services
	start_services
	sleep 2

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

		#echo "Module has been inserted, press ENTER after starting stab probe";
		#read -r

		kmod_insert_module

		kmod_get_module_stats >> ${testfile_prefix}-instance-1-load.log
		if [ "$enable_module" == "kmod_fifo" -o "$enable_module" == "kmod_lru" -o "$enable_module" == "kmod_random" ]; then
			ssh root@$server_ip "cat /proc/dime_config" > ${testfile_prefix}-load-kmod_stats.log
			ssh root@$server_ip "cat /proc/dime_prp_config" > ${testfile_prefix}-load-kmod_prp_stats.log
		fi


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

		if [ "$enable_module" == "kmod_fifo" -o "$enable_module" == "kmod_lru" -o "$enable_module" == "kmod_random" ]; then
			ssh root@$server_ip "cat /proc/dime_config" > ${testfile_prefix}-run-kmod_stats.log
			ssh root@$server_ip "cat /proc/dime_prp_config" > ${testfile_prefix}-run-kmod_prp_stats.log
		fi

	popd > /dev/null

	kmod_remove_module
	ssh root@$server_ip "dmesg -c" > ${testfile_prefix}-dmesg.log

	#echo "DONE, stop stap";
	#read -r
}

function run_test {
	percent_local_mem=$(echo "4 * $kmod_local_npages * 100 / $workload_b" | bc);
	if [ $test_mode = shared ]; then
		percent_local_mem=$(($percent_local_mem / 2))
	fi
	
	
	testname="test-${testcase}-cth-${client_threads}-cpt-${clients_per_thread}-bench-${bench_tool}-mod-${enable_module}-test_mode-${test_mode}-process1-${process1}-process2-${process2}"

	if [ "$enable_module" == "no" ]; then
		berk_remove_module
		testname="${testname}"
	elif [ "$enable_module" == "kmod_lru" ]; then
		actual_free_list_max_size=$(echo "$kmod_local_npages * 10 / 100" | bc);
		if [ $actual_free_list_max_size -gt $kmod_free_list_max_size ]
		then
			actual_free_list_max_size=$kmod_free_list_max_size;
		fi
		berk_remove_module

		testname="${testname}-plocal-${percent_local_mem}-local-${kmod_local_npages}-latency-${kmod_latency_ns}-bandwidth-${kmod_bandwidth_bps}-ksleep-${kmod_kswapd_sleep_ms}-free_size-${actual_free_list_max_size}"
	elif [ "$enable_module" == "kmod_fifo" ]; then
		if [ $test_mode = shared ]; then
			percent_local_mem=$(($percent_local_mem / 2))
		fi
		berk_remove_module
		testname="${testname}-plocal-${percent_local_mem}-local-${kmod_local_npages}-latency-${kmod_latency_ns}-bandwidth-${kmod_bandwidth_bps}-ksleep-FIFO-free_size-FIFO"
	elif [ "$enable_module" == "kmod_random" ]; then
		if [ $test_mode = shared ]; then
			percent_local_mem=$(($percent_local_mem / 2))
		fi
		berk_remove_module
		testname="${testname}-plocal-${percent_local_mem}-local-${kmod_local_npages}-latency-${kmod_latency_ns}-bandwidth-${kmod_bandwidth_bps}-ksleep-RAND-free_size-RAND"
	elif [ "$enable_module" == "berk" ]; then
		percent_local_mem=$(echo "(7.6 - $berk_remote_memory_gb)*100" | bc | cut -d. -f1);
		if [ $test_mode = shared ]; then
			percent_local_mem=$(($percent_local_mem / 2))
		fi
		berk_insert_module
		testname="test-${testcase}-client_threads-${client_threads}-berk-insert_module-${enable_module}-test_mode-${test_mode}-process1-${process1}-process2-${process2}-percent_local_mem-${percent_local_mem}-remote_mem-${berk_remote_memory_gb}-latency-${berk_latency_us}-bandwidth-${berk_bandwith_gbps}"
	fi

	testfile_prefix=$curdir/$testname

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
kmod_prp_random_path_on_server="/root/DiME/kernel/prp_random_module.ko"
redis_workload_config="${ycsb_home}/workloads/workloada_r"
memcached_workload_config="${ycsb_home}/workloads/workloada_m"
#kmod_process_in_module="redis1"	# shared/separate/redis1/redis2
redis_instance1_port=6381
redis_instance2_port=6382
memcached_instance1_port=11211
memcached_instance2_port=11212
process1="redis"
process2="memcached"
test_mode="single"		# shared/separate/single/multisingle
enable_module="no"		# kmod_lru kmod_fifo

workload_b=6000000		# number of 1000b records
workload_b_run=20000000	# number of 1000b records

client_threads=100
clients_per_thread=50
bench_tool="ycsb"	# memtier ycsb


# kmodule parameterss
kmod_latency_ns=2500
kmod_bandwidth_bps=100000000000
kmod_local_npages=1000000000
kmod_kswapd_sleep_ms=1
kmod_free_list_max_size=4000


for kmod_latency_ns in 2500;
do
	for testcase in {1..10};
	do
		for process1 in "redis" "memcached";
		do
			for bench_tool in "ycsb"; # "memtier"; #"ycsb" 
			do
				if [ $bench_tool = memtier ];
				then
					client_threads=9;
					clients_per_thread=50;
				else
					client_threads=100;
					clients_per_thread=50;
				fi

				for kmod_local_npages in 150000;
				do
					temp_local_npages=$kmod_local_npages;
					for test_mode in "single"; #"separate" "shared";
					do
						if [ "$test_mode" == "shared" ];
						then
							kmod_local_npages=$((temp_local_npages*2));
						else
							kmod_local_npages=$temp_local_npages;
						fi


						for enable_module in "kmod_random" "kmod_fifo";
						do
							run_test
						done

						
						for kmod_free_list_max_size in 15000; #25600 100 1600;
						do
							for kmod_kswapd_sleep_ms in 64; #1 16 64 256 512;
							do
								for enable_module in "kmod_lru";
								do
									run_test
								done
							done
						done
					done
				done
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

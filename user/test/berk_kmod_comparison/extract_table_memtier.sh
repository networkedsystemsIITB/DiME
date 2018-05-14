#!/bin/bash


# Initialize variables
curdir=`pwd -P`
pushd `dirname $0` > /dev/null
scriptdir=`pwd -P`
popd > /dev/null


for f in `ls -tr *instance-1*run*log`; 
#for f in `ls test-1-kmod-insert_module-kmod_lru-test_mode-single-process1-memcached-process2-memcached-percent_local_mem-10-local_npages-50000-latency-2500-bandwidth-100000000000-kswapd_sleep_ms-1-free_list_max_size-1600-instance-1-run.log`
do
	echo -n $f | tr '-' ' ';
	echo -n " throughput " ; 
	bench=$(echo "$f" | grep "memtier" | wc -l)
	if [ $bench -eq 1 ]; then
		cat $f | grep "Totals" | awk '{print $2;}' | tr "\n" '\t';
	else
		cat $f | grep "Throughput(ops/sec)" | awk -v FS='[ ,]+' '{print " ", $3;}' | tr "\n" '\t';
	fi

	logfile=$f
	page_faults_inst0_run=$(grep "instance_id" -A1 $logfile | awk '{if($1=="0"){print $5}}');
	logfile=$(echo $f | sed 's/-run/-load/g')
	page_faults_inst0_load=$(grep "instance_id" -A1 $logfile | awk '{if($1=="0"){print $5}}');
	page_faults_inst0=$((page_faults_inst0_run - page_faults_inst0_load))
	echo -n " pfcount:0 ${page_faults_inst0}";

	if [ $(echo $f | grep "kmod_process_in_module-separate" | wc -l) -gt 0 ];
	then
		logfile=$f
		page_faults_inst1_run=$(grep "instance_id" -A2 $logfile  | awk '{if($1=="1"){print $5}}');
		logfile=$(echo $f | sed 's/-run/-load/g')
		page_faults_inst1_load=$(grep "instance_id" -A2 $logfile  | awk '{if($1=="1"){print $5}}');
		page_faults_inst1=$((page_faults_inst1_run - page_faults_inst1_load))
		echo -n " pfcount:1 ${page_faults_inst1}";
	fi

	statfile_run=$(echo $f | sed 's/instance-1-run.log/run-kmod_prp_stats.log/g');
	statfile_load=$(echo $f | sed 's/instance-1-run.log/load-kmod_prp_stats.log/g');

	awk '
		BEGIN {
			# print "Start"
			ORS="  "
		}

		{
			if(NR == 1) {
				for(i = 1; i <= NF; i++) {
					labels[i] = $i;
				}
			} else if(FNR==2) {
				if(FNR==NR) {
					for(i = 1; i <= NF; i++) {
						values[i] = $i;
					}
				} else {
					for(i = 1; i <= NF; i++) {
						values[i] -= $i;
					}
				}
			}
		}

		END {
			print " ";
			for(i in labels) {
				print labels[i] " " values[i];
			}
		}
		' $statfile_run $statfile_load;
	echo ""
done;

#!/bin/bash
for f in `ls *instance-1*run*log`; 
do
	echo -n $f | tr '-' ' ';
	echo -n " throughput " ; 
	cat $f | grep "Throughput(ops/sec)" | awk -v FS='[ ,]+' '{print " ", $3;}' | tr "\n" '\t';

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
	echo ""
done;

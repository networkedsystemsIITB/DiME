#!/bin/bash


# Initialize variables
curdir=`pwd -P`
pushd `dirname $0` > /dev/null
scriptdir=`pwd -P`
popd > /dev/null


echo "Extracting raw results"

for f in `ls -tr *instance-1*run*log`; 
do
	basename=$(echo $f | sed 's/-instance-1-run.log//g')
	run_log="${basename}-instance-1-run.log"
	load_log="${basename}-instance-1-load.log"
	load_core_stats="${basename}-load-kmod_stats.log"
	run_core_stats="${basename}-run-kmod_stats.log"
	load_prp_stats="${basename}-load-kmod_prp_stats.log"
	run_prp_stats="${basename}-run-kmod_prp_stats.log"

	bench=$(echo "$basename" | grep "memtier" | wc -l)
	if [ $bench -eq 1 ]; then
		throughput_run=$(cat $run_log | grep "Totals" | awk '{print $2;}' | tr "\n" '\t');
		throughput_load=$(cat $load_log | grep "Totals" | awk '{print $2;}' | tr "\n" '\t');
	else
		throughput_run=$(cat $run_log | grep "Throughput(ops/sec)" | awk -v FS='[ ,]+' '{print " ", $3;}' | tr "\n" '\t');
		throughput_load=$(cat $load_log | grep "Throughput(ops/sec)" | awk -v FS='[ ,]+' '{print " ", $3;}' | tr "\n" '\t');
	fi

	echo -n $basename | tr '-' ' '
	echo -n " exec_type run throughput $throughput_run"
	awk -f ${scriptdir}/extract_run_stats.awk $run_core_stats;
	awk -f ${scriptdir}/extract_run_stats.awk $run_prp_stats;
	echo ""
#	echo -n $basename | tr '-' ' '
#	echo -n " exec_type load throughput $throughput_load"
#	awk -f ${scriptdir}/extract_run_stats.awk $load_core_stats;
#	awk -f ${scriptdir}/extract_run_stats.awk $load_prp_stats;
#	echo ""
done | tee ${scriptdir}/results.txt;

echo "Extracting tabularzed results"

cat ${scriptdir}/results.txt | awk -f ${scriptdir}/tabularize_results.awk | tee ${scriptdir}/results_table.txt

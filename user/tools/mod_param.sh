#!/bin/bash

DA_DEBUG_ALERT_FLAG=0x00000001
DA_DEBUG_WARNING_FLAG=0x00000002
DA_DEBUG_ERROR_FLAG=0x00000004
DA_DEBUG_INFO_FLAG=0x00000008
DA_DEBUG_ENTRYEXIT_FLAG=0x00000010
DA_DEBUG_DEBUG_FLAG=0x00000020

usage() {
echo -n "\
Usage : $0 --instance <instance id> [OPERATION]..
Set module parameters in runtime

Mandatory arguments to long options are mandatory for short options too.
  -s, --instance   <value>   instance id of the target dime instance,
							 must be id of existing instance or one greater 
							 than max instance id for new instance
  -n, --pages      <value>   set number of local pages
  -p, --pids       <value>   comma separated list of pids
  -b, --bandwidth  <value>   set bandwidth in bits-per-sec
  -l, --latency    <value>   set one way latency in nano-sec 
  -f, --pfcount    <value>   set page-fault count

  -a, --set-alert            enable alert log level
  -A, --reset-alert          disable alert log level
  -d, --set-debug            enable debug log level
  -D, --reset-debug          disable debug log level
  -i, --set-info             enable info log level
  -I, --reset-info           disable info log level
  -r, --set-error            enable error log level
  -R, --reset-error          disable error log level
  -x, --set-entryexit        enable entryexit log level
  -X, --reset-entryexit      disable entryexit log level
  -w, --set-warning          enable warning log level
  -W, --reset-warning        disable warning log level
"
}

get_params() {
	local_npages=`cat /proc/dime_config | awk '{if($1=="'$instance_id'") print $4}'`
	bandwidth_bps=`cat /proc/dime_config | awk '{if($1=="'$instance_id'") print $3}'`
	latency_ns=`cat /proc/dime_config | awk '{if($1=="'$instance_id'") print $2}'`
	pids=`cat /proc/dime_config | awk '{if($1=="'$instance_id'") print $6}'`
	page_fault_count=`cat /proc/dime_config | awk '{if($1=="'$instance_id'") print $5}'`
	da_debug_flag=`cat /sys/module/kmodule/parameters/da_debug_flag`
}

update_params() {
	parameter_list="instance_id=$instance_id local_npages=$local_npages bandwidth_bps=$bandwidth_bps latency_ns=$latency_ns pid=$pids"
	if [ "$page_fault_count_changed" == "1" ]
	then
		parameter_list+=" page_fault_count=$page_fault_count"
	fi
	echo "$parameter_list" | tee /proc/dime_config
	echo $da_debug_flag > /sys/module/kmodule/parameters/da_debug_flag
}

print_params() {
	echo -e "    Instance ID              : $instance_id"
	echo -e "    Number of local pages    : $local_npages"
	echo -e "    Bandwidth (bits-per-sec) : $bandwidth_bps"
	echo -e "    Latency (nano-sec)       : $latency_ns"
	echo -e "    PID list                 : $pids"
	echo -e "    Page-fault count         : $page_fault_count"
	printf  "    Debug flags              : %s\n" $(printf "%8s" $(echo "obase=2;$da_debug_flag" | bc) | tr ' ' '0')
}

num_params=$#

while [[ $# -ge 1 ]]
do
	key="$1"
	case $key in
		-s|--instance)
			instance_id="$2"
			get_params
			shift 2
			;;
		-n|--pages)
			local_npages="$2"
			shift 2
			;;
		-p|--pids)
			pids="$2"
			shift 2
			;;
		-b|--bandwidth)
			bandwidth_bps="$2"
			shift 2
			;;
		-l|--latency)
			latency_ns="$2"
			shift 2
			;;
		-f|--pfcount)
			page_fault_count="$2"
			page_fault_count_changed="1"
			shift 2
			;;
		-a|--set-alert)
			da_debug_flag=$((da_debug_flag | DA_DEBUG_ALERT_FLAG))
			shift
			;;
		-w|--set-warning)
			da_debug_flag=$((da_debug_flag | DA_DEBUG_WARNING_FLAG))
			shift
			;;
		-r|--set-error)
			da_debug_flag=$((da_debug_flag | DA_DEBUG_ERROR_FLAG))
			shift
			;;
		-i|--set-info)
			da_debug_flag=$((da_debug_flag | DA_DEBUG_INFO_FLAG))
			shift
			;;
		-d|--set-debug)
			da_debug_flag=$((da_debug_flag | DA_DEBUG_DEBUG_FLAG))
			shift
			;;
		-x|--set-entryexit)
			da_debug_flag=$((da_debug_flag | DA_DEBUG_ENTRYEXIT_FLAG))
			shift
			;;
		-A|--reset-alert)
			da_debug_flag=$((da_debug_flag & ~DA_DEBUG_ALERT_FLAG))
			shift
			;;
		-W|--reset-warning)
			da_debug_flag=$((da_debug_flag & ~DA_DEBUG_WARNING_FLAG))
			shift
			;;
		-R|--reset-error)
			da_debug_flag=$((da_debug_flag & ~DA_DEBUG_ERROR_FLAG))
			shift
			;;
		-I|--reset-info)
			da_debug_flag=$((da_debug_flag & ~DA_DEBUG_INFO_FLAG))
			shift
			;;
		-D|--reset-debug)
			da_debug_flag=$((da_debug_flag & ~DA_DEBUG_DEBUG_FLAG))
			shift
			;;
		-X|--reset-entryexit)
			da_debug_flag=$((da_debug_flag & ~DA_DEBUG_ENTRYEXIT_FLAG))
			shift
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			echo "$0: unrecognized option $1"
			echo "Try '$0 -h|--help' for more information."
			exit 1
			;;
	esac
done


if [ "$instance_id" == "" ]
then
	echo "Instance ID is mandatory, refer to following instance configuration:"
	cat /proc/dime_config
	usage
elif [ $num_params -le 2 ]
then
	echo "Getting params from module.."
	get_params
	print_params
else
	echo "Updating new param values.."
	update_params
	echo "Getting params from module to verify.."
	get_params
	echo "Updated param values :"
	print_params
fi
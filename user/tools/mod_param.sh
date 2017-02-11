#!/bin/bash

DA_DEBUG_ALERT_FLAG=0x00000001
DA_DEBUG_WARNING_FLAG=0x00000002
DA_DEBUG_ERROR_FLAG=0x00000004
DA_DEBUG_INFO_FLAG=0x00000008
DA_DEBUG_ENTRYEXIT_FLAG=0x00000010
DA_DEBUG_DEBUG_FLAG=0x00000020

usage() {
echo -n "\
Usage : $0 [OPERATION]..
Set module parameters in runtime

Mandatory arguments to long options are mandatory for short options too.
  -s, --localstart <value>   set start address of local memory
  -e, --localend   <value>   set end address of local memory
  -b, --bandwidth  <value>   set bandwidth in bits-per-sec
  -l, --latency    <value>   set one way latency in nano-sec 

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
    local_start=`cat /sys/module/da_kmodule/parameters/local_start`
    local_end=`cat /sys/module/da_kmodule/parameters/local_end`
    bandwidth_bps=`cat /sys/module/da_kmodule/parameters/bandwidth_bps`
    latency_ns=`cat /sys/module/da_kmodule/parameters/latency_ns`
    da_debug_flag=`cat /sys/module/da_kmodule/parameters/da_debug_flag`
}

update_params() {
    echo $local_start > /sys/module/da_kmodule/parameters/local_start
    echo $local_end > /sys/module/da_kmodule/parameters/local_end
    echo $bandwidth_bps > /sys/module/da_kmodule/parameters/bandwidth_bps
    echo $latency_ns > /sys/module/da_kmodule/parameters/latency_ns
    echo $da_debug_flag > /sys/module/da_kmodule/parameters/da_debug_flag
}

print_params() {
    echo -e "    Local address start      : $local_start"
    echo -e "    Local address end        : $local_end"
    echo -e "    Bandwidth (bits-per-sec) : $bandwidth_bps"
    echo -e "    Latency (nano-sec)       : $latency_ns"
    printf  "    Debug flags              : %s\n" $(printf "%8s" $(echo "obase=2;$da_debug_flag" | bc) | tr ' ' '0')
}

num_params=$#

get_params

while [[ $# -ge 1 ]]
do
    key="$1"
    case $key in
        -s|--localstart)
            local_start="$2"
            shift 2
            ;;
        -e|--localend)
            local_end="$2"
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

if [ $num_params -eq 0 ]
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
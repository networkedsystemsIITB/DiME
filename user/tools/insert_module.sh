#!/bin/bash


# Change pwd to script path
pushd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null
SCRIPT_PATH="$( pwd )"
popd >/dev/null



usage() {
echo -n "\
Usage : $0 [OPERATION]..
Insert module with config file

Mandatory arguments to long options are mandatory for short options too.
  -p, --pids       <value>   a comma (,) separated list of pids to add into emulator
  -c, --config     <value>   path to config file
"
}

config=""
pids=""
while [[ $# -ge 1 ]]
do
    key="$1"
    case $key in
        -c|--config)
            config="$2"
            shift 2
            ;;
        -p|--pids)
            pids="$2"
            shift 2
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


if [ "$pids" == "" ]; then
	echo "Please provide ',' separated list of pids"
	echo "Try '$0 -h|--help' for more information"
	exit 1
elif [ "$config" == "" ]; then
	echo "Please provide valid config path"
	echo "Try '$0 -h|--help' for more information"
	exit 1
fi

latency_ns=$(grep -v '^#' $config | grep "latency_ns" | awk '{print $2}')
bandwidth_bps=$(grep -v '^#' $config | grep "bandwidth_bps" | awk '{print $2}')
local_npages=$(grep -v '^#' $config | grep "local_npages" | awk '{print $2}')
da_debug_flag=$(grep -v '^#' $config | grep "da_debug_flag" | awk '{print $2}')


# filter set values in config files, other parameters will be default by module
parameter_list=""

if [ "$latency_ns" != "" ]; then
	parameter_list+=" latency_ns=$latency_ns"
fi
if [ "$bandwidth_bps" != "" ]; then
	parameter_list+=" bandwidth_bps=$bandwidth_bps"
fi
if [ "$local_npages" != "" ]; then
	parameter_list+=" local_npages=$local_npages"
fi
if [ "$da_debug_flag" != "" ]; then
	parameter_list+=" da_debug_flag=$da_debug_flag"
fi


# Disable huge pages
echo never > /sys/kernel/mm/transparent_hugepage/enabled

echo "Inserting module.. pid=$pids $parameter_list"
insmod $SCRIPT_PATH/../../kernel/kmodule.ko pid=$pids $parameter_list
insmod $SCRIPT_PATH/../../kernel/prp_fifo_module.ko

#!/bin/bash

# Change pwd to script path
SCRIPT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

if [ "$#" -lt "1" ]
then
	echo "Number of pages required"
	exit 1
fi

$SCRIPT_PATH/../exec_command.sh "$SCRIPT_PATH/test_prog $1" &
#$SCRIPT_PATH/test_prog $1 &
pid=$!

echo "PID : $pid"
cat /proc/$pid/maps

echo "[NOTE] Press enter to send the test program a signal to start testing..."
read r

#pid=$(ps | grep test_prog | awk '{print $1;}')
if [ "$pid" == "" ]
then
	echo "Could not find test program"
	exit 1
fi

sudo pkill -USR1 test_prog

wait $pid


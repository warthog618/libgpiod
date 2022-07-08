#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com>

# Simple test harness for the gpio-tools.

# Where output from coprocesses is stored
COPROC_OUTPUT=$BATS_TMPDIR/gpio-tools-test-output
# Save the PID of coprocess - otherwise we won't be able to wait for it
# once it exits as the COPROC_PID will be cleared.
COPROC_SAVED_PID=""

GPIOSIM_CHIPS=""
GPIOSIM_CONFIGFS="/sys/kernel/config/gpio-sim/"
GPIOSIM_SYSFS="/sys/devices/platform/"

# Run the command in $* and return 0 if the command failed. The way we do it
# here is a workaround for the way bats handles failing processes.
assert_fail() {
	$* || return 0
	return 1
}

# Check if the string in $2 matches against the pattern in $1.
regex_matches() {
	local PATTERN=$1
	local STRING=$2

	[[ $STRING =~ $PATTERN ]]
}

# Iterate over all lines in the output of the last command invoked with bats'
# 'run' or the coproc helper and check if at least one is equal to $1.
output_contains_line() {
	local LINE=$1

	for line in "${lines[@]}"
	do
		test "$line" = "$LINE" && return 0
	done

	return 1
}

# Same as above but match against the regex pattern in $1.
output_regex_match() {
	local PATTERN=$1

	for line in "${lines[@]}"
	do
		regex_matches "$PATTERN" "$line" && return 0
	done

	return 1
}

random_name() {
	cat /proc/sys/kernel/random/uuid
}

gpiosim_chip() {
	local VAR=$1
	local NAME=$(random_name)
	local DEVPATH=$GPIOSIM_CONFIGFS/$NAME
	local BANKPATH=$DEVPATH/$NAME

	mkdir -p $BANKPATH

	for ARG in $*
	do
		local KEY=$(echo $ARG | cut -d"=" -f1)
		local VAL=$(echo $ARG | cut -d"=" -f2)

		if [ "$KEY" = "num_lines" ]
		then
			echo $VAL > $BANKPATH/num_lines
		elif [ "$KEY" = "line_name" ]
		then
			local OFFSET=$(echo $VAL | cut -d":" -f1)
			local LINENAME=$(echo $VAL | cut -d":" -f2)
			local LINEPATH=$BANKPATH/line$OFFSET

			mkdir -p $LINEPATH
			echo $LINENAME > $LINEPATH/name
		fi
	done

	echo 1 > $DEVPATH/live

	GPIOSIM_CHIPS="$VAR:$NAME $GPIOSIM_CHIPS"
}

gpiosim_chip_map_name() {
	local VAR=$1

	for CHIP in $GPIOSIM_CHIPS
	do
		KEY=$(echo $CHIP | cut -d":" -f1)
		VAL=$(echo $CHIP | cut -d":" -f2)

		if [ "$KEY" = "$VAR" ]
		then
			echo $VAL
		fi
	done
}

gpiosim_chip_name() {
	local VAR=$1
	local NAME=$(gpiosim_chip_map_name $VAR)

	cat $GPIOSIM_CONFIGFS/$NAME/$NAME/chip_name
}

gpiosim_dev_name() {
	local VAR=$1
	local NAME=$(gpiosim_chip_map_name $VAR)

	cat $GPIOSIM_CONFIGFS/$NAME/dev_name
}

gpiosim_set_pull() {
	local VAR=$1
	local OFFSET=$2
	local PULL=$3
	local DEVNAME=$(gpiosim_dev_name $VAR)
	local CHIPNAME=$(gpiosim_chip_name $VAR)

	echo $PULL > $GPIOSIM_SYSFS/$DEVNAME/$CHIPNAME/sim_gpio$OFFSET/pull
}

gpiosim_check_value() {
	local VAR=$1
	local OFFSET=$2
	local EXPECTED=$3
	local DEVNAME=$(gpiosim_dev_name $VAR)
	local CHIPNAME=$(gpiosim_chip_name $VAR)

	VAL=$(cat $GPIOSIM_SYSFS/$DEVNAME/$CHIPNAME/sim_gpio$OFFSET/value)
	if [ "$VAL" = "$EXPECTED" ]
	then
		return 0
	fi

	return 1
}

gpiosim_cleanup() {
	for CHIP in $GPIOSIM_CHIPS
	do
		local NAME=$(echo $CHIP | cut -d":" -f2)

		local DEVPATH=$GPIOSIM_CONFIGFS/$NAME
		local BANKPATH=$DEVPATH/$NAME

		echo 0 > $DEVPATH/live

		ls $BANKPATH/line* 2> /dev/null
		if [ "$?" = "0" ]
		then
			for LINE in $(find $BANKPATH/ | egrep "line[0-9]+$")
			do
				test -e $LINE/hog && rmdir $LINE/hog
				rmdir $LINE
			done
		fi

		rmdir $BANKPATH
		rmdir $DEVPATH
	done

	GPIOSIM_CHIPS=""
}

run_tool() {
	# Executables to test are expected to be in the same directory as the
	# testing script.
	run timeout 10s $BATS_TEST_DIRNAME/"$@"
}

coproc_run_tool() {
	rm -f $BR_PROC_OUTPUT
	coproc timeout 10s $BATS_TEST_DIRNAME/"$@" > $COPROC_OUTPUT 2> $COPROC_OUTPUT
	COPROC_SAVED_PID=$COPROC_PID
	# FIXME We're giving the background process some time to get up, but really this
	# should be more reliable...
	sleep 0.2
}

coproc_tool_stdin_write() {
	echo $* >&${COPROC[1]}
}

coproc_tool_kill() {
	SIGNUM=$1

	kill $SIGNUM $COPROC_SAVED_PID
}

coproc_tool_wait() {
	status="0"
	# A workaround for the way bats handles command failures.
	wait $COPROC_SAVED_PID || export status=$?
	test "$status" -ne 0 || export status="0"
	output=$(cat $COPROC_OUTPUT)
	local ORIG_IFS="$IFS"
	IFS=$'\n' lines=($output)
	IFS="$ORIG_IFS"
	rm -f $COPROC_OUTPUT
}

teardown() {
	if [ -n "$BG_PROC_PID" ]
	then
		kill -9 $BG_PROC_PID
		run wait $BG_PROC_PID
		BG_PROC_PID=""
	fi

	gpiosim_cleanup
}


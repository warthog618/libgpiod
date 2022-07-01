#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com>

# Simple test harness for the gpio-tools.

# Where output from the dut is stored
DUT_OUTPUT=$BATS_TMPDIR/gpio-tools-test-output
# Save the PID of coprocess - otherwise we won't be able to wait for it
# once it exits as the COPROC_PID will be cleared.
DUT_PID=""

# mappings from local name to system chip name, path, dev name
# -g required for the associative arrays, cos BATS...
declare -g -A GPIOSIM_CHIP_NAME
declare -g -A GPIOSIM_CHIP_PATH
declare -g -A GPIOSIM_DEV_NAME
GPIOSIM_CONFIGFS="/sys/kernel/config/gpio-sim"
GPIOSIM_SYSFS="/sys/devices/platform/"
GPIOSIM_APP_NAME="gpio-tools-test"

# Run the command in $* and return 0 if the command failed. The way we do it
# here is a workaround for the way bats handles failing processes.
assert_fail() {
	$* || return 0
	return 1
}

# Check if the string in $2 matches against the pattern in $1.
regex_matches() {
	[[ $2 =~ $1 ]] || (echo "Mismatched: \"$2\"" && false)
}

# Iterate over all lines in the output of the last command invoked with bats'
# 'run' or the coproc helper and check if at least one is equal to $1.
output_contains_line() {
	local LINE=$1

	for line in "${lines[@]}"
	do
		test "$line" = "$LINE" && return 0
	done
	echo "Mismatched:"
	echo "$output"
	return 1
}

output_is() {
	test "$output" = "$1" || (echo "Mismatched: \"$output\"" && false)
}

num_lines_is() {
	test ${#lines[@]} -eq $1 || (echo "Num lines is : ${#lines[@]}" && false)
}

status_is() {
	test "$status" -eq "$1"
}

# Same as above but match against the regex pattern in $1.
output_regex_match() {
	for line in "${lines[@]}"
	do
		[[ "$line" =~ $1 ]] && return 0
	done
	echo "Mismatched:"
	echo "$output"
	return 1
}

gpiosim_chip() {
	local VAR=$1
	local NAME=${GPIOSIM_APP_NAME}-$$-${VAR}
	local DEVPATH=$GPIOSIM_CONFIGFS/$NAME
	local BANKPATH=$DEVPATH/bank0

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

	local chip_name=$(<$BANKPATH/chip_name)
	GPIOSIM_CHIP_NAME[$1]=$chip_name
	GPIOSIM_CHIP_PATH[$1]="/dev/$chip_name"
	GPIOSIM_DEV_NAME[$1]=$(<$DEVPATH/dev_name)
}

gpiosim_chip_number() {
	local NAME=${GPIOSIM_CHIP_NAME[$1]}
	echo ${NAME#"gpiochip"}
}

gpiosim_chip_symlink() {
	GPIOSIM_CHIP_LINK="$2/${GPIOSIM_APP_NAME}-$$-lnk"
	ln -s ${GPIOSIM_CHIP_PATH[$1]} "$GPIOSIM_CHIP_LINK"
}

gpiosim_chip_symlink_cleanup() {
        if [ -n "$GPIOSIM_CHIP_LINK" ]
        then
		rm "$GPIOSIM_CHIP_LINK"
	fi
	unset GPIOSIM_CHIP_LINK
}

gpiosim_set_pull() {
	local OFFSET=$2
	local PULL=$3
	local DEVNAME=${GPIOSIM_DEV_NAME[$1]}
	local CHIPNAME=${GPIOSIM_CHIP_NAME[$1]}

	echo $PULL > $GPIOSIM_SYSFS/$DEVNAME/$CHIPNAME/sim_gpio$OFFSET/pull
}

gpiosim_check_value() {
	local OFFSET=$2
	local EXPECTED=$3
	local DEVNAME=${GPIOSIM_DEV_NAME[$1]}
	local CHIPNAME=${GPIOSIM_CHIP_NAME[$1]}

	VAL=$(<$GPIOSIM_SYSFS/$DEVNAME/$CHIPNAME/sim_gpio$OFFSET/value)
	[ "$VAL" = "$EXPECTED" ]
}

gpiosim_cleanup() {
	for CHIP in ${!GPIOSIM_CHIP_NAME[@]}
	do
		local NAME=${GPIOSIM_APP_NAME}-$$-$CHIP

		local DEVPATH=$GPIOSIM_CONFIGFS/$NAME
		local BANKPATH=$DEVPATH/bank0

		echo 0 > $DEVPATH/live

		ls $BANKPATH/line* > /dev/null 2>&1
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

	gpiosim_chip_symlink_cleanup

	GPIOSIM_CHIP_NAME=()
	GPIOSIM_CHIP_PATH=()
	GPIOSIM_DEV_NAME=()
}

run_tool() {
	# Executables to test are expected to be in the same directory as the
	# testing script.
	run timeout 10s $BATS_TEST_DIRNAME/"$@"
}

dut_run() {
	coproc timeout 10s stdbuf -oL $BATS_TEST_DIRNAME/"$@" 2>&1
	DUT_PID=$COPROC_PID
	read -t1 -n1 -u ${COPROC[0]} DUT_FIRST_CHAR
}

dut_run_redirect() {
	coproc timeout 10s $BATS_TEST_DIRNAME/"$@" > $DUT_OUTPUT 2>&1
	DUT_PID=$COPROC_PID
	# give the process time to spin up
	# FIXME - find a better solution
	sleep 0.2
}

dut_read_redirect() {
	output=$(<$DUT_OUTPUT)
        local ORIG_IFS="$IFS"
        IFS=$'\n' lines=($output)
        IFS="$ORIG_IFS"
}

dut_read() {
	local LINE
	lines=()
	while read -t 0.2 -u ${COPROC[0]} LINE;
	do
		if [ -n "$DUT_FIRST_CHAR" ]
		then
			LINE=${DUT_FIRST_CHAR}${LINE}
			unset DUT_FIRST_CHAR
		fi
		lines+=("$LINE")
	done
	output="${lines[@]}"
}

dut_readable() {
	read -t 0 -u ${COPROC[0]} LINE
}

dut_flush() {
	local JUNK
	lines=()
	output=
	unset DUT_FIRST_CHAR
	while read -t 0 -u ${COPROC[0]} JUNK;
	do
		read -t 0.1 -u ${COPROC[0]} JUNK || true
	done
}

# check the next line of output matches the regex
dut_regex_match() {
	PATTERN=$1

	read -t 0.2 -u ${COPROC[0]} LINE || (echo Timeout && false)
	if [ -n "$DUT_FIRST_CHAR" ]
	then
		LINE=${DUT_FIRST_CHAR}${LINE}
		unset DUT_FIRST_CHAR
	fi
	[[ $LINE =~ $PATTERN ]] || (echo "Mismatched: \"$LINE\"" && false)
}

dut_write() {
	echo $* >&${COPROC[1]}
}

dut_kill() {
	SIGNUM=$1

	kill $SIGNUM $DUT_PID
}

dut_wait() {
	status="0"
	# A workaround for the way bats handles command failures.
	wait $DUT_PID || export status=$?
	test "$status" -ne 0 || export status="0"
	unset DUT_PID
}

dut_cleanup() {
        if [ -n "$DUT_PID" ]
        then
		kill -SIGTERM $DUT_PID
		wait $DUT_PID || false
        fi
        rm -f $DUT_OUTPUT
}

teardown() {
	dut_cleanup
	gpiosim_cleanup
}

request_release_line() {
	$BATS_TEST_DIRNAME/gpioget -c $* >/dev/null
}

#
# gpiodetect test cases
#

@test "gpiodetect: all chips" {
	gpiosim_chip sim0 num_lines=4
	gpiosim_chip sim1 num_lines=8
	gpiosim_chip sim2 num_lines=16

	run_tool gpiodetect

	status_is 0
	output_contains_line "${GPIOSIM_CHIP_NAME[sim0]} [${GPIOSIM_DEV_NAME[sim0]}-node0] (4 lines)"
	output_contains_line "${GPIOSIM_CHIP_NAME[sim1]} [${GPIOSIM_DEV_NAME[sim1]}-node0] (8 lines)"
	output_contains_line "${GPIOSIM_CHIP_NAME[sim2]} [${GPIOSIM_DEV_NAME[sim2]}-node0] (16 lines)"

	# ignoring symlinks
	local initial_output=$output
	gpiosim_chip_symlink sim1 /dev

	run_tool gpiodetect

	status_is 0
	output_is "$initial_output"
}

@test "gpiodetect: a chip" {
	gpiosim_chip sim0 num_lines=4
	gpiosim_chip sim1 num_lines=8
	gpiosim_chip sim2 num_lines=16

	# by name
	run_tool gpiodetect ${GPIOSIM_CHIP_NAME[sim0]}

	status_is 0
	num_lines_is 1
	output_contains_line "${GPIOSIM_CHIP_NAME[sim0]} [${GPIOSIM_DEV_NAME[sim0]}-node0] (4 lines)"

	# by path
	run_tool gpiodetect ${GPIOSIM_CHIP_PATH[sim1]}

	status_is 0
	num_lines_is 1
	output_contains_line "${GPIOSIM_CHIP_NAME[sim1]} [${GPIOSIM_DEV_NAME[sim1]}-node0] (8 lines)"

	# by number
	run_tool gpiodetect $(gpiosim_chip_number sim2)

	status_is 0
	num_lines_is 1
	output_contains_line "${GPIOSIM_CHIP_NAME[sim2]} [${GPIOSIM_DEV_NAME[sim2]}-node0] (16 lines)"

	# by symlink
	gpiosim_chip_symlink sim2 .
	run_tool gpiodetect $GPIOSIM_CHIP_LINK

	status_is 0
	num_lines_is 1
	output_contains_line "${GPIOSIM_CHIP_NAME[sim2]} [${GPIOSIM_DEV_NAME[sim2]}-node0] (16 lines)"
}

@test "gpiodetect: multiple chips" {
	gpiosim_chip sim0 num_lines=4
	gpiosim_chip sim1 num_lines=8
	gpiosim_chip sim2 num_lines=16

	local sim0=${GPIOSIM_CHIP_NAME[sim0]}
	local sim1=${GPIOSIM_CHIP_NAME[sim1]}
	local sim2=${GPIOSIM_CHIP_NAME[sim2]}

	run_tool gpiodetect $sim0 $sim1 $sim2

	status_is 0
	num_lines_is 3
	output_contains_line "$sim0 [${GPIOSIM_DEV_NAME[sim0]}-node0] (4 lines)"
	output_contains_line "$sim1 [${GPIOSIM_DEV_NAME[sim1]}-node0] (8 lines)"
	output_contains_line "$sim2 [${GPIOSIM_DEV_NAME[sim2]}-node0] (16 lines)"
}

@test "gpiodetect: with nonexistent chip" {
	run_tool gpiodetect nonexistent-chip

	status_is 1
	output_regex_match \
".*cannot find a GPIO chip character device corresponding to nonexistent-chip"
}

#
# gpioinfo test cases
#

@test "gpioinfo: all chips" {
	gpiosim_chip sim0 num_lines=4
	gpiosim_chip sim1 num_lines=8

	run_tool gpioinfo

	status_is 0
	output_contains_line "${GPIOSIM_CHIP_NAME[sim0]} - 4 lines:"
	output_contains_line "${GPIOSIM_CHIP_NAME[sim1]} - 8 lines:"
	output_regex_match "\\s+line\\s+0:\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "\\s+line\\s+7:\\s+unnamed\\s+unused\\s+\\[input\\]"

	# ignoring symlinks
	local initial_output=$output
	gpiosim_chip_symlink sim1 /dev

	run_tool gpioinfo

	status_is 0
	output_is "$initial_output"
}

@test "gpioinfo: all chips with some used lines" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar
	gpiosim_chip sim1 num_lines=8 line_name=3:baz line_name=4:xyz

	dut_run gpioset -i --active-low foo=1 baz=0

	run_tool gpioinfo

	status_is 0
	output_contains_line "${GPIOSIM_CHIP_NAME[sim0]} - 4 lines:"
	output_contains_line "${GPIOSIM_CHIP_NAME[sim1]} - 8 lines:"
	output_regex_match "\\s+line\\s+0:\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "\\s+line\\s+1:\\s+foo\\s+gpioset\\s+\\[output\\s+active-low\\s+used\\]"
	output_regex_match "\\s+line\\s+3:\\s+baz\\s+gpioset\\s+\\[output\\s+active-low\\s+used\\]"
}

@test "gpioinfo: a chip" {
	gpiosim_chip sim0 num_lines=8
	gpiosim_chip sim1 num_lines=4
	local sim1=${GPIOSIM_CHIP_NAME[sim1]}

	# by name
	run_tool gpioinfo --chip $sim1

	status_is 0
	num_lines_is 5
	output_contains_line "$sim1 - 4 lines:"
	output_regex_match "\\s+line\\s+0:\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "\\s+line\\s+1:\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "\\s+line\\s+2:\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "\\s+line\\s+3:\\s+unnamed\\s+unused\\s+\\[input\\]"

	# by path
	run_tool gpioinfo --chip $sim1

	status_is 0
	num_lines_is 5
	output_contains_line "$sim1 - 4 lines:"
	output_regex_match "\\s+line\\s+0:\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "\\s+line\\s+1:\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "\\s+line\\s+2:\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "\\s+line\\s+3:\\s+unnamed\\s+unused\\s+\\[input\\]"

	# by number
	run_tool gpioinfo --chip $sim1

	status_is 0
	num_lines_is 5
	output_contains_line "$sim1 - 4 lines:"
	output_regex_match "\\s+line\\s+0:\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "\\s+line\\s+1:\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "\\s+line\\s+2:\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "\\s+line\\s+3:\\s+unnamed\\s+unused\\s+\\[input\\]"

	# by symlink
	gpiosim_chip_symlink sim1 .
	run_tool gpioinfo --chip $GPIOSIM_CHIP_LINK

	status_is 0
	num_lines_is 5
	output_contains_line "$sim1 - 4 lines:"
	output_regex_match "\\s+line\\s+0:\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "\\s+line\\s+1:\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "\\s+line\\s+2:\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "\\s+line\\s+3:\\s+unnamed\\s+unused\\s+\\[input\\]"
}

@test "gpioinfo: a line" {
	gpiosim_chip sim0 num_lines=8 line_name=5:bar
	gpiosim_chip sim1 num_lines=4 line_name=2:bar
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}
	local sim1=${GPIOSIM_CHIP_NAME[sim1]}

	# by offset
	run_tool gpioinfo --chip $sim1 2

	status_is 0
	num_lines_is 1
	output_regex_match "$sim1\\s+2\\s+bar\\s+unused\\s+\\[input\\]"

	# by name
	run_tool gpioinfo bar

	status_is 0
	num_lines_is 1
	output_regex_match "$sim0\\s+5\\s+bar\\s+unused\\s+\\[input\\]"

	# by chip and name
	run_tool gpioinfo --chip $sim1 2

	status_is 0
	num_lines_is 1
	output_regex_match "$sim1\\s+2\\s+bar\\s+unused\\s+\\[input\\]"
}

@test "gpioinfo: first matching named line" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar \
				      line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	run_tool gpioinfo foobar

	status_is 0
	num_lines_is 1
	output_regex_match "${GPIOSIM_CHIP_NAME[sim0]}\\s+3\\s+foobar\\s+unused\\s+\\[input\\]"
}

@test "gpioinfo: multiple lines" {
	gpiosim_chip sim0 num_lines=8 line_name=5:bar
	gpiosim_chip sim1 num_lines=4 line_name=2:baz
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}
	local sim1=${GPIOSIM_CHIP_NAME[sim1]}

	# by offset
	run_tool gpioinfo --chip $sim1 1 2

	status_is 0
	num_lines_is 2
	output_regex_match "${GPIOSIM_CHIP_NAME[sim1]}\\s+1\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "${GPIOSIM_CHIP_NAME[sim1]}\\s+2\\s+baz\\s+unused\\s+\\[input\\]"

	# by name
	run_tool gpioinfo bar baz

	status_is 0
	num_lines_is 2
	output_regex_match "$sim0\\s+5\\s+bar\\s+unused\\s+\\[input\\]"
	output_regex_match "${GPIOSIM_CHIP_NAME[sim1]}\\s+2\\s+baz\\s+unused\\s+\\[input\\]"

	# by name and offset
	run_tool gpioinfo --chip $sim0 bar 3

	status_is 0
	num_lines_is 2
	output_regex_match "$sim0\\s+5\\s+bar\\s+unused\\s+\\[input\\]"
	output_regex_match "$sim0\\s+3\\s+unnamed\\s+unused\\s+\\[input\\]"
}

@test "gpioinfo: line attribute menagerie" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo
	gpiosim_chip sim1 num_lines=8 line_name=3:baz
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}
	local sim1=${GPIOSIM_CHIP_NAME[sim1]}

	dut_run gpioset -i --active-low --bias=pull-up --drive=open-source foo=1 baz=0

	run_tool gpioinfo foo baz

	status_is 0
	num_lines_is 2
	output_regex_match \
"$sim0\\s+1\\s+foo\\s+gpioset\\s+\\[output\\s+active-low\\s+used\\s+open-source\\s+pull-up\\]"
	output_regex_match \
"$sim1\\s+3\\s+baz\\s+gpioset\\s+\\[output\\s+active-low\\s+used\\s+open-source\\s+pull-up\\]"

	dut_kill
	dut_wait

	dut_run gpioset -i --bias=pull-down --drive=open-drain foo=1 baz=0

	run_tool gpioinfo foo baz

	status_is 0
	num_lines_is 2
	output_regex_match \
"$sim0\\s+1\\s+foo\\s+gpioset\\s+\\[output\\s+used\\s+open-drain\\s+pull-down\\]"
	output_regex_match \
"$sim1\\s+3\\s+baz\\s+gpioset\\s+\\[output\\s+used\\s+open-drain\\s+pull-down\\]"

	dut_kill
	dut_wait

	dut_run gpiomon --banner --bias=disabled --utc --debounce-period=10ms foo baz

	run_tool gpioinfo foo baz

	status_is 0
	num_lines_is 2
	output_regex_match \
"$sim0\\s+1\\s+foo\\s+gpiomon\\s+\\[input\\s+used\\s+bias-disabled\\s+both-edges\\s+event-clock-realtime\\s+debounce_period=10000us\\]"
	output_regex_match \
"$sim1\\s+3\\s+baz\\s+gpiomon\\s+\\[input\\s+used\\s+bias-disabled\\s+both-edges\\s+event-clock-realtime\\s+debounce_period=10000us\\]"

	dut_kill
	dut_wait

	dut_run gpiomon --banner --edge=rising --localtime foo baz

	run_tool gpioinfo foo baz

	status_is 0
	num_lines_is 2
	output_regex_match \
"$sim0\\s+1\\s+foo\\s+gpiomon\\s+\\[input\\s+used\\s+rising-edges\\s+event-clock-realtime\\]"
	output_regex_match \
"$sim1\\s+3\\s+baz\\s+gpiomon\\s+\\[input\\s+used\\s+rising-edges\\s+event-clock-realtime\\]"

	dut_kill
	dut_wait

	dut_run gpiomon --banner --edge=falling foo baz

	run_tool gpioinfo foo baz

	status_is 0
	num_lines_is 2
	output_regex_match "$sim0\\s+1\\s+foo\\s+gpiomon\\s+\\[input\\s+used\\s+falling-edges\\]"
	output_regex_match "$sim1\\s+3\\s+baz\\s+gpiomon\\s+\\[input\\s+used\\s+falling-edges\\]"
}

@test "gpioinfo: with same line twice" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	# by offset
	run_tool gpioinfo --chip $sim0 1 1

	status_is 0
	num_lines_is 1
	output_regex_match "$sim0\\s+1\\s+foo\\s+unused\\s+\\[input\\]"

	# by name
	run_tool gpioinfo foo foo

	status_is 0
	num_lines_is 1
	output_regex_match "$sim0\\s+1\\s+foo\\s+unused\\s+\\[input\\]"

	# by name and offset
	run_tool gpioinfo --chip $sim0 foo 1

	status_is 0
	num_lines_is 1
	output_regex_match "$sim0\\s+1\\s+foo\\s+unused\\s+\\[input\\]"
}

@test "gpioinfo: all lines matching name" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar \
				      line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}
	local sim1=${GPIOSIM_CHIP_NAME[sim1]}

	run_tool gpioinfo --strict foobar

	status_is 1
	num_lines_is 3
	output_regex_match "$sim0\\s+3\\s+foobar\\s+unused\\s+\\[input\\]"
	output_regex_match "$sim1\\s+2\\s+foobar\\s+unused\\s+\\[input\\]"
	output_regex_match "$sim1\\s+7\\s+foobar\\s+unused\\s+\\[input\\]"
}

@test "gpioinfo: with lines strictly by name" {
	# not suggesting this setup makes sense - just test that we can deal with it
	gpiosim_chip sim0 num_lines=8 line_name=1:42 line_name=6:13
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	run_tool gpioinfo --by-name --chip $sim0 42 13

	status_is 0
	num_lines_is 2
	output_regex_match "$sim0\\s+1\\s+42\\s+unused\\s+\\[input\\]"
	output_regex_match "$sim0\\s+6\\s+13\\s+unused\\s+\\[input\\]"
}

@test "gpioinfo: with nonexistent chip" {
	run_tool gpioinfo --chip nonexistent-chip

	status_is 1
	output_regex_match \
".*cannot find a GPIO chip character device corresponding to nonexistent-chip"
}

@test "gpioinfo: with nonexistent line" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioinfo nonexistent-line

	status_is 1
	output_regex_match ".*cannot find line nonexistent-line"

	run_tool gpioinfo --chip ${GPIOSIM_CHIP_NAME[sim0]} nonexistent-line

	status_is 1
	output_regex_match ".*cannot find line nonexistent-line"
}

@test "gpioinfo: with offset out of range" {
	gpiosim_chip sim0 num_lines=4
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	run_tool gpioinfo --chip $sim0 0 1 2 3 4 5

	status_is 1
	num_lines_is 6
	output_regex_match "$sim0\\s+0\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "$sim0\\s+1\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "$sim0\\s+2\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "$sim0\\s+3\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match ".*offset 4 is out of range on chip $sim0"
	output_regex_match ".*offset 5 is out of range on chip $sim0"
}

#
# gpiofind test cases
#

@test "gpiofind: by name" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=3:bar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	run_tool gpiofind foobar

	status_is 0
	output_is "${GPIOSIM_CHIP_NAME[sim1]} 7"
}

@test "gpiofind: by chip and name" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar \
				      line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16
	local sim1=${GPIOSIM_CHIP_NAME[sim1]}

	run_tool gpiofind --chip $sim1 foobar

	status_is 0
	output_is "$sim1 2"
}

@test "gpiofind: first matching named line" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar \
				      line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	run_tool gpiofind foobar

	status_is 0
	output_is "${GPIOSIM_CHIP_NAME[sim0]} 3"
}

@test "gpiofind: with info" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=3:bar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	run_tool gpiofind --info baz

	status_is 0
	num_lines_is 1
	output_regex_match "${GPIOSIM_CHIP_NAME[sim1]}\\s+0\\s+baz\\s+unused\\s+\\[input\\]"
}

@test "gpiofind: all lines matching name" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar \
				      line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}
	local sim1=${GPIOSIM_CHIP_NAME[sim1]}

	run_tool gpiofind --strict foobar

	status_is 1
	num_lines_is 3
	output_regex_match "$sim0\\s+3"
	output_regex_match "$sim1\\s+2"
	output_regex_match "$sim1\\s+7"
}

@test "gpiofind: with nonexistent chip" {
	run_tool gpiofind --chip nonexistant-chip 0

	status_is 1
	output_regex_match \
".*cannot find a GPIO chip character device corresponding to nonexistant-chip"
}

@test "gpiofind: with nonexistent line" {
	gpiosim_chip sim0 num_lines=4
	gpiosim_chip sim1 num_lines=8
	gpiosim_chip sim2 num_lines=16

	run_tool gpiofind nonexistent-line

	status_is 1
	output_regex_match ".*cannot find line nonexistent-line"
}

#
# gpioget test cases
#

@test "gpioget: by name" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo

	gpiosim_set_pull sim0 1 pull-up

	run_tool gpioget foo

	status_is 0
	output_is "foo=active"
}

@test "gpioget: by offset" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 1 pull-up

	run_tool gpioget --chip ${GPIOSIM_CHIP_NAME[sim0]} 1

	status_is 0
	output_is "1=active"
}

@test "gpioget: by symlink" {
	gpiosim_chip sim0 num_lines=8
	gpiosim_chip_symlink sim0 .

	gpiosim_set_pull sim0 1 pull-up

	run_tool gpioget --chip $GPIOSIM_CHIP_LINK 1

	status_is 0
	output_is "1=active"
}

@test "gpioget: by chip and name" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo
	gpiosim_chip sim1 num_lines=8 line_name=3:foo

	gpiosim_set_pull sim1 3 pull-up

	run_tool gpioget --chip ${GPIOSIM_CHIP_NAME[sim1]} foo

	status_is 0
	output_is "foo=active"
}

@test "gpioget: first matching named line" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar \
				      line_name=3:foobar line_name=7:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar line_name=4:xyz
	gpiosim_chip sim2 num_lines=16

	gpiosim_set_pull sim0 3 pull-up

	run_tool gpioget foobar

	status_is 0
	output_is "foobar=active"
}

@test "gpioget: multiple lines" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 2 pull-up
	gpiosim_set_pull sim0 3 pull-up
	gpiosim_set_pull sim0 5 pull-up
	gpiosim_set_pull sim0 7 pull-up

	run_tool gpioget --chip ${GPIOSIM_CHIP_NAME[sim0]} 0 1 2 3 4 5 6 7

	status_is 0
	output_is "0=inactive 1=inactive 2=active 3=active 4=inactive 5=active 6=inactive 7=active"
}

@test "gpioget: multiple lines by name and offset" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo line_name=6:bar
	gpiosim_chip sim1 num_lines=8 line_name=1:baz line_name=3:bar
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	gpiosim_set_pull sim0 1 pull-up
	gpiosim_set_pull sim0 4 pull-up
	gpiosim_set_pull sim0 6 pull-up

	run_tool gpioget --chip $sim0 0 foo 4 bar

	status_is 0
	output_is "0=inactive foo=active 4=active bar=active"
}

@test "gpioget: multiple lines across multiple chips" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=4:xyz

	gpiosim_set_pull sim0 1 pull-up
	gpiosim_set_pull sim1 4 pull-up

	run_tool gpioget baz bar foo xyz

	status_is 0
	output_is "baz=inactive bar=inactive foo=active xyz=active"
}

@test "gpioget: with numeric values" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 2 pull-up
	gpiosim_set_pull sim0 3 pull-up
	gpiosim_set_pull sim0 5 pull-up
	gpiosim_set_pull sim0 7 pull-up

	run_tool gpioget --numeric --chip ${GPIOSIM_CHIP_NAME[sim0]} 0 1 2 3 4 5 6 7

	status_is 0
	output_is "0 0 1 1 0 1 0 1"
}

@test "gpioget: with active-low" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 2 pull-up
	gpiosim_set_pull sim0 3 pull-up
	gpiosim_set_pull sim0 5 pull-up
	gpiosim_set_pull sim0 7 pull-up

	run_tool gpioget --active-low --chip ${GPIOSIM_CHIP_NAME[sim0]} 0 1 2 3 4 5 6 7

	status_is 0
	output_is "0=active 1=active 2=inactive 3=inactive 4=active 5=inactive 6=active 7=inactive"
}

@test "gpioget: with pull-up" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 2 pull-up
	gpiosim_set_pull sim0 3 pull-up
	gpiosim_set_pull sim0 5 pull-up
	gpiosim_set_pull sim0 7 pull-up

	run_tool gpioget --bias=pull-up --chip ${GPIOSIM_CHIP_NAME[sim0]} 0 1 2 3 4 5 6 7

	status_is 0
	output_is "0=active 1=active 2=active 3=active 4=active 5=active 6=active 7=active"
}

@test "gpioget: with pull-down" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 2 pull-up
	gpiosim_set_pull sim0 3 pull-up
	gpiosim_set_pull sim0 5 pull-up
	gpiosim_set_pull sim0 7 pull-up

	run_tool gpioget --bias=pull-down --chip ${GPIOSIM_CHIP_NAME[sim0]} 0 1 2 3 4 5 6 7

	status_is 0
	output_is \
"0=inactive 1=inactive 2=inactive 3=inactive 4=inactive 5=inactive 6=inactive 7=inactive"
}

@test "gpioget: with direction as-is" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	# flip to output
	run_tool gpioset foo=1

	status_is 0

	run_tool gpioinfo foo
	status_is 0
	output_regex_match "$sim0\\s+1\\s+foo\\s+unused\\s+\\[output\\]"

	run_tool gpioget --as-is foo
	status_is 0
	# note gpio-sim reverts line to its pull when released
	output_is "foo=inactive"

	run_tool gpioinfo foo
	status_is 0
	output_regex_match "$sim0\\s+1\\s+foo\\s+unused\\s+\\[output\\]"

	# whereas the default behaviour forces to input
	run_tool gpioget foo
	status_is 0
	# note gpio-sim reverts line to its pull when released (defaults to pull-down)
	output_is "foo=inactive"

	run_tool gpioinfo foo
	status_is 0
	output_regex_match "$sim0\\s+1\\s+foo\\s+unused\\s+\\[input\\]"
}

@test "gpioget: with hold-period" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo

	# only test parsing - testing the hold-period itself is tricky
	run_tool gpioget --hold-period=100ms foo
	status_is 0
	output_is "foo=inactive"
}

@test "gpioget: with strict named line check" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar \
				      line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	run_tool gpioget --strict foobar

	status_is 1
	output_regex_match ".*line foobar is not unique"
}

@test "gpioget: with lines strictly by name" {
	# not suggesting this setup makes sense - just test that we can deal with it
	gpiosim_chip sim0 num_lines=8 line_name=1:42 line_name=6:13

	gpiosim_set_pull sim0 1 pull-up
	gpiosim_set_pull sim0 6 pull-down

	run_tool gpioget --by-name --chip ${GPIOSIM_CHIP_NAME[sim0]} 42 13

	status_is 0
	output_is "42=active 13=inactive"
}

@test "gpioget: with no arguments" {
	run_tool gpioget

	status_is 1
	output_regex_match ".*at least one GPIO line must be specified"
}

@test "gpioget: with chip but no line specified" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioget --chip ${GPIOSIM_CHIP_NAME[sim0]}

	status_is 1
	output_regex_match ".*at least one GPIO line must be specified"
}

@test "gpioget: with offset out of range" {
	gpiosim_chip sim0 num_lines=4
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	run_tool gpioget --chip $sim0 0 1 2 3 4 5

	status_is 1
	output_regex_match ".*offset 4 is out of range on chip $sim0"
	output_regex_match ".*offset 5 is out of range on chip $sim0"
}

@test "gpioget: with nonexistent line" {
	run_tool gpioget nonexistent-line

	status_is 1
	output_regex_match ".*cannot find line nonexistent-line"
}

@test "gpioget: with same line twice" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	# by offset
	run_tool gpioget --chip $sim0 0 0

	status_is 1
	output_regex_match ".*lines 0 and 0 are the same"

	# by name
	run_tool gpioget foo foo

	status_is 1
	output_regex_match ".*lines foo and foo are the same"

	# by chip and name
	run_tool gpioget --chip $sim0 foo foo

	status_is 1
	output_regex_match ".*lines foo and foo are the same"
}

@test "gpioget: with invalid bias" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioget --bias=bad --chip ${GPIOSIM_CHIP_NAME[sim0]} 0 1

	status_is 1
	output_regex_match ".*invalid bias.*"
}

@test "gpioget: with invalid hold-period" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioget --hold-period=bad --chip ${GPIOSIM_CHIP_NAME[sim0]} 0

	status_is 1
	output_regex_match ".*invalid period.*"
}

#
# gpioset test cases
#

@test "gpioset: by name" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo

	dut_run gpioset -i foo=1

	gpiosim_check_value sim0 1 1
}

@test "gpioset: by offset" {
	gpiosim_chip sim0 num_lines=8

	dut_run gpioset -i --chip ${GPIOSIM_CHIP_NAME[sim0]} 1=1

	gpiosim_check_value sim0 1 1
}

@test "gpioset: by symlink" {
	gpiosim_chip sim0 num_lines=8
	gpiosim_chip_symlink sim0 .

	dut_run gpioset -i --chip $GPIOSIM_CHIP_LINK 1=1

	gpiosim_check_value sim0 1 1
}

@test "gpioset: by chip and name" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo
	gpiosim_chip sim1 num_lines=8 line_name=3:foo

	dut_run gpioset -i --chip ${GPIOSIM_CHIP_NAME[sim1]} foo=1

	gpiosim_check_value sim1 3 1
}

@test "gpioset: first matching named line" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar \
				      line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	dut_run gpioset -i foobar=1

	gpiosim_check_value sim0 3 1
}

@test "gpioset: multiple lines" {
	gpiosim_chip sim0 num_lines=8

	dut_run gpioset -i --chip ${GPIOSIM_CHIP_NAME[sim0]} 0=0 1=0 2=1 3=1 4=1 5=1 6=0 7=1

	gpiosim_check_value sim0 0 0
	gpiosim_check_value sim0 1 0
	gpiosim_check_value sim0 2 1
	gpiosim_check_value sim0 3 1
	gpiosim_check_value sim0 4 1
	gpiosim_check_value sim0 5 1
	gpiosim_check_value sim0 6 0
	gpiosim_check_value sim0 7 1
}

@test "gpioset: multiple lines by name and offset" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar

	dut_run gpioset -i --chip ${GPIOSIM_CHIP_NAME[sim0]} 0=1 foo=1 bar=1 3=1

	gpiosim_check_value sim0 0 1
	gpiosim_check_value sim0 1 1
	gpiosim_check_value sim0 2 1
	gpiosim_check_value sim0 3 1
}


@test "gpioset: multiple lines across multiple chips" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=4:xyz

	dut_run gpioset -i foo=1 bar=1 baz=1 xyz=1

	gpiosim_check_value sim0 1 1
	gpiosim_check_value sim0 2 1
	gpiosim_check_value sim1 0 1
	gpiosim_check_value sim1 4 1
}

@test "gpioset: with active-low" {
	gpiosim_chip sim0 num_lines=8
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	dut_run gpioset -i --active-low --chip $sim0 0=0 1=0 2=1 3=1 4=1 5=1 6=0 7=1

	gpiosim_check_value sim0 0 1
	gpiosim_check_value sim0 1 1
	gpiosim_check_value sim0 2 0
	gpiosim_check_value sim0 3 0
	gpiosim_check_value sim0 4 0
	gpiosim_check_value sim0 5 0
	gpiosim_check_value sim0 6 1
	gpiosim_check_value sim0 7 0
}

@test "gpioset: with push-pull" {
	gpiosim_chip sim0 num_lines=8

	dut_run gpioset -i --drive=push-pull --chip ${GPIOSIM_CHIP_NAME[sim0]} \
					0=0 1=0 2=1 3=1 4=1 5=1 6=0 7=1

	gpiosim_check_value sim0 0 0
	gpiosim_check_value sim0 1 0
	gpiosim_check_value sim0 2 1
	gpiosim_check_value sim0 3 1
	gpiosim_check_value sim0 4 1
	gpiosim_check_value sim0 5 1
	gpiosim_check_value sim0 6 0
	gpiosim_check_value sim0 7 1
}

@test "gpioset: with open-drain" {
	gpiosim_chip sim0 num_lines=8
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	gpiosim_set_pull sim0 2 pull-up
	gpiosim_set_pull sim0 3 pull-up
	gpiosim_set_pull sim0 5 pull-up
	gpiosim_set_pull sim0 7 pull-up

	dut_run gpioset -i --drive=open-drain --chip $sim0 0=0 1=0 2=1 3=1 4=1 5=1 6=0 7=1

	gpiosim_check_value sim0 0 0
	gpiosim_check_value sim0 1 0
	gpiosim_check_value sim0 2 1
	gpiosim_check_value sim0 3 1
	gpiosim_check_value sim0 4 0
	gpiosim_check_value sim0 5 1
	gpiosim_check_value sim0 6 0
	gpiosim_check_value sim0 7 1
}

@test "gpioset: with open-source" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 2 pull-up
	gpiosim_set_pull sim0 3 pull-up
	gpiosim_set_pull sim0 5 pull-up
	gpiosim_set_pull sim0 7 pull-up

	dut_run gpioset -i --drive=open-source --chip ${GPIOSIM_CHIP_NAME[sim0]} \
					0=0 1=0 2=1 3=0 4=1 5=1 6=0 7=1

	gpiosim_check_value sim0 0 0
	gpiosim_check_value sim0 1 0
	gpiosim_check_value sim0 2 1
	gpiosim_check_value sim0 3 1
	gpiosim_check_value sim0 4 1
	gpiosim_check_value sim0 5 1
	gpiosim_check_value sim0 6 0
	gpiosim_check_value sim0 7 1
}

@test "gpioset: with pull-up" {
	gpiosim_chip sim0 num_lines=8

	dut_run gpioset -i --bias=pull-up --drive=open-drain \
			--chip ${GPIOSIM_CHIP_NAME[sim0]} 0=0 1=0 2=1 3=0 4=1 5=1 6=0 7=1

	gpiosim_check_value sim0 0 0
	gpiosim_check_value sim0 1 0
	gpiosim_check_value sim0 2 1
	gpiosim_check_value sim0 3 0
	gpiosim_check_value sim0 4 1
	gpiosim_check_value sim0 5 1
	gpiosim_check_value sim0 6 0
	gpiosim_check_value sim0 7 1
}

@test "gpioset: with pull-down" {
	gpiosim_chip sim0 num_lines=8

	dut_run gpioset -i --bias=pull-down --drive=open-source \
			--chip ${GPIOSIM_CHIP_NAME[sim0]} 0=0 1=0 2=1 3=0 4=1 5=1 6=0 7=1

	gpiosim_check_value sim0 0 0
	gpiosim_check_value sim0 1 0
	gpiosim_check_value sim0 2 1
	gpiosim_check_value sim0 3 0
	gpiosim_check_value sim0 4 1
	gpiosim_check_value sim0 5 1
	gpiosim_check_value sim0 6 0
	gpiosim_check_value sim0 7 1
}

@test "gpioset: with value variants" {
	gpiosim_chip sim0 num_lines=8
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	gpiosim_set_pull sim0 0 pull-up
	gpiosim_set_pull sim0 1 pull-down
	gpiosim_set_pull sim0 2 pull-down
	gpiosim_set_pull sim0 3 pull-up
	gpiosim_set_pull sim0 4 pull-down
	gpiosim_set_pull sim0 5 pull-up
	gpiosim_set_pull sim0 6 pull-up
	gpiosim_set_pull sim0 7 pull-down

	dut_run gpioset -i --chip $sim0	0=0 1=1 2=active 3=inactive 4=on 5=off 6=false 7=true

	gpiosim_check_value sim0 0 0
	gpiosim_check_value sim0 1 1
	gpiosim_check_value sim0 2 1
	gpiosim_check_value sim0 3 0
	gpiosim_check_value sim0 4 1
	gpiosim_check_value sim0 5 0
	gpiosim_check_value sim0 6 0
	gpiosim_check_value sim0 7 1
}

@test "gpioset: with hold-period" {
	gpiosim_chip sim0 num_lines=8
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	gpiosim_set_pull sim0 5 pull-up

	# redirect as no startup output for dut_run
	dut_run_redirect gpioset --hold-period=1200ms --chip $sim0 0=1 5=0 7=1

	gpiosim_check_value sim0 0 1
	gpiosim_check_value sim0 5 0
	gpiosim_check_value sim0 7 1

	dut_wait

	status_is 0
}

@test "gpioset: interactive exit" {
	gpiosim_chip sim0 num_lines=8
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	dut_run gpioset --interactive --chip $sim0 1=0 2=1 5=1 6=0 7=1

	gpiosim_check_value sim0 1 0
	gpiosim_check_value sim0 2 1
	gpiosim_check_value sim0 5 1
	gpiosim_check_value sim0 6 0
	gpiosim_check_value sim0 7 1

	dut_write "exit"
	dut_wait

	status_is 0
}

@test "gpioset: interactive help" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo line_name=4:bar line_name=7:baz

	dut_run gpioset --interactive foo=1 bar=0 baz=0

	gpiosim_check_value sim0 1 1
	gpiosim_check_value sim0 4 0
	gpiosim_check_value sim0 7 0

	dut_write "help"

	dut_read
	output_regex_match "COMMANDS:.*"
	output_regex_match ".*get \[line\] \.\.\..*"
	output_regex_match ".*set <line=value> \.\.\..*"
	output_regex_match ".*toggle \[line\] \.\.\..*"
	output_regex_match ".*sleep <period>.*"
}

@test "gpioset: interactive get" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo line_name=4:bar line_name=7:baz

	dut_run gpioset --interactive foo=1 bar=0 baz=0

	gpiosim_check_value sim0 1 1
	gpiosim_check_value sim0 4 0
	gpiosim_check_value sim0 7 0

	dut_write "get"

	dut_read
	output_regex_match "\\s*foo=active\\s+bar=inactive\\s+baz=inactive\\s*"

	dut_write "get bar"

	dut_read
	output_regex_match "\\s*bar=inactive\\s*"
}

@test "gpioset: interactive set" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo line_name=4:bar line_name=7:baz

	dut_run gpioset --interactive foo=1 bar=0 baz=0

	gpiosim_check_value sim0 1 1
	gpiosim_check_value sim0 4 0
	gpiosim_check_value sim0 7 0

	dut_write "set bar=active"

	gpiosim_check_value sim0 1 1
	gpiosim_check_value sim0 4 1
	gpiosim_check_value sim0 7 0
	dut_write "get"
	dut_read
	output_regex_match "\\s*foo=active\\s+bar=active\\s+baz=inactive\\s*"
}

@test "gpioset: interactive toggle" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo line_name=4:bar line_name=7:baz

	gpiosim_set_pull sim0 4 pull-up
	gpiosim_set_pull sim0 7 pull-up

	dut_run gpioset --interactive foo=1 bar=0 baz=0

	gpiosim_check_value sim0 1 1
	gpiosim_check_value sim0 4 0
	gpiosim_check_value sim0 7 0

	dut_write "toggle"

	gpiosim_check_value sim0 1 0
	gpiosim_check_value sim0 4 1
	gpiosim_check_value sim0 7 1
	dut_write "get"
	dut_read
	output_regex_match "\\s*foo=inactive\\s+bar=active\\s+baz=active\\s*"

	dut_write "toggle baz"

	gpiosim_check_value sim0 1 0
	gpiosim_check_value sim0 4 1
	gpiosim_check_value sim0 7 0
	dut_write "get"
	dut_read
	output_regex_match "\\s*foo=inactive\\s+bar=active\\s+baz=inactive\\s*"
}

@test "gpioset: interactive sleep" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo line_name=4:bar line_name=7:baz

	dut_run gpioset --interactive foo=1 bar=0 baz=0

	dut_write "sleep 500ms"
	dut_flush

	assert_fail dut_readable

	sleep 1

	# prompt, but not a full line...
	dut_readable
}

@test "gpioset: toggle (continuous)" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo line_name=4:bar line_name=7:baz

	gpiosim_set_pull sim0 4 pull-up
	gpiosim_set_pull sim0 7 pull-up

	# redirect as gpioset has no banner
	dut_run_redirect gpioset --toggle 1s foo=1 bar=0 baz=0

	gpiosim_check_value sim0 1 1
	gpiosim_check_value sim0 4 0
	gpiosim_check_value sim0 7 0

	sleep 1

	gpiosim_check_value sim0 1 0
	gpiosim_check_value sim0 4 1
	gpiosim_check_value sim0 7 1

	sleep 1

	gpiosim_check_value sim0 1 1
	gpiosim_check_value sim0 4 0
	gpiosim_check_value sim0 7 0
}

@test "gpioset: toggle (terminated)" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo line_name=4:bar line_name=7:baz

	gpiosim_set_pull sim0 4 pull-up

	# redirect as gpioset has no banner
	# hold-period to allow test to sample before gpioset exits
	dut_run_redirect gpioset --toggle 1s,0 -p 600ms foo=1 bar=0 baz=1

	gpiosim_check_value sim0 1 1
	gpiosim_check_value sim0 4 0
	gpiosim_check_value sim0 7 1

	sleep 1

	gpiosim_check_value sim0 1 0
	gpiosim_check_value sim0 4 1
	gpiosim_check_value sim0 7 0

	dut_wait

	status_is 0
}

@test "gpioset: with invalid toggle period" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo line_name=4:bar line_name=7:baz

	run_tool gpioset --toggle 1ns foo=1 bar=0 baz=0

	status_is 1
	output_regex_match ".*invalid period.*"
}

@test "gpioset: with strict named line check" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar \
				      line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	run_tool gpioset --strict foobar=active

	status_is 1
	output_regex_match ".*line foobar is not unique"
}

@test "gpioset: with lines strictly by name" {
	# not suggesting this setup makes sense - just test that we can deal with it
	gpiosim_chip sim0 num_lines=8 line_name=1:42 line_name=6:13

	gpiosim_set_pull sim0 6 pull-up

	dut_run gpioset -i --by-name --chip ${GPIOSIM_CHIP_NAME[sim0]} 42=1 13=0

	gpiosim_check_value sim0 1 1
	gpiosim_check_value sim0 6 0
}

@test "gpioset: interactive after SIGINT" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo

	dut_run gpioset -i foo=1

	dut_kill -SIGINT
	dut_wait

	status_is 130
}

@test "gpioset: interactive after SIGTERM" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo

	dut_run gpioset -i foo=1

	dut_kill -SIGTERM
	dut_wait

	status_is 143
}

@test "gpioset: with no arguments" {
	run_tool gpioset

	status_is 1
	output_regex_match ".*at least one GPIO line value must be specified"
}

@test "gpioset: with chip but no line specified" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioset --chip ${GPIOSIM_CHIP_NAME[sim0]}

	status_is 1
	output_regex_match ".*at least one GPIO line value must be specified"
}

@test "gpioset: with offset out of range" {
	gpiosim_chip sim0 num_lines=4
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	run_tool gpioset --chip $sim0 0=1 1=1 2=1 3=1 4=1 5=1

	status_is 1
	output_regex_match ".*offset 4 is out of range on chip $sim0"
	output_regex_match ".*offset 5 is out of range on chip $sim0"
}

@test "gpioset: with invalid hold-period" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioset --hold-period=bad --chip ${GPIOSIM_CHIP_NAME[sim0]} 0=1

	status_is 1
	output_regex_match ".*invalid period.*"
}

@test "gpioset: with invalid value" {
	gpiosim_chip sim0 num_lines=8
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	# by name
	run_tool gpioset --chip $sim0 0=c

	status_is 1
	output_regex_match ".*invalid line value.*"

	# by value
	run_tool gpioset --chip $sim0 0=3

	status_is 1
	output_regex_match ".*invalid line value.*"
}

@test "gpioset: with invalid offset" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioset --chip ${GPIOSIM_CHIP_NAME[sim0]} 4000000000=0

	status_is 1
	output_regex_match ".*cannot find line 4000000000"
}

@test "gpioset: with invalid bias" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioset --bias=bad --chip ${GPIOSIM_CHIP_NAME[sim0]} 0=1 1=1

	status_is 1
	output_regex_match ".*invalid bias.*"
}

@test "gpioset: with invalid drive" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioset --drive=bad --chip ${GPIOSIM_CHIP_NAME[sim0]} 0=1 1=1

	status_is 1
	output_regex_match ".*invalid drive.*"
}

@test "gpioset: with daemonize and interactive" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioset --interactive --daemonize --chip ${GPIOSIM_CHIP_NAME[sim1]} 0=1

	status_is 1
	output_regex_match ".*can't combine daemonize with interactive"
}

@test "gpioset: with interactive and toggle" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioset --interactive --toggle 1s --chip ${GPIOSIM_CHIP_NAME[sim1]} 0=1

	status_is 1
	output_regex_match ".*can't combine interactive with toggle"
}

@test "gpioset: with nonexistent line" {
	run_tool gpioset nonexistent-line=0

	status_is 1
	output_regex_match ".*cannot find line nonexistent-line"
}

@test "gpioset: with same line twice" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	# by offset
	run_tool gpioset --chip $sim0 0=1 0=1

	status_is 1
	output_regex_match ".*lines 0 and 0 are the same"

	# by name
	run_tool gpioset --chip $sim0 foo=1 foo=1

	status_is 1
	output_regex_match ".*lines foo and foo are the same"

	# by name and offset
	run_tool gpioset --chip $sim0 foo=1 1=1

	status_is 1
	output_regex_match ".*lines foo and 1 are the same"
}

#
# gpiomon test cases
#

@test "gpiomon: by name" {
	gpiosim_chip sim0 num_lines=8 line_name=4:foo

	gpiosim_set_pull sim0 4 pull-up

	dut_run gpiomon --banner --edge=rising foo
	dut_flush

	gpiosim_set_pull sim0 4 pull-down
	gpiosim_set_pull sim0 4 pull-up
	gpiosim_set_pull sim0 4 pull-down
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RISING\\s+foo"
	assert_fail dut_readable
}

@test "gpiomon: by offset" {
	gpiosim_chip sim0 num_lines=8
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	gpiosim_set_pull sim0 4 pull-up

	dut_run gpiomon --banner --edge=rising --chip $sim0 4
	dut_regex_match "Monitoring line .*"

	gpiosim_set_pull sim0 4 pull-down
	gpiosim_set_pull sim0 4 pull-up
	gpiosim_set_pull sim0 4 pull-down
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RISING\\s+chip: $sim0\\s+offset:\\s+4"
	assert_fail dut_readable
}

@test "gpiomon: by symlink" {
	gpiosim_chip sim0 num_lines=8
	gpiosim_chip_symlink sim0 .

	gpiosim_set_pull sim0 4 pull-up

	dut_run gpiomon --banner --edge=rising --chip $GPIOSIM_CHIP_LINK 4
	dut_regex_match "Monitoring line .*"

	gpiosim_set_pull sim0 4 pull-down
	gpiosim_set_pull sim0 4 pull-up
	gpiosim_set_pull sim0 4 pull-down
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RISING\\s+chip: $GPIOSIM_CHIP_LINK\\s+offset:\\s+4"
	assert_fail dut_readable
}


@test "gpiomon: by chip and name" {
	gpiosim_chip sim0 num_lines=8 line_name=0:foo
	gpiosim_chip sim1 num_lines=8 line_name=2:foo
	local sim1=${GPIOSIM_CHIP_NAME[sim1]}

	gpiosim_set_pull sim1 0 pull-up

	dut_run gpiomon --banner --edge=rising --chip $sim1 foo
	dut_regex_match "Monitoring line .*"

	gpiosim_set_pull sim1 2 pull-down
	gpiosim_set_pull sim1 2 pull-up
	gpiosim_set_pull sim1 2 pull-down
	dut_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+RISING\\s+chip: $sim1\\s+offset:\\s+2\\s+name:\\s+foo"
	assert_fail dut_readable
}

@test "gpiomon: first matching named line" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar \
				      line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	dut_run gpiomon --banner foobar
	dut_regex_match "Monitoring line .*"

	gpiosim_set_pull sim0 3 pull-up
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RISING\\s+foobar"
	assert_fail dut_readable
}

@test "gpiomon: rising edge" {
	gpiosim_chip sim0 num_lines=8
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	gpiosim_set_pull sim0 4 pull-up

	dut_run gpiomon --banner --edge=rising --chip $sim0 4
	dut_flush

	gpiosim_set_pull sim0 4 pull-down
	gpiosim_set_pull sim0 4 pull-up
	gpiosim_set_pull sim0 4 pull-down
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RISING\\s+chip: $sim0\\s+offset:\\s+4"
	assert_fail dut_readable
}

@test "gpiomon: falling edge" {
	gpiosim_chip sim0 num_lines=8
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	gpiosim_set_pull sim0 4 pull-down

	dut_run gpiomon --banner --edge=falling --chip $sim0 4
	dut_flush

	gpiosim_set_pull sim0 4 pull-up
	gpiosim_set_pull sim0 4 pull-down
	gpiosim_set_pull sim0 4 pull-up
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+FALLING\\s+chip: $sim0\\s+offset:\\s+4"
	assert_fail dut_readable
}

@test "gpiomon: both edges" {
	gpiosim_chip sim0 num_lines=8
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	dut_run gpiomon --banner --edge=both --chip $sim0 4
	dut_regex_match "Monitoring line .*"

	gpiosim_set_pull sim0 4 pull-up
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RISING\\s+chip: $sim0\\s+offset:\\s+4"

	gpiosim_set_pull sim0 4 pull-down
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+FALLING\\s+chip: $sim0\\s+offset:\\s+4"
}

@test "gpiomon: with pull-up" {
	gpiosim_chip sim0 num_lines=8
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	gpiosim_set_pull sim0 4 pull-down

	dut_run gpiomon --banner --bias=pull-up --chip $sim0 4
	dut_flush

	gpiosim_set_pull sim0 4 pull-down
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+FALLING\\s+chip: $sim0\\s+offset:\\s+4"
	assert_fail dut_readable
}

@test "gpiomon: with pull-down" {
	gpiosim_chip sim0 num_lines=8
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	gpiosim_set_pull sim0 4 pull-up

	dut_run gpiomon --banner --bias=pull-down --chip $sim0 4
	dut_flush

	gpiosim_set_pull sim0 4 pull-up

	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RISING\\s+chip: $sim0\\s+offset:\\s+4"
	assert_fail dut_readable
}

@test "gpiomon: with active-low" {
	gpiosim_chip sim0 num_lines=8
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	gpiosim_set_pull sim0 4 pull-up

	dut_run gpiomon --banner --active-low --chip $sim0 4
	dut_flush

	gpiosim_set_pull sim0 4 pull-down
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RISING\\s+chip: $sim0\\s+offset:\\s+4"
	gpiosim_set_pull sim0 4 pull-up
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+FALLING\\s+chip: $sim0\\s+offset:\\s+4"
	assert_fail dut_readable
}

@test "gpiomon: with quiet mode" {
	gpiosim_chip sim0 num_lines=8

	dut_run gpiomon --banner --edge=rising --quiet --chip ${GPIOSIM_CHIP_NAME[sim0]} 4
	dut_flush

	gpiosim_set_pull sim0 4 pull-up
	assert_fail dut_readable
}

@test "gpiomon: with num-events" {
	gpiosim_chip sim0 num_lines=8
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	# redirect, as gpiomon exits after 4 events
	dut_run_redirect gpiomon --num-events=4 --chip $sim0 4

	gpiosim_set_pull sim0 4 pull-up
	gpiosim_set_pull sim0 4 pull-down
	gpiosim_set_pull sim0 4 pull-up
	gpiosim_set_pull sim0 4 pull-down

	dut_wait
	dut_read_redirect

	num_lines_is 4

	regex_matches "[0-9]+\.[0-9]+\\s+RISING\\s+chip:\\s+$sim0\\s+offset:\\s+4" "${lines[0]}"
	regex_matches "[0-9]+\.[0-9]+\\s+FALLING\\s+chip:\\s+$sim0\\s+offset:\\s+4" "${lines[1]}"
	regex_matches "[0-9]+\.[0-9]+\\s+RISING\\s+chip:\\s+$sim0\\s+offset:\\s+4" "${lines[2]}"
	regex_matches "[0-9]+\.[0-9]+\\s+FALLING\\s+chip:\\s+$sim0\\s+offset:\\s+4" "${lines[3]}"
}

@test "gpiomon: multiple lines" {
	gpiosim_chip sim0 num_lines=8

	dut_run gpiomon --banner --format=%o --chip ${GPIOSIM_CHIP_NAME[sim0]} 1 3 2 5 4
	dut_regex_match "Monitoring lines .*"

	gpiosim_set_pull sim0 2 pull-up
	dut_regex_match "2"
	gpiosim_set_pull sim0 3 pull-up
	dut_regex_match "3"
	gpiosim_set_pull sim0 4 pull-up
	dut_regex_match "4"

	assert_fail dut_readable
}

@test "gpiomon: multiple lines by name and offset" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar

	dut_run gpiomon --banner --format=%o --chip ${GPIOSIM_CHIP_NAME[sim0]} foo bar 3
	dut_regex_match "Monitoring lines .*"

	gpiosim_set_pull sim0 2 pull-up
	dut_regex_match "2"
	gpiosim_set_pull sim0 3 pull-up
	dut_regex_match "3"
	gpiosim_set_pull sim0 1 pull-up
	dut_regex_match "1"

	assert_fail dut_readable
}

@test "gpiomon: multiple lines across multiple chips" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=4:xyz

	dut_run gpiomon --banner --format=%l foo bar baz
	dut_regex_match "Monitoring lines .*"

	gpiosim_set_pull sim0 2 pull-up
	dut_regex_match "bar"
	gpiosim_set_pull sim1 0 pull-up
	dut_regex_match "baz"
	gpiosim_set_pull sim0 1 pull-up
	dut_regex_match "foo"

	assert_fail dut_readable
}

@test "gpiomon: exit after SIGINT" {
	gpiosim_chip sim0 num_lines=8

	dut_run gpiomon --banner --chip ${GPIOSIM_CHIP_NAME[sim0]} 4
	dut_regex_match "Monitoring line .*"

	dut_kill -SIGINT
	dut_wait

	status_is 130
}

@test "gpiomon: exit after SIGTERM" {
	gpiosim_chip sim0 num_lines=8

	dut_run gpiomon --banner --chip ${GPIOSIM_CHIP_NAME[sim0]} 4
	dut_regex_match "Monitoring line .*"

	dut_kill -SIGTERM
	dut_wait

	status_is 143
}

@test "gpiomon: with nonexistent line" {
	run_tool gpiomon nonexistent-line

	status_is 1
	output_regex_match ".*cannot find line nonexistent-line"
}

@test "gpiomon: with same line twice" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	# by offset
	run_tool gpiomon --chip $sim0 0 0

	status_is 1
	output_regex_match ".*lines 0 and 0 are the same"

	# by name
	run_tool gpiomon foo foo

	status_is 1
	output_regex_match ".*lines foo and foo are the same"

	# by name and offset
	run_tool gpiomon --chip $sim0 1 foo

	status_is 1
	output_regex_match ".*lines 1 and foo are the same"
}

@test "gpiomon: with strict named line check" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar \
				      line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	run_tool gpiomon --strict foobar

	status_is 1
	output_regex_match ".*line foobar is not unique"
}

@test "gpiomon: with lines strictly by name" {
	# not suggesting this setup makes sense - just test that we can deal with it
	gpiosim_chip sim0 num_lines=8 line_name=1:42 line_name=6:13
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	gpiosim_set_pull sim0 1 pull-up

	dut_run gpiomon --banner --by-name --chip $sim0 42 13
	dut_flush

	gpiosim_set_pull sim0 1 pull-down
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+FALLING\\s+chip: $sim0\\s+offset:\\s+1"
	gpiosim_set_pull sim0 1 pull-up
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RISING\\s+chip: $sim0\\s+offset:\\s+1"
	gpiosim_set_pull sim0 6 pull-up
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RISING\\s+chip: $sim0\\s+offset:\\s+6"
	gpiosim_set_pull sim0 6 pull-down
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+FALLING\\s+chip: $sim0\\s+offset:\\s+6"
	assert_fail dut_readable
}

@test "gpiomon: with no arguments" {
	run_tool gpiomon

	status_is 1
	output_regex_match ".*at least one GPIO line must be specified"
}

@test "gpiomon: with no line specified" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpiomon --chip ${GPIOSIM_CHIP_NAME[sim0]}

	status_is 1
	output_regex_match ".*at least one GPIO line must be specified"
}

@test "gpiomon: with offset out of range" {
	gpiosim_chip sim0 num_lines=4

	run_tool gpiomon --chip ${GPIOSIM_CHIP_NAME[sim0]} 5

	status_is 1
	output_regex_match ".*offset 5 is out of range on chip ${GPIOSIM_CHIP_NAME[sim0]}"
}

@test "gpiomon: with invalid bias" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpiomon --bias=bad -c ${GPIOSIM_CHIP_NAME[sim0]} 0 1

	status_is 1
	output_regex_match ".*invalid bias.*"
}

@test "gpiomon: with custom format (event type + offset)" {
	gpiosim_chip sim0 num_lines=8

	dut_run gpiomon --banner "--format=%e %o" -c ${GPIOSIM_CHIP_NAME[sim0]} 4
	dut_flush

	gpiosim_set_pull sim0 4 pull-up
	dut_read
	output_is "1 4"
}

@test "gpiomon: with custom format (event type + offset joined)" {
	gpiosim_chip sim0 num_lines=8

	dut_run gpiomon --banner "--format=%e%o" -c ${GPIOSIM_CHIP_NAME[sim0]} 4
	dut_flush

	gpiosim_set_pull sim0 4 pull-up
	dut_read
	output_is "14"
}

@test "gpiomon: with custom format (format menagerie)" {
	gpiosim_chip sim0 num_lines=8 line_name=4:baz

	dut_run gpiomon --banner "--format=%e %o %E %l %c %T" --utc -c ${GPIOSIM_CHIP_NAME[sim0]} baz
	dut_flush

	gpiosim_set_pull sim0 4 pull-up
	dut_regex_match \
"1 4 rising baz /dev/gpiochip[0-9]+ [0-9][0-9][0-9][0-9]-[0-1][0-9]-[0-3][0-9]T[0-1][0-9]:[0-5][0-9]:[0-5][0-9]\\.[0-9]+Z"
}

@test "gpiomon: with custom format (timestamp)" {
	gpiosim_chip sim0 num_lines=8

	dut_run gpiomon --banner "--format=%e %o %s.%n" -c ${GPIOSIM_CHIP_NAME[sim0]} 4
	dut_flush

	gpiosim_set_pull sim0 4 pull-up
	dut_regex_match "1 4 [0-9]+\\.[0-9]+"
}

@test "gpiomon: with custom format (double percent sign)" {
	gpiosim_chip sim0 num_lines=8

	dut_run gpiomon --banner "--format=start%%end" -c ${GPIOSIM_CHIP_NAME[sim0]} 4
	dut_flush

	gpiosim_set_pull sim0 4 pull-up
	dut_read
	output_is "start%end"
}

@test "gpiomon: with custom format (double percent sign + event type specifier)" {
	gpiosim_chip sim0 num_lines=8

	dut_run gpiomon --banner "--format=%%e" -c ${GPIOSIM_CHIP_NAME[sim0]} 4
	dut_flush

	gpiosim_set_pull sim0 4 pull-up
	dut_read
	output_is "%e"
}

@test "gpiomon: with custom format (single percent sign)" {
	gpiosim_chip sim0 num_lines=8

	dut_run gpiomon --banner "--format=%" -c ${GPIOSIM_CHIP_NAME[sim0]} 4
	dut_flush

	gpiosim_set_pull sim0 4 pull-up
	dut_read
	output_is "%"
}

@test "gpiomon: with custom format (single percent sign between other characters)" {
	gpiosim_chip sim0 num_lines=8

	dut_run gpiomon --banner "--format=foo % bar" -c ${GPIOSIM_CHIP_NAME[sim0]} 4
	dut_flush

	gpiosim_set_pull sim0 4 pull-up
	dut_read
	output_is "foo % bar"
}

@test "gpiomon: with custom format (unknown specifier)" {
	gpiosim_chip sim0 num_lines=8

	dut_run gpiomon --banner "--format=%x" -c ${GPIOSIM_CHIP_NAME[sim0]} 4
	dut_flush

	gpiosim_set_pull sim0 4 pull-up
	dut_read
	output_is "%x"
}

#
# gpiowatch test cases
#

@test "gpiowatch: by name" {
	gpiosim_chip sim0 num_lines=8 line_name=4:foo
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	dut_run gpiowatch --banner foo
	dut_regex_match "Watching line .*"

	request_release_line $sim0 4

	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+foo\\s+.*"
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+foo\\s+.*"
	# tools currently have no way to generate a RECONFIG event
}

@test "gpiowatch: by offset" {
	gpiosim_chip sim0 num_lines=8
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	dut_run gpiowatch --banner --chip $sim0 4
	dut_regex_match "Watching line .*"

	request_release_line $sim0 4
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+$sim0\\s+4\\s+.*"
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+$sim0\\s+4\\s+.*"

	assert_fail dut_readable
}

@test "gpiowatch: by symlink" {
	gpiosim_chip sim0 num_lines=8
	gpiosim_chip_symlink sim0 .
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	dut_run gpiowatch --banner --chip $GPIOSIM_CHIP_LINK 4
	dut_regex_match "Watching line .*"

	request_release_line $sim0 4
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+$GPIOSIM_CHIP_LINK\\s+4\\s+.*"
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+$GPIOSIM_CHIP_LINK\\s+4\\s+.*"

	assert_fail dut_readable
}

@test "gpiowatch: by chip and name" {
	gpiosim_chip sim0 num_lines=8 line_name=4:foo
	gpiosim_chip sim1 num_lines=8 line_name=2:foo
	local sim1=${GPIOSIM_CHIP_NAME[sim1]}

	dut_run gpiowatch --banner --chip $sim1 foo
	dut_regex_match "Watching line .*"

	request_release_line $sim1 2
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+$sim1\\s+2\\s+foo\\s+.*"
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+$sim1\\s+2\\s+foo\\s+.*"

	assert_fail dut_readable
}

@test "gpiowatch: first matching named line" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar \
				      line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	dut_run gpiowatch --banner foobar
	dut_regex_match "Watching line .*"

	request_release_line ${GPIOSIM_CHIP_NAME[sim0]} 3
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+foobar\\s+.*"
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+foobar\\s+.*"

	assert_fail dut_readable
}

@test "gpiowatch: multiple lines" {
	gpiosim_chip sim0 num_lines=8
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	dut_run gpiowatch --banner --chip $sim0 1 2 3 4 5
	dut_regex_match "Watching lines .*"

	request_release_line $sim0 2
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+$sim0\\s+2\\s+.*"
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+$sim0\\s+2\\s+.*"

	request_release_line $sim0 3
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+$sim0\\s+3\\s+.*"
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+$sim0\\s+3\\s+.*"

	request_release_line $sim0 4
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+$sim0\\s+4\\s+.*"
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+$sim0\\s+4\\s+.*"

	assert_fail dut_readable
}

@test "gpiowatch: multiple lines by name and offset" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	dut_run gpiowatch --banner --chip $sim0 bar foo 3
	dut_regex_match "Watching lines .*"

	request_release_line $sim0 2
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+$sim0\\s+2\\s+bar\\s+.*"
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+$sim0\\s+2\\s+bar\\s+.*"

	request_release_line $sim0 1
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+$sim0\\s+1\\s+foo\\s+.*"
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+$sim0\\s+1\\s+foo\\s+.*"

	request_release_line $sim0 3
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+$sim0\\s+3\\s+unnamed\\s+.*"
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+$sim0\\s+3\\s+unnamed\\s+.*"

	assert_fail dut_readable
}

@test "gpiowatch: multiple lines across multiple chips" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=4:xyz
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}
	local sim1=${GPIOSIM_CHIP_NAME[sim1]}

	dut_run gpiowatch --banner baz bar foo xyz
	dut_regex_match "Watching lines .*"

	request_release_line $sim0 2
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+bar\\s+.*"
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+bar\\s+.*"

	request_release_line $sim0 1
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+foo\\s+.*"
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+foo\\s+.*"

	request_release_line $sim1 4
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+xyz\\s+.*"
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+xyz\\s+.*"

	request_release_line $sim1 0
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+baz\\s+.*"
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+baz\\s+.*"

	assert_fail dut_readable
}

@test "gpiowatch: exit after SIGINT" {
	gpiosim_chip sim0 num_lines=8

	dut_run gpiowatch --banner --chip ${GPIOSIM_CHIP_NAME[sim0]} 4
	dut_regex_match "Watching line .*"

	dut_kill -SIGINT
	dut_wait

	status_is 130
}

@test "gpiowatch: exit after SIGTERM" {
	gpiosim_chip sim0 num_lines=8

	dut_run gpiowatch --banner --chip ${GPIOSIM_CHIP_NAME[sim0]} 4
	dut_regex_match "Watching line .*"

	dut_kill -SIGTERM
	dut_wait

	status_is 143
}

@test "gpiowatch: with nonexistent line" {
	run_tool gpiowatch nonexistent-line

	status_is 1
	output_regex_match ".*cannot find line nonexistent-line"
}

@test "gpiowatch: with same line twice" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	# by offset
	run_tool gpiowatch --chip $sim0 0 0

	status_is 1
	num_lines_is 1
	output_regex_match ".*lines 0 and 0 are the same"

	# by name
	run_tool gpiowatch --chip $sim0 foo foo

	status_is 1
	num_lines_is 1
	output_regex_match ".*lines foo and foo are the same"

	# by name and offset
	run_tool gpiowatch --chip $sim0 1 foo

	status_is 1
	num_lines_is 1
	output_regex_match ".*lines 1 and foo are the same"
}

@test "gpiowatch: with strict named line check" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar \
				      line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	run_tool gpiowatch --strict foobar

	status_is 1
	output_regex_match ".*line foobar is not unique"
}

@test "gpiowatch: with lines strictly by name" {
	# not suggesting this setup makes sense - just test that we can deal with it
	gpiosim_chip sim0 num_lines=8 line_name=1:42 line_name=6:13
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	dut_run gpiowatch --banner --by-name --chip $sim0 42 13
	dut_flush

	request_release_line $sim0 1
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+$sim0\\s+1\\s+.*"
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+$sim0\\s+1\\s+.*"

	request_release_line $sim0 6
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+$sim0\\s+6\\s+.*"
	dut_regex_match "\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+$sim0\\s+6\\s+.*"

	assert_fail dut_readable
}

@test "gpiowatch: with no arguments" {
	run_tool gpiowatch

	status_is 1
	output_regex_match ".*at least one GPIO line must be specified"
}

@test "gpiowatch: with no line specified" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpiowatch --chip ${GPIOSIM_CHIP_NAME[sim0]}

	status_is 1
	output_regex_match ".*at least one GPIO line must be specified"
}

@test "gpiowatch: with offset out of range" {
	gpiosim_chip sim0 num_lines=4
	local sim0=${GPIOSIM_CHIP_NAME[sim0]}

	run_tool gpiowatch --chip $sim0 5

	status_is 1
	output_regex_match ".*offset 5 is out of range on chip $sim0"
}

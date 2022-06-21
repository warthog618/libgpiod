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
GPIOSIM_CONFIGFS="/sys/kernel/config/gpio-sim"
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

gpiosim_chip() {
	local VAR=$1
	local NAME=gpio-tools-test-$$-${VAR}
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

	cat $GPIOSIM_CONFIGFS/$NAME/bank0/chip_name
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

	GPIOSIM_CHIPS=""
}

run_tool() {
	# Executables to test are expected to be in the same directory as the
	# testing script.
	run timeout 10s $BATS_TEST_DIRNAME/"$@"
}

coproc_run_tool() {
	rm -f $COPROC_OUTPUT
	coproc timeout 10s $BATS_TEST_DIRNAME/"$@" > $COPROC_OUTPUT 2>&1
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
	unset COPROC_SAVED_PID
}

coproc_tool_cleanup() {
        if [ -n "$COPROC_SAVED_PID" ]
        then
                kill -9 $COPROC_SAVED_PID
                wait $COPROC_SAVED_PID
                unset COPROC_SAVED_PID
        fi
}

teardown() {
	coproc_tool_cleanup
	gpiosim_cleanup
}

request_release_line() {
	local CHIP=$1
	local LINE=$2
	$BATS_TEST_DIRNAME/gpioget -c $CHIP $LINE
}

#
# gpiodetect test cases
#

@test "gpiodetect: list all chips" {
	gpiosim_chip sim0 num_lines=4
	gpiosim_chip sim1 num_lines=8
	gpiosim_chip sim2 num_lines=16

	run_tool gpiodetect

	test "$status" -eq 0
	output_contains_line "$(gpiosim_chip_name sim0) [$(gpiosim_dev_name sim0)-node0] (4 lines)"
	output_contains_line "$(gpiosim_chip_name sim1) [$(gpiosim_dev_name sim1)-node0] (8 lines)"
	output_contains_line "$(gpiosim_chip_name sim2) [$(gpiosim_dev_name sim2)-node0] (16 lines)"
}

@test "gpiodetect: list some chips" {
	gpiosim_chip sim0 num_lines=4
	gpiosim_chip sim1 num_lines=8
	gpiosim_chip sim2 num_lines=16

	run_tool gpiodetect $(gpiosim_chip_name sim0)

	test "$status" -eq 0
	output_contains_line "$(gpiosim_chip_name sim0) [$(gpiosim_dev_name sim0)-node0] (4 lines)"
	assert_fail output_contains_line "$(gpiosim_chip_name sim1) [$(gpiosim_dev_name sim1)-node0] (8 lines)"
	assert_fail output_contains_line "$(gpiosim_chip_name sim2) [$(gpiosim_dev_name sim2)-node0] (16 lines)"

	run_tool gpiodetect $(gpiosim_chip_name sim1)

	test "$status" -eq 0
	assert_fail output_contains_line "$(gpiosim_chip_name sim0)\\s+"
	output_contains_line "$(gpiosim_chip_name sim1) [$(gpiosim_dev_name sim1)-node0] (8 lines)"
	assert_fail output_contains_line "$(gpiosim_chip_name sim2)\\s+"

	run_tool gpiodetect $(gpiosim_chip_name sim2)

	test "$status" -eq 0
	assert_fail output_contains_line "$(gpiosim_chip_name sim0)\\s+"
	assert_fail output_contains_line "$(gpiosim_chip_name sim1)\\s+"
	output_contains_line "$(gpiosim_chip_name sim2) [$(gpiosim_dev_name sim2)-node0] (16 lines)"
}

@test "gpiodetect: nonexistent chip" {
	run_tool gpiodetect "nonexistent"

	test "$status" -eq 1
}

#
# gpioinfo test cases
#

@test "gpioinfo: dump all chips" {
	gpiosim_chip sim0 num_lines=4
	gpiosim_chip sim1 num_lines=8

	run_tool gpioinfo

	test "$status" -eq 0
	output_contains_line "$(gpiosim_chip_name sim0) - 4 lines:"
	output_contains_line "$(gpiosim_chip_name sim1) - 8 lines:"

	output_regex_match "\\s+line\\s+0:\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "\\s+line\\s+7:\\s+unnamed\\s+unused\\s+\\[input\\]"
}

@test "gpioinfo: dump all chips with one line requested" {
	gpiosim_chip sim0 num_lines=4
	gpiosim_chip sim1 num_lines=8

	coproc_run_tool gpioset -i --active-low --chip "$(gpiosim_chip_name sim1)" 7=1

	run_tool gpioinfo

	test "$status" -eq 0
	output_contains_line "$(gpiosim_chip_name sim0) - 4 lines:"
	output_contains_line "$(gpiosim_chip_name sim1) - 8 lines:"
	output_regex_match "\\s+line\\s+0:\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "\\s+line\\s+7:\\s+unnamed\\s+gpioset\\s+\\[output\\s+active-low\\s+used\\]"
	coproc_tool_kill
	coproc_tool_wait
}

@test "gpioinfo: dump one chip" {
	gpiosim_chip sim0 num_lines=8
	gpiosim_chip sim1 num_lines=4

	run_tool gpioinfo --chip "$(gpiosim_chip_name sim1)"

	test "$status" -eq 0
	assert_fail output_contains_line "$(gpiosim_chip_name sim0)\\s+"
	output_contains_line "$(gpiosim_chip_name sim1) - 4 lines:"
	output_regex_match "\\s+line\\s+0:\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "\\s+line\\s+1:\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "\\s+line\\s+2:\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "\\s+line\\s+3:\\s+unnamed\\s+unused\\s+\\[input\\]"
	assert_fail output_regex_match "\\s+line\\s+4:\\s"
}

@test "gpioinfo: dump select line" {
	gpiosim_chip sim0 num_lines=8
	gpiosim_chip sim1 num_lines=4

	run_tool gpioinfo --chip "$(gpiosim_chip_name sim1)" 2

	test "$status" -eq 0
	assert_fail output_contains_line "$(gpiosim_chip_name sim0) - 8 lines:"
	assert_fail output_contains_line "$(gpiosim_chip_name sim1) - 4 lines:"
	assert_fail output_regex_match "$(gpiosim_chip_name sim1)\\s+0\\s+"
	assert_fail output_regex_match "$(gpiosim_chip_name sim1)\\s+1\\s+"
	output_regex_match "$(gpiosim_chip_name sim1)\\s+2\\s+unnamed\\s+unused\\s+\\[input\\]"
	assert_fail output_regex_match "$(gpiosim_chip_name sim1)\\s+3\\s+"
	assert_fail output_regex_match "\\s+line\\s+4:\\s+unnamed\\s+unused\\s+\\[input\\]"
}

@test "gpioinfo: dump select lines" {
	gpiosim_chip sim0 num_lines=8
	gpiosim_chip sim1 num_lines=4

	run_tool gpioinfo --chip "$(gpiosim_chip_name sim1)" 1 3

	test "$status" -eq 0
	assert_fail output_contains_line "$(gpiosim_chip_name sim0)\\s+"
	assert_fail output_contains_line "$(gpiosim_chip_name sim1) - 4 lines:"
	assert_fail output_regex_match "$(gpiosim_chip_name sim1)\\s+0\\s+"
	output_regex_match "$(gpiosim_chip_name sim1)\\s+1\\s+unnamed\\s+unused\\s+\\[input\\]"
	assert_fail output_regex_match "$(gpiosim_chip_name sim1)\\s+2\\s+"
	output_regex_match "$(gpiosim_chip_name sim1)\\s+3\\s+unnamed\\s+unused\\s+\\[input\\]"
	assert_fail output_regex_match "$(gpiosim_chip_name sim1)\\s+4\\s+"
}

@test "gpioinfo: line by name" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo

	run_tool gpioinfo foo

	test "$status" -eq 0
	output_regex_match "$(gpiosim_chip_name sim0)\\s+1\\s+foo\\s+unused\\s+\\[input\\]"
}

@test "gpioinfo: line by offset" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo

	run_tool gpioinfo --chip $(gpiosim_chip_name sim0) 1

	test "$status" -eq 0
	output_regex_match "$(gpiosim_chip_name sim0)\\s+1\\s+foo\\s+unused\\s+\\[input\\]"
}

@test "gpioinfo: line by chip and name" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo

	run_tool gpioinfo --chip $(gpiosim_chip_name sim0) foo

	test "$status" -eq 0
	output_regex_match "$(gpiosim_chip_name sim0)\\s+1\\s+foo\\s+unused\\s+\\[input\\]"
}

@test "gpioinfo: nonexistent chip" {
	run_tool gpioinfo --chip "nonexistent"

	test "$status" -eq 1
}

@test "gpioinfo: nonexistent line" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioinfo "nonexistent"

	test "$status" -eq 1

	run_tool gpioinfo --chip "$(gpiosim_chip_name sim0)" "nonexistent"

	test "$status" -eq 1
}

@test "gpioinfo: offset out of range" {
	gpiosim_chip sim0 num_lines=4

	run_tool gpioinfo --chip "$(gpiosim_chip_name sim0)" 0 1 2 3 4 5

	test "$status" -eq "1"
	output_regex_match "$(gpiosim_chip_name sim0)\\s+0\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "$(gpiosim_chip_name sim0)\\s+1\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "$(gpiosim_chip_name sim0)\\s+2\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match "$(gpiosim_chip_name sim0)\\s+3\\s+unnamed\\s+unused\\s+\\[input\\]"
	output_regex_match ".*cannot find line 4.*"
	output_regex_match ".*cannot find line 5.*"
}

@test "gpioinfo: first non-unique line" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	run_tool gpioinfo foobar

	test "$status" -eq 0
	output_regex_match "$(gpiosim_chip_name sim0)\\s+3\\s+foobar\\s+unused\\s+\\[input\\]"
	assert_fail output_regex_match "$(gpiosim_chip_name sim1)\\s+"
	assert_fail output_regex_match "$(gpiosim_chip_name sim2)\\s+"
}


@test "gpioinfo: all lines matching name" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	run_tool gpioinfo --strict foobar

	test "$status" -eq 1
	output_regex_match "$(gpiosim_chip_name sim0)\\s+3\\s+foobar\\s+unused\\s+\\[input\\]"
	output_regex_match "$(gpiosim_chip_name sim1)\\s+2\\s+foobar\\s+unused\\s+\\[input\\]"
	output_regex_match "$(gpiosim_chip_name sim1)\\s+7\\s+foobar\\s+unused\\s+\\[input\\]"
	assert_fail output_regex_match "$(gpiosim_chip_name sim2)\\s+"
}

#
# gpiofind test cases
#

@test "gpiofind: line found" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=3:bar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	run_tool gpiofind foobar

	test "$status" -eq "0"
	test "$output" = "$(gpiosim_chip_name sim1) 7"
}

@test "gpiofind: line found with info" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=3:bar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	run_tool gpiofind --info foobar

	test "$status" -eq "0"
	assert_fail output_regex_match "$(gpiosim_chip_name sim0)"
	output_regex_match "$(gpiosim_chip_name sim1)\\s+7\\s+foobar\\s+unused\\s+\\[input\\]"
	assert_fail output_regex_match "$(gpiosim_chip_name sim2)"
}

@test "gpiofind: non-unique lines found" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	run_tool gpiofind foobar

	test "$status" -eq "0"
	output_regex_match "$(gpiosim_chip_name sim0)\\s+3"
	assert_fail output_regex_match "$(gpiosim_chip_name sim1)"
	assert_fail output_regex_match "$(gpiosim_chip_name sim2)"
}

@test "gpiofind: strict lines found" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	run_tool gpiofind --strict foobar

	test "$status" -eq "1"
	output_regex_match "$(gpiosim_chip_name sim0)\\s+3"
	output_regex_match "$(gpiosim_chip_name sim1)\\s+2"
	output_regex_match "$(gpiosim_chip_name sim1)\\s+7"
	assert_fail output_regex_match "$(gpiosim_chip_name sim2)"
}


@test "gpiofind: line not found" {
	gpiosim_chip sim0 num_lines=4
	gpiosim_chip sim1 num_lines=8
	gpiosim_chip sim2 num_lines=16

	run_tool gpiofind nonexistent-line

	test "$status" -eq "1"
}

#
# gpioget test cases
#

@test "gpioget: read all lines" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 2 pull-up
	gpiosim_set_pull sim0 3 pull-up
	gpiosim_set_pull sim0 5 pull-up
	gpiosim_set_pull sim0 7 pull-up

	run_tool gpioget --chip "$(gpiosim_chip_name sim0)" 0 1 2 3 4 5 6 7

	test "$status" -eq "0"
	test "$output" = "0=inactive 1=inactive 2=active 3=active 4=inactive 5=active 6=inactive 7=active"
}

@test "gpioget: read all lines numerically" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 2 pull-up
	gpiosim_set_pull sim0 3 pull-up
	gpiosim_set_pull sim0 5 pull-up
	gpiosim_set_pull sim0 7 pull-up

	run_tool gpioget --numeric --chip "$(gpiosim_chip_name sim0)" 0 1 2 3 4 5 6 7

	test "$status" -eq "0"
	test "$output" = "0 0 1 1 0 1 0 1"
}

@test "gpioget: read all lines (active-low)" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 2 pull-up
	gpiosim_set_pull sim0 3 pull-up
	gpiosim_set_pull sim0 5 pull-up
	gpiosim_set_pull sim0 7 pull-up

	run_tool gpioget --active-low --chip "$(gpiosim_chip_name sim0)" 0 1 2 3 4 5 6 7

	test "$status" -eq "0"
	test "$output" = "0=active 1=active 2=inactive 3=inactive 4=active 5=inactive 6=active 7=inactive"
}

@test "gpioget: read all lines (pull-up)" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 2 pull-up
	gpiosim_set_pull sim0 3 pull-up
	gpiosim_set_pull sim0 5 pull-up
	gpiosim_set_pull sim0 7 pull-up

	run_tool gpioget --bias=pull-up --chip "$(gpiosim_chip_name sim0)" 0 1 2 3 4 5 6 7

	test "$status" -eq "0"
	test "$output" = "0=active 1=active 2=active 3=active 4=active 5=active 6=active 7=active"
}

@test "gpioget: read all lines (pull-down)" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 2 pull-up
	gpiosim_set_pull sim0 3 pull-up
	gpiosim_set_pull sim0 5 pull-up
	gpiosim_set_pull sim0 7 pull-up

	run_tool gpioget --bias=pull-down --chip "$(gpiosim_chip_name sim0)" 0 1 2 3 4 5 6 7

	test "$status" -eq "0"
	test "$output" = "0=inactive 1=inactive 2=inactive 3=inactive 4=inactive 5=inactive 6=inactive 7=inactive"
}

@test "gpioget: read some lines" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 1 pull-up
	gpiosim_set_pull sim0 4 pull-up
	gpiosim_set_pull sim0 6 pull-up

	run_tool gpioget --chip "$(gpiosim_chip_name sim0)" 0 1 4 6

	test "$status" -eq "0"
	test "$output" = "0=inactive 1=active 4=active 6=active"
}

@test "gpioget: read some lines by name" {
	gpiosim_chip sim0 num_lines=8  line_name=1:foo line_name=6:bar

	gpiosim_set_pull sim0 1 pull-up
	gpiosim_set_pull sim0 4 pull-up
	gpiosim_set_pull sim0 6 pull-down

	run_tool gpioget --chip "$(gpiosim_chip_name sim0)" 0 foo 4 bar

	test "$status" -eq "0"
	test "$output" = "0=inactive foo=active 4=active bar=inactive"
}

@test "gpioget: read some lines strictly by name" {
	# not suggesting this setup is in any way a good idea - just test that we can deal with it
	gpiosim_chip sim0 num_lines=8 line_name=1:42 line_name=6:13

	gpiosim_set_pull sim0 1 pull-up
	gpiosim_set_pull sim0 6 pull-down

	run_tool gpioget --by-name --chip "$(gpiosim_chip_name sim0)" 42 13

	test "$status" -eq "0"
	test "$output" = "42=active 13=inactive"
}

@test "gpioget: no arguments" {
	run_tool gpioget

	test "$status" -eq "1"
	output_regex_match ".*at least one GPIO line must be specified"
}

@test "gpioget: no lines specified" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioget --chip "$(gpiosim_chip_name sim0)"

	test "$status" -eq "1"
	output_regex_match ".*at least one GPIO line must be specified"
}

@test "gpioget: offset out of range" {
	gpiosim_chip sim0 num_lines=4

	run_tool gpioget --chip "$(gpiosim_chip_name sim0)" 0 1 2 3 4 5

	test "$status" -eq "1"
	output_regex_match ".*cannot find line 4.*"
	output_regex_match ".*cannot find line 5.*"
}

@test "gpioget: unknown line specified by name" {
	gpiosim_chip sim0 num_lines=4

	run_tool gpioget foobar

	test "$status" -eq "1"
	output_regex_match ".*cannot find line foobar"

}

@test "gpioget: same line twice" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioget --chip "$(gpiosim_chip_name sim0)" 0 0

	test "$status" -eq "1"
	output_regex_match ".*lines 0 and 0 are the same"
}

@test "gpioget: same line twice by name" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo

	run_tool gpioget --chip "$(gpiosim_chip_name sim0)" foo foo

	test "$status" -eq "1"
	output_regex_match ".*lines foo and foo are the same"
}

@test "gpioget: same line by name and offset" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo

	run_tool gpioget --chip "$(gpiosim_chip_name sim0)" foo 1

	test "$status" -eq "1"
	output_regex_match ".*lines foo and 1 are the same"
}

@test "gpioget: invalid bias" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioget --bias=bad --chip "$(gpiosim_chip_name sim0)" 0 1

	test "$status" -eq "1"
	output_regex_match ".*invalid bias.*"
}

#
# gpioset test cases
#

@test "gpioset: line by name" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo

	coproc_run_tool gpioset -i foo=1

	gpiosim_check_value sim0 1 1

	coproc_tool_kill
	coproc_tool_wait
}

@test "gpioset: line by offset" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpioset -i --chip "$(gpiosim_chip_name sim0)" 1=1

	gpiosim_check_value sim0 1 1

	coproc_tool_kill
	coproc_tool_wait
}

@test "gpioset: line by chip and name" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo

	coproc_run_tool gpioset -i --chip "$(gpiosim_chip_name sim0)" foo=1

	gpiosim_check_value sim0 1 1

	coproc_tool_kill
	coproc_tool_wait
}

@test "gpioset: set lines" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpioset -i --chip "$(gpiosim_chip_name sim0)" \
					0=0 1=0 2=1 3=1 4=1 5=1 6=0 7=1

	gpiosim_check_value sim0 0 0
	gpiosim_check_value sim0 1 0
	gpiosim_check_value sim0 2 1
	gpiosim_check_value sim0 3 1
	gpiosim_check_value sim0 4 1
	gpiosim_check_value sim0 5 1
	gpiosim_check_value sim0 6 0
	gpiosim_check_value sim0 7 1

	coproc_tool_kill
	coproc_tool_wait
}

@test "gpioset: set lines (active-low)" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpioset -i --active-low --chip "$(gpiosim_chip_name sim0)" \
					0=0 1=0 2=1 3=1 4=1 5=1 6=0 7=1

	gpiosim_check_value sim0 0 1
	gpiosim_check_value sim0 1 1
	gpiosim_check_value sim0 2 0
	gpiosim_check_value sim0 3 0
	gpiosim_check_value sim0 4 0
	gpiosim_check_value sim0 5 0
	gpiosim_check_value sim0 6 1
	gpiosim_check_value sim0 7 0

	coproc_tool_kill
	coproc_tool_wait
}

@test "gpioset: set lines (push-pull)" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpioset -i --drive=push-pull --chip "$(gpiosim_chip_name sim0)" \
					0=0 1=0 2=1 3=1 4=1 5=1 6=0 7=1

	gpiosim_check_value sim0 0 0
	gpiosim_check_value sim0 1 0
	gpiosim_check_value sim0 2 1
	gpiosim_check_value sim0 3 1
	gpiosim_check_value sim0 4 1
	gpiosim_check_value sim0 5 1
	gpiosim_check_value sim0 6 0
	gpiosim_check_value sim0 7 1

	coproc_tool_kill
	coproc_tool_wait
}

@test "gpioset: set lines (open-drain)" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 2 pull-up
	gpiosim_set_pull sim0 3 pull-up
	gpiosim_set_pull sim0 5 pull-up
	gpiosim_set_pull sim0 7 pull-up

	coproc_run_tool gpioset -i --drive=open-drain --chip "$(gpiosim_chip_name sim0)" \
					0=0 1=0 2=1 3=1 4=1 5=1 6=0 7=1

	gpiosim_check_value sim0 0 0
	gpiosim_check_value sim0 1 0
	gpiosim_check_value sim0 2 1
	gpiosim_check_value sim0 3 1
	gpiosim_check_value sim0 4 0
	gpiosim_check_value sim0 5 1
	gpiosim_check_value sim0 6 0
	gpiosim_check_value sim0 7 1

	coproc_tool_kill
	coproc_tool_wait
}

@test "gpioset: set lines (open-source)" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 2 pull-up
	gpiosim_set_pull sim0 3 pull-up
	gpiosim_set_pull sim0 5 pull-up
	gpiosim_set_pull sim0 7 pull-up

	coproc_run_tool gpioset -i --drive=open-source --chip "$(gpiosim_chip_name sim0)" \
					0=0 1=0 2=1 3=0 4=1 5=1 6=0 7=1

	gpiosim_check_value sim0 0 0
	gpiosim_check_value sim0 1 0
	gpiosim_check_value sim0 2 1
	gpiosim_check_value sim0 3 1
	gpiosim_check_value sim0 4 1
	gpiosim_check_value sim0 5 1
	gpiosim_check_value sim0 6 0
	gpiosim_check_value sim0 7 1

	coproc_tool_kill
	coproc_tool_wait
}

@test "gpioset: set lines with value variants" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 0 pull-up
	gpiosim_set_pull sim0 1 pull-down
	gpiosim_set_pull sim0 2 pull-down
	gpiosim_set_pull sim0 3 pull-up
	gpiosim_set_pull sim0 4 pull-down
	gpiosim_set_pull sim0 5 pull-up
	gpiosim_set_pull sim0 6 pull-up
	gpiosim_set_pull sim0 7 pull-down

	coproc_run_tool gpioset -i --chip "$(gpiosim_chip_name sim0)" \
					0=0 1=1 2=active 3=inactive 4=on 5=off 6=false 7=true

	gpiosim_check_value sim0 0 0
	gpiosim_check_value sim0 1 1
	gpiosim_check_value sim0 2 1
	gpiosim_check_value sim0 3 0
	gpiosim_check_value sim0 4 1
	gpiosim_check_value sim0 5 0
	gpiosim_check_value sim0 6 0
	gpiosim_check_value sim0 7 1

	coproc_tool_kill
	coproc_tool_wait
}

@test "gpioset: set some lines interactive and wait for exit" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpioset --interactive --chip "$(gpiosim_chip_name sim0)" \
					1=0 2=1 5=1 6=0 7=1

	gpiosim_check_value sim0 1 0
	gpiosim_check_value sim0 2 1
	gpiosim_check_value sim0 5 1
	gpiosim_check_value sim0 6 0
	gpiosim_check_value sim0 7 1

	coproc_tool_stdin_write "exit"
	coproc_tool_wait

	test "$status" -eq "0"
}

@test "gpioset: set some lines interactive and wait for SIGTERM" {
	gpiosim_chip sim0 num_lines=4

	coproc_run_tool gpioset --interactive --chip "$(gpiosim_chip_name sim0)" 0=1

	gpiosim_check_value sim0 0 1

	coproc_tool_kill
	coproc_tool_wait

	test "$status" -eq "143"
}

@test "gpioset: set some lines interactive and wait for SIGINT" {
	gpiosim_chip sim0 num_lines=4

	coproc_run_tool gpioset --interactive --chip "$(gpiosim_chip_name sim0)" 0=1

	gpiosim_check_value sim0 0 1

	coproc_tool_kill -SIGINT
	coproc_tool_wait

	test "$status" -eq "130"
}

@test "gpioset: set some lines with hold-period" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpioset --hold-period=1200ms \
				--chip "$(gpiosim_chip_name sim0)" 0=1 5=0 7=1

	gpiosim_check_value sim0 0 1
	gpiosim_check_value sim0 5 0
	gpiosim_check_value sim0 7 1

	coproc_tool_wait

	test "$status" -eq "0"
}

@test "gpioset: no arguments" {
	run_tool gpioset

	test "$status" -eq "1"
	output_regex_match ".*at least one GPIO line value must be specified"
}

@test "gpioset: no lines specified" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioset --chip "$(gpiosim_chip_name sim1)"

	test "$status" -eq "1"
	output_regex_match ".*at least one GPIO line value must be specified"
}

@test "gpioset: offset out of range" {
	gpiosim_chip sim0 num_lines=4

	run_tool gpioset --chip "$(gpiosim_chip_name sim0)" 0=1 1=1 2=1 3=1 4=1 5=1

	test "$status" -eq "1"
	output_regex_match ".*cannot find line 4.*"
	output_regex_match ".*cannot find line 5.*"
}

@test "gpioset: with bad hold-period" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioset --hold-period=bad --chip "$(gpiosim_chip_name sim0)" 0=1

	test "$status" -eq "1"
	output_regex_match ".*invalid period.*"
}

@test "gpioset: invalid value name" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioset --chip "$(gpiosim_chip_name sim0)" 0=c

	test "$status" -eq "1"
	output_regex_match ".*invalid line value.*"
}

@test "gpioset: invalid value" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioset --chip "$(gpiosim_chip_name sim0)" 0=3

	test "$status" -eq "1"
	output_regex_match ".*invalid line value.*"
}

@test "gpioset: invalid offset" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioset --chip "$(gpiosim_chip_name sim0)" 4000000000=0

	test "$status" -eq "1"
	output_regex_match ".*cannot find line.*"
}

@test "gpioset: invalid bias" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioset --bias=bad --chip "$(gpiosim_chip_name sim0)" 0=1 1=1

	test "$status" -eq "1"
	output_regex_match ".*invalid bias.*"
}

@test "gpioset: invalid drive" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioset --drive=bad --chip "$(gpiosim_chip_name sim0)" 0=1 1=1

	test "$status" -eq "1"
	output_regex_match ".*invalid drive.*"
}

@test "gpioset: daemonize with interactive" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioset --interactive --daemonize --chip "$(gpiosim_chip_name sim1)" 0=1

	test "$status" -eq "1"
	output_regex_match ".*can't combine daemonize with interactive"
}

@test "gpioset: interactive with toggle" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioset --interactive --toggle 1s --chip "$(gpiosim_chip_name sim1)" 0=1

	test "$status" -eq "1"
	output_regex_match ".*can't combine interactive with toggle"
}

@test "gpioset: same line twice" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpioset --chip "$(gpiosim_chip_name sim0)" 0=1 0=1

	test "$status" -eq "1"
	output_regex_match ".*lines 0 and 0 are the same"
}

@test "gpioset: same line by name and offset" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo

	run_tool gpioset --chip "$(gpiosim_chip_name sim0)" foo=1 1=1

	test "$status" -eq "1"
	output_regex_match ".*lines foo and 1 are the same"
}

@test "gpioset: same line twice by name" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo

	run_tool gpioset --chip "$(gpiosim_chip_name sim0)" foo=1 foo=1

	test "$status" -eq "1"
	output_regex_match ".*lines foo and foo are the same"
}

#
# gpiomon test cases
#

@test "gpiomon: line by offset" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 4 pull-up

	coproc_run_tool gpiomon --edge=rising --chip "$(gpiosim_chip_name sim0)" 4

	gpiosim_set_pull sim0 4 pull-down
	gpiosim_set_pull sim0 4 pull-up
	gpiosim_set_pull sim0 4 pull-down
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+RISING\\s+chip: $(gpiosim_chip_name sim0)\\s+offset:\\s+4"
	assert_fail output_regex_match ".*FALLING.*"
}

@test "gpiomon: line by name" {
	gpiosim_chip sim0 num_lines=8 line_name=4:foo

	gpiosim_set_pull sim0 4 pull-up

	coproc_run_tool gpiomon --edge=rising foo

	gpiosim_set_pull sim0 4 pull-down
	gpiosim_set_pull sim0 4 pull-up
	gpiosim_set_pull sim0 4 pull-down
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	output_regex_match "\\s*[0-9]+\.[0-9]+\\s+RISING\\s+foo"
	assert_fail output_regex_match ".*FALLING.*"
}

@test "gpiomon: line by chip and name" {
	gpiosim_chip sim0 num_lines=8 line_name=4:foo

	gpiosim_set_pull sim0 4 pull-up

	coproc_run_tool gpiomon --edge=rising --chip "$(gpiosim_chip_name sim0)" foo

	gpiosim_set_pull sim0 4 pull-down
	gpiosim_set_pull sim0 4 pull-up
	gpiosim_set_pull sim0 4 pull-down
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+RISING\\s+chip: $(gpiosim_chip_name sim0)\\s+offset:\\s+4\\s+name:\\s+foo"
	assert_fail output_regex_match ".*FALLING.*"
}

@test "gpiomon: single falling edge event" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 4 pull-down

	coproc_run_tool gpiomon --edge=falling --chip "$(gpiosim_chip_name sim0)" 4

	gpiosim_set_pull sim0 4 pull-up
	gpiosim_set_pull sim0 4 pull-down
	gpiosim_set_pull sim0 4 pull-up
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+FALLING\\s+chip: $(gpiosim_chip_name sim0)\\s+offset:\\s+4"
	assert_fail output_regex_match ".*RISING.*"
}

@test "gpiomon: single falling edge event (pull-up)" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 4 pull-down

	coproc_run_tool gpiomon --bias=pull-up --chip "$(gpiosim_chip_name sim0)" 4

	gpiosim_set_pull sim0 4 pull-down
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+FALLING\\s+chip: $(gpiosim_chip_name sim0)\\s+offset:\\s+4"
	assert_fail output_regex_match ".*RISING.*"
}

@test "gpiomon: single rising edge event (pull-down)" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 4 pull-up

	coproc_run_tool gpiomon --bias=pull-down --chip "$(gpiosim_chip_name sim0)" 4

	gpiosim_set_pull sim0 4 pull-up
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+RISING\\s+chip: $(gpiosim_chip_name sim0)\\s+offset:\\s+4"
	assert_fail output_regex_match ".*FALLING.*"
}

@test "gpiomon: single rising edge event (active-low)" {
	gpiosim_chip sim0 num_lines=8

	gpiosim_set_pull sim0 4 pull-up

	coproc_run_tool gpiomon --edge=rising --active-low --chip "$(gpiosim_chip_name sim0)" 4

	gpiosim_set_pull sim0 4 pull-down
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+RISING\\s+chip: $(gpiosim_chip_name sim0)\\s+offset:\\s+4"
	assert_fail output_regex_match ".*FALLING.*"
}

@test "gpiomon: single rising edge event (quiet mode)" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiomon --edge=rising --quiet --chip "$(gpiosim_chip_name sim0)" 4

	gpiosim_set_pull sim0 4 pull-up
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	test -z "$output"
}

@test "gpiomon: four alternating events" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiomon --num-events=4 --chip "$(gpiosim_chip_name sim0)" 4

	gpiosim_set_pull sim0 4 pull-up
	sleep 0.2
	gpiosim_set_pull sim0 4 pull-down
	sleep 0.2
	gpiosim_set_pull sim0 4 pull-up
	sleep 0.2
	gpiosim_set_pull sim0 4 pull-down
	sleep 0.2

	coproc_tool_wait

	test "$status" -eq "0"
	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+RISING\\s+chip: $(gpiosim_chip_name sim0)\\s+offset:\\s+4"
	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+FALLING\\s+chip: $(gpiosim_chip_name sim0)\\s+offset:\\s+4"
}

@test "gpiomon: exit after SIGINT" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiomon --chip "$(gpiosim_chip_name sim0)" 4

	coproc_tool_kill -SIGINT
	coproc_tool_wait

	test "$status" -eq "130"
	test -z "$output"
}

@test "gpiomon: exit after SIGTERM" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiomon --chip "$(gpiosim_chip_name sim0)" 4

	coproc_tool_kill -SIGTERM
	coproc_tool_wait

	test "$status" -eq "143"
	test -z "$output"
}

@test "gpiomon: both edges" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiomon --edge=both --chip "$(gpiosim_chip_name sim0)" 4

	gpiosim_set_pull sim0 4 pull-up
	sleep 0.2
	gpiosim_set_pull sim0 4 pull-down
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+RISING\\s+chip: $(gpiosim_chip_name sim0)\\s+offset:\\s+4"
	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+FALLING\\s+chip: $(gpiosim_chip_name sim0)\\s+offset:\\s+4"
}

@test "gpiomon: watch multiple lines" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiomon --format=%o --chip "$(gpiosim_chip_name sim0)" 1 2 3 4 5

	gpiosim_set_pull sim0 2 pull-up
	gpiosim_set_pull sim0 3 pull-up
	gpiosim_set_pull sim0 4 pull-up
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	test "${lines[0]}" = "2"
	test "${lines[1]}" = "3"
	test "${lines[2]}" = "4"
}

@test "gpiomon: watch multiple lines (lines in mixed-up order)" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiomon --format=%o --chip "$(gpiosim_chip_name sim0)" 5 2 7 1 6

	gpiosim_set_pull sim0 2 pull-up
	gpiosim_set_pull sim0 1 pull-up
	gpiosim_set_pull sim0 6 pull-up
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	test "${lines[0]}" = "2"
	test "${lines[1]}" = "1"
	test "${lines[2]}" = "6"
}

@test "gpiomon: same line twice" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpiomon --chip "$(gpiosim_chip_name sim0)" 0 0

	test "$status" -eq "1"
	output_regex_match ".*lines 0 and 0 are the same"
}

@test "gpiomon: same line by name and offset" {
	gpiosim_chip sim0 num_lines=8 line_name=4:foo

	run_tool gpiomon --chip "$(gpiosim_chip_name sim0)" 4 foo

	test "$status" -eq "1"
	output_regex_match ".*lines 4 and foo are the same"
}

@test "gpiomon: same line twice by name" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo

	run_tool gpiomon --chip "$(gpiosim_chip_name sim0)" foo foo

	test "$status" -eq "1"
	output_regex_match ".*lines foo and foo are the same"
}

@test "gpiomon: first non-unique line" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	coproc_run_tool gpiomon foobar

	gpiosim_set_pull sim0 3 pull-up
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	output_regex_match "\\s*[0-9]+\.[0-9]+\\s+RISING\\s+foobar"
	assert_fail output_regex_match ".*FALLING.*"
}

@test "gpiomon: strictly unique line" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	run_tool gpiomon --strict foobar

	test "$status" -eq "1"
	output_regex_match ".*line foobar is not unique"
}

@test "gpiomon: no arguments" {
	run_tool gpiomon

	test "$status" -eq "1"
	output_regex_match ".*at least one GPIO line must be specified"
}

@test "gpiomon: line not specified" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpiomon --chip "$(gpiosim_chip_name sim0)"

	test "$status" -eq "1"
	output_regex_match ".*at least one GPIO line must be specified"
}

@test "gpiomon: offset out of range" {
	gpiosim_chip sim0 num_lines=4

	run_tool gpiomon --chip "$(gpiosim_chip_name sim0)" 5

	test "$status" -eq "1"
	output_regex_match ".*cannot find line 5"
}

@test "gpiomon: invalid bias" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpiomon --bias=bad -c "$(gpiosim_chip_name sim0)" 0 1

	test "$status" -eq "1"
	output_regex_match ".*invalid bias.*"
}

@test "gpiomon: custom format (event type + offset)" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiomon "--format=%e %o" -c "$(gpiosim_chip_name sim0)" 4

	gpiosim_set_pull sim0 4 pull-up
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	test "$output" = "1 4"
}

@test "gpiomon: custom format (event type + offset joined)" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiomon "--format=%e%o" -c "$(gpiosim_chip_name sim0)" 4

	gpiosim_set_pull sim0 4 pull-up
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	test "$output" = "14"
}

@test "gpiomon: custom format (format menagerie)" {
	gpiosim_chip sim0 num_lines=8 line_name=4:baz

	coproc_run_tool gpiomon "--format=%e %o %E %l %c %u" --utc -c "$(gpiosim_chip_name sim0)" baz

	gpiosim_set_pull sim0 4 pull-up
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	output_regex_match \
"1 4 rising baz /dev/gpiochip[0-9]+ [0-9][0-9][0-9][0-9]-[0-1][0-9]-[0-3][0-9]T[0-1][0-9]:[0-5][0-9]:[0-5][0-9]\\.[0-9]+Z"
}

@test "gpiomon: custom format (timestamp)" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiomon "--format=%e %o %s.%n" -c "$(gpiosim_chip_name sim0)" 4

	gpiosim_set_pull sim0 4 pull-up
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	output_regex_match "1 4 [0-9]+\\.[0-9]+"
}

@test "gpiomon: custom format (double percent sign)" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiomon "--format=start%%end" -c "$(gpiosim_chip_name sim0)" 4

	gpiosim_set_pull sim0 4 pull-up
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	test "$output" = "start%end"
}

@test "gpiomon: custom format (double percent sign + event type specifier)" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiomon "--format=%%e" -c "$(gpiosim_chip_name sim0)" 4

	gpiosim_set_pull sim0 4 pull-up
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	test "$output" = "%e"
}

@test "gpiomon: custom format (single percent sign)" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiomon "--format=%" -c "$(gpiosim_chip_name sim0)" 4

	gpiosim_set_pull sim0 4 pull-up
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	test "$output" = "%"
}

@test "gpiomon: custom format (single percent sign between other characters)" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiomon "--format=foo % bar" -c "$(gpiosim_chip_name sim0)" 4

	gpiosim_set_pull sim0 4 pull-up
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	test "$output" = "foo % bar"
}

@test "gpiomon: custom format (unknown specifier)" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiomon "--format=%x" -c "$(gpiosim_chip_name sim0)" 4

	gpiosim_set_pull sim0 4 pull-up
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	test "$output" = "%x"
}

#
# gpiowatch test cases
#

@test "gpiowatch: line by name" {
	gpiosim_chip sim0 num_lines=8 line_name=4:foo

	coproc_run_tool gpiowatch -L foo

	request_release_line "$(gpiosim_chip_name sim0)" 4
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	output_regex_match "\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+foo\\s+.*"
	output_regex_match "\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+foo\\s+.*"
}

@test "gpiowatch: line by offset" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiowatch -L --chip "$(gpiosim_chip_name sim0)" 4

	request_release_line "$(gpiosim_chip_name sim0)" 4
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+"$(gpiosim_chip_name sim0)"\\s+4\\s+.*"
	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+"$(gpiosim_chip_name sim0)"\\s+4\\s+.*"
}

@test "gpiowatch: line by chip and name" {
	gpiosim_chip sim0 num_lines=8 line_name=4:foo

	coproc_run_tool gpiowatch -L --chip "$(gpiosim_chip_name sim0)" foo

	request_release_line "$(gpiosim_chip_name sim0)" 4
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+$(gpiosim_chip_name sim0)\\s+4\\s+foo\\s+.*"
	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+$(gpiosim_chip_name sim0)\\s+4\\s+foo\\s+.*"
}


@test "gpiowatch: exit after SIGINT" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiowatch --chip "$(gpiosim_chip_name sim0)" 4

	coproc_tool_kill -SIGINT
	coproc_tool_wait

	test "$status" -eq "130"
	test -z "$output"
}

@test "gpiowatch: exit after SIGTERM" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiowatch --chip "$(gpiosim_chip_name sim0)" 4

	coproc_tool_kill -SIGTERM
	coproc_tool_wait

	test "$status" -eq "143"
	test -z "$output"
}

@test "gpiowatch: watch multiple lines" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiowatch -L --chip "$(gpiosim_chip_name sim0)" 1 2 3 4 5

	request_release_line "$(gpiosim_chip_name sim0)" 2
	request_release_line "$(gpiosim_chip_name sim0)" 3
	request_release_line "$(gpiosim_chip_name sim0)" 4
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+"$(gpiosim_chip_name sim0)"\\s+2\\s+.*"
	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+"$(gpiosim_chip_name sim0)"\\s+2\\s+.*"
	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+"$(gpiosim_chip_name sim0)"\\s+3\\s+.*"
	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+"$(gpiosim_chip_name sim0)"\\s+3\\s+.*"
	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+"$(gpiosim_chip_name sim0)"\\s+4\\s+.*"
	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+"$(gpiosim_chip_name sim0)"\\s+4\\s+.*"
}

@test "gpiowatch: watch multiple lines (lines in mixed-up order)" {
	gpiosim_chip sim0 num_lines=8

	coproc_run_tool gpiowatch -L --chip "$(gpiosim_chip_name sim0)" 5 2 7 1 6

	request_release_line "$(gpiosim_chip_name sim0)" 2
	request_release_line "$(gpiosim_chip_name sim0)" 1
	request_release_line "$(gpiosim_chip_name sim0)" 6
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+"$(gpiosim_chip_name sim0)"\\s+2\\s+.*"
	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+"$(gpiosim_chip_name sim0)"\\s+2\\s+.*"
	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+"$(gpiosim_chip_name sim0)"\\s+1\\s+.*"
	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+"$(gpiosim_chip_name sim0)"\\s+1\\s+.*"
	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+"$(gpiosim_chip_name sim0)"\\s+6\\s+.*"
	output_regex_match \
"\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+"$(gpiosim_chip_name sim0)"\\s+6\\s+.*"
}

@test "gpiowatch: same line twice" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpiowatch --chip "$(gpiosim_chip_name sim0)" 0 0

	test "$status" -eq "1"
	output_regex_match ".*lines 0 and 0 are the same"
}

@test "gpiowatch: same line by name and offset" {
	gpiosim_chip sim0 num_lines=8 line_name=4:foo

	run_tool gpiowatch --chip "$(gpiosim_chip_name sim0)" 4 foo

	test "$status" -eq "1"
	output_regex_match ".*lines 4 and foo are the same"
}

@test "gpiowatch: same line twice by name" {
	gpiosim_chip sim0 num_lines=8 line_name=1:foo

	run_tool gpiowatch --chip "$(gpiosim_chip_name sim0)" foo foo

	test "$status" -eq "1"
	output_regex_match ".*lines foo and foo are the same"
}

@test "gpiowatch: first non-unique line" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	coproc_run_tool gpiowatch -L foobar

	request_release_line "$(gpiosim_chip_name sim0)" 3
	sleep 0.2

	coproc_tool_kill
	coproc_tool_wait

	output_regex_match "\\s*[0-9]+\.[0-9]+\\s+REQUESTED\\s+foobar\\s+.*"
	output_regex_match "\\s*[0-9]+\.[0-9]+\\s+RELEASED\\s+foobar\\s+.*"
}

@test "gpiowatch: strictly unique line" {
	gpiosim_chip sim0 num_lines=4 line_name=1:foo line_name=2:bar line_name=3:foobar
	gpiosim_chip sim1 num_lines=8 line_name=0:baz line_name=2:foobar line_name=4:xyz line_name=7:foobar
	gpiosim_chip sim2 num_lines=16

	run_tool gpiowatch -L --strict foobar

	test "$status" -eq "1"
	output_regex_match ".*line foobar is not unique"
}

@test "gpiowatch: no arguments" {
	run_tool gpiowatch

	test "$status" -eq "1"
	output_regex_match ".*at least one GPIO line must be specified"
}

@test "gpiowatch: line not specified" {
	gpiosim_chip sim0 num_lines=8

	run_tool gpiowatch --chip "$(gpiosim_chip_name sim0)"

	test "$status" -eq "1"
	output_regex_match ".*at least one GPIO line must be specified"
}

@test "gpiowatch: offset out of range" {
	gpiosim_chip sim0 num_lines=4

	run_tool gpiowatch --chip "$(gpiosim_chip_name sim0)" 5

	test "$status" -eq "1"
	output_regex_match ".*cannot find line 5"
}

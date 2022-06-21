// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com>

#include <ctype.h>
#include <gpiod.h>
#include <getopt.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <readline/history.h>
#include <readline/readline.h>

#include "tools-common.h"

static int by_name = 0;
static const struct option longopts[] = {
	{ "active-low",		no_argument,		NULL,	'l' },
	{ "bias",		required_argument,	NULL,	'b' },
	{ "by-name",		no_argument,		&by_name,	1 },
	{ "chip",		required_argument,	NULL,	'c' },
	{ "daemonize",		no_argument,		NULL,	'z' },
	{ "drive",		required_argument,	NULL,	'd' },
	{ "help",		no_argument,		NULL,	'h' },
	{ "hold-period",	required_argument,	NULL,	'p' },
	{ "interactive",	no_argument,		NULL,	'i' },
	{ "strict",		no_argument,		NULL,	's' },
	{ "toggle",		required_argument,	NULL,	't' },
	{ "version",		no_argument,		NULL,	'v' },
	{ GETOPT_NULL_LONGOPT },
};

static const char *const shortopts = "+c:b:d:ilp:st:zhv";

static void print_help(void)
{
	printf("Usage: %s [OPTIONS] <line>=<value> ...\n", get_progname());
	printf("\n");
	printf("Set GPIO line values of a GPIO chip.\n");
	printf("\n");
	printf("Lines are specified by name, or optionally by offset if the chip option\n");
	printf("is provided.\n");
	printf("Values may be '1' or '0', or equivalently 'active'/'inactive' or 'on'/'off'.\n");
	printf("\n");
	printf("The line output state is maintained until the process exits, but after that\n");
	printf("is not guaranteed.\n");
	printf("\n");
	printf("Options:\n");
	print_chip_help();
	printf("  -l, --active-low\ttreat the line as active low\n");
	print_bias_help();
	printf("  -d, --drive <drive>\tspecify the line drive mode.\n");
	printf("\t\t\t(possible values: push-pull, open-drain, open-source)\n");
	printf("\t\t\t(defaults to 'push-pull')\n");
	printf("      --by-name\t\ttreat lines as names even if they would parse as an offset\n");
	printf("  -s, --strict\t\tabort if requested line names are not unique\n");
	printf("  -p, --hold-period <period>\n:");
	printf("\t\t\tthe minimum time period to hold lines at\n");
	printf("\t\t\tthe requested values\n");
	printf("  -i, --interactive\tset the lines then wait for additional set commands\n");
	printf("\t\t\tUse the \"help\" command at the interactive prompt to get help\n");
	printf("\t\t\tfor the supported commands.\n");
	printf("  -t, --toggle <period>[,period]...\n");
	printf("\t\t\ttoggle the line(s) after the specified period(s)\n");
	printf("\t\t\tIf the last period is non-zero then the sequence repeats.\n");
	printf("  -z, --daemonize\tset values then detach from the controlling terminal\n");
	printf("  -h, --help\t\tdisplay this message and exit\n");
	printf("  -v, --version\tdisplay the version and exit\n");
	print_period_help();
	printf("\n");
	printf("Note: the state of a GPIO line controlled over the character device reverts to default\n");
	printf("when the last process referencing the file descriptor representing the device file exits.\n");
	printf("This means that it's wrong to run gpioset, have it exit and expect the line to continue\n");
	printf("being driven high or low. It may happen if given pin is floating but it must be interpreted\n");
	printf("as undefined behavior.\n");
}

static int parse_drive_or_die(const char *option)
{
	if (strcmp(option, "open-drain") == 0)
		return GPIOD_LINE_DRIVE_OPEN_DRAIN;
	if (strcmp(option, "open-source") == 0)
		return GPIOD_LINE_DRIVE_OPEN_SOURCE;
	if (strcmp(option, "push-pull") != 0)
		die("invalid drive: %s", option);
	return 0;
}

static int parse_value(const char *option)
{
	if (strcmp(option, "0") == 0)
		return 0;
	if (strcmp(option, "1") == 0)
		return 1;
	if (strcmp(option, "inactive") == 0)
		return 0;
	if (strcmp(option, "active") == 0)
		return 1;
	if (strcmp(option, "off") == 0)
		return 0;
	if (strcmp(option, "on") == 0)
		return 1;
	if (strcmp(option, "false") == 0)
		return 0;
	if (strcmp(option, "true") == 0)
		return 1;
	return -1;
}

// parse num_lines line id and values from lvs into lines and values
static bool parse_line_values(int num_lines, char **lvs, char **lines, int *values)
{
	int i;
	char *value;

	for (i = 0; i < num_lines; i++) {
		value = strchr(lvs[i], '=');
		if (!value) {
			printf("invalid line value: %s\n", lvs[i]);
			return false;
		} else {
			*value = '\0';
			value++;
			values[i] = parse_value(value);
			if (values[i] < 0) {
				printf("invalid line value: %s\n",value);
				return false;
			}
			lines[i] = lvs[i];
		}
	}
	return true;
}

// parse num_lines line id and values from lvs into lines and values, or die trying.
static void parse_line_values_or_die(int num_lines, char **lvs, char **lines, int *values)
{
	if (!parse_line_values(num_lines, lvs, lines, values))
		exit(EXIT_FAILURE);
}

static void wait_fd(int fd)
{
	struct pollfd pfd;

	pfd.fd = fd;
	pfd.events = POLLERR;

	if (poll(&pfd, 1, -1) < 0)
		die_perror("error waiting on request");
}

// Apply values from the resolver to the requests.
// offset and values are scratch pads for working.
static void apply_values(struct gpiod_line_request **requests, struct line_resolver *resolver,
			  unsigned int *offsets, int *values)
{
	int i;

	for (i = 0; i < resolver->num_chips; i++) {
		get_line_offsets_and_values(resolver, i, offsets, values);
		if (gpiod_line_request_set_values(requests[i], values))
			log_perror("failed to set values on chip %s",
				   resolver->chip_paths[i]);
	}
}

// set the values in the resolver for the line values specified by the remaining parameters.
static void set_line_values_subset(struct line_resolver *resolver,
				   int num_lines, char **lines, int *values)
{
	int l, i;

	for (l = 0; l < num_lines; l++)
		for (i = 0; i < resolver->num_lines; i++)
			if (strcmp(lines[l], resolver->lines[i].id) == 0) {
				resolver->lines[i].value = values[l];
				break;
			}
}

static void print_all_line_values(struct line_resolver *resolver)
{
	int i;
	char *fmt = "%s=%s ";

	for (i = 0; i < resolver->num_lines; i++) {
		if (i == resolver->num_lines -1)
			fmt = "%s=%s\n";
		printf(fmt, resolver->lines[i].id,
		       resolver->lines[i].value ? "active" : "inactive");
	}
}

// print the resovler line values for a subset of lines, specified by num_lines and lines.
static void print_line_values(struct line_resolver *resolver, int num_lines, char **lines)
{
	int i, j;
	char *fmt = "%s=%s ";

	for (i = 0; i < num_lines; i++) {
		if (i == num_lines - 1)
			fmt = "%s=%s\n";
		for (j = 0; j < resolver->num_lines; j++)
			if (strcmp(lines[i], resolver->lines[j].id) == 0) {
				printf(fmt, resolver->lines[j].id,
				       resolver->lines[j].value ? "active" : "inactive");
				break;
			}
	}
}

// toggle the values of all lines in the resolver
static void toggle_all_lines(struct line_resolver *resolver)
{
	int i;

	for (i = 0; i < resolver->num_lines; i++)
		resolver->lines[i].value = !resolver->lines[i].value;
}

// toggle a subset of lines, specified by num_lines and lines, in the resolver.
static void toggle_lines(struct line_resolver *resolver, int num_lines, char **lines)
{
	int i, l;

	for (l = 0; l < num_lines; l++)
		for (i = 0; i < resolver->num_lines; i++)
			if (strcmp(lines[l], resolver->lines[i].id) == 0) {
				resolver->lines[i].value = !resolver->lines[i].value;
				break;
			}
}

// toggle the resolved lines as specified by the toggle_periods,
// and apply the values to the requests.
// offset and values are scratch pads for working.
static void toggle_sequence(int toggles, unsigned int *toggle_periods,
			 struct gpiod_line_request **requests,
			 struct line_resolver *resolver,
			 unsigned int *offsets, int *values)
{
	int i = 0;

	for (;;) {
		usleep(toggle_periods[i]);
		toggle_all_lines(resolver);
		apply_values(requests, resolver, offsets, values);

		i++;
		if ((i == toggles - 1) && (toggle_periods[i] == 0))
			return;

		if (i == toggles)
			i = 0;
	}
}

// check that a set of lines, specified by num_lines and lines, are all resolved lines.
static bool valid_lines(struct line_resolver *resolver, int num_lines, char **lines)
{
	bool ret = true;
	int i, l;
	bool found;

	for (l = 0; l < num_lines; l++) {
		found = false;
		for (i = 0; i < resolver->num_lines; i++) {
			if (strcmp(lines[l], resolver->lines[i].id) == 0) {
				found = true;
				break;
			}
		}
		if (!found) {
			printf("unknown line: '%s'\n", lines[l]);
			ret = false;
		}
	}
	return  ret;
}

static void print_interactive_help()
{
	printf("COMMANDS:\n\n");
	printf("\tget [line] ...\n");
	printf("\t\tDisplay the output values of the given requested lines\n\n");
	printf("\t\tIf no lines are specified then all requested lines are displayed\n\n");
	printf("\tset <line=value> ...\n");
	printf("\t\tUpdate the output values of the given requested lines\n\n");
	printf("\ttoggle [line] ...\n");
	printf("\t\tToggle the output values of the given requested lines\n\n");
	printf("\t\tIf no lines are specified then all requested lines are toggled\n\n");
	printf("\tsleep <period>\n");
	printf("\t\tSleep for the specified period\n\n");
	printf("\thelp\n");
	printf("\t\tPrint this help\n\n");
	printf("\texit\n");
	printf("\t\tExit the program\n");
}

// split a line into words, returning the each of the words and the count.
// max_words specifies the msax number of words that may be returned in words.
static int split_words(char *line, int max_words, char **words)
{
	int num_words = 0;
	bool in_word = false;

	while (*line != '\0') {
		if (!in_word && !isspace(*line)) {
			in_word = true;
			// count all words, but only store max_words
			if (num_words < max_words)
				words[num_words] = line;
		} else if (isspace(*line)) {
			if (in_word) {
				num_words++;
				in_word = false;
			}
			*line = '\0';
		}
		line++;
	}
	if (in_word)
		num_words++;
	return num_words;
}

// check if a line is specified somewhere in the rl_line_buffer
static bool in_line_buffer(const char *id)
{
	int len = strlen(id);
	char *match = rl_line_buffer;

	while ((match = strstr(rl_line_buffer, id))) {
		if ((match > rl_line_buffer && isspace(match[-1])) &&
		    (isspace(match[len]) || (match[len] == '=')))
		    return true;
		match += len;
	}

	return false;
}

// context for complete_line_id, so it can provide valid line ids.
static struct line_resolver *completion_context;

// tab completion helper for line ids.
static char *complete_line_id(const char *text, int state)
{
	static int idx, len;
	const char *id;

	if (!state) {
		idx = 0;
		len = strlen(text);
	}
	while (idx < completion_context->num_lines) {
		id = completion_context->lines[idx].id;
		idx++;
		if ((strncmp(id, text, len) == 0) &&
		    (!in_line_buffer(id)))
			return strdup(id);
	}
	return NULL;
}

// tab completion helper for line values (just the value component)
static char *complete_value(const char *text, int state)
{
	static const char *values[] = {
		"1", "0", "active", "inactive", "on", "off", "true", "false", NULL
	};
	static int idx, len;
	const char *value;

	if (!state) {
		idx = 0;
		len = strlen(text);
	}
	while ((value = values[idx])) {
		idx++;
		if (strncmp(value, text, len) == 0)
			return strdup(value);
	}
	return NULL;
}

// tab completion help for interactive commands
static char *complete_command(const char *text, int state)
{
	static const char *commands[] = {
		"get", "set", "toggle", "sleep", "help", "exit", NULL
	};
	static int idx, len;
	const char *cmd;

	if (!state) {
		idx = 0;
		len = strlen(text);
	}
	while ((cmd = commands[idx])) {
		idx++;
		if (strncmp(cmd, text, len) == 0)
			return strdup(cmd);
	}
	return NULL;
}

// tab completion for interactive command lines
static char ** tab_completion(const char *text, int start, int end)
{
	char **matches = NULL;
	int cmd_start, cmd_end, len;

	rl_attempted_completion_over = true;
	rl_completion_type = '@';
	rl_sort_completion_matches = false;

	for (cmd_start = 0;
	     isspace(rl_line_buffer[cmd_start]) && cmd_start < end;
	     cmd_start++){};
	if (cmd_start == start)
		matches = rl_completion_matches(text, complete_command);
	for (cmd_end = cmd_start + 1;
	     !isspace(rl_line_buffer[cmd_end]) && cmd_end < end;
	     cmd_end++){};

	len = cmd_end - cmd_start;
	if (len == 3 && strncmp("set", &rl_line_buffer[cmd_start], 3) == 0) {
		if (rl_line_buffer[start-1] == '=') {
			matches = rl_completion_matches(text, complete_value);
		} else {
			rl_completion_append_character = '=';
			matches = rl_completion_matches(text, complete_line_id);
		}
	}
	if ((len == 3 && strncmp("get", &rl_line_buffer[cmd_start], 3) == 0) ||
	    (len == 6 && strncmp("toggle", &rl_line_buffer[cmd_start], 6) == 0))
		matches = rl_completion_matches(text, complete_line_id);
	return (matches);
}

static void interact(struct gpiod_line_request **requests,
		    struct line_resolver *resolver,
		    char ** lines, unsigned int *offsets, int *values)
{
	char *line;
	int num_words, num_lines, max_words;
	char **words;
	int period_us, i;
	char *line_buf;
	bool done;

	stifle_history(20);
	rl_attempted_completion_function = tab_completion;
	completion_context = resolver;

	max_words = resolver->num_lines + 1;
	words = calloc(max_words, sizeof(*words));
	if (!words)
		die("out of memory");
	for(done = false; !done;) {
		line = readline("gpioset> ");
		if (!line || line[0] == '\0')
			continue;
		for (i = strlen(line) - 1; (i > 0) && isspace(line[i]); i--)
			line[i] = '\0';
		line_buf = strdup(line);
		num_words = split_words(line_buf, max_words, words);
		if (num_words > max_words) {
			printf("too many command parameters provided\n");
			goto cmd_done;
		}
		num_lines = num_words - 1;
		if (strcmp(words[0], "get") == 0) {
			if (num_lines == 0)
				print_all_line_values(resolver);
			else if (valid_lines(resolver, num_lines, &words[1]))
				print_line_values(resolver, num_lines, &words[1]);
			goto cmd_ok;
		}
		if (strcmp(words[0], "set") == 0) {
			if (num_lines == 0)
				printf("at least one GPIO line value must be specified\n");
			else if (parse_line_values(num_lines, &words[1], lines, values) &&
				 valid_lines(resolver, num_lines, lines)) {
				set_line_values_subset(resolver, num_lines, lines, values);
				apply_values(requests, resolver, offsets, values);
			}
			goto cmd_ok;
		}
		if (strcmp(words[0], "toggle") == 0) {
			if (num_lines == 0)
				toggle_all_lines(resolver);
			else if (valid_lines(resolver, num_lines, &words[1]))
				toggle_lines(resolver, num_lines, &words[1]);
			apply_values(requests, resolver, offsets, values);
			goto cmd_ok;
		}
		if (strcmp(words[0], "sleep") == 0) {
			if (num_lines == 0) {
				printf("a period must be specified\n");
				goto cmd_ok;
			}
			if (num_lines > 1) {
				printf("only one period can be specified\n");
				goto cmd_ok;
			}
			period_us = parse_period(words[1]);
			if (period_us < 0) {
				printf("invalid period: %s\n", words[1]);
				goto cmd_ok;
			}
			usleep(period_us);
			goto cmd_ok;
		}
		if (strcmp(words[0], "exit") == 0) {
			done = true;
			goto cmd_done;
		}
		if (strcmp(words[0], "help") == 0) {
			print_interactive_help();
			goto cmd_done;
		}
		printf("unknown command: %s\n", words[0]);
		printf("Try the 'help' command\n");
cmd_ok:
		for (i = 0; isspace(line[i]); i++){};
		if ((history_length) == 0 ||
		    (strcmp(history_list()[history_length -1]->line, &line[i]) != 0)) {
			add_history(&line[i]);
		}
cmd_done:
		free(line);
		free(line_buf);
	}
	free(words);
}

int main(int argc, char **argv)
{
	int i, num_lines, optc, opti, bias = 0, drive = 0, toggles = 0, *values;
	struct gpiod_request_config *req_cfg;
	struct gpiod_line_request **requests;
	struct gpiod_line_config *line_cfg;
	struct gpiod_chip *chip;
	bool active_low = false, by_name = false, strict = false;
	bool daemonize = false, interactive = false;
	unsigned int *offsets, *toggle_periods, hold_period_us = 0;
	struct line_resolver *resolver;
	char *chip_id = NULL;
	char **lines;

	for (;;) {
		optc = getopt_long(argc, argv, shortopts, longopts, &opti);
		if (optc < 0)
			break;

		switch (optc) {
		case 'b':
			bias = parse_bias_or_die(optarg);
			break;
		case 'c':
			chip_id = optarg;
			break;
		case 'd':
			drive = parse_drive_or_die(optarg);
			break;
		case 'i':
			interactive = true;
			break;
		case 'l':
			active_low = true;
			break;
		case 'N':
			by_name = true;
			break;
		case 'p':
			hold_period_us = parse_period_or_die(optarg);
			break;
		case 's':
			strict = true;
			break;
		case 't':
			toggles = parse_periods_or_die(optarg, &toggle_periods);
			break;
		case 'z':
			daemonize = true;
			break;
		case 'h':
			print_help();
			return EXIT_SUCCESS;
		case 'v':
			print_version();
			return EXIT_SUCCESS;
		case '?':
			die("try %s --help", get_progname());
		case 0:
			break;
		default:
			abort();
		}
	}

	argc -= optind;
	argv += optind;

	if (daemonize && interactive)
		die("can't combine daemonize with interactive");

	if (toggles && interactive)
		die("can't combine interactive with toggle");

	if (argc < 1)
		die("at least one GPIO line value must be specified");

	num_lines = argc;

	lines = calloc(num_lines, sizeof(*lines));
	values = calloc(num_lines, sizeof(*values));
	if (!lines || !values)
		die("out of memory");

	parse_line_values_or_die(argc, argv, lines, values);

	line_cfg = gpiod_line_config_new();
	if (!line_cfg)
		die_perror("unable to allocate the line config structure");

	if (bias)
		gpiod_line_config_set_bias_default(line_cfg, bias);
	if (drive)
		gpiod_line_config_set_drive_default(line_cfg, drive);
	if (active_low)
		gpiod_line_config_set_active_low_default(line_cfg, true);
	gpiod_line_config_set_direction_default(line_cfg, GPIOD_LINE_DIRECTION_OUTPUT);

	req_cfg = gpiod_request_config_new();
	if (!req_cfg)
		die_perror("unable to allocate the request config structure");

	gpiod_request_config_set_consumer(req_cfg, "gpioset");
	resolver = resolve_lines(num_lines, lines, chip_id, strict, by_name);
	for (i = 0; i < num_lines; i++)
		resolver->lines[i].value = values[i];
	requests = calloc(resolver->num_chips, sizeof(*requests));
	offsets = calloc(num_lines, sizeof(*offsets));
	if (!requests || !offsets)
		die("out of memory");
	for (i = 0; i < resolver->num_chips; i++) {
		num_lines = get_line_offsets_and_values(resolver, i, offsets, values);
		gpiod_request_config_set_offsets(req_cfg, num_lines, offsets);
		gpiod_line_config_set_output_values(line_cfg, num_lines,
						    offsets, values);

		chip = gpiod_chip_open(resolver->chip_paths[i]);
		if (!chip)
			die_perror("unable to open chip %s", resolver->chip_paths[i]);

		requests[i] = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
		if (!requests[i])
			die_perror("unable to request lines on chip %s",
				   resolver->chip_paths[i]);

		gpiod_chip_close(chip);
	}
	gpiod_request_config_free(req_cfg);
	gpiod_line_config_free(line_cfg);

	if (daemonize)
		if (daemon(0, 0) < 0)
			die_perror("unable to daemonize");

	if (toggles) {
		for (i = 0; i < toggles; i++)
			if (hold_period_us > toggle_periods[i])
				toggle_periods[i] = hold_period_us;
		toggle_sequence(toggles, toggle_periods, requests, resolver, offsets, values);
		free(toggle_periods);
	}

	if (hold_period_us)
		usleep(hold_period_us);

	if (interactive)
		interact(requests, resolver, lines, offsets, values);

	if (daemonize)
		wait_fd(gpiod_line_request_get_fd(requests[0]));

	for (i = 0; i < resolver->num_chips; i++)
		gpiod_line_request_release(requests[i]);
	free(requests);
	free_line_resolver(resolver);
	free(lines);
	free(values);
	free(offsets);

	return EXIT_SUCCESS;
}

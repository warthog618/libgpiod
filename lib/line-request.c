// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2021 Bartosz Golaszewski <brgl@bgdev.pl>

#include <assert.h>
#include <errno.h>
#include <gpiod.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#include "internal.h"

struct gpiod_line_request {
	char *chip_name;
	unsigned int offsets[GPIO_V2_LINES_MAX];
	size_t num_lines;
	int fd;
};

struct gpiod_line_request *
gpiod_line_request_from_uapi(struct gpio_v2_line_request *uapi_req,
			     const char *chip_name)
{
	struct gpiod_line_request *request;

	request = malloc(sizeof(*request));
	if (!request)
		return NULL;

	memset(request, 0, sizeof(*request));

	request->chip_name = strdup(chip_name);
	if (!request->chip_name) {
		free(request);
		return NULL;
	}

	request->fd = uapi_req->fd;
	request->num_lines = uapi_req->num_lines;
	memcpy(request->offsets, uapi_req->offsets,
	       sizeof(*request->offsets) * request->num_lines);

	return request;
}

GPIOD_API void gpiod_line_request_release(struct gpiod_line_request *request)
{
	if (!request)
		return;

	close(request->fd);
	free(request->chip_name);
	free(request);
}

GPIOD_API const char *
gpiod_line_request_get_chip_name(struct gpiod_line_request *request)
{
	assert(request);

	return request->chip_name;
}

GPIOD_API size_t
gpiod_line_request_get_num_requested_lines(struct gpiod_line_request *request)
{
	assert(request);

	return request->num_lines;
}

GPIOD_API size_t
gpiod_line_request_get_requested_offsets(struct gpiod_line_request *request,
					 unsigned int *offsets,
					 size_t max_offsets)
{
	size_t num_offsets;

	assert(request);

	if (!offsets || !max_offsets)
		return 0;

	num_offsets = MIN(request->num_lines, max_offsets);

	memcpy(offsets, request->offsets, sizeof(*offsets) * num_offsets);

	return num_offsets;
}

GPIOD_API enum gpiod_line_value
gpiod_line_request_get_value(struct gpiod_line_request *request,
			     unsigned int offset)
{
	enum gpiod_line_value val;
	unsigned int ret;

	assert(request);

	ret = gpiod_line_request_get_values_subset(request, 1, &offset, &val);
	if (ret)
		return GPIOD_LINE_VALUE_ERROR;

	return val;
}

static int offset_to_bit(struct gpiod_line_request *request,
			 unsigned int offset)
{
	size_t i;

	assert(request);

	for (i = 0; i < request->num_lines; i++) {
		if (offset == request->offsets[i])
			return i;
	}

	return -1;
}

GPIOD_API int
gpiod_line_request_get_values_subset(struct gpiod_line_request *request,
				     size_t num_values,
				     const unsigned int *offsets,
				     enum gpiod_line_value *values)
{
	struct gpio_v2_line_values uapi_values;
	uint64_t mask = 0, bits = 0;
	size_t i;
	int bit, ret;

	assert(request);

	if (!offsets || !values) {
		errno = EINVAL;
		return -1;
	}

	uapi_values.bits = 0;

	for (i = 0; i < num_values; i++) {
		bit = offset_to_bit(request, offsets[i]);
		if (bit < 0) {
			errno = EINVAL;
			return -1;
		}

		gpiod_line_mask_set_bit(&mask, bit);
	}

	uapi_values.mask = mask;

	ret = gpiod_ioctl(request->fd, GPIO_V2_LINE_GET_VALUES_IOCTL,
			  &uapi_values);
	if (ret)
		return -1;

	bits = uapi_values.bits;
	memset(values, 0, sizeof(*values) * num_values);

	for (i = 0; i < num_values; i++) {
		bit = offset_to_bit(request, offsets[i]);
		values[i] = gpiod_line_mask_test_bit(&bits, bit) ? 1 : 0;
	}

	return 0;
}

GPIOD_API int gpiod_line_request_get_values(struct gpiod_line_request *request,
					    enum gpiod_line_value *values)
{
	assert(request);

	return gpiod_line_request_get_values_subset(request, request->num_lines,
						    request->offsets, values);
}

GPIOD_API int gpiod_line_request_set_value(struct gpiod_line_request *request,
					   unsigned int offset,
					   enum gpiod_line_value value)
{
	return gpiod_line_request_set_values_subset(request, 1,
						    &offset, &value);
}

GPIOD_API int
gpiod_line_request_set_values_subset(struct gpiod_line_request *request,
				     size_t num_values,
				     const unsigned int *offsets,
				     const enum gpiod_line_value *values)
{
	struct gpio_v2_line_values uapi_values;
	uint64_t mask = 0, bits = 0;
	size_t i;
	int bit;

	assert(request);

	if (!offsets || !values) {
		errno = EINVAL;
		return -1;
	}

	for (i = 0; i < num_values; i++) {
		bit = offset_to_bit(request, offsets[i]);
		if (bit < 0) {
			errno = EINVAL;
			return -1;
		}

		gpiod_line_mask_set_bit(&mask, bit);
		gpiod_line_mask_assign_bit(&bits, bit, values[i]);
	}

	memset(&uapi_values, 0, sizeof(uapi_values));
	uapi_values.mask = mask;
	uapi_values.bits = bits;

	return gpiod_ioctl(request->fd, GPIO_V2_LINE_SET_VALUES_IOCTL,
			   &uapi_values);
}

GPIOD_API int gpiod_line_request_set_values(struct gpiod_line_request *request,
					    const enum gpiod_line_value *values)
{
	assert(request);

	return gpiod_line_request_set_values_subset(request, request->num_lines,
						    request->offsets, values);
}

static bool offsets_equal(struct gpiod_line_request *request,
			  struct gpio_v2_line_request *uapi_cfg)
{
	size_t i;

	if (request->num_lines != uapi_cfg->num_lines)
		return false;

	for (i = 0; i < request->num_lines; i++) {
		if (request->offsets[i] != uapi_cfg->offsets[i])
			return false;
	}

	return true;
}

GPIOD_API int
gpiod_line_request_reconfigure_lines(struct gpiod_line_request *request,
				     struct gpiod_line_config *config)
{
	struct gpio_v2_line_request uapi_cfg;
	int ret;

	assert(request);

	if (!config) {
		errno = EINVAL;
		return -1;
	}

	memset(&uapi_cfg, 0, sizeof(uapi_cfg));

	ret = gpiod_line_config_to_uapi(config, &uapi_cfg);
	if (ret)
		return ret;

	if (!offsets_equal(request, &uapi_cfg)) {
		errno = EINVAL;
		return -1;
	}

	ret = gpiod_ioctl(request->fd, GPIO_V2_LINE_SET_CONFIG_IOCTL,
			  &uapi_cfg.config);
	if (ret)
		return ret;

	return 0;
}

GPIOD_API int gpiod_line_request_get_fd(struct gpiod_line_request *request)
{
	assert(request);

	return request->fd;
}

GPIOD_API int
gpiod_line_request_wait_edge_events(struct gpiod_line_request *request,
				    int64_t timeout_ns)
{
	assert(request);

	return gpiod_poll_fd(request->fd, timeout_ns);
}

GPIOD_API int
gpiod_line_request_read_edge_events(struct gpiod_line_request *request,
				    struct gpiod_edge_event_buffer *buffer,
				    size_t max_events)
{
	assert(request);

	return gpiod_edge_event_buffer_read_fd(request->fd, buffer, max_events);
}

#ifdef GPIOD_EXTENSIONS

static struct gpiod_line_request *
ext_request(const char  *path, unsigned int offset,
	    enum gpiod_line_direction direction,
	    enum gpiod_line_value value)
{
	struct gpiod_line_request *request = NULL;
	struct gpiod_line_settings *settings;
	struct gpiod_line_config *line_cfg;
	struct gpiod_chip *chip;
	int ret;

	chip = gpiod_chip_open(path);
	if (!chip)
		return NULL;

	settings = gpiod_line_settings_new();
	if (!settings)
		goto close_chip;

	gpiod_line_settings_set_direction(settings, direction);
	if (direction == GPIOD_LINE_DIRECTION_OUTPUT)
		gpiod_line_settings_set_output_value(settings, value);

	line_cfg = gpiod_line_config_new();
	if (!line_cfg)
		goto free_settings;

	ret = gpiod_line_config_add_line_settings(line_cfg, &offset, 1,
						  settings);
	if (ret)
		goto free_line_cfg;

	request = gpiod_chip_request_lines(chip, NULL, line_cfg);

free_line_cfg:
	gpiod_line_config_free(line_cfg);

free_settings:
	gpiod_line_settings_free(settings);

close_chip:
	gpiod_chip_close(chip);

	return request;
}

GPIOD_API struct gpiod_line_request *
gpiod_ext_request_input(const char  *path, unsigned int offset)
{
	return ext_request(path, offset, GPIOD_LINE_DIRECTION_INPUT, 0);
}

GPIOD_API struct gpiod_line_request *
gpiod_ext_request_output(const char  *path, unsigned int offset,
			 enum gpiod_line_value value)
{
	return ext_request(path, offset, GPIOD_LINE_DIRECTION_OUTPUT, value);
}

static struct gpiod_line_settings *
ext_line_settings(struct gpiod_line_request * req)
{
	struct gpiod_line_settings *settings = NULL;
	struct gpiod_line_info *line_info;
	struct gpiod_chip *chip;
	char path[32];

	assert(req);

	if (req->num_lines != 1) {
		errno = EINVAL;
		return NULL;
	}

	/*
	 * This is all decidedly non-optimal, as generally the user has the
	 * config available from when they made the request, but here we need to
	 * rebuild it from the line info...
	 */
	memcpy(path, "/dev/", 5);
	strncpy(&path[5], req->chip_name, 26);
	chip = gpiod_chip_open(path);
	if (!chip)
		return NULL;

	// get info
	line_info = gpiod_chip_get_line_info(chip, req->offsets[0]);
	gpiod_chip_close(chip);
	if (!line_info)
		return NULL;

	if (gpiod_line_info_get_direction(line_info) != GPIOD_LINE_DIRECTION_INPUT) {
		errno = EINVAL;
		goto free_info;
	}

	settings = gpiod_line_settings_new();
	if (!settings)
		goto free_info;

	gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
	gpiod_line_settings_set_bias(settings,
		gpiod_line_info_get_bias(line_info));
	gpiod_line_settings_set_edge_detection(settings,
		gpiod_line_info_get_edge_detection(line_info));
	gpiod_line_settings_set_debounce_period_us(settings,
		gpiod_line_info_get_debounce_period_us(line_info));

free_info:
	gpiod_line_info_free(line_info);

	return settings;
}

static int
ext_reconfigure(struct gpiod_line_request *request, struct gpiod_line_settings *settings)
{
	struct gpiod_line_config *line_cfg;
	int ret;

	line_cfg = gpiod_line_config_new();
	if (!line_cfg)
		return -1;

	ret = gpiod_line_config_add_line_settings(line_cfg, request->offsets, 1,
						  settings);
	if (ret)
		goto free_line_cfg;

	ret = gpiod_line_request_reconfigure_lines(request, line_cfg);

free_line_cfg:
	gpiod_line_config_free(line_cfg);

	return ret;
}

GPIOD_API int
gpiod_ext_set_bias(struct gpiod_line_request * req,
		   enum gpiod_line_bias bias)
{
	int ret;

	struct gpiod_line_settings *settings;

	settings = ext_line_settings(req);
	if (!settings)
		return -1;

	ret = gpiod_line_settings_set_bias(settings, bias);
	if (!ret)
		ret = ext_reconfigure(req, settings);

	gpiod_line_settings_free(settings);

	return ret;
}

GPIOD_API int
gpiod_ext_set_debounce_period_us(struct gpiod_line_request * req,
				 unsigned long period)
{
	struct gpiod_line_settings *settings;
	int ret;

	settings = ext_line_settings(req);
	if (!settings)
		return -1;

	gpiod_line_settings_set_debounce_period_us(settings, period);
	ret = ext_reconfigure(req, settings);

	gpiod_line_settings_free(settings);

	return ret;
}

GPIOD_API int
gpiod_ext_set_edge_detection(struct gpiod_line_request * req,
			     enum gpiod_line_edge edge)
{
	struct gpiod_line_settings *settings;
	int ret;

	settings = ext_line_settings(req);
	if (!settings)
		return -1;

	ret = gpiod_line_settings_set_edge_detection(settings, edge);
	if (!ret)
		ret = ext_reconfigure(req, settings);

	gpiod_line_settings_free(settings);

	return ret;
}

#endif

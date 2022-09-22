/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* SPDX-FileCopyrightText: 2017-2022 Bartosz Golaszewski <brgl@bgdev.pl> */

/**
 * @file gpiod.h
 */

#ifndef __LIBGPIOD_GPIOD_H__
#define __LIBGPIOD_GPIOD_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @mainpage libgpiod public API
 *
 * This is the complete documentation of the public API made available to
 * users of libgpiod.
 *
 * <p>The API is logically split into several parts such as: GPIO chip & line
 * operators, GPIO events handling etc.
 *
 * <p>General note on error handling: all functions exported by libgpiod that
 * can fail, set errno to one of the error values defined in errno.h upon
 * failure. The way of notifying the caller that an error occurred varies
 * between functions, but in general a function that returns an int, returns -1
 * on error, while a function returning a pointer indicates an error condition
 * by returning a NULL pointer. It's not practical to list all possible error
 * codes for every function as they propagate errors from the underlying libc
 * functions.
 *
 * <p>In general libgpiod functions are not NULL-aware and it's expected that
 * users pass valid pointers to objects as arguments. An exception to this rule
 * are the functions that free/close/release resources - which work when passed
 * a NULL-pointer as argument. Other exceptions are documented.
 */

struct gpiod_chip;
struct gpiod_line_info;
struct gpiod_line_config;
struct gpiod_request_config;
struct gpiod_line_request;
struct gpiod_info_event;
struct gpiod_edge_event;
struct gpiod_edge_event_buffer;

/**
 * @defgroup chips GPIO chips
 * @{
 *
 * Functions and data structures for GPIO chip operations.
 *
 * A GPIO chip object is associated with an open file descriptor to the GPIO
 * character device. It exposes basic information about the chip and allows
 * callers to retrieve information about each line, watch lines for state
 * changes and make line requests.
 */

/**
 * @brief Open a chip by path.
 * @param path Path to the gpiochip device file.
 * @return GPIO chip request or NULL if an error occurred.
 */
struct gpiod_chip *gpiod_chip_open(const char *path);

/**
 * @brief Close the chip and release all associated resources.
 * @param chip Chip to close.
 */
void gpiod_chip_close(struct gpiod_chip *chip);

/**
 * @brief Get the name of the chip as represented in the kernel.
 * @param chip GPIO chip object.
 * @return Pointer to a human-readable string containing the chip name.
 */
const char *gpiod_chip_get_name(struct gpiod_chip *chip);

/**
 * @brief Get the label of the chip as represented in the kernel.
 * @param chip GPIO chip object.
 * @return Pointer to a human-readable string containing the chip label.
 */
const char *gpiod_chip_get_label(struct gpiod_chip *chip);

/**
 * @brief Get the path used to open the chip.
 * @param chip GPIO chip object.
 * @return Path to the file passed as argument to ::gpiod_chip_open.
 */
const char *gpiod_chip_get_path(struct gpiod_chip *chip);

/**
 * @brief Get the number of lines exposed by the chip.
 * @param chip GPIO chip object.
 * @return Number of GPIO lines.
 */
size_t gpiod_chip_get_num_lines(struct gpiod_chip *chip);

/**
 * @brief Get a snapshot of information about a line.
 * @param chip GPIO chip object.
 * @param offset The offset of the GPIO line.
 * @return New GPIO line info object or NULL if an error occurred. The returned
 *	   object must be freed by the caller using ::gpiod_line_info_free.
 */
struct gpiod_line_info *gpiod_chip_get_line_info(struct gpiod_chip *chip,
						 unsigned int offset);

/**
 * @brief Get a snapshot of the status of a line and start watching it for
 *	  future changes.
 * @param chip GPIO chip object.
 * @param offset The offset of the GPIO line.
 * @return New GPIO line info object or NULL if an error occurred. The returned
 *	   object must be freed by the caller using ::gpiod_line_info_free.
 * @note Line status does not include the line value.  To monitor the line
 *	 value the line must be requested as an input with edge detection set.
 */
struct gpiod_line_info *gpiod_chip_watch_line_info(struct gpiod_chip *chip,
						   unsigned int offset);

/**
 * @brief Stop watching a line for status changes.
 * @param chip GPIO chip object.
 * @param offset The offset of the line to stop watching.
 * @return 0 on success, -1 on failure.
 */
int gpiod_chip_unwatch_line_info(struct gpiod_chip *chip, unsigned int offset);

/**
 * @brief Get the file descriptor associated with the chip.
 * @param chip GPIO chip object.
 * @return File descriptor number for the chip.
 *	   This function never fails.
 *	   The returned file descriptor must not be closed by the caller.
 *	   Call ::gpiod_chip_close to close the file descriptor.
 */
int gpiod_chip_get_fd(struct gpiod_chip *chip);

/**
 * @brief Wait for line status change events on any of the watched lines
 *	  on the chip.
 * @param chip GPIO chip object.
 * @param timeout_ns Wait time limit in nanoseconds.
 * @return 0 if wait timed out, -1 if an error occurred, 1 if an event is
 *	   pending.
 */
int gpiod_chip_wait_info_event(struct gpiod_chip *chip, uint64_t timeout_ns);

/**
 * @brief Read a single line status change event from the chip.
 * @param chip GPIO chip object.
 * @return Newly read watch event object or NULL on error. The event must be
 *	   freed by the caller using ::gpiod_info_event_free.
 * @note If no events are pending, this function will block.
 */
struct gpiod_info_event *gpiod_chip_read_info_event(struct gpiod_chip *chip);

/**
 * @brief Map a line's name to its offset within the chip.
 * @param chip GPIO chip object.
 * @param name Name of the GPIO line to map.
 * @return Offset of the line within the chip or -1 on error.
 * @note If a line with given name is not exposed by the chip, the function
 *       sets errno to ENOENT.
 */
int gpiod_chip_find_line(struct gpiod_chip *chip, const char *name);

/**
 * @brief Request a set of lines for exclusive usage.
 * @param chip GPIO chip object.
 * @param req_cfg Request config object.
 * @param line_cfg Line config object.
 * @return New line request object or NULL if an error occurred. The request
 *	   must be released by the caller using ::gpiod_line_request_release.
 * @note Line configuration overrides for lines that are not requested are
 *	 silently ignored.
 */
struct gpiod_line_request *
gpiod_chip_request_lines(struct gpiod_chip *chip,
			 struct gpiod_request_config *req_cfg,
			 struct gpiod_line_config *line_cfg);

/**
 * @}
 *
 * @defgroup line_settings Line definitions
 * @{
 *
 * These defines are used across the API.
 */

/**
 * @brief Logical line state.
 */
enum {
	GPIOD_LINE_VALUE_INACTIVE = 0,
	GPIOD_LINE_VALUE_ACTIVE = 1,
};

/**
 * @brief Direction settings.
 */
enum {
	GPIOD_LINE_DIRECTION_AS_IS = 1,
	/**< Request the line(s), but don't change direction. */
	GPIOD_LINE_DIRECTION_INPUT,
	/**< Direction is input - for reading the value of an externally driven GPIO line. */
	GPIOD_LINE_DIRECTION_OUTPUT
	/**< Direction is output - for driving the GPIO line. */
};

/**
 * @brief Internal bias settings.
 */
enum {
	GPIOD_LINE_BIAS_AS_IS = 1,
	/**< Don't change the bias setting when applying line config. */
	GPIOD_LINE_BIAS_UNKNOWN,
	/**< The internal bias state is unknown. */
	GPIOD_LINE_BIAS_DISABLED,
	/**< The internal bias is disabled. */
	GPIOD_LINE_BIAS_PULL_UP,
	/**< The internal pull-up bias is enabled. */
	GPIOD_LINE_BIAS_PULL_DOWN
	/**< The internal pull-down bias is enabled. */
};

/**
 * @brief Drive settings.
 */
enum {
	GPIOD_LINE_DRIVE_PUSH_PULL = 1,
	/**< Drive setting is push-pull. */
	GPIOD_LINE_DRIVE_OPEN_DRAIN,
	/**< Line output is open-drain. */
	GPIOD_LINE_DRIVE_OPEN_SOURCE
	/**< Line output is open-source. */
};

/**
 * @brief Edge detection settings.
 */
enum {
	GPIOD_LINE_EDGE_NONE = 1,
	/**< Line edge detection is disabled. */
	GPIOD_LINE_EDGE_RISING,
	/**< Line detects rising edge events. */
	GPIOD_LINE_EDGE_FALLING,
	/**< Line detects falling edge events. */
	GPIOD_LINE_EDGE_BOTH
	/**< Line detects both rising and falling edge events. */
};

/**
 * @brief Event clock settings.
 */
enum {
	GPIOD_LINE_EVENT_CLOCK_MONOTONIC = 1,
	/**< Line uses the monotonic clock for edge event timestamps. */
	GPIOD_LINE_EVENT_CLOCK_REALTIME,
	/**< Line uses the realtime clock for edge event timestamps. */
};

/**
 * @}
 *
 * @defgroup line_info Line info
 * @{
 *
 * Functions for retrieving kernel information about both requested and free
 * lines.
 *
 * Line info object contains an immutable snapshot of a line's status.
 *
 * The line info contains all the publicly available information about a
 * line, which does not include the line value.  The line must be requested
 * to access the line value.
 *
 * Some accessor methods return pointers.  Those pointers refer to internal
 * fields.  The lifetimes of those fields are tied to the lifetime of the
 * containing line info object.
 * Such pointers remain valid until ::gpiod_line_info_free is called on the
 * containing line info object. They must not be freed by the caller.
 */

/**
 * @brief Free a line info object and release all associated resources.
 * @param info GPIO line info object to free.
 */
void gpiod_line_info_free(struct gpiod_line_info *info);

/**
 * @brief Copy a line info object.
 * @param info Line info to copy.
 * @return Copy of the line info or NULL on error. The returned object must
 *	   be freed by the caller using :gpiod_line_info_free.
 */
struct gpiod_line_info *gpiod_line_info_copy(struct gpiod_line_info *info);

/**
 * @brief Get the offset of the line.
 * @param info GPIO line info object.
 * @return Offset of the line within the parent chip.
 *
 * The offset uniquely identifies the line on the chip.
 * The combination of the chip and offset uniquely identifies the line within
 * the system.
 */
unsigned int gpiod_line_info_get_offset(struct gpiod_line_info *info);

/**
 * @brief Get the name of the line.
 * @param info GPIO line info object.
 * @return Name of the GPIO line as it is represented in the kernel.
 *	   This function returns a pointer to a null-terminated string
 *	   or NULL if the line is unnamed.
 */
const char *gpiod_line_info_get_name(struct gpiod_line_info *info);

/**
 * @brief Check if the line is in use.
 * @param info GPIO line object.
 * @return True if the line is in use, false otherwise.
 *
 * The exact reason a line is busy cannot be determined from user space.
 * It may have been requested by another process or hogged by the kernel.
 * It only matters that the line is used and can't be requested until
 * released by the existing consumer.
 */
bool gpiod_line_info_is_used(struct gpiod_line_info *info);

/**
 * @brief Get the name of the consumer of the line.
 * @param info GPIO line info object.
 * @return Name of the GPIO consumer as it is represented in the kernel.
 *	   This function returns a pointer to a null-terminated string
 *	   or NULL if the consumer name is not set.
 */
const char *gpiod_line_info_get_consumer(struct gpiod_line_info *info);

/**
 * @brief Get the direction setting of the line.
 * @param info GPIO line info object.
 * @return Returns ::GPIOD_LINE_DIRECTION_INPUT or
 *	   ::GPIOD_LINE_DIRECTION_OUTPUT.
 */
int gpiod_line_info_get_direction(struct gpiod_line_info *info);

/**
 * @brief Check if the logical value of the line is inverted compared to the
 *	  physical.
 * @param info GPIO line object.
 * @return True if the line is "active-low", false otherwise.
 */
bool gpiod_line_info_is_active_low(struct gpiod_line_info *info);

/**
 * @brief Get the bias setting of the line.
 * @param info GPIO line object.
 * @return Returns ::GPIOD_LINE_BIAS_PULL_UP, ::GPIOD_LINE_BIAS_PULL_DOWN,
 *	   ::GPIOD_LINE_BIAS_DISABLED or ::GPIOD_LINE_BIAS_UNKNOWN.
 */
int gpiod_line_info_get_bias(struct gpiod_line_info *info);

/**
 * @brief Get the drive setting of the line.
 * @param info GPIO line info object.
 * @return Returns ::GPIOD_LINE_DRIVE_PUSH_PULL, ::GPIOD_LINE_DRIVE_OPEN_DRAIN
 *	   or ::GPIOD_LINE_DRIVE_OPEN_SOURCE.
 */
int gpiod_line_info_get_drive(struct gpiod_line_info *info);

/**
 * @brief Get the edge detection setting of the line.
 * @param info GPIO line info object.
 * @return Returns ::GPIOD_LINE_EDGE_NONE, ::GPIOD_LINE_EDGE_RISING,
 *	   ::GPIOD_LINE_EDGE_FALLING or ::GPIOD_LINE_EDGE_BOTH.
 */
int gpiod_line_info_get_edge_detection(struct gpiod_line_info *info);

/**
 * @brief Get the event clock setting used for edge event timestamps for the
 *	  line.
 * @param info GPIO line info object.
 * @return Returns ::GPIOD_LINE_EVENT_CLOCK_MONOTONIC or
 *	   ::GPIOD_LINE_EVENT_CLOCK_REALTIME.
 */
int gpiod_line_info_get_event_clock(struct gpiod_line_info *info);

/**
 * @brief Check if the line is debounced (either by hardware or by the kernel
 *	  software debouncer).
 * @param info GPIO line info object.
 * @return True if the line is debounced, false otherwise.
 */
bool gpiod_line_info_is_debounced(struct gpiod_line_info *info);

/**
 * @brief Get the debounce period of the line, in microseconds.
 * @param info GPIO line info object.
 * @return Debounce period in microseconds.
 *	   0 if the line is not debounced.
 */
unsigned long
gpiod_line_info_get_debounce_period_us(struct gpiod_line_info *info);

/**
 * @}
 *
 * @defgroup line_watch Line status watch events
 * @{
 *
 * Accessors for the info event objects allowing to monitor changes in GPIO
 * line status.
 *
 * Callers are notified about changes in a line's status due to GPIO uAPI
 * calls. Each info event contains information about the event itself
 * (timestamp, type) as well as a snapshot of line's status in the form
 * of a line-info object.
 */

/**
 * @brief Line status change event types.
 */
enum {
	GPIOD_INFO_EVENT_LINE_REQUESTED = 1,
	/**< Line has been requested. */
	GPIOD_INFO_EVENT_LINE_RELEASED,
	/**< Previously requested line has been released. */
	GPIOD_INFO_EVENT_LINE_CONFIG_CHANGED
	/**< Line configuration has changed. */
};

/**
 * @brief Free the info event object and release all associated resources.
 * @param event Info event to free.
 */
void gpiod_info_event_free(struct gpiod_info_event *event);

/**
 * @brief Get the event type of the status change event.
 * @param event Line status watch event.
 * @return One of ::GPIOD_INFO_EVENT_LINE_REQUESTED,
 *	   ::GPIOD_INFO_EVENT_LINE_RELEASED or
 *	   ::GPIOD_INFO_EVENT_LINE_CONFIG_CHANGED.
 */
int gpiod_info_event_get_event_type(struct gpiod_info_event *event);

/**
 * @brief Get the timestamp of the event.
 * @param event Line status watch event.
 * @return Timestamp in nanoseconds, read from the monotonic clock.
 */
uint64_t gpiod_info_event_get_timestamp(struct gpiod_info_event *event);

/**
 * @brief Get the snapshot of line-info associated with the event.
 * @param event Line info event object.
 * @return Returns a pointer to the line-info object associated with the event
 *	   whose lifetime is tied to the event object. It must not be freed by
 *	   the caller.
 */
struct gpiod_line_info *
gpiod_info_event_get_line_info(struct gpiod_info_event *event);

/**
 * @}
 *
 * @defgroup line_config Line configuration objects
 * @{
 *
 * Functions for manipulating line configuration objects.
 *
 * The line-config object contains the configuration for lines that can be
 * used in two cases:
 *  - when making a line request
 *  - when reconfiguring a set of already requested lines.
 *
 * A new line-config object is instantiated with a set of sane defaults
 * for all supported configuration settings. Those defaults can be modified by
 * the caller. Default values can be overridden by applying different values
 * for specific lines. When making a request or reconfiguring an existing one,
 * the overridden settings for specific lines take precedance. For lines
 * without an override the requested default settings are used.
 *
 * For every setting there are two mutators (one setting the default and one
 * for the per-line override), two getters (one for reading the global
 * default and one for retrieving the effective value for the line),
 * a function for testing if a setting is overridden for the line
 * and finally a function for clearing the overrides (per line).
 *
 * The mutators don't return errors. If the set of options is too complex to
 * be translated into kernel uAPI structures then an error will be returned at
 * the time of the request or reconfiguration. If an invalid value was passed
 * to any of the mutators then the default value will be silently used instead.
 *
 * Operating on lines in struct line_config has no immediate effect on real
 * GPIOs, it only manipulates the config object in memory.  Those changes are
 * only applied to the hardware at the time of the request or reconfiguration.
 *
 * Overrides for lines that don't end up being requested are silently ignored
 * both in ::gpiod_chip_request_lines as well as in
 * ::gpiod_line_request_reconfigure_lines.
 *
 * In cases where all requested lines are using the one configuration, the
 * line overrides can be entirely ignored when preparing the configuration.
 */

/**
 * @brief Create a new line config object.
 * @return New line config object or NULL on error.
 */
struct gpiod_line_config *gpiod_line_config_new(void);

/**
 * @brief Free the line config object and release all associated resources.
 * @param config Line config object to free.
 */
void gpiod_line_config_free(struct gpiod_line_config *config);

/**
 * @brief Reset the line config object.
 * @param config Line config object to free.
 *
 * Resets the entire configuration stored in the object. This is useful if
 * the user wants to reuse the object without reallocating it.
 */
void gpiod_line_config_reset(struct gpiod_line_config *config);

/**
 * @brief Set the default line direction.
 * @param config Line config object.
 * @param direction New direction.
 */
void gpiod_line_config_set_direction_default(struct gpiod_line_config *config,
					     int direction);

/**
 * @brief Set the direction override for a line.
 * @param config Line config object.
 * @param direction New direction.
 * @param offset The offset of the line for which to set the override.
 */
void gpiod_line_config_set_direction_override(struct gpiod_line_config *config,
					      int direction,
					      unsigned int offset);

/**
 * @brief Clear the direction override for a line.
 * @param config Line config object.
 * @param offset The offset of the line for which to clear the override.
 * @note Does nothing if no override is set for the line.
 */
void
gpiod_line_config_clear_direction_override(struct gpiod_line_config *config,
					   unsigned int offset);

/**
 * @brief Check if the direction is overridden for a line.
 * @param config Line config object.
 * @param offset The offset of the line to check for the override.
 * @return True if direction is overridden on the line, false otherwise.
 */
bool gpiod_line_config_direction_is_overridden(struct gpiod_line_config *config,
					       unsigned int offset);

/**
 * @brief Get the default direction setting.
 * @param config Line config object.
 * @return Direction setting used for any non-overridden line.
 */
int gpiod_line_config_get_direction_default(struct gpiod_line_config *config);

/**
 * @brief Get the direction setting for a line.
 * @param config Line config object.
 * @param offset The offset of the line for which to read the direction.
 * @return Direction setting for the line if the config object were used
 *	   in a request.
 */
int gpiod_line_config_get_direction_offset(struct gpiod_line_config *config,
					   unsigned int offset);

/**
 * @brief Set the default edge event detection.
 * @param config Line config object.
 * @param edge Type of edge events to detect.
 */
void
gpiod_line_config_set_edge_detection_default(struct gpiod_line_config *config,
					     int edge);

/**
 * @brief Set the edge detection override for a line.
 * @param config Line config object.
 * @param edge Type of edge events to detect.
 * @param offset The offset of the line for which to set the override.
 */
void
gpiod_line_config_set_edge_detection_override(struct gpiod_line_config *config,
					      int edge, unsigned int offset);

/**
 * @brief Clear the edge detection override for a line.
 * @param config Line config object.
 * @param offset The offset of the line for which to clear the override.
 * @note Does nothing if no override is set for the line.
 */
void
gpiod_line_config_clear_edge_detection_override(
			struct gpiod_line_config *config, unsigned int offset);

/**
 * @brief Check if the edge detection setting is overridden for a line.
 * @param config Line config object.
 * @param offset The offset of the line to check for the override.
 * @return True if edge detection is overridden for the line, false otherwise.
 */
bool
gpiod_line_config_edge_detection_is_overridden(struct gpiod_line_config *config,
					       unsigned int offset);

/**
 * @brief Get the default edge detection setting.
 * @param config Line config object.
 * @return Edge detection setting used for any non-overridden line.
 */
int
gpiod_line_config_get_edge_detection_default(struct gpiod_line_config *config);

/**
 * @brief Get the edge event detection setting for a line.
 * @param config Line config object.
 * @param offset The offset of the line for which to read the edge event detection
 *		 setting.
 * @return Edge event detection setting for the line if the config object
 *	   were used in a request.
 */
int
gpiod_line_config_get_edge_detection_offset(struct gpiod_line_config *config,
					    unsigned int offset);

/**
 * @brief Set the default bias setting.
 * @param config Line config object.
 * @param bias New bias.
 */
void gpiod_line_config_set_bias_default(struct gpiod_line_config *config,
					int bias);

/**
 * @brief Set the bias override for a line.
 * @param config Line config object.
 * @param bias New bias setting.
 * @param offset The offset of the line for which to set the override.
 */
void gpiod_line_config_set_bias_override(struct gpiod_line_config *config,
					 int bias, unsigned int offset);

/**
 * @brief Clear the bias override for a line.
 * @param config Line config object.
 * @param offset The offset of the line for which to clear the override.
 * @note Does nothing if no override is set for the line.
 */
void gpiod_line_config_clear_bias_override(struct gpiod_line_config *config,
					   unsigned int offset);

/**
 * @brief Check if the bias setting is overridden for a line.
 * @param config Line config object.
 * @param offset The offset of the line to check for the override.
 * @return True if bias is overridden for the line, false otherwise.
 */
bool gpiod_line_config_bias_is_overridden(struct gpiod_line_config *config,
					  unsigned int offset);
/**
 * @brief Get the default bias setting.
 * @param config Line config object.
 * @return Bias setting used for any non-overridden line.
 */
int gpiod_line_config_get_bias_default(struct gpiod_line_config *config);

/**
 * @brief Get the bias setting for a line.
 * @param config Line config object.
 * @param offset The offset of the line for which to read the bias setting.
 * @return Bias setting used for the line if the config object were used
 *	   in a request.
 */
int gpiod_line_config_get_bias_offset(struct gpiod_line_config *config,
			       unsigned int offset);

/**
 * @brief Set the default drive setting.
 * @param config Line config object.
 * @param drive New drive.
 */
void gpiod_line_config_set_drive_default(struct gpiod_line_config *config,
					 int drive);

/**
 * @brief Set the drive override for a line.
 * @param config Line config object.
 * @param drive New drive setting.
 * @param offset The offset of the line for which to set the override.
 */
void gpiod_line_config_set_drive_override(struct gpiod_line_config *config,
					  int drive, unsigned int offset);

/**
 * @brief Clear the drive override for a line.
 * @param config Line config object.
 * @param offset The offset of the line for which to clear the override.
 * @note Does nothing if no override is set for the line.
 */
void gpiod_line_config_clear_drive_override(struct gpiod_line_config *config,
					    unsigned int offset);

/**
 * @brief Check if the drive setting is overridden for a line.
 * @param config Line config object.
 * @param offset The offset of the line to check for the override.
 * @return True if drive is overridden for the line, false otherwise.
 */
bool gpiod_line_config_drive_is_overridden(struct gpiod_line_config *config,
					   unsigned int offset);

/**
 * @brief Get the default drive setting.
 * @param config Line config object.
 * @return Drive setting for any non-overridden line.
 */
int gpiod_line_config_get_drive_default(struct gpiod_line_config *config);

/**
 * @brief Get the drive setting for a line.
 * @param config Line config object.
 * @param offset The offset of the line for which to read the drive setting.
 * @return Drive setting for the line if the config object were used in a
 *	   request.
 */
int gpiod_line_config_get_drive_offset(struct gpiod_line_config *config,
				       unsigned int offset);

/**
 * @brief Set the default active-low setting.
 * @param config Line config object.
 * @param active_low New active-low setting.
 */
void gpiod_line_config_set_active_low_default(struct gpiod_line_config *config,
					      bool active_low);

/**
 * @brief Override the active-low setting for a line.
 * @param config Line config object.
 * @param active_low New active-low setting.
 * @param offset The offset of the line for which to set the override.
 */
void gpiod_line_config_set_active_low_override(struct gpiod_line_config *config,
					       bool active_low,
					       unsigned int offset);

/**
 * @brief Clear the active-low override for a line.
 * @param config Line config object.
 * @param offset The offset of the line for which to clear the override.
 * @note Does nothing if no override is set for the line.
 */
void
gpiod_line_config_clear_active_low_override(struct gpiod_line_config *config,
					    unsigned int offset);

/**
 * @brief Check if the active-low setting is overridden for a line.
 * @param config Line config object.
 * @param offset The offset of the line to check for the override.
 * @return True if active-low is overridden for the line, false otherwise.
 */
bool
gpiod_line_config_active_low_is_overridden(struct gpiod_line_config *config,
					   unsigned int offset);

/**
 * @brief Check if active-low is the default setting.
 * @param config Line config object.
 * @return Active-low setting for any non-overridden line.
 */
bool gpiod_line_config_get_active_low_default(struct gpiod_line_config *config);

/**
 * @brief Check if a line is configured as active-low.
 * @param config Line config object.
 * @param offset The offset of the line for which to read the active-low setting.
 * @return Active-low setting for the line if the config object were used in
 *	   a request.
 */
bool gpiod_line_config_get_active_low_offset(struct gpiod_line_config *config,
					     unsigned int offset);

/**
 * @brief Set the default debounce period.
 * @param config Line config object.
 * @param period New debounce period in microseconds. Disables debouncing if 0.
 * @note Debouncing is only useful on input lines with edge detection.
 *	 Its purpose is to filter spurious events due to noise during the
 *	 edge transition.  It has no effect on normal get or set operations.
 */
void gpiod_line_config_set_debounce_period_us_default(
		struct gpiod_line_config *config, unsigned long period);

/**
 * @brief Override the debounce period setting for a line.
 * @param config Line config object.
 * @param period New debounce period in microseconds.
 * @param offset The offset of the line for which to set the override.
 */
void
gpiod_line_config_set_debounce_period_us_override(
					struct gpiod_line_config *config,
					unsigned long period,
					unsigned int offset);

/**
 * @brief Clear the debounce period override for a line.
 * @param config Line config object.
 * @param offset The offset of the line for which to clear the override.
 * @note Does nothing if no override is set for the line.
 */
void gpiod_line_config_clear_debounce_period_us_override(
					struct gpiod_line_config *config,
					unsigned int offset);

/**
 * @brief Check if the debounce period setting is overridden for a line.
 * @param config Line config object.
 * @param offset The offset of the line to check for the override.
 * @return True if debounce period is overridden for the line, false
 *	   otherwise.
 */
bool gpiod_line_config_debounce_period_us_is_overridden(
					struct gpiod_line_config *config,
					unsigned int offset);

/**
 * @brief Get the default debounce period.
 * @param config Line config object.
 * @return Debounce period for any non-overridden line.
 *	   Measured in microseconds.
 *	   0 if debouncing is disabled.
 */
unsigned long gpiod_line_config_get_debounce_period_us_default(
					struct gpiod_line_config *config);

/**
 * @brief Get the debounce period for a line.
 * @param config Line config object.
 * @param offset The offset of the line for which to read the debounce period.
 * @return Debounce period for the line if the config object were used in a
 *	   request.
 *	   Measured in microseconds.
 *	   0 if debouncing is disabled.
 */
unsigned long
gpiod_line_config_get_debounce_period_us_offset(
			struct gpiod_line_config *config, unsigned int offset);

/**
 * @brief Set the default event timestamp clock.
 * @param config Line config object.
 * @param clock New clock to use.
 */
void gpiod_line_config_set_event_clock_default(struct gpiod_line_config *config,
					       int clock);

/**
 * @brief Override the event clock setting for a line.
 * @param config Line config object.
 * @param clock New event clock to use.
 * @param offset The offset of the line for which to set the override.
 */
void
gpiod_line_config_set_event_clock_override(struct gpiod_line_config *config,
					   int clock, unsigned int offset);

/**
 * @brief Clear the event clock override for a line.
 * @param config Line config object.
 * @param offset The offset of the line for which to clear the override.
 * @note Does nothing if no override is set for the line.
 */
void
gpiod_line_config_clear_event_clock_override(struct gpiod_line_config *config,
					     unsigned int offset);

/**
 * @brief Check if the event clock setting is overridden for a line.
 * @param config Line config object.
 * @param offset The offset of the line to check for the override.
 * @return True if event clock period is overridden for the line, false
 *	   otherwise.
 */
bool
gpiod_line_config_event_clock_is_overridden(struct gpiod_line_config *config,
					    unsigned int offset);

/**
 * @brief Get the default event clock setting.
 * @param config Line config object.
 * @return Event clock setting for any non-overridden line.
 */
int gpiod_line_config_get_event_clock_default(struct gpiod_line_config *config);

/**
 * @brief Get the event clock setting for a line.
 * @param config Line config object.
 * @param offset The offset of the line for which to read the event clock setting.
 * @return Event clock setting for the line if the config object were used in a
 *	   request.
 */
int gpiod_line_config_get_event_clock_offset(struct gpiod_line_config *config,
					     unsigned int offset);

/**
 * @brief Set the default output value.
 * @param config Line config object.
 * @param value New value.
 *
 * The default output value applies to all non-overridden output lines.
 * It does not apply to input lines or overridden lines.
 */
void
gpiod_line_config_set_output_value_default(struct gpiod_line_config *config,
					   int value);

/**
 * @brief Override the output value for a line.
 * @param config Line config object.
 * @param offset The offset of the line for which to override the output value.
 * @param value Output value to set.
 */
void
gpiod_line_config_set_output_value_override(struct gpiod_line_config *config,
					    unsigned int offset, int value);

/**
 * @brief Override the output values for multiple offsets.
 * @param config Line config object.
 * @param num_values Number of offsets for which to override values.
 * @param offsets Array of line offsets to override values for.
 * @param values Array of output values associated with the offsets passed in
 *               the previous argument.
 */
void gpiod_line_config_set_output_values(struct gpiod_line_config *config,
					 size_t num_values,
					 const unsigned int *offsets,
					 const int *values);

/**
 * @brief Clear the output value override for a line.
 * @param config Line config object.
 * @param offset The offset of the line for which to clear the override.
 * @note Does nothing if no override is set for the line.
 */
void
gpiod_line_config_clear_output_value_override(struct gpiod_line_config *config,
					      unsigned int offset);

/**
 * @brief Check if the output value is overridden for a line.
 * @param config Line config object.
 * @param offset The offset of the line to check for the override.
 * @return True if output value is overridden for the line, false otherwise.
 */
bool
gpiod_line_config_output_value_is_overridden(struct gpiod_line_config *config,
					     unsigned int offset);

/**
 * @brief Get the default output value.
 * @param config Line config object.
 * @return Output value for any non-overridden line.
 */
int
gpiod_line_config_get_output_value_default(struct gpiod_line_config *config);

/**
 * @brief Get the configured output value for a line.
 * @param config Line config object.
 * @param offset Line offset for which to read the value.
 * @return Output value for the line if the config object were used in a
 *	   request.
 */
int gpiod_line_config_get_output_value_offset(struct gpiod_line_config *config,
					      unsigned int offset);

/**
 * @brief List of properties that can be stored in a line_config object.
 *
 * Used when retrieving the overrides.
 */
enum {
	GPIOD_LINE_CONFIG_PROP_END = 0,
	/**< Delimiter. */
	GPIOD_LINE_CONFIG_PROP_DIRECTION,
	/**< Line direction. */
	GPIOD_LINE_CONFIG_PROP_EDGE,
	/**< Edge detection. */
	GPIOD_LINE_CONFIG_PROP_BIAS,
	/**< Bias. */
	GPIOD_LINE_CONFIG_PROP_DRIVE,
	/**< Drive. */
	GPIOD_LINE_CONFIG_PROP_ACTIVE_LOW,
	/**< Active-low setting. */
	GPIOD_LINE_CONFIG_PROP_DEBOUNCE_PERIOD,
	/** Debounce period. */
	GPIOD_LINE_CONFIG_PROP_EVENT_CLOCK,
	/**< Event clock type. */
	GPIOD_LINE_CONFIG_PROP_OUTPUT_VALUE,
	/**< Output value. */
};

/**
 * @brief Get the total number of overridden settings stored in the line config
 *	  object.
 * @param config Line config object.
 * @return Number of individual overridden settings.
 */
size_t gpiod_line_config_get_num_overrides(struct gpiod_line_config *config);

/**
 * @brief Get the list of overridden offsets and the corresponding types of
 *	  overridden settings.
 * @param config Line config object.
 * @param offsets Array to store the overidden offsets. Must be sized to hold
 *		  the number of unsigned integers returned by
 *		  ::gpiod_line_config_get_num_overrides.
 * @param props Array to store the types of overridden settings. Must be sized
 *		to hold the number of integers returned by
 *		::gpiod_line_config_get_num_overrides.
 *
 * The overridden (offset, prop) pairs are stored in the \p offsets and
 * \p props arrays, with the pairs having the same index.
 */
void
gpiod_line_config_get_overrides(struct gpiod_line_config *config,
				unsigned int *offsets, int *props);

/**
 * @}
 *
 * @defgroup request_config Request configuration objects
 * @{
 *
 * Functions for manipulating request configuration objects.
 *
 * Request config objects are used to pass a set of options to the kernel at
 * the time of the line request. Similarly to the line-config - the mutators
 * don't return error values. If the values are invalid, in general they are
 * silently adjusted to acceptable ranges.
 */

/**
 * @brief Create a new request config object.
 * @return New request config object or NULL on error.
 */
struct gpiod_request_config *gpiod_request_config_new(void);

/**
 * @brief Free the request config object and release all associated resources.
 * @param config Line config object.
 */
void gpiod_request_config_free(struct gpiod_request_config *config);

/**
 * @brief Set the consumer name for the request.
 * @param config Request config object.
 * @param consumer Consumer name.
 * @note If the consumer string is too long, it will be truncated to the max
 *       accepted length.
 */
void gpiod_request_config_set_consumer(struct gpiod_request_config *config,
				       const char *consumer);

/**
 * @brief Get the consumer string.
 * @param config Request config object.
 * @return Current consumer string stored in this request config.
 */
const char *
gpiod_request_config_get_consumer(struct gpiod_request_config *config);

/**
 * @brief Set line offsets for this request.
 * @param config Request config object.
 * @param num_offsets Number of offsets.
 * @param offsets Array of line offsets.
 * @note If too many offsets were specified, the offsets above the limit
 *       accepted by the kernel (64 lines) are silently dropped.
 */
void gpiod_request_config_set_offsets(struct gpiod_request_config *config,
				      size_t num_offsets,
				      const unsigned int *offsets);

/**
 * @brief Get the number of offsets configured in this request config.
 * @param config Request config object.
 * @return Number of line offsets in this request config.
 */
size_t
gpiod_request_config_get_num_offsets(struct gpiod_request_config *config);

/**
 * @brief Get the hardware offsets of lines in this request config.
 * @param config Request config object.
 * @param offsets Array to store offsets. Must hold at least the number of
 *                lines returned by ::gpiod_request_config_get_num_offsets.
 */
void gpiod_request_config_get_offsets(struct gpiod_request_config *config,
				      unsigned int *offsets);

/**
 * @brief Set the size of the kernel event buffer.
 * @param config Request config object.
 * @param event_buffer_size New event buffer size.
 * @note The kernel may adjust the value if it's too high. If set to 0, the
 *       default value will be used.
 */
void
gpiod_request_config_set_event_buffer_size(struct gpiod_request_config *config,
					   size_t event_buffer_size);

/**
 * @brief Get the edge event buffer size from this request config.
 * @param config Request config object.
 * @return Current edge event buffer size setting.
 */
size_t
gpiod_request_config_get_event_buffer_size(struct gpiod_request_config *config);

/**
 * @}
 *
 * @defgroup request_request Line request operations
 * @{
 *
 * Functions allowing interactions with requested lines.
 */

/**
 * @brief Release the requested lines and free all associated resources.
 * @param request Line request object to release.
 */
void gpiod_line_request_release(struct gpiod_line_request *request);

/**
 * @brief Get the number of lines in the request.
 * @param request Line request object.
 * @return Number of requested lines.
 */
size_t gpiod_line_request_get_num_lines(struct gpiod_line_request *request);

/**
 * @brief Get the offsets of the lines in the request.
 * @param request Line request object.
 * @param offsets Array to store offsets. Must be sized to hold the number of
 *		  lines returned by ::gpiod_line_request_get_num_lines.
 */
void gpiod_line_request_get_offsets(struct gpiod_line_request *request,
				    unsigned int *offsets);

/**
 * @brief Get the value of a single requested line.
 * @param request Line request object.
 * @param offset The offset of the line of which the value should be read.
 * @return Returns 1 or 0 on success and -1 on error.
 */
int gpiod_line_request_get_value(struct gpiod_line_request *request,
				 unsigned int offset);

/**
 * @brief Get the values of a subset of requested lines.
 * @param request GPIO line request.
 * @param num_lines Number of lines for which to read values.
 * @param offsets Array of offsets identifying the subset of requested lines
 *		  from which to read values.
 * @param values Array in which the values will be stored.  Must be sized
 *		 to hold \p num_lines entries.  Each value is associated with the
 *		 line identified by the corresponding entry in \p offsets.
 * @return 0 on success, -1 on failure.
 */
int gpiod_line_request_get_values_subset(struct gpiod_line_request *request,
					 size_t num_lines,
					 const unsigned int *offsets,
					 int *values);

/**
 * @brief Get the values of all requested lines.
 * @param request GPIO line request.
 * @param values Array in which the values will be stored. Must be sized to
 *		 hold the number of lines returned by
 *		 ::gpiod_line_request_get_num_lines.
 *		 Each value is associated with the line identified by the
 *		 corresponding entry in the offset array returned by
 *		 ::gpiod_line_request_get_offsets.
 * @return 0 on success, -1 on failure.
 */
int gpiod_line_request_get_values(struct gpiod_line_request *request,
				  int *values);

/**
 * @brief Set the value of a single requested line.
 * @param request Line request object.
 * @param offset The offset of the line for which the value should be set.
 * @param value Value to set.
 */
int gpiod_line_request_set_value(struct gpiod_line_request *request,
				 unsigned int offset, int value);

/**
 * @brief Set the values of a subset of requested lines.
 * @param request GPIO line request.
 * @param num_lines Number of lines for which to set values.
 * @param offsets Array of offsets, containing the number of entries specified
 *		  by \p num_lines, identifying the requested lines for
 *		  which to set values.
 * @param values Array of values to set, containing the number of entries
 *		 specified by \p num_lines.  Each value is associated with the
 *		 line identified by the corresponding entry in \p offsets.
 * @return 0 on success, -1 on failure.
 */
int gpiod_line_request_set_values_subset(struct gpiod_line_request *request,
					 size_t num_lines,
					 const unsigned int *offsets,
					 const int *values);

/**
 * @brief Set the values of all lines associated with a request.
 * @param request GPIO line request.
 * @param values Array containing the values to set. Must be sized to
 *		 contain the number of lines returned by
 *		 ::gpiod_line_request_get_num_lines.
 *		 Each value is associated with the line identified by the
 *		 corresponding entry in the offset array returned by
 *		 ::gpiod_line_request_get_offsets.
 */
int gpiod_line_request_set_values(struct gpiod_line_request *request,
				  const int *values);

/**
 * @brief Update the configuration of lines associated with a line request.
 * @param request GPIO line request.
 * @param config New line config to apply.
 * @return 0 on success, -1 on failure.
 * @note The new line configuration completely replaces the old.
 * @note Any requested lines without overrides are configured to the requested
 *	 defaults.
 * @note Any configured overrides for lines that have not been requested
 *	 are silently ignored.
 */
int gpiod_line_request_reconfigure_lines(struct gpiod_line_request *request,
					 struct gpiod_line_config *config);

/**
 * @brief Get the file descriptor associated with a line request.
 * @param request GPIO line request.
 * @return The file descriptor associated with the request.
 *	   This function never fails.
 *	   The returned file descriptor must not be closed by the caller.
 *	   Call ::gpiod_line_request_release to close the file.
 */
int gpiod_line_request_get_fd(struct gpiod_line_request *request);

/**
 * @brief Wait for edge events on any of the requested lines.
 * @param request GPIO line request.
 * @param timeout_ns Wait time limit in nanoseconds.
 * @return 0 if wait timed out, -1 if an error occurred, 1 if an event is
 *	   pending.
 *q
 * Lines must have edge detection set for edge events to be emitted.
 * By default edge detection is disabled.
 */
int gpiod_line_request_wait_edge_event(struct gpiod_line_request *request,
				       uint64_t timeout_ns);

/**
 * @brief Read a number of edge events from a line request.
 * @param request GPIO line request.
 * @param buffer Edge event buffer, sized to hold at least \p max_events.
 * @param max_events Maximum number of events to read.
 * @return On success returns the number of events read from the file
 *	   descriptor, on failure return -1.
 * @note This function will block if no event was queued for the line request.
 * @note Any exising events in the buffer are overwritten.  This is not an
 *       append operation.
 */
int gpiod_line_request_read_edge_event(struct gpiod_line_request *request,
				       struct gpiod_edge_event_buffer *buffer,
				       size_t max_events);

/**
 * @}
 *
 * @defgroup edge_event Line edge events handling
 * @{
 *
 * Functions and data types for handling edge events.
 *
 * An edge event object contains information about a single line edge event.
 * It contains the event type, timestamp and the offset of the line on which
 * the event occurred as well as two sequence numbers (global for all lines
 * in the associated request and local for this line only).
 *
 * Edge events are stored into an edge-event buffer object to improve
 * performance and to limit the number of memory allocations when a large
 * number of events are being read.
 */

/**
 * @brief Event types.
 */
enum {
	GPIOD_EDGE_EVENT_RISING_EDGE = 1,
	/**< Rising edge event. */
	GPIOD_EDGE_EVENT_FALLING_EDGE
	/**< Falling edge event. */
};

/**
 * @brief Free the edge event object.
 * @param event Edge event object to free.
 */
void gpiod_edge_event_free(struct gpiod_edge_event *event);

/**
 * @brief Copy the edge event object.
 * @param event Edge event to copy.
 * @return Copy of the edge event or NULL on error. The returned object must
 *	   be freed by the caller using ::gpiod_edge_event_free.
 */
struct gpiod_edge_event *gpiod_edge_event_copy(struct gpiod_edge_event *event);

/**
 * @brief Get the event type.
 * @param event GPIO edge event.
 * @return The event type (::GPIOD_EDGE_EVENT_RISING_EDGE or
 *	   ::GPIOD_EDGE_EVENT_FALLING_EDGE).
 */
int gpiod_edge_event_get_event_type(struct gpiod_edge_event *event);

/**
 * @brief Get the timestamp of the event.
 * @param event GPIO edge event.
 * @return Timestamp in nanoseconds.
 * @note The source clock for the timestamp depends on the event_clock
 *	 setting for the line.
 */
uint64_t gpiod_edge_event_get_timestamp(struct gpiod_edge_event *event);

/**
 * @brief Get the offset of the line which triggered the event.
 * @param event GPIO edge event.
 * @return Line offset.
 */
unsigned int gpiod_edge_event_get_line_offset(struct gpiod_edge_event *event);

/**
 * @brief Get the global sequence number of the event.
 * @param event GPIO edge event.
 * @return Sequence number of the event in the series of events for all lines
 *	   in the associated line request.
 */
unsigned long gpiod_edge_event_get_global_seqno(struct gpiod_edge_event *event);

/**
 * @brief Get the event sequence number specific to the line.
 * @param event GPIO edge event.
 * @return Sequence number of the event in the series of events only for this
 *	   line within the lifetime of the associated line request.
 */
unsigned long gpiod_edge_event_get_line_seqno(struct gpiod_edge_event *event);

/**
 * @brief Create a new edge event buffer.
 * @param capacity Number of events the buffer can store (min = 1, max = 1024).
 * @return New edge event buffer or NULL on error.
 * @note If capacity equals 0, it will be set to a default value of 64. If
 *	 capacity is larger than 1024, it will be limited to 1024.
 * @note The user space buffer is independent of the kernel buffer
 *	 (::gpiod_request_config_set_event_buffer_size).  As the user space
 *	 buffer is filled from the kernel buffer, there is no benefit making
 *	 the user space buffer larger than the kernel buffer.
 *	 The default kernel buffer size for each request is 16*num_lines.
 */
struct gpiod_edge_event_buffer *
gpiod_edge_event_buffer_new(size_t capacity);

/**
 * @brief Get the capacity (the max number of events that can be stored) of
 *	  the event buffer.
 * @param buffer Edge event buffer.
 * @return The capacity of the buffer.
 */
size_t
gpiod_edge_event_buffer_get_capacity(struct gpiod_edge_event_buffer *buffer);

/**
 * @brief Free the edge event buffer and release all associated resources.
 * @param buffer Edge event buffer to free.
 */
void gpiod_edge_event_buffer_free(struct gpiod_edge_event_buffer *buffer);

/**
 * @brief Get an event stored in the buffer.
 * @param buffer Edge event buffer.
 * @param index Index of the event in the buffer.
 * @return Pointer to an event stored in the buffer. The lifetime of the
 *	   event is tied to the buffer object. Users must not free the event
 *	   returned by this function.
 */
struct gpiod_edge_event *
gpiod_edge_event_buffer_get_event(struct gpiod_edge_event_buffer *buffer,
				  unsigned long index);

/**
 * @brief Get the number of events a buffer has stored.
 * @param buffer Edge event buffer.
 * @return Number of events stored in the buffer.
 */
size_t
gpiod_edge_event_buffer_get_num_events(struct gpiod_edge_event_buffer *buffer);

/**
 * @}
 *
 * @defgroup misc Stuff that didn't fit anywhere else
 * @{
 *
 * Various libgpiod-related functions.
 */

/**
 * @brief Check if the file pointed to by path is a GPIO chip character device.
 * @param path Path to check.
 * @return True if the file exists and is either a GPIO chip character device
 *	   or a symbolic link to one.
 */
bool gpiod_is_gpiochip_device(const char *path);

/**
 * @brief Get the API version of the library as a human-readable string.
 * @return Human-readable string containing the library version.
 */
const char *gpiod_version_string(void);

/**
 * @}
 */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __LIBGPIOD_GPIOD_H__ */

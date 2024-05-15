/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* SPDX-FileCopyrightText: 2024 Kent Gibson <warthog618@gmail.com> */

/**
 * @file gpiod-ext.h
 */

#ifndef __LIBGPIOD_GPIOD_EXT_H__
#define __LIBGPIOD_GPIOD_EXT_H__

#include <gpiod.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @}
 *
 * @defgroup ext Extensions - helper functions for basic use cases
 * @{
 *
 * Various functions that provide a simplified interface for basic use cases
 * where the request only contains one line.
 */

/**
 * @brief Request a single input line.
 * @param chip Path to the GPIO chip.
 * @param offset The offset of the GPIO line.
 * @return New line request object or NULL if an error occurred. The request
 *         must be released by the caller using ::gpiod_line_request_release.
 */
struct gpiod_line_request *
gpiod_ext_request_input(const char *path, unsigned int offset);

/**
 * @brief Request a single input line.
 * @param chip Path to the GPIO chip.
 * @param offset The offset of the GPIO line.
 * @param value The value to set the line.
 * @return New line request object or NULL if an error occurred. The request
 *         must be released by the caller using ::gpiod_line_request_release.
 */
struct gpiod_line_request *
gpiod_ext_request_output(const char *path,
			 unsigned int offset,
			 enum gpiod_line_value value);

/**
 * @brief Set the bias of requested input line.
 * @param olr The request to reconfigure.
 * @param bias The new bias to apply to requested input line.
 * @return 0 on success, -1 on failure.
 */
int gpiod_ext_set_bias(struct gpiod_line_request * olr,
		       enum gpiod_line_bias bias);

/**
 * @brief Set the debounce period of requested input line.
 * @param olr The request to reconfigure.
 * @param period The new debounce period to apply, in microseconds.
 * @return 0 on success, -1 on failure.
 */
int gpiod_ext_set_debounce_period_us(struct gpiod_line_request *olr,
				     unsigned long period);

/**
 * @brief Set the edge detection of requested input line.
 * @param olr The request to reconfigure.
 * @param edge The new edge detection setting.
 * @return 0 on success, -1 on failure.
 */
int gpiod_ext_set_edge_detection(struct gpiod_line_request * olr,
				 enum gpiod_line_edge edge);

/**
 * @}
 */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __LIBGPIOD_GPIOD_EXT_H__ */

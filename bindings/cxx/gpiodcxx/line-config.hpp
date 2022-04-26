/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* SPDX-FileCopyrightText: 2021-2022 Bartosz Golaszewski <brgl@bgdev.pl> */

/**
 * @file line-config.hpp
 */

#ifndef __LIBGPIOD_CXX_LINE_CONFIG_HPP__
#define __LIBGPIOD_CXX_LINE_CONFIG_HPP__

#if !defined(__LIBGPIOD_GPIOD_CXX_INSIDE__)
#error "Only gpiod.hpp can be included directly."
#endif

#include <any>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <map>
#include <memory>
#include <utility>

namespace gpiod {

class chip;
class line_request;

/**
 * @ingroup gpiod_cxx
 * @{
 */

/**
 * @brief Contains a set of line config options used in line requests and
 *        reconfiguration.
 */
class line_config
{
public:

	/**
	 * @brief List of available configuration properties. Used in the
	 *        constructor, :line_config::set_property_default and
	 *        :line_config::set_property_override.
	 */
	enum class property {
		DIRECTION = 1,
		/**< Line direction. */
		EDGE,
		/**< Edge detection. */
		BIAS,
		/**< Bias. */
		DRIVE,
		/**< Drive. */
		ACTIVE_LOW,
		/**< Active-low setting. */
		DEBOUNCE_PERIOD,
		/**< Debounce period. */
		EVENT_CLOCK,
		/**< Event clock. */
		OUTPUT_VALUE,
		/**< Output value. */
		OUTPUT_VALUES,
		/**< Set of offset-to-value mappings. Only used in the constructor. */
	};

	/**
	 * @brief List of configuration properties passed to the constructor.
	 *        The first member is the property indicator, the second is
	 *        the value stored as `std::any` that is interpreted by the
	 *        relevant methods depending on the property value.
	 */
	using properties = ::std::map<property, ::std::any>;

	/**
	 * @brief Stored information about a single configuration override. The
	 *        first member is the overridden line offset, the second is
	 *        the property being overridden.
	 */
	using override = ::std::pair<line::offset, property>;

	/**
	 * @brief List of line configuration overrides.
	 */
	using override_list = ::std::vector<override>;

	/**
	 * @brief Constructor.
	 * @param props List of configuration properties. See
	 *              :set_property_default for details. Additionally the
	 *              constructor takes another property type as argument:
	 *              :property::OUTPUT_VALUES which takes
	 *              :line::value_mappings as property value. This
	 *              effectively sets the overrides for output values for
	 *              the mapped offsets.
	 */
	explicit line_config(const properties& props = properties());

	line_config(const line_config& other) = delete;

	/**
	 * @brief Move constructor.
	 * @param other Object to move.
	 */
	line_config(line_config&& other) noexcept;

	~line_config(void);

	line_config& operator=(const line_config& other) = delete;

	/**
	 * @brief Move assignment operator.
	 * @param other Object to move.
	 * @return Reference to self.
	 */
	line_config& operator=(line_config&& other) noexcept;

	/**
	 * @brief Reset the line config object.
	 */
	void reset(void) noexcept;

	/**
	 * @brief Set the default value of a single configuration property.
	 * @param prop Property to set.
	 * @param val Property value. The type must correspond with the
	 *            property being set: :line::direction for
	 *            :property::DIRECTION, :line::edge for :property::EDGE,
	 *            :line::bias for :property::BIAS, :line::drive for
	 *            :property::DRIVE, `bool` for :property::ACTIVE_LOW,
	 *            `std::chrono:microseconds` for
	 *            :property::DEBOUNCE_PERIOD, :line::clock for
	 *            :property::EVENT_CLOCK and :line::value
	 *            for :property::OUTPUT_VALUE.
	 *
	 */
	void set_property_default(property prop, const ::std::any& val);

	/**
	 * @brief Set the override value of a single configuration property.
	 * @param prop Property to set.
	 * @param offset Line offset to override.
	 * @param val Property value. See :set_property_default for details.
	 */
	void set_property_offset(property prop, line::offset offset, const ::std::any& val);

	/**
	 * @brief Set the default direction setting.
	 * @param direction New direction.
	 */
	void set_direction_default(line::direction direction);

	/**
	 * @brief Set the direction for a single line at given offset.
	 * @param direction New direction.
	 * @param offset Offset of the line for which to set the direction.
	 */
	void set_direction_override(line::direction direction, line::offset offset);

	/**
	 * @brief Get the default direction setting.
	 * @return Direction setting that would have been used for any offset
	 * 	   not assigned its own direction value.
	 */
	line::direction direction_default(void) const;

	/**
	 * @brief Get the direction setting for a given offset.
	 * @param offset Line offset for which to read the direction setting.
	 * @return Direction setting that would have been used for given offset
	 *         if the config object was used in a request at the time of
	 *         the call.
	 */
	line::direction direction_offset(line::offset offset) const;

	/**
	 * @brief Clear the direction override at given offset.
	 * @param offset Offset of the line for which to clear the override.
	 * @note Does nothing if no override is set for this line.
	 */
	void clear_direction_override(line::offset offset) noexcept;

	/**
	 * @brief Check if the direction setting is overridden at given offset.
	 * @param offset Offset of the line for which to check the override.
	 * @return True if direction is overridden at this offset, false
	 *         otherwise.
	 */
	bool direction_is_overridden(line::offset offset) const noexcept;

	/**
	 * @brief Set the default edge event detection.
	 * @param edge Type of edge events to detect.
	 */
	void set_edge_detection_default(line::edge edge);

	/**
	 * @brief Set the edge event detection for a single line at given
	 *        offset.
	 * @param edge Type of edge events to detect.
	 * @param offset Offset of the line for which to set the direction.
	 */
	void set_edge_detection_override(line::edge edge, line::offset offset);

	/**
	 * @brief Get the default edge detection setting.
	 * @return Edge detection setting that would have been used for any
	 *         offset not assigned its own direction value.
	 */
	line::edge edge_detection_default(void) const;

	/**
	 * @brief Get the edge event detection setting for a given offset.
	 * @param offset Line offset for which to read the edge detection
	 *               setting.
	 * @return Edge event detection setting that would have been used for
	 * 	   given offset if the config object was used in a request at
	 * 	   the time of the call.
	 */
	line::edge edge_detection_offset(line::offset offset) const;

	/**
	 * @brief Clear the edge detection override at given offset.
	 * @param offset Offset of the line for which to clear the override.
	 * @note Does nothing if no override is set for this line.
	 */
	void clear_edge_detection_override(line::offset offset) noexcept;

	/**
	 * @brief Check if the edge detection setting is overridden at given
	 *        offset.
	 * @param offset Offset of the line for which to check the override.
	 * @return True if edge detection is overridden at this offset, false
	 *         otherwise.
	 */
	bool edge_detection_is_overridden(line::offset offset) const noexcept;

	/**
	 * @brief Set the default bias setting.
	 * @param bias New bias.
	 */
	void set_bias_default(line::bias bias);

	/**
	 * @brief Set the bias for a single line at given offset.
	 * @param bias New bias.
	 * @param offset Offset of the line for which to set the bias.
	 */
	void set_bias_override(line::bias bias, line::offset offset);

	/**
	 * @brief Get the default bias setting.
	 * @return Bias setting that would have been used for any offset not
	 *         assigned its own direction value.
	 */
	line::bias bias_default(void) const;

	/**
	 * @brief Get the bias setting for a given offset.
	 * @param offset Line offset for which to read the bias setting.
	 * @return Bias setting that would have been used for given offset if
	 *         the config object was used in a request at the time of the
	 *         call.
	 */
	line::bias bias_offset(line::offset offset) const;

	/**
	 * @brief Clear the bias override at given offset.
	 * @param offset Offset of the line for which to clear the override.
	 * @note Does nothing if no override is set for this line.
	 */
	void clear_bias_override(line::offset offset) noexcept;

	/**
	 * @brief Check if the bias setting is overridden at given offset.
	 * @param offset Offset of the line for which to check the override.
	 * @return True if bias is overridden at this offset, false otherwise.
	 */
	bool bias_is_overridden(line::offset offset) const noexcept;

	/**
	 * @brief Set the default drive setting.
	 * @param drive New drive.
	 */
	void set_drive_default(line::drive drive);

	/**
	 * @brief Set the drive for a single line at given offset.
	 * @param drive New drive.
	 * @param offset Offset of the line for which to set the drive.
	 */
	void set_drive_override(line::drive drive, line::offset offset);

	/**
	 * @brief Set the drive for a subset of offsets.
	 * @param drive New drive.
	 * @param offsets Vector of line offsets for which to set the drive.
	 */
	void set_drive(line::drive drive, const line::offsets& offsets);

	/**
	 * @brief Get the default drive setting.
	 * @return Drive setting that would have been used for any offset not
	 *         assigned its own direction value.
	 */
	line::drive drive_default(void) const;

	/**
	 * @brief Get the drive setting for a given offset.
	 * @param offset Line offset for which to read the drive setting.
	 * @return Drive setting that would have been used for given offset if
	 *         the config object was used in a request at the time of the
	 *         call.
	 */
	line::drive drive_offset(line::offset offset) const;

	/**
	 * @brief Clear the drive override at given offset.
	 * @param offset Offset of the line for which to clear the override.
	 * @note Does nothing if no override is set for this line.
	 */
	void clear_drive_override(line::offset offset) noexcept;

	/**
	 * @brief Check if the drive setting is overridden at given offset.
	 * @param offset Offset of the line for which to check the override.
	 * @return True if drive is overridden at this offset, false otherwise.
	 */
	bool drive_is_overridden(line::offset offset) const noexcept;

	/**
	 * @brief Set lines to active-low by default.
	 * @param active_low New active-low setting.
	 */
	void set_active_low_default(bool active_low) noexcept;

	/**
	 * @brief Set a single line as active-low.
	 * @param active_low New active-low setting.
	 * @param offset Offset of the line for which to set the active setting.
	 */
	void set_active_low_override(bool active_low, line::offset offset) noexcept;

	/**
	 * @brief Check if active-low is the default setting.
	 * @return Active-low setting that would have been used for any offset
         *         not assigned its own value.
	 */
	bool active_low_default(void) const noexcept;

	/**
	 * @brief Check if the line at given offset was configured as
	 *        active-low.
	 * @param offset Line offset for which to read the active-low setting.
	 * @return Active-low setting that would have been used for given
	 *         offset if the config object was used in a request at the
	 *         time of the call.
	 */
	bool active_low_offset(line::offset offset) const noexcept;

	/**
	 * @brief Clear the active-low override at given offset.
	 * @param offset Offset of the line for which to clear the override.
	 * @note Does nothing if no override is set for this line.
	 */
	void clear_active_low_override(line::offset offset) noexcept;

	/**
	 * @brief Check if the active-low setting is overridden at given offset.
	 * @param offset Offset of the line for which to check the override.
	 * @return True if active-low is overridden at this offset, false
	 *         otherwise.
	 */
	bool active_low_is_overridden(line::offset offset) const noexcept;

	/**
	 * @brief Set the default debounce period.
	 * @param period New debounce period. Disables debouncing if 0.
	 */
	void set_debounce_period_default(const ::std::chrono::microseconds& period) noexcept;

	/**
	 * @brief Set the debounce period for a single line at given offset.
	 * @param period New debounce period. Disables debouncing if 0.
	 * @param offset Offset of the line for which to set the debounce
	 *               period.
	 */
	void set_debounce_period_override(const ::std::chrono::microseconds& period,
					     line::offset offset) noexcept;

	/**
	 * @brief Get the default debounce period.
	 * @return Debounce period that would have been used for any offset not
	 *         assigned its own debounce period. 0 if not debouncing is
	 *         disabled.
	 */
	::std::chrono::microseconds debounce_period_default(void) const noexcept;

	/**
	 * @brief Get the debounce period for a given offset.
	 * @param offset Line offset for which to read the debounce period.
	 * @return Debounce period that would have been used for given offset
	 *         if the config object was used in a request at the time of
	 *         the call. 0 if debouncing is disabled.
	 */
	::std::chrono::microseconds debounce_period_offset(line::offset offset) const noexcept;

	/**
	 * @brief Clear the debounce period override at given offset.
	 * @param offset Offset of the line for which to clear the override.
	 * @note Does nothing if no override is set for this line.
	 */
	void clear_debounce_period_override(line::offset offset) noexcept;

	/**
	 * @brief Check if the debounce period setting is overridden at given offset.
	 * @param offset Offset of the line for which to check the override.
	 * @return True if debounce period is overridden at this offset, false
	 *         otherwise.
	 */
	bool debounce_period_is_overridden(line::offset offset) const noexcept;

	/**
	 * @brief Set the default event timestamp clock.
	 * @param clock New clock to use.
	 */
	void set_event_clock_default(line::clock clock);

	/**
	 * @brief Set the event clock for a single line at given offset.
	 * @param clock New clock to use.
	 * @param offset Offset of the line for which to set the event clock
	 *               type.
	 */
	void set_event_clock_override(line::clock clock, line::offset offset);

	/**
	 * @brief Get the default event clock setting.
	 * @return Event clock setting that would have been used for any offset
	 *         not assigned its own direction value.
	 */
	line::clock event_clock_default(void) const;

	/**
	 * @brief Get the event clock setting for a given offset.
	 * @param offset Line offset for which to read the event clock setting.
	 * @return Event clock setting that would have been used for given
	 *         offset if the config object was used in a request at the
	 *         time of the call.
	 */
	line::clock event_clock_offset(line::offset offset) const;

	/**
	 * @brief Clear the event clock override at given offset.
	 * @param offset Offset of the line for which to clear the override.
	 * @note Does nothing if no override is set for this line.
	 */
	void clear_event_clock_override(line::offset offset) noexcept;

	/**
	 * @brief Check if the event clock setting is overridden at given
	 *        offset.
	 * @param offset Offset of the line for which to check the override.
	 * @return True if event clock is overridden at this offset, false
	 *         otherwise.
	 */
	bool event_clock_is_overridden(line::offset offset) const noexcept;

	/**
	 * @brief Set the default output value.
	 * @param value New value.
	 */
	void set_output_value_default(line::value value) noexcept;

	/**
	 * @brief Set the output value for a single offset.
	 * @param offset Line offset to associate the value with.
	 * @param value New value.
	 */
	void set_output_value_override(line::value value, line::offset offset) noexcept;

	/**
	 * @brief Set the output values for a set of line offsets.
	 * @param values Vector of offset->value mappings.
	 */
	void set_output_values(const line::value_mappings& values);

	/**
	 * @brief Set the output values for a set of line offsets.
	 * @param offsets Vector of line offsets for which to set output values.
	 * @param values Vector of new line values with indexes of values
	 *               corresponding to the indexes of offsets.
	 */
	void set_output_values(const line::offsets& offsets, const line::values& values);

	/**
	 * @brief Get the default output value.
	 * @return Output value that would have been used for any offset not
	 *         assigned its own output value.
	 */
	line::value output_value_default(void) const noexcept;

	/**
	 * @brief Get the output value configured for a given line.
	 * @param offset Line offset for which to read the value.
	 * @return Output value that would have been used for given offset if
	 *         the config object was used in a request at the time of the
	 *         call.
	 */
	line::value output_value_offset(line::offset offset) const noexcept;

	/**
	 * @brief Clear the output value override at given offset.
	 * @param offset Offset of the line for which to clear the override.
	 * @note Does nothing if no override is set for this line.
	 */
	void clear_output_value_override(line::offset offset) noexcept;

	/**
	 * @brief Check if the output value setting is overridden at given
	 *        offset.
	 * @param offset Offset of the line for which to check the override.
	 * @return True if output value is overridden at this offset, false
	 *         otherwise.
	 */
	bool output_value_is_overridden(line::offset offset) const noexcept;

	/**
	 * @brief Get the number of configuration overrides.
	 * @return Number of overrides held by this object.
	 */
	::std::size_t num_overrides(void) const noexcept;

	/**
	 * @brief Get the list of property overrides.
	 * @return List of configuration property overrides held by this object.
	 */
	override_list overrides(void) const;

private:

	struct impl;

	::std::unique_ptr<impl> _m_priv;

	friend chip;
	friend line_request;
};

/**
 * @brief Stream insertion operator for GPIO line config objects.
 * @param out Output stream to write to.
 * @param config Line config object to insert into the output stream.
 * @return Reference to out.
 */
::std::ostream& operator<<(::std::ostream& out, const line_config& config);

/**
 * @}
 */

} /* namespace gpiod */

#endif /* __LIBGPIOD_CXX_LINE_CONFIG_HPP__ */

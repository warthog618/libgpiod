/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* SPDX-FileCopyrightText: 2021 Bartosz Golaszewski <brgl@bgdev.pl> */

/**
 * @file request-config.hpp
 */

#ifndef __LIBGPIOD_CXX_REQUEST_CONFIG_HPP__
#define __LIBGPIOD_CXX_REQUEST_CONFIG_HPP__

#if !defined(__LIBGPIOD_GPIOD_CXX_INSIDE__)
#error "Only gpiod.hpp can be included directly."
#endif

#include <any>
#include <cstddef>
#include <iostream>
#include <map>
#include <memory>
#include <string>

#include "line.hpp"

namespace gpiod {

class chip;

/**
 * @ingroup gpiod_cxx
 * @{
 */

/**
 * @brief Stores a set of options passed to the kernel when making a line
 *        request.
 */
class request_config
{
public:

	/**
	 * @brief List of available configuration settings. Used in the
	 *        constructor and :request_config::set_property.
	 */
	enum class property {
		OFFSETS = 1,
		/**< List of line offsets to request. */
		CONSUMER,
		/**< Consumer string. */
		EVENT_BUFFER_SIZE,
		/**< Suggested size of the edge event buffer. */
	};

	/**
	 * @brief Map of mappings between property types and property values.
	 */
	using properties = ::std::map<property, ::std::any>;

	/**
	 * @brief Constructor.
	 * @param props List of config properties. See
	 *              :request_config::set_property.
	 */
	explicit request_config(const properties& props = properties());

	request_config(const request_config& other) = delete;

	/**
	 * @brief Move constructor.
	 * @param other Object to move.
	 */
	request_config(request_config&& other) noexcept;

	~request_config();

	request_config& operator=(const request_config& other) = delete;

	/**
	 * @brief Move assignment operator.
	 * @param other Object to move.
	 * @return Reference to self.
	 */
	request_config& operator=(request_config&& other) noexcept;

	/**
	 * @brief Set the value of a single config property.
	 * @param prop Property to set.
	 * @param val Property value. The type must correspond to the property
	 *            being set: `std::string` or `const char*` for
	 *            :property::CONSUMER, `:line::offsets` for
	 *            :property::OFFSETS and `unsigned long` for
	 *            :property::EVENT_BUFFER_SIZE.
	 */
	void set_property(property prop, const ::std::any& val);

	/**
	 * @brief Set line offsets for this request.
	 * @param offsets Vector of line offsets to request.
	 */
	void set_offsets(const line::offsets& offsets) noexcept;

	/**
	 * @brief Get the number of offsets configured in this request config.
	 * @return Number of line offsets in this request config.
	 */
	::std::size_t num_offsets() const noexcept;

	/**
	 * @brief Set the consumer name.
	 * @param consumer New consumer name.
	 */
	void set_consumer(const ::std::string& consumer) noexcept;

	/**
	 * @brief Get the consumer name.
	 * @return Currently configured consumer name. May be an empty string.
	 */
	::std::string consumer() const noexcept;

	/**
	 * @brief Get the hardware offsets of lines in this request config.
	 * @return List of line offsets.
	 */
	line::offsets offsets() const;

	/**
	 * @brief Set the size of the kernel event buffer.
	 * @param event_buffer_size New event buffer size.
	 * @note The kernel may adjust the value if it's too high. If set to 0,
	 *       the default value will be used.
	 */
	void set_event_buffer_size(::std::size_t event_buffer_size) noexcept;

	/**
	 * @brief Get the edge event buffer size from this request config.
	 * @return Current edge event buffer size setting.
	 */
	::std::size_t event_buffer_size() const noexcept;

private:

	struct impl;

	::std::unique_ptr<impl> _m_priv;

	friend chip;
};

/**
 * @brief Stream insertion operator for request_config objects.
 * @param out Output stream to write to.
 * @param config request_config to insert into the output stream.
 * @return Reference to out.
 */
::std::ostream& operator<<(::std::ostream& out, const request_config& config);

/**
 * @}
 */

} /* namespace gpiod */

#endif /* __LIBGPIOD_CXX_REQUEST_CONFIG_HPP__ */

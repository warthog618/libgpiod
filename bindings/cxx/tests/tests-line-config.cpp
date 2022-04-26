// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2022 Bartosz Golaszewski <brgl@bgdev.pl>

#include <catch2/catch.hpp>
#include <gpiod.hpp>
#include <sstream>

#include "helpers.hpp"

using lineprop = ::gpiod::line_config::property;
using value = ::gpiod::line::value;
using direction = ::gpiod::line::direction;
using edge = ::gpiod::line::edge;
using bias = ::gpiod::line::bias;
using drive = ::gpiod::line::drive;
using clock_type = ::gpiod::line::clock;
using mappings = ::gpiod::line::value_mappings;
using offsets = ::gpiod::line::offsets;

using namespace ::std::chrono_literals;

namespace {

TEST_CASE("line_config constructor works", "[line-config]")
{
	SECTION("no arguments - default values")
	{
		::gpiod::line_config cfg;

		REQUIRE_NOTHROW(cfg.direction_default() == direction::INPUT);
		REQUIRE(cfg.edge_detection_default() == edge::NONE);
		REQUIRE(cfg.bias_default() == bias::AS_IS);
		REQUIRE(cfg.drive_default() == drive::PUSH_PULL);
		REQUIRE_FALSE(cfg.active_low_default());
		REQUIRE(cfg.debounce_period_default() == 0us);
		REQUIRE(cfg.event_clock_default() == clock_type::MONOTONIC);
		REQUIRE(cfg.output_value_default() == value::INACTIVE);
		REQUIRE(cfg.num_overrides() == 0);
		REQUIRE(cfg.overrides().empty());
	}

	SECTION("default values set from constructor")
	{
		/*
		 * These are wrong and the request would fail but we're just
		 * testing the object's behavior.
		 */
		::gpiod::line_config cfg({
			{ lineprop::DIRECTION, direction::OUTPUT },
			{ lineprop::EDGE, edge::FALLING },
			{ lineprop::BIAS, bias::DISABLED },
			{ lineprop::DRIVE, drive::OPEN_DRAIN },
			{ lineprop::ACTIVE_LOW, true },
			{ lineprop::DEBOUNCE_PERIOD, 3000us },
			{ lineprop::EVENT_CLOCK, clock_type::REALTIME },
			{ lineprop::OUTPUT_VALUE, value::ACTIVE }
		});

		REQUIRE_NOTHROW(cfg.direction_default() == direction::OUTPUT);
		REQUIRE(cfg.edge_detection_default() == edge::FALLING);
		REQUIRE(cfg.bias_default() == bias::DISABLED);
		REQUIRE(cfg.drive_default() == drive::OPEN_DRAIN);
		REQUIRE(cfg.active_low_default());
		/* Test implicit conversion between duration types. */
		REQUIRE(cfg.debounce_period_default() == 3ms);
		REQUIRE(cfg.event_clock_default() == clock_type::REALTIME);
		REQUIRE(cfg.output_value_default() == value::ACTIVE);
		REQUIRE(cfg.num_overrides() == 0);
		REQUIRE(cfg.overrides().empty());
	}

	SECTION("output value overrides can be set from constructor")
	{
		::gpiod::line_config cfg({
			{
				lineprop::OUTPUT_VALUES, mappings({
					{ 0, value::ACTIVE },
					{ 3, value::INACTIVE },
					{ 1, value::ACTIVE }
				})
			}
		});

		REQUIRE(cfg.num_overrides() == 3);
		auto overrides = cfg.overrides();
		REQUIRE(overrides[0].first == 0);
		REQUIRE(overrides[0].second == lineprop::OUTPUT_VALUE);
		REQUIRE(overrides[1].first == 3);
		REQUIRE(overrides[1].second == lineprop::OUTPUT_VALUE);
		REQUIRE(overrides[2].first == 1);
		REQUIRE(overrides[2].second == lineprop::OUTPUT_VALUE);
	}
}

TEST_CASE("line_config overrides work")
{
	::gpiod::line_config cfg;

	SECTION("direction")
	{
		cfg.set_direction_default(direction::AS_IS);
		cfg.set_direction_override(direction::INPUT, 3);

		REQUIRE(cfg.direction_is_overridden(3));
		REQUIRE(cfg.direction_offset(3) == direction::INPUT);
		cfg.clear_direction_override(3);
		REQUIRE_FALSE(cfg.direction_is_overridden(3));
		REQUIRE(cfg.direction_offset(3) == direction::AS_IS);
	}

	SECTION("edge detection")
	{
		cfg.set_edge_detection_default(edge::NONE);
		cfg.set_edge_detection_override(edge::BOTH, 0);

		REQUIRE(cfg.edge_detection_is_overridden(0));
		REQUIRE(cfg.edge_detection_offset(0) == edge::BOTH);
		cfg.clear_edge_detection_override(0);
		REQUIRE_FALSE(cfg.edge_detection_is_overridden(0));
		REQUIRE(cfg.edge_detection_offset(0) == edge::NONE);
	}

	SECTION("bias")
	{
		cfg.set_bias_default(bias::AS_IS);
		cfg.set_bias_override(bias::PULL_DOWN, 3);

		REQUIRE(cfg.bias_is_overridden(3));
		REQUIRE(cfg.bias_offset(3) == bias::PULL_DOWN);
		cfg.clear_bias_override(3);
		REQUIRE_FALSE(cfg.bias_is_overridden(3));
		REQUIRE(cfg.bias_offset(3) == bias::AS_IS);
	}

	SECTION("drive")
	{
		cfg.set_drive_default(drive::PUSH_PULL);
		cfg.set_drive_override(drive::OPEN_DRAIN, 4);

		REQUIRE(cfg.drive_is_overridden(4));
		REQUIRE(cfg.drive_offset(4) == drive::OPEN_DRAIN);
		cfg.clear_drive_override(4);
		REQUIRE_FALSE(cfg.drive_is_overridden(4));
		REQUIRE(cfg.drive_offset(4) == drive::PUSH_PULL);
	}

	SECTION("active-low")
	{
		cfg.set_active_low_default(false);
		cfg.set_active_low_override(true, 16);

		REQUIRE(cfg.active_low_is_overridden(16));
		REQUIRE(cfg.active_low_offset(16));
		cfg.clear_active_low_override(16);
		REQUIRE_FALSE(cfg.active_low_is_overridden(16));
		REQUIRE_FALSE(cfg.active_low_offset(16));
	}

	SECTION("debounce period")
	{
		/*
		 * Test the chrono literals and implicit duration conversions
		 * too.
		 */

		cfg.set_debounce_period_default(5000us);
		cfg.set_debounce_period_override(3ms, 1);

		REQUIRE(cfg.debounce_period_is_overridden(1));
		REQUIRE(cfg.debounce_period_offset(1) == 3ms);
		cfg.clear_debounce_period_override(1);
		REQUIRE_FALSE(cfg.debounce_period_is_overridden(1));
		REQUIRE(cfg.debounce_period_offset(1) == 5ms);
	}

	SECTION("event clock")
	{
		cfg.set_event_clock_default(clock_type::MONOTONIC);
		cfg.set_event_clock_override(clock_type::REALTIME, 4);

		REQUIRE(cfg.event_clock_is_overridden(4));
		REQUIRE(cfg.event_clock_offset(4) == clock_type::REALTIME);
		cfg.clear_event_clock_override(4);
		REQUIRE_FALSE(cfg.event_clock_is_overridden(4));
		REQUIRE(cfg.event_clock_offset(4) == clock_type::MONOTONIC);
	}

	SECTION("output value")
	{
		cfg.set_output_value_default(value::INACTIVE);
		cfg.set_output_value_override(value::ACTIVE, 0);
		cfg.set_output_values({ 1, 2, 8 }, { value::ACTIVE, value::ACTIVE, value::ACTIVE });
		cfg.set_output_values({ { 17, value::ACTIVE }, { 21, value::ACTIVE } });

		for (const auto& off: offsets({ 0, 1, 2, 8, 17, 21 })) {
			REQUIRE(cfg.output_value_is_overridden(off));
			REQUIRE(cfg.output_value_offset(off) == value::ACTIVE);
			cfg.clear_output_value_override(off);
			REQUIRE_FALSE(cfg.output_value_is_overridden(off));
			REQUIRE(cfg.output_value_offset(off) == value::INACTIVE);
		}
	}
}

TEST_CASE("line_config can be moved", "[line-config]")
{
	::gpiod::line_config cfg({
		{ lineprop::DIRECTION, direction::INPUT },
		{ lineprop::EDGE, edge::BOTH },
		{ lineprop::DEBOUNCE_PERIOD, 3000us },
		{ lineprop::EVENT_CLOCK, clock_type::REALTIME },
	});

	cfg.set_direction_override(direction::OUTPUT, 2);
	cfg.set_edge_detection_override(edge::NONE, 2);

	SECTION("move constructor works")
	{
		auto moved(::std::move(cfg));

		REQUIRE(moved.direction_default() == direction::INPUT);
		REQUIRE(moved.edge_detection_default() == edge::BOTH);
		REQUIRE(moved.debounce_period_default() == 3000us);
		REQUIRE(moved.event_clock_default() == clock_type::REALTIME);
		REQUIRE(moved.direction_offset(2) == direction::OUTPUT);
		REQUIRE(moved.edge_detection_offset(2) == edge::NONE);
	}

	SECTION("move constructor works")
	{
		::gpiod::line_config moved;

		moved = ::std::move(cfg);

		REQUIRE(moved.direction_default() == direction::INPUT);
		REQUIRE(moved.edge_detection_default() == edge::BOTH);
		REQUIRE(moved.debounce_period_default() == 3000us);
		REQUIRE(moved.event_clock_default() == clock_type::REALTIME);
		REQUIRE(moved.direction_offset(2) == direction::OUTPUT);
		REQUIRE(moved.edge_detection_offset(2) == edge::NONE);
	}
}

TEST_CASE("line_config stream insertion operator works", "[line-config]")
{
	::gpiod::line_config cfg({
		{ lineprop::DIRECTION, direction::INPUT },
		{ lineprop::EDGE, edge::BOTH },
		{ lineprop::DEBOUNCE_PERIOD, 3000us },
		{ lineprop::EVENT_CLOCK, clock_type::REALTIME },
	});

	cfg.set_direction_override(direction::OUTPUT, 2);
	cfg.set_edge_detection_override(edge::NONE, 2);

	::std::stringstream buf;

	buf << cfg;

	::std::string expected(
		"gpiod::line_config(defaults=(direction=INPUT, edge_detection=BOTH_EDGES, bias="
		"AS_IS, drive=PUSH_PULL, active-high, debounce_period=3000us, event_clock="
		"REALTIME, default_output_value=INACTIVE), overrides=[(offset=2 -> direction="
		"OUTPUT), (offset=2 -> edge_detection=NONE)])"
	);

	REQUIRE_THAT(buf.str(), Catch::Equals(expected));
}

} /* namespace */

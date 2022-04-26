// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2022 Bartosz Golaszewski <brgl@bgdev.pl>

#include <catch2/catch.hpp>
#include <gpiod.hpp>
#include <string>

#include "helpers.hpp"
#include "gpiosim.hpp"

using property = ::gpiosim::chip::property;
using line_name = ::gpiosim::chip::line_name;
using line_hog = ::gpiosim::chip::line_hog;
using hog_dir = ::gpiosim::chip::hog_direction;
using direction = ::gpiod::line::direction;
using edge = ::gpiod::line::edge;
using bias = ::gpiod::line::bias;
using drive = ::gpiod::line::drive;
using event_clock = ::gpiod::line::clock;

using namespace ::std::chrono_literals;

namespace {

TEST_CASE("get_line_info() works", "[chip][line-info]")
{
	::gpiosim::chip sim({
		{ property::NUM_LINES, 8 },
		{ property::LINE_NAME, line_name(0, "foobar") },
		{ property::HOG, line_hog(0, "hog", hog_dir::OUTPUT_HIGH ) }
	});

	::gpiod::chip chip(sim.dev_path());

	SECTION("line_info can be retrieved from chip")
	{
		auto info = chip.get_line_info(0);

		REQUIRE(info.offset() == 0);
		REQUIRE_THAT(info.name(), Catch::Equals("foobar"));
		REQUIRE(info.used());
		REQUIRE_THAT(info.consumer(), Catch::Equals("hog"));
		REQUIRE(info.direction() == ::gpiod::line::direction::OUTPUT);
		REQUIRE_FALSE(info.active_low());
		REQUIRE(info.bias() == ::gpiod::line::bias::UNKNOWN);
		REQUIRE(info.drive() == ::gpiod::line::drive::PUSH_PULL);
		REQUIRE(info.edge_detection() == ::gpiod::line::edge::NONE);
		REQUIRE(info.event_clock() == ::gpiod::line::clock::MONOTONIC);
		REQUIRE_FALSE(info.debounced());
		REQUIRE(info.debounce_period() == 0us);
	}

	SECTION("offset out of range")
	{
		REQUIRE_THROWS_AS(chip.get_line_info(8), ::std::invalid_argument);
	}
}

TEST_CASE("line properties can be retrieved", "[line-info]")
{
	::gpiosim::chip sim({
		{ property::NUM_LINES, 8 },
		{ property::LINE_NAME, line_name(1, "foo") },
		{ property::LINE_NAME, line_name(2, "bar") },
		{ property::LINE_NAME, line_name(4, "baz") },
		{ property::LINE_NAME, line_name(5, "xyz") },
		{ property::HOG, line_hog(3, "hog3", hog_dir::OUTPUT_HIGH) },
		{ property::HOG, line_hog(4, "hog4", hog_dir::OUTPUT_LOW) }
	});

	::gpiod::chip chip(sim.dev_path());

	SECTION("basic properties")
	{
		auto info4 = chip.get_line_info(4);
		auto info6 = chip.get_line_info(6);

		REQUIRE(info4.offset() == 4);
		REQUIRE_THAT(info4.name(), Catch::Equals("baz"));
		REQUIRE(info4.used());
		REQUIRE_THAT(info4.consumer(), Catch::Equals("hog4"));
		REQUIRE(info4.direction() == direction::OUTPUT);
		REQUIRE(info4.edge_detection() == edge::NONE);
		REQUIRE_FALSE(info4.active_low());
		REQUIRE(info4.bias() == bias::UNKNOWN);
		REQUIRE(info4.drive() == drive::PUSH_PULL);
		REQUIRE(info4.event_clock() == event_clock::MONOTONIC);
		REQUIRE_FALSE(info4.debounced());
		REQUIRE(info4.debounce_period() == 0us);
	}
}

TEST_CASE("line_info can be copied and moved")
{
	::gpiosim::chip sim({
		{ property::NUM_LINES, 4 },
		{ property::LINE_NAME, line_name(2, "foobar") }
	});

	::gpiod::chip chip(sim.dev_path());
	auto info = chip.get_line_info(2);

	SECTION("copy constructor works")
	{
		auto copy(info);
		REQUIRE(copy.offset() == 2);
		REQUIRE_THAT(copy.name(), Catch::Equals("foobar"));
		/* info can still be used */
		REQUIRE(info.offset() == 2);
		REQUIRE_THAT(info.name(), Catch::Equals("foobar"));
	}

	SECTION("assignment operator works")
	{
		auto copy = chip.get_line_info(0);
		copy = info;
		REQUIRE(copy.offset() == 2);
		REQUIRE_THAT(copy.name(), Catch::Equals("foobar"));
		/* info can still be used */
		REQUIRE(info.offset() == 2);
		REQUIRE_THAT(info.name(), Catch::Equals("foobar"));
	}

	SECTION("move constructor works")
	{
		auto copy(::std::move(info));
		REQUIRE(copy.offset() == 2);
		REQUIRE_THAT(copy.name(), Catch::Equals("foobar"));
	}

	SECTION("move assignment operator works")
	{
		auto copy = chip.get_line_info(0);
		copy = ::std::move(info);
		REQUIRE(copy.offset() == 2);
		REQUIRE_THAT(copy.name(), Catch::Equals("foobar"));
	}
}

TEST_CASE("line_info stream insertion operator works")
{
	::gpiosim::chip sim({
		{ property::LINE_NAME, line_name(0, "foo") },
		{ property::HOG, line_hog(0, "hogger", hog_dir::OUTPUT_HIGH) }
	});

	::gpiod::chip chip(sim.dev_path());

	auto info = chip.get_line_info(0);

	REQUIRE_THAT(info, stringify_matcher<::gpiod::line_info>(
		"gpiod::line_info(offset=0, name='foo', used=true, consumer='foo', direction=OUTPUT, "
		"active_low=false, bias=UNKNOWN, drive=PUSH_PULL, edge_detection=NONE, event_clock=MONOTONIC, debounced=false)"));
}

} /* namespace */

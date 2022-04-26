// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2021-2022 Bartosz Golaszewski <brgl@bgdev.pl>

#include <catch2/catch.hpp>
#include <gpiod.hpp>
#include <sstream>

#include "gpiosim.hpp"
#include "helpers.hpp"

using property = ::gpiosim::chip::property;

namespace {

TEST_CASE("chip_info properties can be read", "[chip-info][chip]")
{
	::gpiosim::chip sim({{ property::NUM_LINES, 8 }, { property::LABEL, "foobar" }});
	::gpiod::chip chip(sim.dev_path());
	auto info = chip.get_info();

	SECTION("get chip name")
	{
		REQUIRE_THAT(info.name(), Catch::Equals(sim.name()));
	}

	SECTION("get chip label")
	{
		REQUIRE_THAT(info.label(), Catch::Equals("foobar"));
	}

	SECTION("get num_lines")
	{
		REQUIRE(info.num_lines() == 8);
	}
}

TEST_CASE("chip_info can be copied and moved", "[chip-info]")
{
	::gpiosim::chip sim({{ property::NUM_LINES, 4 }, { property::LABEL, "foobar" }});
	::gpiod::chip chip(sim.dev_path());
	auto info = chip.get_info();

	SECTION("copy constructor works")
	{
		auto copy(info);

		REQUIRE_THAT(copy.name(), Catch::Equals(sim.name()));
		REQUIRE_THAT(copy.label(), Catch::Equals("foobar"));
		REQUIRE(copy.num_lines() == 4);

		REQUIRE_THAT(info.name(), Catch::Equals(sim.name()));
		REQUIRE_THAT(info.label(), Catch::Equals("foobar"));
		REQUIRE(info.num_lines() == 4);
	}

	SECTION("assignment operator works")
	{
		auto copy = chip.get_info();

		copy = info;

		REQUIRE_THAT(copy.name(), Catch::Equals(sim.name()));
		REQUIRE_THAT(copy.label(), Catch::Equals("foobar"));
		REQUIRE(copy.num_lines() == 4);

		REQUIRE_THAT(info.name(), Catch::Equals(sim.name()));
		REQUIRE_THAT(info.label(), Catch::Equals("foobar"));
		REQUIRE(info.num_lines() == 4);
	}

	SECTION("move constructor works")
	{
		auto moved(std::move(info));

		REQUIRE_THAT(moved.name(), Catch::Equals(sim.name()));
		REQUIRE_THAT(moved.label(), Catch::Equals("foobar"));
		REQUIRE(moved.num_lines() == 4);
	}

	SECTION("move assignment operator works")
	{
		auto moved = chip.get_info();

		moved = ::std::move(info);

		REQUIRE_THAT(moved.name(), Catch::Equals(sim.name()));
		REQUIRE_THAT(moved.label(), Catch::Equals("foobar"));
		REQUIRE(moved.num_lines() == 4);
	}
}

TEST_CASE("stream insertion operator works for chip_info", "[chip-info]")
{
	::gpiosim::chip sim({
		{ property::NUM_LINES, 4 },
		{ property::LABEL, "foobar" }
	});

	::gpiod::chip chip(sim.dev_path());
	auto info = chip.get_info();
	::std::stringstream expected;

	expected << "gpiod::chip_info(name=\"" << sim.name() <<
		    "\", label=\"foobar\", num_lines=4)";

	REQUIRE_THAT(info, stringify_matcher<::gpiod::chip_info>(expected.str()));
}

} /* namespace */

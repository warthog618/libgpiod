// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2021-2022 Bartosz Golaszewski <brgl@bgdev.pl>

#include <catch2/catch.hpp>
#include <cstddef>
#include <gpiod.hpp>
#include <string>
#include <sstream>

#include "helpers.hpp"

using property = ::gpiod::request_config::property;
using offsets = ::gpiod::line::offsets;

namespace {

TEST_CASE("request_config constructor works", "[request-config]")
{
	SECTION("no arguments")
	{
		::gpiod::request_config cfg;

		REQUIRE(cfg.consumer().empty());
		REQUIRE(cfg.offsets().empty());
		REQUIRE(cfg.event_buffer_size() == 0);
	}

	SECTION("constructor with default settings")
	{
		offsets offsets({ 0, 1, 2, 3 });

		::gpiod::request_config cfg({
			{ property::CONSUMER, "foobar" },
			{ property::OFFSETS, offsets},
			{ property::EVENT_BUFFER_SIZE, 64 }
		});

		REQUIRE_THAT(cfg.consumer(), Catch::Equals("foobar"));
		REQUIRE_THAT(cfg.offsets(), Catch::Equals(offsets));
		REQUIRE(cfg.event_buffer_size() == 64);
	}

	SECTION("invalid default value types passed to constructor")
	{
		REQUIRE_THROWS_AS(::gpiod::request_config({
			{ property::CONSUMER, 42 }
		}), ::std::invalid_argument);

		REQUIRE_THROWS_AS(::gpiod::request_config({
			{ property::OFFSETS, 42 }
		}), ::std::invalid_argument);

		REQUIRE_THROWS_AS(::gpiod::request_config({
			{ property::EVENT_BUFFER_SIZE, "foobar" }
		}), ::std::invalid_argument);
	}
}

TEST_CASE("request_config can be moved", "[request-config]")
{
	offsets offsets({ 0, 1, 2, 3 });

	::gpiod::request_config cfg({
		{ property::CONSUMER, "foobar" },
		{ property::OFFSETS, offsets },
		{ property::EVENT_BUFFER_SIZE, 64 }
	});

	SECTION("move constructor works")
	{
		auto moved(::std::move(cfg));
		REQUIRE_THAT(moved.consumer(), Catch::Equals("foobar"));
		REQUIRE_THAT(moved.offsets(), Catch::Equals(offsets));
		REQUIRE(moved.event_buffer_size() == 64);
	}

	SECTION("move assignment operator works")
	{
		::gpiod::request_config moved;

		moved = ::std::move(cfg);

		REQUIRE_THAT(moved.consumer(), Catch::Equals("foobar"));
		REQUIRE_THAT(moved.offsets(), Catch::Equals(offsets));
		REQUIRE(moved.event_buffer_size() == 64);
	}
}

TEST_CASE("request_config mutators work", "[request-config]")
{
	::gpiod::request_config cfg;

	SECTION("set consumer")
	{
		cfg.set_consumer("foobar");
		REQUIRE_THAT(cfg.consumer(), Catch::Equals("foobar"));
	}

	SECTION("set offsets")
	{
		offsets offsets({ 3, 1, 2, 7, 5 });
		cfg.set_offsets(offsets);
		REQUIRE_THAT(cfg.offsets(), Catch::Equals(offsets));
	}

	SECTION("set event_buffer_size")
	{
		cfg.set_event_buffer_size(128);
		REQUIRE(cfg.event_buffer_size() == 128);
	}
}

TEST_CASE("request_config generic property setting works", "[request-config]")
{
	::gpiod::request_config cfg;

	SECTION("set consumer")
	{
		cfg.set_property(property::CONSUMER, "foobar");
		REQUIRE_THAT(cfg.consumer(), Catch::Equals("foobar"));
	}

	SECTION("set offsets")
	{
		offsets offsets({ 3, 1, 2, 7, 5 });
		cfg.set_property(property::OFFSETS, offsets);
		REQUIRE_THAT(cfg.offsets(), Catch::Equals(offsets));
	}

	SECTION("set event_buffer_size")
	{
		cfg.set_property(property::EVENT_BUFFER_SIZE, 128);
		REQUIRE(cfg.event_buffer_size() == 128);
	}
}

TEST_CASE("request_config stream insertion operator works", "[request-config]")
{
	::gpiod::request_config cfg({
		{ property::CONSUMER, "foobar" },
		{ property::OFFSETS, offsets({ 0, 1, 2, 3 })},
		{ property::EVENT_BUFFER_SIZE, 32 }
	});

	::std::stringstream buf;

	buf << cfg;

	::std::string expected("gpiod::request_config(consumer='foobar', num_offsets=4, "
			       "offsets=(gpiod::offsets(0, 1, 2, 3)), event_buffer_size=32)");

	REQUIRE_THAT(buf.str(), Catch::Equals(expected));
}

} /* namespace */

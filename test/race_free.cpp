
//          Copyright Michael Florian Hava.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file ../LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <vector>
#include <iostream>
#include <algorithm>
#include <execution>
#include <catch.hpp>
#include <race_free.hpp>

TEST_CASE("race_free", "[race_free]") {
	std::vector<std::size_t> values(1'000'000);
	std::iota(std::begin(values), std::end(values), 0);

	const auto reference{std::accumulate(std::begin(values), std::end(values), std::size_t{0})};

	p2774::race_free<std::size_t> tls;
	std::for_each(std::execution::par, std::begin(values), std::end(values), [&](auto val) {
		auto handle{tls.get()};
		if(!handle) handle.emplace(val);
		else *handle += val;
	});

	const auto count{std::distance(tls.begin(), tls.end())};
	std::cout << "tls entries: " << count << "\n";
	const auto value{std::accumulate(tls.begin(), tls.end(), std::size_t{0})};
	REQUIRE(value == reference);
}

//TODO: further tests

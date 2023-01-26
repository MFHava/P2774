
//          Copyright Michael Florian Hava.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file ../LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <catch.hpp>
#include <tls.hpp>

namespace {
	struct no_default_ctor {
		no_default_ctor(int) {}
	};

	struct move_only {
		move_only() =default;
		move_only(const move_only &) =delete;
		move_only(move_only &&) noexcept =default;
		auto operator=(const move_only &) -> move_only & =delete;
		auto operator=(move_only &&) noexcept -> move_only & =default;
		~move_only() noexcept =default;
	};
}


TEST_CASE("tls default ctor", "[tls] [ctor] [default]") {
	p2774::tls<int> tls0;
	p2774::tls<move_only> tls1;
	static_assert(!std::is_constructible_v<p2774::tls<no_default_ctor>>);
}

TEST_CASE("tls custom ctor copy", "[tls] [ctor] [custom] [copy]") {
	//TODO: tls(const Type &) requires std::is_copy_constructible_v<Type>;
}

TEST_CASE("tls custom ctor move", "[tls] [ctor] [custom] [move]") {
	//TODO: tls(Type &&) requires std::is_copy_constructible_v<Type>;
}

TEST_CASE("tls custom ctor args", "[tls] [ctor] [custom] [args]") {
	//TODO: template<typename... Args> requires (std::is_constructible_v<Type, Args...> && sizeof...(Args) >= 1 && !std::is_same_v<Type, std::decay_t<first<Args...>>> && !std::is_same_v<tls, std::decay_t<first<Args...>>>) tls(Args &&...)
}

TEST_CASE("tls custom ctor functor", "[tls] [ctor] [custom] [functor]") {
	//TODO: template<typename Func> requires std::is_constructible_v<init_func, Func>  tls(Func);
}

TEST_CASE("tls copy ctor", "[tls] [ctor] [copy]") {
	//TODO: tls(const tls &) requires std::is_copy_constructible_v<Type>;
}

TEST_CASE("tls move move", "[tls] [ctor] [move]") {
	//TODO: tls(tls &&) noexcept;
}

TEST_CASE("tls copy assign", "[tls] [assign] [copy]") {
	//TODO: auto operator=(const tls &) -> tls & requires std::is_copy_constructible_v<Type>;
}

TEST_CASE("tls move assign", "[tls] [assign] [move]") {
	//TODO: auto operator=(tls &&) noexcept -> tls &;
}

TEST_CASE("tls dtor", "[tls] [dtor]") {
	//TODO: ~tls() noexcept;
}

TEST_CASE("tls local", "[tls]") {
	//TODO: [[nodiscard]] auto local() -> std::tuple<Type &, bool>
}

TEST_CASE("tls clear", "[tls]") {
	//TODO: void clear() noexcept;
}

TEST_CASE("tls iterators", "[tls]") {
	//TODO: auto begin()       noexcept -> iterator;
	//TODO: auto end()       noexcept -> iterator;
}

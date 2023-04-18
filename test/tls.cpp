
//          Copyright Michael Florian Hava.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file ../LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <future>
#include <algorithm>
#include <execution>
#include <catch.hpp>
#include <tls.hpp>

namespace {
	struct no_default_ctor {
		int val;

		no_default_ctor(int val) : val{val} {}
	};

	struct move_only {
		move_only() =default;
		move_only(const move_only &) =delete;
		move_only(move_only &&) noexcept =default;
		auto operator=(const move_only &) -> move_only & =delete;
		auto operator=(move_only &&) noexcept -> move_only & =default;
		~move_only() noexcept =default;
	};


	class allocator_state final {
		std::mutex m;
		std::byte * pool;
		std::size_t offset{0};
	public:
		explicit
		allocator_state(std::size_t size) : pool{reinterpret_cast<std::byte *>(std::malloc(size))} {
			if(!pool) throw std::bad_alloc{};
		}

		allocator_state(const allocator_state &) =delete;
		auto operator=(const allocator_state &) -> allocator_state & =delete;

		~allocator_state() noexcept { std::free(pool); }

		template<typename T>
		auto allocate(std::size_t n) -> T * {
			if(!n) return nullptr;

			const std::lock_guard lock{m};
			auto ptr{reinterpret_cast<T *>(pool + offset)};
			offset += n * sizeof(T);
			return ptr;
		}

		template<typename T>
		void deallocate(T *, std::size_t) noexcept {
			//nop ... only dtor releases memory
		}
	};

	template<typename T>
	class allocator final {
		template<typename U>
		friend
		class allocator;

		std::shared_ptr<allocator_state> state;
	public:
		using value_type = T;

		explicit
		allocator(std::size_t size) : state{std::make_shared<allocator_state>(sizeof(T) * size)} {}

		template<typename U>
		requires (not std::same_as<T, U>)
		allocator(const allocator<U> & other) noexcept : state{other.state} {}

		auto allocate(std::size_t n) -> T * { return state->template allocate<T>(n); }

		void deallocate(T * ptr, std::size_t n) noexcept { state->deallocate(ptr, n); }

		friend
		auto operator==(const allocator & lhs, const allocator & rhs) noexcept -> bool =default;
	};
}

TEST_CASE("tls default ctor", "[tls] [ctor] [default]") {
	p2774::tls<int> tls0;
	p2774::tls<move_only> tls1;
	static_assert(!std::is_constructible_v<p2774::tls<no_default_ctor>>);
}

TEST_CASE("tls custom ctor copy", "[tls] [ctor] [custom] [copy]") {
	int i{10};
	p2774::tls<int> tls0{i};
	REQUIRE(std::get<0>(tls0.local()) == i);

	no_default_ctor nd{1};
	p2774::tls<no_default_ctor> tls1{nd};
	REQUIRE(std::get<0>(tls1.local()).val == nd.val);

	static_assert(!std::is_constructible_v<p2774::tls<move_only>, const move_only &>);
}

TEST_CASE("tls custom ctor move", "[tls] [ctor] [custom] [move]") {
	p2774::tls<int> tls0{10};
	REQUIRE(std::get<0>(tls0.local()) == 10);

	p2774::tls<no_default_ctor> tls1{no_default_ctor{1}};
	REQUIRE(std::get<0>(tls1.local()).val == 1);

	static_assert(!std::is_constructible_v<p2774::tls<move_only>, move_only &&>);
}

TEST_CASE("tls custom ctor functor", "[tls] [ctor] [custom] [functor]") {
	p2774::tls<int> tls0{[] { return 10; }};
	REQUIRE(std::get<0>(tls0.local()) == 10);

	p2774::tls<no_default_ctor> tls1{[] { return no_default_ctor{1}; }};
	REQUIRE(std::get<0>(tls1.local()).val == 1);

	p2774::tls<move_only> tls2{[] { return move_only{}; }};
	(void)tls2.local();
}

TEST_CASE("tls clear", "[tls]") {
	p2774::tls<int> tls;
	REQUIRE(std::get<1>(tls.local()));
	REQUIRE(!std::get<1>(tls.local()));

	tls.clear();
	REQUIRE(std::get<1>(tls.local()));
	REQUIRE(!std::get<1>(tls.local()));
}

TEST_CASE("tls custom allocator", "[tls] [allocator]") {
	std::vector<int> vec(1'000'000);
	std::iota(std::begin(vec), std::end(vec), 0);


	p2774::tls<int, allocator<int>> tls{allocator<int>{1'024}};
	std::for_each(std::execution::par, std::begin(vec), std::end(vec), [&](const auto &) {
		auto [local, _]{tls.local()};
	});

	REQUIRE(std::distance(tls.begin(), tls.end()));
}

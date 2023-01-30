
//          Copyright Michael Florian Hava.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file ../LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once
#include <tuple>
#include <atomic>
#include <thread>
#include <concepts>
#include <functional>
#include <type_traits>

namespace p2774 {
	//! @brief scoped thread-local storage
	//! @tparam Type type of thread-local storage
	template<typename Type>
	requires (!std::is_const_v<Type> && !std::is_reference_v<Type>)
	class tls final {
		using init_func = std::move_only_function<Type() const>; //TODO: use copyable_function if LEWG decides on copy-ability

		struct node final {
			node(const init_func & init) : value{init()} {}
			node(const node & other) requires std::is_copy_constructible_v<Type> : value{other.value}, owner{other.owner} {}

			Type value;
			const std::thread::id owner{std::this_thread::get_id()};
			node * next{nullptr};
		};

		std::atomic<node *> head{nullptr}; //atomic_forward_list for simplicity
		init_func init;

		template<bool IsConst>
		class iterator_t final {
			node * ptr{nullptr};

			friend tls;

			iterator_t(node * ptr) noexcept : ptr{ptr} {}
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type        = Type;
			using difference_type   = std::ptrdiff_t;
			using pointer           = std::conditional_t<IsConst, const Type, Type> *;
			using reference         = std::conditional_t<IsConst, const Type, Type> &;

			iterator_t() noexcept =default;

			auto operator++() noexcept -> iterator_t & {
				assert(ptr);
				ptr = ptr->next;
				return *this;
			}
			auto operator++(int) noexcept -> iterator_t {
				auto tmp{*this};
				++*this;
				return tmp;
			}

			auto operator*() const noexcept -> reference {
				assert(ptr);
				return ptr->value;
			}
			auto operator->() const noexcept -> pointer { return &**this; }

			friend
			auto operator==(const iterator_t & lhs, const iterator_t & rhs) noexcept -> bool { return lhs.ptr == rhs.ptr; }
		};
	public:
		template<typename... Args>
		requires (std::is_constructible_v<Type, Args...> && (std::is_copy_constructible_v<Args> && ...))
		tls(Args &&... args) : init{[=] { return Type(args...); }} {}

		template<typename Func>
		requires std::is_same_v<Type, std::invoke_result_t<Func>>
		tls(Func f) : init{std::move(f)} {}

		tls(const tls &) =delete;
		tls(tls && other) noexcept : head{other.head.exchange(nullptr)}, init{std::move(other.init)} {} //! @attention other will be in valid but unspecified state and can only be destroyed

		auto operator=(tls other) noexcept -> tls & {
			if(this != &other) [[likely]] {
				clear();
				head = other.head.exchange(nullptr);
				init = std::move(other.init);
			}
			return *this;
		}

		~tls() noexcept { clear(); }

		//! @brief get access to thread-local storage
		//! @returns tuple with reference to thread-local storage and a bool flag specifying whether the element was newly allocated (=true)
		//! @throws any exception thrown by the respective constructor of Type
		//! @note allocates thread-local storage on first call from thread
		[[nodiscard]]
		auto local() -> std::tuple<Type &, bool> {
			for(auto ptr{head.load()}; ptr; ptr = ptr->next)
				if(ptr->owner == std::this_thread::get_id())
					return {ptr->value, false};

			auto ptr{new node(init)};
			ptr->next = head.load();
			while(!head.compare_exchange_weak(ptr->next, ptr));
			return {ptr->value, true};
		}

		//! @brief clears all thread-local storage
		//! @attention never invoke from concurrent context!
		void clear() noexcept {
			for(auto ptr{head.exchange(nullptr)}; ptr;) {
				const auto old{ptr};
				ptr = ptr->next;
				delete old;
			}
		}

		//! @name Iteration
		//! @brief forward iteration support for thread-local storage
		//! @{
		using iterator       = iterator_t<false>;
		static_assert(std::forward_iterator<iterator>);
		using const_iterator = iterator_t<true>;
		static_assert(std::forward_iterator<const_iterator>);

		auto begin() const noexcept -> const_iterator { return head.load(); }
		auto begin()       noexcept -> iterator { return head.load(); }
		auto end() const noexcept -> const_iterator { return {}; }
		auto end()       noexcept -> iterator { return {}; }

		auto cbegin() const noexcept -> const_iterator { return begin(); }
		auto cend() const noexcept -> const_iterator { return end(); }
		//! @}
	};
}


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

//#define TLS_IMPL_HASHMAP

namespace p2774 {
	//! @brief scoped thread-local storage
	//! @tparam Type type of thread-local storage
	template<typename Type>
	requires (!std::is_const_v<Type> && !std::is_reference_v<Type>)
	class tls final {
		using init_func = std::function<Type()>; //TODO: copyable_function after P2548 is adopted

		struct node final {
			node(const init_func & init) : value{init()} {}
			node(const node & other) requires std::is_copy_constructible_v<Type> : value{other.value}, owner{other.owner} {}

			Type value;
			const std::thread::id owner{std::this_thread::get_id()};
			node * next{nullptr};
		};

#ifndef TLS_IMPL_HASHMAP
		std::atomic<node *> head{nullptr}; //atomic_forward_list for simplicity

		template<bool IsConst>
		class iterator_t final {
			node * ptr{nullptr};

			friend tls;

			using tmp_type = std::atomic<node *>;
			using head_type = std::conditional_t<IsConst, const tmp_type, tmp_type>;

			iterator_t(head_type & head) noexcept : ptr{head.load()} {}
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
#else
		std::atomic<node *> * head{nullptr}; //atomic_unordered_map

		inline
		static
		std::size_t bucket_count{std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 1};

		template<bool IsConst>
		class iterator_t final {
			std::atomic<node *> * head{nullptr};
			std::size_t bucket{0};
			node * ptr{nullptr};

			friend tls;

			iterator_t(std::atomic<node *> * head) noexcept : head{head} {
				if(head)
					for(; bucket < bucket_count;) {
						ptr = head[bucket].load();
						if(ptr) break;
						++bucket;
					}
			}
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
				if(!ptr)
					for(++bucket; bucket < bucket_count;) {
						ptr = head[bucket].load();
						if(ptr) break;
						++bucket;
					}
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
#endif
		init_func init;

	public:
		template<typename Func>
		requires std::is_convertible_v<Type, std::invoke_result_t<Func>>
		tls(Func f) : init{std::move(f)} {
#ifdef TLS_IMPL_HASHMAP
			head = new std::atomic<node *>[bucket_count]{nullptr};
#endif
		}

		template<typename... Args>
		requires (std::is_constructible_v<Type, Args...> && (std::is_copy_constructible_v<Args> && ...))
		tls(Args &&... args) : tls{[=] { return Type(args...); }} {}

		tls(const tls & other) requires std::is_copy_constructible_v<Type> : init{other.init} {
#ifdef TLS_IMPL_HASHMAP
			if(!other.head) return;
			head = new std::atomic<node *>[bucket_count]{nullptr};

			for(auto i{0}; i < bucket_count; ++i) {
				node * prev{nullptr};
				for(auto ptr{other.head[i].load()}; ptr; ptr = ptr->next) {
					auto tmp{new node{*ptr}};
					if(prev) {
						prev->next = tmp;
						prev = tmp;
					} else {
						head[i] = prev = tmp;
					}
				}
			}
#else
			node * prev{nullptr};
			for(auto ptr{other.head.load()}; ptr; ptr = ptr->next) {
				auto tmp{new node{*ptr}};
				if(prev) {
					prev->next = tmp;
					prev = tmp;
				} else {
					head = prev = tmp;
				}
			}
#endif
		}
		tls(tls && other) noexcept : head{
#ifndef TLS_IMPL_HASHMAP
			other.head.exchange(nullptr)
#else
			std::exchange(other.head, nullptr)
#endif
		}, init{std::move(other.init)} {} //! @attention other will be in valid but unspecified state and can only be destroyed

		auto operator=(tls other) noexcept -> tls & {
			clear();
#ifndef TLS_IMPL_HASHMAP
			head = other.head.exchange(nullptr);
#else
			head = std::exchange(other.head, nullptr);
#endif
			init = std::move(other.init);
			return *this;
		}

		~tls() noexcept {
			clear();
#ifdef TLS_IMPL_HASHMAP
			delete head;
#endif
		}

		//! @brief get access to thread-local storage
		//! @returns tuple with reference to thread-local storage and a bool flag specifying whether the element was newly allocated (=true)
		//! @throws any exception thrown by the respective constructor of Type
		//! @note allocates thread-local storage on first call from thread
		[[nodiscard]]
		auto local() -> std::tuple<Type &, bool> {
#ifndef TLS_IMPL_HASHMAP
			for(auto ptr{head.load()}; ptr; ptr = ptr->next)
				if(ptr->owner == std::this_thread::get_id())
					return {ptr->value, false};

			auto ptr{new node(init)};
			ptr->next = head.load();
			while(!head.compare_exchange_weak(ptr->next, ptr));
			return {ptr->value, true};
#else
			const auto tid{std::this_thread::get_id()};
			const auto hash{std::hash<std::thread::id>{}(tid)};
			const auto ind{hash % bucket_count};
			auto & bucket{head[ind]};
			for(auto ptr{bucket.load()}; ptr; ptr = ptr->next)
				if(ptr->owner == tid)
					return {ptr->value, false};

			auto ptr{new node(init)};
			ptr->next = bucket.load();
			while(!bucket.compare_exchange_weak(ptr->next, ptr));
			return {ptr->value, true};
#endif
		}

		//! @brief clears all thread-local storage
		//! @attention never invoke from concurrent context!
		void clear() noexcept {
#ifndef TLS_IMPL_HASHMAP
			for(auto ptr{head.exchange(nullptr)}; ptr;) {
				const auto old{ptr};
				ptr = ptr->next;
				delete old;
			}
#else
			if(head)
				for(std::size_t i{0}; i < bucket_count; ++i) {
					for(auto ptr{head[i].exchange(nullptr)}; ptr;) {
						const auto old{ptr};
						ptr = ptr->next;
						delete old;
					}
				}
#endif
		}

		//! @name Iteration
		//! @brief forward iteration support for thread-local storage
		//! @{
		using iterator       = iterator_t<false>;
		static_assert(std::forward_iterator<iterator>);
		using const_iterator = iterator_t<true>;
		static_assert(std::forward_iterator<const_iterator>);

		auto begin() const noexcept -> const_iterator { return head; }
		auto begin()       noexcept -> iterator { return head; }
		auto end() const noexcept -> const_iterator { return {}; }
		auto end()       noexcept -> iterator { return {}; }

		auto cbegin() const noexcept -> const_iterator { return begin(); }
		auto cend() const noexcept -> const_iterator { return end(); }
		//! @}
	};
}


//          Copyright Michael Florian Hava.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file ../LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once
#include <tuple>
#include <thread>
#include <concepts>
#include <functional>
#include <type_traits>

namespace p2774 {
	namespace internal {
		template<typename Type, typename Allocator>
		class atomic_forward_list final { //TODO: fold into tls?
			struct node final {
				template<typename... Args>
				node(Args &&... args) : value{std::forward<Args>(args)...} {}

				node * next{nullptr};
				Type value;
			};

			using alloc_traits = typename std::allocator_traits<Allocator>::template rebind_traits<node>;

			std::atomic<node *> head{nullptr};
			[[no_unique_address]] typename alloc_traits::allocator_type alloc;
		public:
			template<bool IsConst>
			class iterator final {
				node * ptr{nullptr};

				friend atomic_forward_list;

				iterator(node * ptr) noexcept : ptr{ptr} {}

				operator iterator<false>() const noexcept { return ptr; }
			public:
				iterator() noexcept =default;

				operator iterator<true>() const noexcept { return ptr; }

				auto operator++() noexcept -> iterator & {
					assert(ptr);
					ptr = ptr->next;
					return *this;
				}

				auto operator*() const noexcept -> std::conditional_t<IsConst, const Type, Type> & {
					assert(ptr);
					return ptr->value;
				}

				friend
				auto operator==(const iterator & lhs, const iterator & rhs) noexcept -> bool { return lhs.ptr == rhs.ptr; }
			};

			atomic_forward_list() noexcept =default;
			atomic_forward_list(const atomic_forward_list &) =delete;
			atomic_forward_list(atomic_forward_list && other) noexcept { swap(other); }
			auto operator=(const atomic_forward_list &) -> atomic_forward_list & =delete;
			auto operator=(atomic_forward_list && other) noexcept -> atomic_forward_list & { swap(other); return *this; }
			~atomic_forward_list() noexcept { clear(); }

			template<typename Arg>
			requires std::is_constructible_v<Type, Arg>
			auto prepend(Arg & arg) -> Type & {
				auto ptr{alloc_traits::allocate(alloc, 1)};
				try {
					alloc_traits::construct(alloc, ptr, arg);
				} catch(...) {
					alloc_traits::deallocate(alloc, ptr, 1);
					throw;
				}
				ptr->next = head.load();
				while(!head.compare_exchange_weak(ptr->next, ptr));
				return ptr->value;
			}

			void clear() noexcept {
				for(auto ptr{head.load()}; ptr;) {
					const auto tmp{ptr};
					ptr = ptr->next;
					alloc_traits::destroy(alloc, tmp);
					alloc_traits::deallocate(alloc, tmp, 1);
				}
				head = nullptr;
			}

			void swap(atomic_forward_list & other) noexcept {
				const auto ptr{head.load()};
				head = other.head.load();
				other.head = ptr;
				using std::swap;
				swap(alloc, other.alloc);
			}

			auto begin() const noexcept -> iterator<true> { return head.load(); }
			auto begin()       noexcept -> iterator<false> { return head.load(); }

			auto end() const noexcept -> iterator<true> { return {}; }
			auto end()       noexcept -> iterator<false> { return {}; }
		};
	}

	//! @brief temporary thread-local storage
	//! @tparam Type type of thread-local storage
	//! @tparam Allocator allocator to use when allocating the thread-local storage
	template<typename Type, typename Allocator = std::allocator<Type>>
	requires (!std::is_const_v<Type> && !std::is_reference_v<Type>) //TODO: constraints sufficient?
	class tls final {
		using init_func = std::move_only_function<Type() const>;

		struct node_type final {
			node_type(const init_func & init) : value{init()} {}

			Type value;
			const std::thread::id owner{std::this_thread::get_id()};
		};

		using list_type = internal::atomic_forward_list<node_type, typename std::allocator_traits<Allocator>::template rebind_alloc<node_type>>;

		list_type list;
		init_func init;

		template<bool IsConst>
		class iterator_t final {
			using iterator = typename list_type::template iterator<IsConst>;
			iterator it;

			friend tls;

			iterator_t(iterator it) noexcept : it{it} {}
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type        = Type;
			using difference_type   = std::ptrdiff_t;
			using pointer           = std::conditional_t<IsConst, const Type, Type> *;
			using reference         = std::conditional_t<IsConst, const Type, Type> &;

			iterator_t() noexcept =default;

			auto operator++() noexcept -> iterator_t & {
				++it;
				return *this;
			}
			auto operator++(int) noexcept -> iterator_t {
				auto tmp{*this};
				++*this;
				return tmp;
			}

			auto operator*() const noexcept -> reference { return (*it).value; }
			auto operator->() const noexcept -> pointer { return &**this; }

			friend
			auto operator==(const iterator_t & lhs, const iterator_t & rhs) noexcept -> bool { return lhs == rhs; }
		};
	public:
		tls() requires std::is_default_constructible_v<Type> : init{[] { return Type{}; }} {} //TODO: constraints sufficient?
		tls(const Type & val) requires std::is_copy_constructible_v<Type> : init{[val] { return val; }} {} //TODO: constraints sufficient?
		tls(Type && val) requires std::is_copy_constructible_v<Type> : init{[val{std::move(val)}] { return val; }} {} //TODO: constraints sufficient?

		template<typename... Args>
		requires std::is_constructible_v<Type, Args...> //TODO: constraints sufficient?
		tls(Args &&... args) : init{[=] { return Type((args)...); }} {}

		template<typename Func>
		requires std::is_constructible_v<init_func, Func> //TODO: constraints sufficient?
		tls(Func f) : init{std::move(f)} {}

		tls(const tls &) =delete;
		tls(tls && other) noexcept : list{std::move(other.list)}, init{std::move(other.init)} {} //NOTE: other will be in valid but unspecified state and can only be destroyed
		auto operator=(const tls &) -> tls & =delete;
		auto operator=(tls && other) noexcept -> tls & { //NOTE: other will be in valid but unspecified state and can only be destroyed
			list = std::move(other.list);
			init = std::move(other.init);
			return *this;
		}
		~tls() noexcept =default;

		//! @brief get access to thread-local storage
		//! @returns tuple with reference to thread-local storage and a bool flag specifying whether the element was newly allocated (=true)
		//! @throws any exception thrown by the allocator, or constructor of Type
		//! @note allocates thread-local storage on first call from thread
		[[nodiscard]]
		auto local() -> std::tuple<Type &, bool> {
			for(auto & node : list)
				if(node.owner == std::this_thread::get_id())
					return {node.value, false};
			return {list.prepend(init).value, true};
		}

		//! @brief clears all thread-local storage
		//! @attention never invoke from concurrent context!
		void clear() noexcept { list.clear(); }

		//! @name Iteration
		//! @brief forward iteration support for thread-local storage
		//! @attention don't invoke concurrently with calls to @ref local
		//! @{
		using iterator       = iterator_t<false>;
		static_assert(std::forward_iterator<iterator>);
		using const_iterator = iterator_t<true>;
		static_assert(std::forward_iterator<const_iterator>);

		auto begin() const noexcept -> const_iterator { return list.begin(); }
		auto begin()       noexcept -> iterator { return list.begin(); }
		auto end() const noexcept -> const_iterator { return list.end(); }
		auto end()       noexcept -> iterator { return list.end(); }

		auto cbegin() const noexcept -> const_iterator { return begin(); }
		auto cend() const noexcept -> const_iterator { return end(); }
		//! @}
	};
}

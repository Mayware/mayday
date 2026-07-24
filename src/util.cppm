export module mayquill:util;
import std;
import :definitions;

export template<class... Ts>
struct overload : Ts... {
	using Ts::operator()...;
};
template<class... Ts>
overload(Ts...) -> overload<Ts...>;

export template<typename T>
class Buffered {
  private:
	bool dirty = false;
	T current;
	T pending;

  public:
	Buffered(T&& initial) : current(std::move(initial)) {}

	void buffer(T&& recent) {
		pending = std::move(recent);
		dirty = true;
	}

	void commit() {
		if (dirty == true) {
			current = std::move(pending);
			// Pending doesn't need to be changed, we won't read from it again
			dirty = false;
		}
	}
};

// A helper to return the userdata, from any of the following heirarchies
export template<typename T, typename Target>
T& gimme_data(Target&& target) {
	if constexpr (requires { target.object.user_data.get(); }) {
		return *static_cast<T*>(target.object.user_data.get());
	} else if constexpr (requires { target.user_data.get(); }) {
		return *static_cast<T*>(target.user_data.get());
	} else if constexpr (requires { target.get(); }) {
		return *static_cast<T*>(target.get());
	} else {
		static_assert(false, "Unsupported source given for user data");
	}
}

export template<typename... Types>
struct TypeList {};

// Contains functionality
template<typename List, typename T>
struct Contains;
// Specialisation of contains
template<typename T, typename... Types>
struct Contains<TypeList<Types...>, T> {
	static constexpr bool value = (std::is_same_v<T, Types> || ...);
};
// Just take the ::value
export template<typename List, typename T>
constexpr bool contains_v = Contains<List, T>::value;
// Similar idea to: https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/simple_cxx11_metaprogramming_2.html#mp_find_index
// Type index functionality
export template<typename List, typename T>
struct TypeIndex;
// If found at the front, the value is 0.
template<typename T, typename... Types>
struct TypeIndex<TypeList<T, Types...>, T> {
	static constexpr std::size_t value = 0;
};
// Like an onion has layers, this strips away the First from the list each recurse.
// Once the first specialisation matches, we add 0.
// value = 1 + value, so
// The value is 1 + value, which in turn is 1 + 1 + value, which continues until the first case is hit, in which the recursion ends
template<typename T, typename First, typename... Types>
struct TypeIndex<TypeList<First, Types...>, T> {
	static constexpr std::size_t value = 1 + TypeIndex<TypeList<Types...>, T>::value;
};
export template<typename List, typename T>
constexpr std::size_t type_index_v = TypeIndex<List, T>::value;

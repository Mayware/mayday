export module mayday.util;
import std;
import mayday.reality;

// For variant matching
export template<typename... Lambdas>
struct overload : Lambdas... {
    // To my understanding, overload resolution only works in a singlular scope, although we inherit the regular operator()'s, they're in different scopes,
    // and so no overload resolution is done, only name resolution (which is ambiguous, since we don't do overload resolution)
	using Lambdas::operator()...;
};

// A helper to return the userdata, from any of the following heirarchies
// T&& is a forwarding type (not temporary), so it allows references, and temporaries
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

export template<typename Target>
Reality& gimme_reality(Target&& target) {
	if constexpr (requires { target.server.reference; }) {
		return *static_cast<Reality*>(target.server.reference);
	} else {
        static_assert(false, "Unsupported source given for reality");
    }
}

export std::uint64_t combine_u32s(std::uint32_t high, std::uint32_t low) {
    return (static_cast<std::uint64_t>(high) << 32) | static_cast<std::uint64_t>(low);
}

// Aribtrary typelist, for example TypeList<MyTyp1, MyType2>
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

export template<typename Fn>
class Defer {
  private:
	Fn callback;

  public:
	Defer(Fn callback) : callback(std::move(callback)) {}
	~Defer() { callback(); }
};

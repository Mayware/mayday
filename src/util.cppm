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

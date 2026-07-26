module;
#include <mayquill/logger.h>
module mayquill:surfaces;
import :logger;
import :definitions;
import :shell;
import :client;
import mayday.util;
import std;

namespace mayquill {

struct WlSubsurfaceData {
	Key surface;
	Key parent;
};

class WlSurfaceData {
  private:
	using RoleList = TypeList<XdgToplevel*, XdgPopup*, WlSubsurface*, ZwlrLayerSurfaceV1*>;

  public:
	std::optional<Key> fractional_scale = std::nullopt;
	Buffered<std::optional<Key>> buffer = Buffered<std::optional<Key>>(std::nullopt);
	std::optional<Key> role = std::nullopt;
	std::vector<Key> children = std::vector<Key> {}; // Subsurfaces are the chidlren
    bool initial_configured = false;

	// A role can be removed, but if re-assigned, it must be the same role
	// as the first role was
	std::optional<std::size_t> first_role;

	WlSurfaceData() {};

	template<typename T>
	bool set_role(Key key) {
		static_assert(contains_v<RoleList, T>, "T isn't a valid role");
		// Check if the surface already has a role
		if (role)
			return false;

		// Get the index the role is
		static constexpr std::size_t new_role = type_index_v<RoleList, T>;

		// Check if we're trying to set a different role to the one already set
		// (skipped if there isn't one set)
		if (first_role && *first_role != new_role)
			return false;

		// Set the permenant role type
		if (!first_role)
			first_role = new_role;

		role = key;
		return true;
	}

    // Void means empty
    template<typename T>
    bool is_role() {
        if constexpr (std::is_same_v<T, void>) {
             return first_role ? false : true;
        }
        return *first_role == type_index_v<RoleList, T>;
    }

	void remove_role() {
		role = std::nullopt;
	}
};
}; // namespace mayquill

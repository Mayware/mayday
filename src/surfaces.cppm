module;
#include <mayquill/logger.h>
module mayquill:surfaces;
import :logger;
import :definitions;
import :roles;
import :client;
import :util;
import std;

namespace mayquill {

struct WlSubsurfaceData {
	Key surface;
	Key parent;
};

struct WlSurfaceData {
	std::optional<Key> fractional_scale;
	Buffered<std::optional<Key>> buffer;

	using Role = std::optional<std::variant<XdgSurfaceData*, WlSubsurfaceData*, ZwlrLayerSurfaceData*>>;
	Role role;
	std::vector<Key> children; // Subsurfaces are the chidlren

	enum class RoleEnum {
		XdgSurface,
		WlSubsurface,
		ZwlrLayerSurface,
	};
	// A role can be removed, but if re-assigned, it must be the same role
	// as the first role was
	std::optional<RoleEnum> first_role;

	bool set_role(Role new_role) {

        // Remove the current role if std::nullopt is given
		if (!new_role) {
			role = std::nullopt;
			return true;
		}

        // Check if the surface already has a role
        if (role) return false;

		RoleEnum new_role_enum = std::visit(
			[]<typename T>(T) -> RoleEnum {
                using Type = std::remove_pointer_t<T>;
				if constexpr (std::is_same_v<Type, XdgSurfaceData>) {
					return RoleEnum::XdgSurface;
				} else if constexpr (std::is_same_v<Type, WlSubsurfaceData>) {
					return RoleEnum::WlSubsurface;
				} else if constexpr (std::is_same_v<Type, ZwlrLayerSurfaceData>) {
					return RoleEnum::ZwlrLayerSurface;
				} else {
					static_assert(false, "Invalid role enum");
				}
			},
			*new_role);

        // Check if the new role is the same as the old one (or if it is the first one)
		if (!first_role || *first_role == new_role_enum) {
			first_role = new_role_enum;
			role = new_role;
            return true;
		} else {
            return false;
		}
	}
};
}; // namespace mayquill

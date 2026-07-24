module;
#include <mayquill/logger.h>
module mayquill:surfaces;
import :logger;
import :definitions;
import :shell;
import :client;
import :util;
import std;

namespace mayquill {

struct WlSubsurfaceData {
	Key surface;
	Key parent;
};

class WlSurfaceData {
  private:
	using Role = std::optional<std::variant<XdgToplevelData*, XdgPopupData*, WlSubsurfaceData*, ZwlrLayerSurfaceData*>>;
	enum class RoleEnum {
		XdgToplevel,
		XdgPopup,
		WlSubsurface,
		ZwlrLayerSurface,
	};
	// A role can be removed, but if re-assigned, it must be the same role
	// as the first role was
	std::optional<RoleEnum> first_role;

  public:
	std::optional<Key> fractional_scale;
	Buffered<std::optional<Key>> buffer;
	Role role;
	std::vector<Key> children; // Subsurfaces are the chidlren

	WlSurfaceData(
		std::optional<Key> fractional_scale,
		Buffered<std::optional<Key>> buffer,
		Role role,
		std::vector<Key> children) : fractional_scale(std::move(fractional_scale)),
									 buffer(std::move(buffer)),
									 role(std::move(role)),
									 children(std::move(children)) {}

	bool set_role(Role new_role) {
		// Remove the current role if std::nullopt is given
		if (!new_role) {
			role = std::nullopt;
			return true;
		}
		// Check if the surface already has a role
		if (role)
			return false;

		RoleEnum new_role_enum = std::visit(
			[]<typename T>(T) -> RoleEnum {
				using Type = std::remove_pointer_t<T>;
				if constexpr (std::is_same_v<Type, XdgToplevelData>) {
					return RoleEnum::XdgToplevel;
				} else if constexpr (std::is_same_v<Type, XdgPopupData>) {
					return RoleEnum::XdgPopup;
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

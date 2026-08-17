module;
#include "mayquill/logger.h"
#include <cassert>
module mayquill;
import mayday.surfaces;
import mayday.util;

namespace mayquill {
struct WpFractionalScaleData {
	Key surface;
};

void WpFractionalScaleManagerV1::handle(Request request) {
	std::visit(overload {
				   [this](GetFractionalScale& request) {
					   auto surface = client.grab_object<WlSurface>(request.surface);
					   auto fractional = client.add_object<WpFractionalScaleV1>(request.id,
						   std::make_unique<WpFractionalScaleData>(surface.key));
					   gimme_data<WlSurfaceData>(surface).fractional_scale = fractional.key;
				   },
				   [this](Destroy& request) {
					   auto& surface = client.get_object<WlSurface>(gimme_data<WpFractionalScaleData>(user_data).surface);
					   gimme_data<WlSurfaceData>(surface).fractional_scale = std::nullopt;
				   }},
		request);
}

void WlCompositor::handle(Request request) {
	std::visit(overload {
				   [this](CreateSurface& request) {
					   client.add_object<WlSurface>(request.id,
						   std::make_unique<WlSurfaceData>(client));
				   },
				   [this](CreateRegion& request) {
					   client.add_object<WlRegion>(request.id);
				   },
				   [this](Release& request) {
				   }},
		request);
};

void WlSurface::handle(Request request) {
	auto& surface_data = gimme_data<WlSurfaceData>(user_data);
	std::visit(overload {
				   [this, &surface_data](Attach& request) {
					   if (request.x != 0 || request.y != 0)
						   client.error(keyd.id, WlSurface::ErrorEnum::InvalidOffset,
							   std::format("X and Y must be 0, but they were {} and {}", request.x, request.y));
					   surface_data.delta.buffer.buffer = request.buffer ? std::optional(client.grab_object<WlBuffer>(*request.buffer).key) : std::nullopt;
				   },
				   [this](Damage& request) {},
				   [this, &surface_data](Frame& request) {
					   auto callback = client.add_object<WlCallback>(request.callback);
					   surface_data.delta.frame_callbacks.push_back(callback.key);
				   },
				   [this](SetOpaqueRegion& request) {},
				   [this](SetInputRegion& request) {},
				   [this, &surface_data](Commit& request) {
					   surface_data.commit_pending_delta();

					   // XDG surfaces expect us to send an 'initial configure' on their first commit
					   // Otherwise, they'll just sit there, waiting, menacingly
					   if (surface_data.initial_configured == false) {
						   if (surface_data.is_role<XdgToplevel>() || surface_data.is_role<XdgPopup>()) {
							   XdgSurface* xdg_surface;
							   if (surface_data.is_role<XdgToplevel>()) {
								   auto& toplevel = client.get_object<XdgToplevel>(*surface_data.role);
								   toplevel.configure(0, 0, std::vector<std::uint8_t> {});
								   xdg_surface = &client.get_object<XdgSurface>(gimme_data<XdgToplevelData>(toplevel).xdg_surface);
							   } else {
								   assert(false);
							   }
							   xdg_surface->configure(client.next_serial(gimme_data<XdgSurfaceData>(*xdg_surface).serials));
							   surface_data.initial_configured = true;
						   } else if (surface_data.is_role<ZwlrLayerSurfaceV1>()) {
							   assert(false);
						   } else if (surface_data.is_role<WlSubsurface>()) {
							   surface_data.initial_configured = true;
						   } else {
							   MQ_XERROR("Incorrectly configured role");
						   }
						   MQ_INFO("Sent initial configure");
					   }

					   // if (surface_data.buffer.held()) {
					   // client.get_object<WlBuffer>((*surface_data.buffer.held())).release();
					   // }
				   },
				   [this](SetBufferTransform& request) {},
				   [this](SetBufferScale& request) {},
				   [this](DamageBuffer& request) {},
				   [this](Offset& request) {},
				   [this](GetRelease& request) {},
				   [this](Destroy& request) {},
			   },
		request);
}

void WlSubcompositor::handle(Request request) {
	std::visit(overload {
				   [this](GetSubsurface& request) {
					   auto surface = client.grab_object<WlSurface>(request.surface);
					   auto parent = client.grab_object<WlSurface>(request.parent);
					   auto subsurface = client.add_object<WlSubsurface>(request.id, std::make_unique<WlSubsurfaceData>(surface.key, parent.key));
					   gimme_data<WlSurfaceData>(surface).set_role<WlSubsurface>(subsurface.key);
					   gimme_data<WlSurfaceData>(parent).children.push_back(subsurface.key);
				   },
				   [this](Destroy& request) {},
			   },
		request);
}

void WlSubsurface::handle(Request request) {
	auto& subsurface_data = gimme_data<WlSubsurfaceData>(user_data);
	std::visit(overload {
				   [this](SetPosition& request) {},
				   [this](PlaceAbove& request) {},
				   [this](PlaceBelow& request) {},
				   [this, &subsurface_data](SetSync& request) {
					   subsurface_data.synchronised = true;
				   },
				   [this, &subsurface_data](SetDesync& request) {
					   subsurface_data.synchronised = false;
				   },
				   [this, &subsurface_data](Destroy& request) {
					   auto& surface = client.get_object<WlSurface>(subsurface_data.surface);
					   gimme_data<WlSurfaceData>(surface).remove_role();

					   auto& parent = client.get_object<WlSurface>(subsurface_data.parent);
					   std::erase(gimme_data<WlSurfaceData>(parent).children, keyd);
				   },
			   },
		request);
}
} // namespace mayquill

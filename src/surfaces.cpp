module mayquill;
import :surfaces;
import :util;

namespace mayquill {
struct WpFractionalScaleData {
	Key surface;
};

void WpFractionalScaleManagerV1::handle(Request request) {
	std::visit(overload {
				   [this](GetFractionalScale& request) {
					   auto surface = client.grab_object<WlSurface>(request.surface);
					   auto fractional = client.add_object<WpFractionalScaleV1>(request.id);
					   gimme_data<WlSurfaceData>(surface).fractional_scale = fractional.key;
				   },
				   [this](Destroy& request) {
				   }},
		request);
}

void WlCompositor::handle(Request request) {
	std::visit(overload {
				   [this](CreateSurface& request) {
					   client.add_object<WlSurface>(request.id,
						   std::make_unique<WlSurfaceData>(std::nullopt, Buffered<std::optional<Key>>(std::nullopt), std::nullopt, std::vector<Key> {}));
				   },
				   [this](CreateRegion& request) {
					   client.add_object<WlRegion>(request.id);
				   },
				   [this](Release& request) {
				   }},
		request);
};

void WlSurface::handle(Request request) {
	auto* surface_data = gimme_data<WlSurfaceData*>(user_data);
	std::visit(overload {
				   [this, &surface_data](Attach& request) {
					   if (request.x != 0 || request.y != 0)
						   client.error(keyd.id, WlSurface::ErrorEnum::InvalidOffset,
							   std::format("X and Y must be 0, but they were {} and {}", request.x, request.y));
					   surface_data->buffer.buffer(request.buffer ? std::optional(client.grab_object<WlBuffer>(*request.buffer).key) : std::nullopt);
				   },
				   [this](Damage& request) {},
				   [this](Frame& request) {},
				   [this](SetOpaqueRegion& request) {},
				   [this](SetInputRegion& request) {},
				   [this, surface_data](Commit& request) {
					   surface_data->buffer.commit();
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
					   gimme_data<WlSurfaceData>(surface).set_role(&gimme_data<WlSubsurfaceData>(subsurface));
					   gimme_data<WlSurfaceData>(parent).children.push_back(subsurface.key);
				   },
				   [this](Destroy& request) {},
			   },
		request);
}

void WlSubsurface::handle(Request request) {
	std::visit(overload {
				   [this](SetPosition& request) {},
				   [this](PlaceAbove& request) {},
				   [this](PlaceBelow& request) {},
				   [this](SetSync& request) {},
				   [this](SetDesync& request) {},
				   [this](Destroy& request) {},
			   },
		request);
}
} // namespace mayquill

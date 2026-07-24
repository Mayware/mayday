module mayquill;
import :surfaces;
import util;

namespace mayquill {
void WlCompositor::handle(Request request) {
	std::visit(overload {
				   [this](CreateSurface& request) {
					   client.add_object<WlSurface>(request.id,
						   std::make_unique<WlSurfaceData>(WlSurfaceData {
							   .buffer = Buffered<std::optional<Key>>(std::nullopt)}));
				   },
				   [this](CreateRegion& request) {
					   client.add_object<WlRegion>(request.id);
				   },
				   [this](Release& request) {
				   }},
		request);
};

void WlSurface::handle(Request request) {
	auto* surface_data = static_cast<WlSurfaceData*>(user_data.get());
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
					   preferred_buffer_scale(1);
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
} // namespace mayquill

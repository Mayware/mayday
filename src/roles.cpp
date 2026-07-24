module mayquill;
import util;

namespace mayquill {
struct WlrLayerSurfaceData {
	std::optional<Key> output;
	ZwlrLayerShellV1::LayerEnum layer;
	std::string _namespace;
};

void ZwlrLayerShellV1::handle(Request request) {
	std::visit(overload {
				   [this](GetLayerSurface& request) {
					   client.add_object<ZwlrLayerSurfaceV1>(request.id,
						   std::make_unique<WlrLayerSurfaceData>(WlrLayerSurfaceData {
							   .output = request.output ? std::optional(client.grab_object<WlOutput>(*request.output).key) : std::nullopt,
							   .layer = request.layer,
							   ._namespace = request._namespace,
						   }));
				   },
				   [this](Destroy& request) {}},
		request);
}

struct XdgSurfaceData {
	Key surface;
};

void XdgWmBase::handle(Request request) {
	std::visit(overload {
				   [this](CreatePositioner& request) {
					   client.add_object<XdgPositioner>(request.id);
				   },
				   [this](GetXdgSurface& request) {
					   client.add_object<XdgSurface>(request.id,
						   std::make_unique<XdgSurfaceData>(XdgSurfaceData {
							   .surface = client.grab_object<WlSurface>(request.surface).key,
						   }));
				   },
				   [this](Pong& request) {},
				   [this](Destroy& request) {},
			   },
		request);
}

void XdgSurface::handle(Request request) {
	std::visit(overload {

				   [this](GetToplevel& request) {
					   client.add_object<XdgToplevel>(request.id);
				   },
				   [this](GetPopup& request) {},
				   [this](SetWindowGeometry& request) {},
				   [this](AckConfigure& request) {},
				   [this](Destroy& request) {},
			   },
		request);
}

void XdgToplevel::handle(Request request) {
	std::visit(overload {
				   [this](SetParent& request) {},
				   [this](SetTitle& request) {},
				   [this](SetAppId& request) {},
				   [this](ShowWindowMenu& request) {},
				   [this](Move& request) {},
				   [this](Resize& request) {},
				   [this](SetMaxSize& request) {},
				   [this](SetMinSize& request) {},
				   [this](SetMaximized& request) {},
				   [this](UnsetMaximized& request) {},
				   [this](SetFullscreen& request) {},
				   [this](UnsetFullscreen& request) {},
				   [this](SetMinimized& request) {},
				   [this](Destroy& request) {},
			   },
		request);
}

} // namespace mayquill

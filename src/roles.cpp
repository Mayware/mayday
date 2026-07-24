module mayquill;
import :roles;
import :util;

namespace mayquill {
void ZwlrLayerShellV1::handle(Request request) {
	std::visit(overload {
				   [this](GetLayerSurface& request) {
					   client.add_object<ZwlrLayerSurfaceV1>(request.id,
						   std::make_unique<ZwlrLayerSurfaceData>(
                               request.output ? std::optional(client.grab_object<WlOutput>(*request.output).key) : std::nullopt,
                               request.layer,
                               request._namespace
						   ));
				   },
				   [this](Destroy& request) {}},
		request);
}

void XdgWmBase::handle(Request request) {
	std::visit(overload {
				   [this](CreatePositioner& request) {
					   client.add_object<XdgPositioner>(request.id);
				   },
				   [this](GetXdgSurface& request) {
					   client.add_object<XdgSurface>(request.id,
						   std::make_unique<XdgSurfaceData>(client.grab_object<WlSurface>(request.surface).key));
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

void XdgPopup::handle(Request request) {
	std::visit(overload {
				   [this](Grab& request) {},
				   [this](Reposition& request) {},
				   [this](Destroy& request) {},
			   },
		request);
}
} // namespace mayquill

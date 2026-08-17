module mayquill;
import mayday.shell;
import mayday.util;
import mayday.surfaces;

namespace mayquill {
void ZwlrLayerShellV1::handle(Request request) {
	std::visit(overload {
				   [this](GetLayerSurface& request) {
					   auto surface = client.grab_object<WlSurface>(request.surface);
					   auto layer_surface = client.add_object<ZwlrLayerSurfaceV1>(request.id,
						   std::make_unique<ZwlrLayerSurfaceData>(
							   surface.key,
							   request.output ? std::optional(client.grab_object<WlOutput>(*request.output).key) : std::nullopt,
							   request.layer,
							   request._namespace));
					   gimme_data<WlSurfaceData>(surface).set_role<ZwlrLayerSurfaceV1>(layer_surface.key);
				   },
				   [this](Destroy& request) {}},
		request);
}

void ZwlrLayerSurfaceV1::handle(Request request) {
	std::visit(overload {
				   [this](SetSize& request) {},
				   [this](SetAnchor& request) {},
				   [this](SetExclusiveZone& request) {},
				   [this](SetMargin& request) {},
				   [this](SetKeyboardInteractivity& request) {},
				   [this](GetPopup& request) {},
				   [this](AckConfigure& request) {},
				   [this](Destroy& request) {
					   auto& surface = client.get_object<WlSurface>(
						   gimme_data<ZwlrLayerSurfaceData>(user_data).surface);
					   gimme_data<WlSurfaceData>(surface).remove_role();
				   },
				   [this](SetLayer& request) {},
				   [this](SetExclusiveEdge& request) {},
			   },
		request);
}

void XdgWmBase::handle(Request request) {
	std::visit(overload {
				   [this](CreatePositioner& request) {
					   client.add_object<XdgPositioner>(request.id);
				   },
				   [this](GetXdgSurface& request) {
					   auto surface = client.grab_object<WlSurface>(request.surface);
					   client.add_object<XdgSurface>(request.id,
						   std::make_unique<XdgSurfaceData>(surface.key));
				   },
				   [this](Pong& request) {},
				   [this](Destroy& request) {},
			   },
		request);
}

void XdgSurface::handle(Request request) {
	std::visit(overload {
				   [this](GetToplevel& request) {
					   auto surface_key = gimme_data<XdgSurfaceData>(user_data).surface;
					   auto& surface = client.get_object<WlSurface>(surface_key);
					   auto toplevel = client.add_object<XdgToplevel>(request.id,
						   std::make_unique<XdgToplevelData>(surface_key, keyd));
					   gimme_data<WlSurfaceData>(surface).set_role<XdgToplevel>(toplevel.key);
				   },
				   [this](GetPopup& request) {
					   auto surface_key = gimme_data<XdgSurfaceData>(user_data).surface;
					   auto& surface = client.get_object<WlSurface>(surface_key);
					   auto popup = client.add_object<XdgPopup>(request.id,
						   std::make_unique<XdgPopupData>(surface_key, keyd));
					   gimme_data<WlSurfaceData>(surface).set_role<XdgPopup>(popup.key);
				   },
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
				   [this](Destroy& request) {
					   auto& surface = client.get_object<WlSurface>(gimme_data<XdgToplevelData>(user_data).surface);
					   gimme_data<WlSurfaceData>(surface).remove_role();
				   },
			   },
		request);
}

void XdgPopup::handle(Request request) {
	std::visit(overload {
				   [this](Grab& request) {},
				   [this](Reposition& request) {},
				   [this](Destroy& request) {
					   auto& surface = client.get_object<WlSurface>(gimme_data<XdgPopupData>(user_data).surface);
					   gimme_data<WlSurfaceData>(surface).remove_role();
				   },
			   },
		request);
}
} // namespace mayquill

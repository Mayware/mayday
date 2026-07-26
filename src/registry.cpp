module mayquill;
import mayday.util;
import std;

namespace mayquill {
void WlDisplay::handle(Request request) {
	std::visit(overload {
				   [this](Sync& request) {
					   auto callback = client.add_object<WlCallback>(request.callback);
					   callback.object.done(0);
					   callback.object.destroy();
				   },
				   [this](GetRegistry& request) {
					   auto registry = client.add_object<WlRegistry>(request.registry);
					   registry.object.global(1, std::string(WlCompositor::interface), WlCompositor::version);
					   registry.object.global(2, std::string(WlSubcompositor::interface), WlSubcompositor::version);
					   registry.object.global(3, std::string(WlShm::interface), WlShm::version);
					   registry.object.global(4, std::string(XdgWmBase::interface), XdgWmBase::version);
					   registry.object.global(5, std::string(ZwlrLayerShellV1::interface), ZwlrLayerShellV1::version);
					   registry.object.global(6, std::string(WpFractionalScaleManagerV1::interface), WpFractionalScaleManagerV1::version);
				   }},
		request);
}

void WlRegistry::handle(Request request) {
	std::visit(overload {
				   [this](Bind& request) {
					   switch (request.name) {
					   case 1:
						   client.add_object<WlCompositor>(request.id);
						   break;
					   case 2:
						   client.add_object<WlSubcompositor>(request.id);
						   break;
					   case 3:
						   client.add_object<WlShm>(request.id).object.format(WlShm::FormatEnum::Argb8888);
						   break;
					   case 4:
						   client.add_object<XdgWmBase>(request.id);
						   break;
					   case 5:
						   client.add_object<ZwlrLayerShellV1>(request.id);
						   break;
					   case 6:
						   client.add_object<WpFractionalScaleManagerV1>(request.id);
						   break;
					   }
				   }},
		request);
}
}; // namespace mayquill

module mayquill;
import util;
import std;

namespace mayquill {
void WlDisplay::handle(Request request) {
	std::visit(overload {
				   [this](Sync& request) {
					   auto& callback = client.add_object<WlCallback>(request.callback);
					   callback.done(0);
					   callback.destroy();
				   },
				   [this](GetRegistry& request) {
					   auto& registry = client.add_object<WlRegistry>(request.registry);
					   registry.global(1, std::string(WlCompositor::interface), WlCompositor::version);
					   registry.global(2, std::string(WlSubcompositor::interface), WlSubcompositor::version);
				   }},
		request);
}

void WlRegistry::handle(Request request) {
	std::visit(overload {
				   [this](Bind& request) {
					   std::println("name {} interface {} version {} id {}", request.name, request.interface, request.version, request.id);
				   }},
		request);
}
}; // namespace mayquill

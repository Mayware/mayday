export module mayquill:roles;
import :definitions;
import :interface;
import std;

namespace mayquill {
struct ZwlrLayerSurfaceData {
	std::optional<Key> output;
	ZwlrLayerShellV1::LayerEnum layer;
	std::string _namespace;
};

struct XdgSurfaceData {
	Key surface;
};
} // namespace mayquill

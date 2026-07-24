export module mayquill:shell;
import :definitions;
import :interface;
import std;

namespace mayquill {
struct ZwlrLayerSurfaceData {
	Key surface;
	std::optional<Key> output;
	ZwlrLayerShellV1::LayerEnum layer;
	std::string _namespace;
};

struct XdgSurfaceData {
	Key surface;
};

struct XdgToplevelData {
	Key surface;
	Key xdg_surface;
};

struct XdgPopupData {
	Key surface;
	Key xdg_surface;
};
} // namespace mayquill

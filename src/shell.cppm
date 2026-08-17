export module mayday.shell;
import mayquill;
import std;

export namespace mayquill {
struct ZwlrLayerSurfaceData {
	Key surface;
	std::optional<Key> output;
	ZwlrLayerShellV1::LayerEnum layer;
	std::string _namespace;
};

class XdgSurfaceData {
  public:
	Key surface;
	// Serials that are currently in flight
	std::deque<std::uint32_t> serials = std::deque<std::uint32_t>();

	XdgSurfaceData(Key surface);
	~XdgSurfaceData();
};

// Constructor and destructor have to be out of line, or clang ices for some random bullshit reason
XdgSurfaceData::XdgSurfaceData(Key surface) : surface(surface) {}
XdgSurfaceData::~XdgSurfaceData() = default;

struct XdgToplevelData {
	Key surface;
	Key xdg_surface;
};

struct XdgPopupData {
	Key surface;
	Key xdg_surface;
};
} // namespace mayquill

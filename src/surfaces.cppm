module mayquill:surfaces;
import :definitions;
import util;

namespace mayquill {
struct WlSurfaceData {
	std::optional<Key> fractional_scale;
	Buffered<std::optional<Key>> buffer;
};
}; // namespace mayquill

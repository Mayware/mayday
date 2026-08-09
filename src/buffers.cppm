module mayquill:buffers;
import std;
import vulkan;

namespace mayquill {
struct WlBufferDataInner {
	std::optional<vk::raii::DeviceMemory> memory;
	vk::raii::Image image;
	vk::raii::ImageView image_view;
};

class WlBufferData {
  public:
	std::mutex lock;
	std::function<WlBufferDataInner()> kicker;
	std::optional<WlBufferDataInner> inner;
    std::jthread uploader;

	WlBufferData(std::function<WlBufferDataInner()> kicker) : kicker(std::move(kicker)) {}
};
} // namespace mayquill

module mayquill:buffers;
import std;
import vulkan;

namespace mayquill {
struct WlBufferDataInner {
	std::vector<vk::raii::DeviceMemory> memories;
	vk::raii::Image image;
	vk::raii::ImageView image_view;
};

struct ThreadedUpload {
	std::mutex lock;
	std::function<WlBufferDataInner()> kicker;
	std::jthread uploader; // Kept, solely so the destructor yields for the thread

	ThreadedUpload(std::function<WlBufferDataInner()> kicker) : kicker(std::move(kicker)) {}
};

class WlBufferData {
  public:
	std::optional<ThreadedUpload> threaded_upload;
	std::optional<WlBufferDataInner> inner;

    // Constructor for a lambda (ie. shm)
	WlBufferData(std::function<WlBufferDataInner()> kicker) : threaded_upload(std::move(kicker)) {}

    // Constructor for immediate data (ie. dambuf)
	WlBufferData(WlBufferDataInner inner) : inner(std::move(inner)) {}
};
} // namespace mayquill

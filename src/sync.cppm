module;
#include <xf86drm.h>
export module mayday.sync;
import logger;
import std;
import mayquill;
import mayday.util;

export namespace mayquill {
struct SyncobjTimelineData {
	int device_fd;
	std::uint32_t handle;

	~SyncobjTimelineData() {
		drmSyncobjDestroy(device_fd, handle);
	}
};

struct SyncobjPoint {
	std::uint64_t point;
	std::shared_ptr<SyncobjTimelineData> timeline_data;

	void signal_release(std::source_location source = std::source_location::current()) {
		// Address of handle, because it actually takes an array. Same with points
		if (drmSyncobjTimelineSignal(timeline_data->device_fd, &timeline_data->handle, &point, 1))
			fail<Er, No>([] { return "Failed to signal release timeline"; }, source);
	}
};

} // namespace mayquill

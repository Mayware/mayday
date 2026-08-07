module;
#include "mayquill/logger.h"
#include <xf86drm.h>
module mayquill:sync;
import :definitions;
import :client;
import :logger;
import mayday.util;

namespace mayquill {
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
            MQ_SXERRNO(source, "Failed to signal release timeline");

	}
};

} // namespace mayquill

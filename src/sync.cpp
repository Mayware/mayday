module;
#include "mayday/macros.h"
#include <unistd.h>
#include <xf86drm.h>
module mayquill;
import mayday.util;
import mayday.surfaces;
import mayday.sync;

namespace mayquill {

struct SyncobjSurfaceData {
	Key surface;
};

// Gives us fences to wait on before we're allowed to read the buffer,
// and a fence we can singal to show we're done with the buffer
void WpLinuxDrmSyncobjManagerV1::handle(Request request) {
	std::visit(overload {
				   [this](GetSurface& request) {
					   auto surface = client.grab_object<WlSurface>(request.surface);
					   auto& surface_data = gimme_data<WlSurfaceData>(surface);
					   if (surface_data.drm_syncobj)
						   client.error(keyd.id, WpLinuxDrmSyncobjManagerV1::ErrorEnum::SurfaceExists, "Big boss, the Surface already has a DRM syncobj surface, surface.");
					   auto syncobj = client.add_object<WpLinuxDrmSyncobjSurfaceV1>(request.id,
						   std::make_unique<SyncobjSurfaceData>(surface.key));
					   surface_data.drm_syncobj = syncobj.key;
				   },
				   // Timelines, like a timeline semaphore, but linux-ism
				   [this](ImportTimeline& request) {
					   DEFER([fd = request.fd]() { close(fd); });
					   auto& reality = gimme_reality(client);
					   std::uint32_t handle;
					   if (drmSyncobjFDToHandle(reality.seat.device_fd, request.fd, &handle))
						   client.error(keyd.id, WpLinuxDrmSyncobjManagerV1::ErrorEnum::InvalidTimeline, combo_errno("Bossman, we failed to import the timeline"));
					   client.add_object<WpLinuxDrmSyncobjTimelineV1>(request.id, std::make_unique<SyncobjTimelineData>(reality.seat.device_fd, handle));
				   },
				   [this](Destroy& request) {},
			   },
		request);
}

// SyncSurfaces co-exist with surfaces for the entire lifetime, but acquire / release points *only* apply to the buffer they've been committed with.
// Additionally, if the syncsurface is destroyed, the points need to remain valid (hence, they hold a shared ptr to the data). The spec says that
// any points in our pending delta (ie. non-committed) can be cleared upon SyncSurface destruction, but it says may so i think it's optional, so i will just keep it
// https://wayland.app/protocols/linux-drm-syncobj-v1#wp_linux_drm_syncobj_surface_v1:request:destroy
void WpLinuxDrmSyncobjSurfaceV1::handle(Request request) {
	auto& data = gimme_data<SyncobjSurfaceData>(user_data);
	std::visit(overload {
				   // Integer we wait on, to read the buffer
				   [this, &data](SetAcquirePoint& request) {
					   // Wayland spec only does u32's, but the timeline integer is a u64, hence we combine two fields into one
					   auto& surface = client.get_object<WlSurface>(data.surface);
					   auto timeline_data = gimme_data<std::shared_ptr<SyncobjTimelineData>>(client.grab_object<WpLinuxDrmSyncobjTimelineV1>(request.timeline));
					   gimme_data<WlSurfaceData>(surface).delta.buffer.acquire_point = SyncobjPoint {
						   .point = combine_u32s(request.point_hi, request.point_lo),
						   .timeline_data = timeline_data,
					   };
				   },
				   // We signal the semaphore, with this integer, when we're done reading
				   [this, &data](SetReleasePoint& request) {
					   auto& surface = client.get_object<WlSurface>(data.surface);
					   auto timeline_data = gimme_data<std::shared_ptr<SyncobjTimelineData>>(client.grab_object<WpLinuxDrmSyncobjTimelineV1>(request.timeline));
					   gimme_data<WlSurfaceData>(surface).delta.buffer.release_point = SyncobjPoint {
						   .point = combine_u32s(request.point_hi, request.point_lo),
						   .timeline_data = timeline_data,
					   };
				   },
				   [this, &data](Destroy& request) {
					   auto& surface = client.get_object<WlSurface>(data.surface);
					   gimme_data<WlSurfaceData>(surface).drm_syncobj = std::nullopt;
				   },
			   },
		request);
}

void WpLinuxDrmSyncobjTimelineV1::handle(Request request) {
	std::visit(overload {
				   [this](Destroy& request) {},
			   },
		request);
}

// Allows clients to essentially specify that a commit they make to a surface,
// must atleast be shown for one frame (one vblank), ie. so we aren't allowed to
// skip any commits they specify, even if they specify a new one after that
// (ie. can't collapse commits, to the newest commit). Hence, FIFO
struct FifoData {
	Key surface;
};

void WpFifoManagerV1::handle(Request request) {
	std::visit(overload {
				   [this](GetFifo& request) {
					   auto surface = client.grab_object<WlSurface>(request.surface);
					   auto& surface_data = gimme_data<WlSurfaceData>(surface);
					   if (surface_data.fifo)
						   client.error(keyd.id, WpFifoManagerV1::ErrorEnum::AlreadyExists, "Surface already got a FIFO");
					   auto fifo = client.add_object<WpFifoV1>(request.id, std::make_unique<FifoData>(surface.key));
					   surface_data.fifo = fifo.key;
				   },
				   [this](Destroy& request) {},
			   },
		request);
}

void WpFifoV1::handle(Request request) {
	auto& data = gimme_data<FifoData>(user_data);
	std::visit(overload {
				   [this, &data](SetBarrier& request) {
					   auto& surface = client.get_object<WlSurface>(data.surface);
					   gimme_data<WlSurfaceData>(surface).delta.fifo_set_barrier = true;
				   },
				   [this, &data](WaitBarrier& request) {
					   auto& surface = client.get_object<WlSurface>(data.surface);
					   gimme_data<WlSurfaceData>(surface).delta.fifo_wait_barrier = true;
				   },
				   [this, &data](Destroy& request) {
					   auto& surface = client.get_object<WlSurface>(data.surface);
					   gimme_data<WlSurfaceData>(surface).fifo = std::nullopt;
				   },
			   },
		request);
}

// Specify that a commit must not be shown before a certain timestamp
struct CommitTimerData {
	Key surface;
};

void WpCommitTimingManagerV1::handle(Request request) {
	std::visit(overload {
				   [this](GetTimer& request) {
					   auto surface = client.grab_object<WlSurface>(request.surface);
					   auto& surface_data = gimme_data<WlSurfaceData>(surface);
					   if (surface_data.commit_timer)
						   client.error(keyd.id, WpCommitTimingManagerV1::ErrorEnum::CommitTimerExists, "Surface already has a commit timer bozo");
					   auto timer = client.add_object<WpCommitTimerV1>(request.id, std::make_unique<CommitTimerData>(surface.key));
					   surface_data.commit_timer = timer.key;
				   },
				   [this](Destroy& request) {},
			   },
		request);
}

void WpCommitTimerV1::handle(Request request) {
	auto& data = gimme_data<CommitTimerData>(user_data);
	std::visit(overload {
				   [this, &data](SetTimestamp& request) {
					   // TV stands for television, or alternatively, time value
					   auto& surface = client.get_object<WlSurface>(data.surface);
					   auto& delta = gimme_data<WlSurfaceData>(surface).delta;
					   if (delta.commit_timestamp)
						   client.error(keyd.id, WpCommitTimerV1::ErrorEnum::TimestampExists, "Timestamp already exists man");

					   auto seconds = std::chrono::seconds(combine_u32s(request.tv_sec_hi, request.tv_sec_lo));
					   auto nanoseconds = std::chrono::nanoseconds(request.tv_nsec);
					   delta.commit_timestamp = std::chrono::steady_clock::time_point(seconds + nanoseconds);
				   },
				   [this, &data](Destroy& request) {
					   auto& surface = client.get_object<WlSurface>(data.surface);
					   gimme_data<WlSurfaceData>(surface).commit_timer = std::nullopt;
				   },
			   },
		request);
}

// We give feedback to the clients of how their surface was presented
struct PresentationFeedbackData {
	Key surface;
};

void WpPresentation::handle(Request request) {
	std::visit(overload {
				   [this](Feedback& request) {
					   // Although the spec is somewhat unclear, the feedback surface is part of the buffered state
					   auto surface = client.grab_object<WlSurface>(request.surface);
					   auto feedback = client.add_object<WpPresentationFeedback>(request.callback,
						   std::make_unique<PresentationFeedbackData>(surface.key));
					   // Multiple presentation feedbacks can be attached to a single surface
					   gimme_data<WlSurfaceData>(surface).delta.presentation_feedbacks.push_back(feedback.key);
				   },
				   [this](Destroy& request) {},
				   // The events for presentation feedback, are destructors, and hence will be destroyed on calling them. However, i didn't hook into the
				   // WpPresentationFeedback::destry() on purpose, because we wouldn't know what delta the feedback is currently in, and so couldn't clear its reference
				   // from the vector. Hence, it's upto the caller to clear it from the vector, before calling an event on it
			   },
		request);
}
} // namespace mayquill

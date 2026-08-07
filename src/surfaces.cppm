module;
#include <mayquill/logger.h>
#include <xf86drm.h>
module mayquill:surfaces;
import :logger;
import :definitions;
import :shell;
import :client;
import :sync;
import mayday.util;
import std;

namespace mayquill {

struct WlSubsurfaceData {
	Key surface;
	Key parent;
	// Subsurfaces can be sychronised, or desynchronised
	bool synchronised = true;
};

struct BufferFriends {
	// Two layers of optionals, as they can not attach a buffer, or they can attach a null buffer (ie. remove the buffer)
	std::optional<std::optional<Key>> buffer;
	std::optional<SyncobjPoint> acquire_point = std::nullopt;
	std::optional<SyncobjPoint> release_point = std::nullopt;
};

class WlSurfaceData;

// Deltas are what the spec describes as "content updates", i.e. they're the pending change
class WlSurfaceDelta {
  public:
	bool synchronised = true;
	bool claimed = false;
	WlSurfaceData& surface_data;
	std::deque<WlSurfaceDelta*> slave_deltas; // They must apply when we apply

	bool fifo_set_barrier = false;
	bool fifo_wait_barrier = false;
	std::optional<std::chrono::steady_clock::time_point> commit_timestamp = std::nullopt;
	std::vector<Key> presentation_feedbacks;
	BufferFriends buffer = {};
	std::vector<Key> frame_callbacks = {};

	WlSurfaceDelta(WlSurfaceData& surface_data) : surface_data(surface_data) {}
};

class WlSurfaceData {
  private:
	using RoleList = TypeList<XdgToplevel, XdgPopup, WlSubsurface, ZwlrLayerSurfaceV1>;

  public:
	bool fifo_set_barrier = false;
	std::optional<Key> fractional_scale = std::nullopt;
	std::optional<Key> drm_syncobj = std::nullopt;
	std::optional<Key> fifo = std::nullopt;
	std::optional<Key> commit_timer = std::nullopt;
	BufferFriends buffer;
	std::optional<std::chrono::steady_clock::time_point> commit_timestamp = std::nullopt;
	std::vector<Key> children = std::vector<Key> {}; // Subsurfaces are the chidlren
	std::vector<Key> frame_callbacks = {};
	bool initial_configured = false;
	// A role can be removed, but if re-assigned, it must be the same role
	// as the first role was
	std::optional<std::size_t> first_role;
	std::optional<Key> role = std::nullopt;

	// The current delta we're modifying (ie. the pending commit)
	WlSurfaceDelta delta;
	// The deltas we've already committed. Remember we can consume as many of these deltas at any time,
	// because they're already commited! We just need to respect their specified restrictions that control when we can,
	// but we don't need to "wait for another commit" for example, because they're already committed!
	std::deque<WlSurfaceDelta> committed_deltas = {};
	Client& client;

	WlSurfaceData(Client& client) : client(client), delta(*this) {};

	template<typename T>
	bool set_role(Key key) {
		static_assert(contains_v<RoleList, T>, "T isn't a valid role");
		// Check if the surface already has a role
		if (role)
			return false;

		// Get the index the role is
		static constexpr std::size_t new_role = type_index_v<RoleList, T>;

		// Check if we're trying to set a different role to the one already set
		// (skipped if there isn't one set)
		if (first_role && *first_role != new_role)
			return false;

		// Set the permenant role type
		if (!first_role)
			first_role = new_role;

		role = key;
		return true;
	}

	// Void means empty
	template<typename T>
	bool is_role() {
		// Ensure the role is currently assigned, before checking the index
		return role && *first_role == type_index_v<RoleList, T>;
	}

	void remove_role() {
		role = std::nullopt;
	}

	void commit_pending_delta() {
		for (auto& subsurface_key : children) {
			auto& subsurface_data = gimme_data<WlSubsurfaceData>(client.get_object<WlSubsurface>(subsurface_key));
			if (!subsurface_data.synchronised)
				continue;

			auto& surface_data = gimme_data<WlSurfaceData>(client.get_object<WlSurface>(subsurface_data.surface));
			if (surface_data.committed_deltas.empty())
				continue;
			auto& rear_delta = surface_data.committed_deltas.back();

			if (rear_delta.claimed)
				continue;

			rear_delta.claimed = true;
			delta.slave_deltas.push_back(&rear_delta);
		}

		committed_deltas.push_back(std::move(delta));
		// Make a new delta, can't use assignment operator, because references can't be reassigned
		std::destroy_at(&delta);
		std::construct_at(&delta, *this); // Construct at just forwards the args
	}

	bool can_apply_deltas(WlSurfaceDelta* limit = nullptr) {
		bool accumulated_set_barrier = fifo_set_barrier;
		for (auto& front : committed_deltas) {

			// Check if set barrier is still active (and if the front wants to wait)
			if (accumulated_set_barrier && front.fifo_wait_barrier)
				return false;

			// Check if the commit timestamp has passed
			if (front.commit_timestamp && std::chrono::steady_clock::now() < *front.commit_timestamp)
				return false;

			// Check if acquire point has been signalled
			if (front.buffer.acquire_point) {
				auto& point = *front.buffer.acquire_point;
				// 1 means we only have one timeline handle, 0 means only wait 0 seconds (ie. yield instantly), 2nd 0 is flags, 3rd 0 is a pointer to write which
				// timeline signalled first and broke the wait (we aren't waiting, nor do we have multiple timelines). 0 means it is ready (ie. timeline past or on that point)
				if (drmSyncobjTimelineWait(point.timeline_data.get()->device_fd, &point.timeline_data.get()->handle, &point.point, 1, 0, 0, nullptr))
					return false;
			}

			accumulated_set_barrier |= front.fifo_set_barrier;

			for (auto& slave_delta : front.slave_deltas) {
				if (!slave_delta->surface_data.can_apply_deltas(slave_delta))
					return false;
			}

			if (&front == limit)
				break;
		}
		return true;
	}

	void apply_committed_deltas(WlSurfaceDelta* limit = nullptr) {
		// Check if we're a subsurface (if we are, we can *only* apply if the parent requested us to)
		if (!limit && is_role<WlSubsurface>()) {
			Key last_key = *role;
			bool is_synchronised = false;
			while (true) {
				auto& subsurface_data = gimme_data<WlSubsurfaceData>(client.get_object<WlSubsurface>(last_key));
				if (subsurface_data.synchronised == true) {
					is_synchronised = true;
					break;
				}

				auto& parent_surface_data = gimme_data<WlSurfaceData>(client.get_object<WlSurface>(subsurface_data.parent));
				if (parent_surface_data.is_role<WlSubsurface>()) {
					// The parent is a subsurface, continue
					last_key = *parent_surface_data.role;
				} else {
					// Parent wasn't a subsurface (must be root), end the search here (if we got here, this subsurface is de-synchronised)
					break;
				}
			}

			// parent_requested only exists for this purpose - parents that call collapse_deltas on their children should obviously
			// skip this is_synchronised break, because is_synchronised should match commits from the parent
			if (is_synchronised)
				return;
		}

		// We're either a non-subsurface, or an effectively desynchronised subsurface
		// Merge front deltas into the real state, as possible, ie. actually applying commits
		while (committed_deltas.size() > 0) {
			auto& front = committed_deltas.front();

			if (!can_apply_deltas(&front))
				break;

			fifo_set_barrier |= front.fifo_set_barrier;

			if (front.buffer.buffer) {
				// Old buffer is being replaced, signal the release
				if (buffer.release_point)
					buffer.release_point->signal_release();

				buffer = std::move(front.buffer);
			}

			frame_callbacks.insert(frame_callbacks.end(),
				std::make_move_iterator(front.frame_callbacks.begin()),
				std::make_move_iterator(front.frame_callbacks.end()));

			while (!front.slave_deltas.empty()) {
				auto& slave_delta = front.slave_deltas.front();
				slave_delta->surface_data.apply_committed_deltas(slave_delta);
				front.slave_deltas.pop_front();
			}

			bool is_limit = &front == limit;
			committed_deltas.pop_front();
			if (is_limit)
				break;
		}
	}
};
}; // namespace mayquill

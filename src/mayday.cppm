module;
#include <mayday/libseat.h>
#include <mayquill/logger.h>
#include <unistd.h>
export module mayday;
import mayquill;

// using namespace mayquill;
struct Seat {
	bool active;
	libseat* seat;
    int seat_fd;
	int device_id;
	int device_fd;
};

void enable_seat(libseat* libseat, void* user_data) {
	auto seat = static_cast<Seat*>(user_data);
	seat->active = true;
	MQ_INFO("Seat was enabled");
}

void disable_seat(libseat* libseat, void* user_data) {
	auto seat = static_cast<Seat*>(user_data);
	seat->active = false;
	MQ_INFO("Seat was disabled");
	libseat_disable_seat(libseat);
	// No need to re-open devices, fds are now valid again
}

export class Mayday {
  public:
	Seat seat;
	mayquill::Server server;

	Mayday() {
		libseat_seat_listener listener = {
			.enable_seat = enable_seat,
			.disable_seat = disable_seat,
		};
		seat.seat = libseat_open_seat(&listener, &seat);
		if (!seat.seat)
			MQ_XERRNO("Failed to open seat");

        seat.seat_fd = libseat_get_fd(seat.seat);
        if (seat.seat_fd < 0) {
            MQ_XERRNO("Failed to get seat fd");
        }

		// Yield until we're given control of the seat
		while (!seat.active) {
			if (libseat_dispatch(seat.seat, -1) == -1)
				MQ_XERRNO("Failed to dispatch libseat");
		}

		// Device id is libseats internal handle to the device, it takes it again when closing the device
		seat.device_id = libseat_open_device(seat.seat, "/dev/dri/card0", &seat.device_fd);
		if (seat.device_id == -1)
			MQ_XERRNO("Failed to open device");

		// server.bind_socket();
	}

	~Mayday() {

		libseat_close_device(seat.seat, seat.device_id);
		close(seat.device_fd);
		libseat_close_seat(seat.seat);
	}
};

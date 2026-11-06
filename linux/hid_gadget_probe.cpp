#include <chrono>
#include <cstdint>
#include <csignal>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <poll.h>
#include <thread>
#include <unistd.h>
#include <sys/ioctl.h>

#if __has_include(<linux/usb/g_hid.h>)
#include <linux/usb/g_hid.h>
#define DS5_HAVE_HID_IOCTL 1
#else
#define DS5_HAVE_HID_IOCTL 0
#endif

static volatile sig_atomic_t running = 1;
#if DS5_HAVE_HID_IOCTL
static void stop(int) { running = 0; }
#endif

#if DS5_HAVE_HID_IOCTL
static void put_le16(std::uint8_t *data, std::size_t offset, std::int16_t value) {
    const auto raw = static_cast<std::uint16_t>(value);
    data[offset] = static_cast<std::uint8_t>(raw);
    data[offset + 1] = static_cast<std::uint8_t>(raw >> 8);
}

static void put_le32(std::uint8_t *data, std::size_t offset, std::uint32_t value) {
    data[offset] = static_cast<std::uint8_t>(value);
    data[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    data[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    data[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

static std::uint16_t fake_report_length(std::uint8_t id) {
    switch (id) {
    case 0x05: return 41;
    case 0x09: return 20;
    case 0x20: return 64;
    default: return 0;
    }
}

static void make_fake_report(std::uint8_t id, usb_hidg_report &response) {
    response.report_id = id;
    response.userspace_req = 1;
    response.length = fake_report_length(id);
    std::memset(response.data, 0, sizeof(response.data));
    response.data[0] = id;

    if (id == 0x09) {
        // Fake MAC 02:00:DE:AD:BE:EF, stored little-endian as required by
        // hid-playstation's pairing-info parser.
        const std::uint8_t fake_mac[] = {0xef, 0xbe, 0xad, 0xde, 0x00, 0x02};
        std::memcpy(response.data + 1, fake_mac, sizeof(fake_mac));
    } else if (id == 0x20) {
        put_le32(response.data, 24, 0x00010001); // hardware version
        put_le32(response.data, 28, 0x00010001); // firmware version
        put_le16(response.data, 44, 0x0201); // update version 2.1
    } else if (id == 0x05) {
        put_le16(response.data, 1, 0);
        put_le16(response.data, 3, 0);
        put_le16(response.data, 5, 0);
        put_le16(response.data, 7, 1000);
        put_le16(response.data, 9, -1000);
        put_le16(response.data, 11, 1000);
        put_le16(response.data, 13, -1000);
        put_le16(response.data, 15, 1000);
        put_le16(response.data, 17, -1000);
        put_le16(response.data, 19, 1000);
        put_le16(response.data, 21, 1000);
        put_le16(response.data, 23, 8192);
        put_le16(response.data, 25, -8192);
        put_le16(response.data, 27, 8192);
        put_le16(response.data, 29, -8192);
        put_le16(response.data, 31, 8192);
        put_le16(response.data, 33, -8192);
    }
}

static void log_get_report(std::uint8_t id, std::uint16_t length,
                           std::map<std::uint8_t, std::pair<std::chrono::steady_clock::time_point, unsigned>> &state) {
    const auto now = std::chrono::steady_clock::now();
    auto &entry = state[id];
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - entry.first).count();
    if (entry.second == 0 || elapsed >= 1000) {
        std::printf("GET_REPORT: id=0x%02x length=%u", id, length);
        if (entry.second != 0) std::printf(" (suppressed %u duplicate requests)", entry.second);
        std::printf("\n");
        entry.first = now;
        entry.second = 0;
    } else {
        ++entry.second;
    }
}

static void log_transport_error(int error, std::chrono::steady_clock::time_point &last_error,
                                unsigned &suppressed) {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_error).count();
    if (suppressed == 0 || elapsed >= 1000) {
        std::fprintf(stderr, "transport error: %s", std::strerror(error));
        if (suppressed != 0) std::fprintf(stderr, " (suppressed %u duplicate errors)", suppressed);
        std::fputc('\n', stderr);
        last_error = now;
        suppressed = 0;
    } else {
        ++suppressed;
    }
}
#endif

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/dev/hidg0";
    bool spike_fake_features = false;
    bool spike_animate = false;
    for (int argument = 2; argument < argc; ++argument) {
        if (std::strcmp(argv[argument], "--spike-fake-features") == 0) {
            spike_fake_features = true;
        } else if (std::strcmp(argv[argument], "--spike-animate") == 0) {
            spike_animate = true;
        } else {
            std::fprintf(stderr, "ERROR: unknown option: %s\n", argv[argument]);
            return 2;
        }
    }
#if !DS5_HAVE_HID_IOCTL
    (void)path;
    (void)spike_fake_features;
    (void)spike_animate;
    (void)running;
    std::fprintf(stderr, "ERROR: <linux/usb/g_hid.h> is unavailable; GET_REPORT ioctl support is unconfirmed.\n");
    return 2;
#else
    int fd = open(path, O_RDWR | O_NONBLOCK);
    if (fd < 0) { std::perror("open /dev/hidg0"); return 1; }
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    std::printf("READY: %s; ioctl support compiled from linux/usb/g_hid.h\n", path);
    if (spike_fake_features) {
        std::printf("!!! SPIKE ONLY: synthetic feature reports enabled; fake MAC 02:00:DE:AD:BE:EF !!!\n");
    } else {
        std::printf("FEATURES: no synthetic payloads; use --spike-fake-features only for the hardware spike\n");
    }
    if (spike_animate) {
        std::printf("!!! SPIKE ONLY: animated input enabled; X axis sweep and button toggle every second !!!\n");
    }
    std::uint8_t input[64]{};
    input[0] = 0x01;
    input[1] = 128;
    const std::uint8_t axis_values[] = {0, 64, 128, 192, 255};
    std::size_t axis_index = 0;
    bool button_pressed = false;
    auto next_animation = std::chrono::steady_clock::now();
    std::map<std::uint8_t, std::pair<std::chrono::steady_clock::time_point, unsigned>> get_log_state;
    auto last_transport_error = std::chrono::steady_clock::time_point{};
    unsigned suppressed_transport_errors = 0;
    while (running) {
        if (spike_animate && std::chrono::steady_clock::now() >= next_animation) {
            input[1] = axis_values[axis_index];
            axis_index = (axis_index + 1) % (sizeof(axis_values) / sizeof(axis_values[0]));
            button_pressed = !button_pressed;
            if (button_pressed) input[9] |= 0x01;
            else input[9] &= static_cast<std::uint8_t>(~0x01);
            std::printf("SPIKE INPUT: X=%u button=%s\n", input[1],
                        button_pressed ? "pressed" : "released");
            next_animation += std::chrono::seconds(1);
        }
        ssize_t sent = write(fd, input, sizeof(input));
        if (sent < 0 && errno != EAGAIN && errno != EINTR) {
            if (errno == ESHUTDOWN || errno == ENODEV) {
                log_transport_error(errno, last_transport_error, suppressed_transport_errors);
                std::fprintf(stderr, "USB gadget disconnected; exiting probe cleanly\n");
                close(fd);
                return 0;
            }
            log_transport_error(errno, last_transport_error, suppressed_transport_errors);
        }
        pollfd p{fd, POLLIN | POLLPRI, 0};
        if (poll(&p, 1, 100) > 0 && (p.revents & POLLIN)) {
            std::uint8_t report[64]{};
            ssize_t n = read(fd, report, sizeof(report));
            if (n > 0) std::printf("SET_REPORT: id=0x%02x length=%zd\n", report[0], n);
        }
        std::uint8_t id = 0;
        if (p.revents & POLLPRI) {
            const int get_id_result = ioctl(fd, GADGET_HID_READ_GET_REPORT_ID, &id);
            if (get_id_result == 0) {
                usb_hidg_report response{};
                if (spike_fake_features) {
                    make_fake_report(id, response);
                } else {
                    response.report_id = id;
                    response.userspace_req = 1;
                    response.length = 0;
                }
                log_get_report(id, response.length, get_log_state);
                if (ioctl(fd, GADGET_HID_WRITE_GET_REPORT, &response) < 0) {
                    if (errno == ESHUTDOWN || errno == ENODEV) {
                        log_transport_error(errno, last_transport_error, suppressed_transport_errors);
                        std::fprintf(stderr, "USB gadget disconnected; exiting probe cleanly\n");
                        close(fd);
                        return 0;
                    }
                    log_transport_error(errno, last_transport_error, suppressed_transport_errors);
                }
            } else if (errno == ENOTTY || errno == EINVAL) {
                std::fprintf(stderr,
                             "ERROR: f_hid GET_REPORT ioctl is unavailable at runtime "
                             "(GADGET_HID_READ_GET_REPORT_ID: %s)\n", std::strerror(errno));
                close(fd);
                return 2;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    close(fd);
    return 0;
#endif
}

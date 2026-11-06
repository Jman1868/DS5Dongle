#include <chrono>
#include <cstdint>
#include <csignal>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
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

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/dev/hidg0";
#if !DS5_HAVE_HID_IOCTL
    (void)path;
    (void)running;
    std::fprintf(stderr, "ERROR: <linux/usb/g_hid.h> is unavailable; GET_REPORT ioctl support is unconfirmed.\n");
    return 2;
#else
    int fd = open(path, O_RDWR | O_NONBLOCK);
    if (fd < 0) { std::perror("open /dev/hidg0"); return 1; }
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    std::printf("READY: %s; ioctl support compiled from linux/usb/g_hid.h\n", path);
    std::printf("FEATURE_SEED: report IDs 0x05(41), 0x09(20), 0x20(64), 0x22(64); no fabricated payloads\n");
    std::uint8_t input[64]{};
    input[0] = 0x01;
    while (running) {
        ssize_t sent = write(fd, input, sizeof(input));
        if (sent < 0 && errno != EAGAIN && errno != EINTR) std::perror("write input report");
        pollfd p{fd, POLLIN, 0};
        if (poll(&p, 1, 100) > 0 && (p.revents & POLLIN)) {
            std::uint8_t report[64]{};
            ssize_t n = read(fd, report, sizeof(report));
            if (n > 0) std::printf("SET_REPORT: id=0x%02x length=%zd\n", report[0], n);
        }
        std::uint8_t id = 0;
        if (ioctl(fd, GADGET_HID_READ_GET_REPORT_ID, &id) == 0) {
            usb_hidg_report response{};
            response.report_id = id;
            response.userspace_req = 1;
            response.length = 0;
            std::printf("GET_REPORT: id=0x%02x length=0 (no controller-backed payload)\n", id);
            if (ioctl(fd, GADGET_HID_WRITE_GET_REPORT, &response) < 0) std::perror("GADGET_HID_WRITE_GET_REPORT");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    close(fd);
    return 0;
#endif
}

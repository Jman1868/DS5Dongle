#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <linux/usb/g_hid.h>
#include <systemd/sd-bus.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr char kAdapter[] = "/org/bluez/hci0";
constexpr char kAgent[] = "/ds5dongle/agent";
constexpr char kHidg[] = "/dev/hidg0";
constexpr uint16_t kControlPsm = 0x11;
constexpr uint16_t kInterruptPsm = 0x13;

struct Bus {
    sd_bus *bus = nullptr;
    ~Bus() { sd_bus_unref(bus); }
};

struct Device {
    std::string path;
    std::string address;
};

struct Discovery {
    Device device;
};

void log_line(const char *format, ...) {
    char timestamp[32]{};
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now.tv_sec));
    fprintf(stderr, "[ds5-bridge] [%s.%03ld] ", timestamp, now.tv_nsec / 1000000);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

void log_bus_error(const char *operation, int result, const sd_bus_error *error) {
    log_line("%s failed: %s (%s)", operation,
             error && error->name ? error->name : strerror(-result),
             error && error->message ? error->message : "no D-Bus error message");
}

int call_method(sd_bus *bus, const char *service, const char *path, const char *interface,
                const char *method, const char *types = nullptr, ...) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = nullptr;
    va_list args;
    va_start(args, types);
    int result = sd_bus_call_methodv(bus, service, path, interface, method, &error, &reply,
                                     types, args);
    va_end(args);
    if (result < 0) log_bus_error(method, result, &error);
    else log_line("D-Bus %s succeeded", method);
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return result;
}

int get_property(sd_bus *bus, const char *path, const char *interface,
                 const char *property, std::string &value) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = nullptr;
    const int result = sd_bus_call_method(bus, "org.bluez", path,
        "org.freedesktop.DBus.Properties", "Get", &error, &reply, "ss",
        interface, property);
    if (result < 0) {
        log_bus_error(property, result, &error);
        sd_bus_error_free(&error);
        return result;
    }
    const char *raw = nullptr;
    const int read_result = sd_bus_message_read(reply, "v", "s", &raw);
    if (read_result >= 0) value = raw;
    if (read_result < 0) log_bus_error(property, read_result, nullptr);
    else log_line("D-Bus %s read succeeded", property);
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return read_result;
}

int set_property(sd_bus *bus, const char *path, const char *interface,
                 const char *property, bool value) {
    return call_method(bus, "org.bluez", path, "org.freedesktop.DBus.Properties",
                       "Set", "ssv", interface, property, "b", value);
}

int agent_success(sd_bus_message *message, void *, sd_bus_error *) {
    return sd_bus_reply_method_return(message, "");
}

int agent_pin(sd_bus_message *message, void *, sd_bus_error *) {
    return sd_bus_reply_method_return(message, "s", "0000");
}

int agent_passkey(sd_bus_message *message, void *, sd_bus_error *) {
    return sd_bus_reply_method_return(message, "u", 0u);
}

const sd_bus_vtable kAgentVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("RequestPinCode", "o", "s", agent_pin, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestPasskey", "o", "u", agent_passkey, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestConfirmation", "ou", "", agent_success, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestAuthorization", "o", "", agent_success, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("AuthorizeService", "os", "", agent_success, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("DisplayPinCode", "os", "", agent_success, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("DisplayPasskey", "ou", "", agent_success, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Release", "", "", agent_success, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Cancel", "", "", agent_success, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

int interfaces_added(sd_bus_message *message, void *userdata, sd_bus_error *) {
    auto *discovery = static_cast<Discovery *>(userdata);
    const char *path = nullptr;
    if (sd_bus_message_read(message, "o", &path) < 0) return 0;
    if (strncmp(path, "/org/bluez/hci0/dev_", 20) != 0) return 0;
    if (sd_bus_message_enter_container(message, 'a', "{sa{sv}}") < 0) return 0;
    bool is_device = false;
    const char *name = nullptr;
    while (sd_bus_message_enter_container(message, 'e', "sa{sv}") > 0) {
        const char *interface = nullptr;
        sd_bus_message_read(message, "s", &interface);
        if (interface && strcmp(interface, "org.bluez.Device1") == 0) is_device = true;
        sd_bus_message_enter_container(message, 'a', "{sv}");
        while (sd_bus_message_enter_container(message, 'e', "sv") > 0) {
            const char *key = nullptr;
            sd_bus_message_read(message, "s", &key);
            if (key && strcmp(key, "Name") == 0) sd_bus_message_read(message, "v", "s", &name);
            else sd_bus_message_skip(message, "v");
            sd_bus_message_exit_container(message);
        }
        sd_bus_message_exit_container(message);
        sd_bus_message_exit_container(message);
    }
    sd_bus_message_exit_container(message);
    if (is_device && name && (strstr(name, "DualSense") || strstr(name, "Wireless Controller"))) {
        discovery->device.path = path;
    }
    return 0;
}

int interfaces_removed(sd_bus_message *, void *, sd_bus_error *) { return 0; }

bool register_agent(Bus &bus) {
    sd_bus_slot *slot = nullptr;
    int result = sd_bus_add_object_vtable(bus.bus, &slot, kAgent, "org.bluez.Agent1",
                                          kAgentVtable, nullptr);
    if (result < 0) {
        log_bus_error("Register Agent object", result, nullptr);
        return false;
    }
    result = call_method(bus.bus, "org.bluez", "/org/bluez",
        "org.bluez.AgentManager1", "RegisterAgent", "os", kAgent, "KeyboardDisplay");
    if (result < 0) return false;
    result = call_method(bus.bus, "org.bluez", "/org/bluez",
        "org.bluez.AgentManager1", "RequestDefaultAgent", "o", kAgent);
    if (result < 0) return false;
    log_line("agent registered");
    return true;
}

bool adapter_setup(Bus &bus) {
    int result = set_property(bus.bus, kAdapter, "org.bluez.Adapter1", "Powered", true);
    if (result < 0 && result != -EALREADY) return false;
    log_line("adapter powered");
    result = set_property(bus.bus, kAdapter, "org.bluez.Adapter1", "Pairable", true);
    if (result < 0 && result != -EALREADY) return false;
    result = set_property(bus.bus, kAdapter, "org.bluez.Adapter1", "Discoverable", false);
    if (result < 0 && result != -EALREADY) return false;
    return register_agent(bus);
}

bool discover(Bus &bus, Discovery &discovery) {
    sd_bus_slot *slot = nullptr;
    int result = sd_bus_match_signal(bus.bus, &slot, "org.bluez",
        "/org/bluez", "org.freedesktop.DBus.ObjectManager", "InterfacesAdded",
        interfaces_added, &discovery);
    if (result < 0) {
        log_bus_error("subscribe InterfacesAdded", result, nullptr);
        return false;
    }
    result = call_method(bus.bus, "org.bluez", kAdapter, "org.bluez.Adapter1", "StartDiscovery");
    if (result < 0 && result != -EALREADY) return false;
    log_line("discovery started");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (discovery.device.path.empty() && std::chrono::steady_clock::now() < deadline) {
        result = sd_bus_process(bus.bus, nullptr);
        if (result < 0) {
            log_bus_error("process discovery signal", result, nullptr);
            break;
        }
        if (result == 0) sd_bus_wait(bus.bus, 1000000);
    }
    call_method(bus.bus, "org.bluez", kAdapter, "org.bluez.Adapter1", "StopDiscovery");
    sd_bus_slot_unref(slot);
    if (!discovery.device.path.empty()) {
        std::string address;
        if (get_property(bus.bus, discovery.device.path.c_str(), "org.bluez.Device1",
                         "Address", address) >= 0)
            discovery.device.address = std::move(address);
    }
    return !discovery.device.path.empty();
}

bool pair_connect(Bus &bus, const Device &device) {
    log_line("device found %s at %s", device.address.c_str(), device.path.c_str());
    int result = call_method(bus.bus, "org.bluez", device.path.c_str(), "org.bluez.Device1", "Pair");
    if (result < 0 && result != -EALREADY && result != -EEXIST) {
        log_line("pairing retry in 2 seconds");
        return false;
    }
    log_line("paired %s", device.address.c_str());
    result = set_property(bus.bus, device.path.c_str(), "org.bluez.Device1", "Trusted", true);
    if (result < 0 && result != -EALREADY && result != -EEXIST) return false;
    result = call_method(bus.bus, "org.bluez", device.path.c_str(), "org.bluez.Device1", "Connect");
    if (result < 0 && result != -EALREADY && result != -EEXIST) return false;
    log_line("connected");
    return true;
}

int open_l2cap(const std::string &address, uint16_t psm) {
    bdaddr_t bdaddr{};
    if (str2ba(address.c_str(), &bdaddr) < 0) return -1;
    const int fd = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (fd < 0) return -1;
    sockaddr_l2 remote{};
    remote.l2_family = AF_BLUETOOTH;
    remote.l2_psm = htobs(psm);
    bacpy(&remote.l2_bdaddr, &bdaddr);
    if (connect(fd, reinterpret_cast<sockaddr *>(&remote), sizeof(remote)) < 0) {
        log_line("L2CAP PSM 0x%02x failed: %s", psm, strerror(errno));
        if (errno == EADDRINUSE || errno == EBUSY)
            log_line("hidp owns L2CAP, no hidraw - controller not exposing HID to kernel; check BlueZ HID profile registration");
        close(fd);
        return -1;
    }
    return fd;
}

bool write_full(int fd, const uint8_t *data, size_t length) {
    while (length) {
        const ssize_t written = write(fd, data, length);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) {
            log_line("write fd %d failed: %s", fd, strerror(errno));
            return false;
        }
        data += written;
        length -= static_cast<size_t>(written);
    }
    return true;
}

std::string hidraw_path() {
    DIR *directory = opendir("/sys/class/hidraw");
    if (!directory) return {};
    dirent *entry = nullptr;
    while ((entry = readdir(directory))) {
        if (strncmp(entry->d_name, "hidraw", 6) != 0) continue;
        std::string path = std::string("/sys/class/hidraw/") + entry->d_name + "/device/uevent";
        FILE *file = fopen(path.c_str(), "r");
        if (!file) continue;
        char line[256];
        bool match = false;
        while (fgets(line, sizeof(line), file))
            if (strstr(line, "DualSense")) match = true;
        fclose(file);
        if (match) {
            closedir(directory);
            return std::string("/dev/") + entry->d_name;
        }
    }
    closedir(directory);
    return {};
}

void fill_report(usb_hidg_report &report, uint8_t id, const std::string &controller,
                 const std::string &adapter) {
    report = {};
    report.report_id = id;
    report.userspace_req = 1;
    report.data[0] = id;
    if (id == 0x09) {
        report.length = 20;
        auto address_bytes = [](const std::string &address) {
            std::array<uint8_t, 6> bytes{};
            unsigned value[6]{};
            if (sscanf(address.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
                       &value[0], &value[1], &value[2], &value[3], &value[4], &value[5]) == 6)
                for (size_t i = 0; i < bytes.size(); ++i)
                    bytes[i] = static_cast<uint8_t>(value[i]);
            return bytes;
        };
        const auto controller_bytes = address_bytes(controller);
        const auto adapter_bytes = address_bytes(adapter);
        std::reverse_copy(controller_bytes.begin(), controller_bytes.end(), report.data + 1);
        report.data[7] = 0x08; report.data[8] = 0x25; report.data[9] = 0;
        std::reverse_copy(adapter_bytes.begin(), adapter_bytes.end(), report.data + 10);
    } else if (id == 0x05) {
        report.length = 41;
        std::fill(report.data, report.data + report.length, 0x80);
        report.data[1] = report.data[2] = report.data[3] = report.data[4] = report.data[5] = 0;
    } else if (id == 0x20) {
        report.length = 64;
        report.data[0] = 1; report.data[3] = 3;
    }
}
}

bool handle_get_report(int hid, const std::string &controller, const std::string &adapter) {
    uint8_t id = 0;
    if (ioctl(hid, GADGET_HID_READ_GET_REPORT_ID, &id) < 0) {
        log_line("GET_REPORT id ioctl failed: %s", strerror(errno));
        return false;
    }
    usb_hidg_report report{};
    fill_report(report, id, controller, adapter);
    log_line("GET_REPORT id=0x%02X len=%u", id, report.length);
    if (report.length == 0) log_line("GET_REPORT warning: unknown report id 0x%02X", id);
    const int status = ioctl(hid, GADGET_HID_WRITE_GET_REPORT, &report);
    log_line("GET_REPORT reply id=0x%02X len=%u status=%d", id, report.length, status);
    return status == 0;
}

uint32_t output_crc(const uint8_t *data, size_t length) {
    uint32_t crc = ~0xEADA2D49u;
    while (length--) {
        crc = (crc >> 8) ^ ([&] {
            uint32_t value = (crc ^ *data++) & 0xff;
            for (int bit = 0; bit < 8; ++bit)
                value = (value >> 1) ^ (0xEDB88320u & -(value & 1));
            return value;
        }());
    }
    return ~crc;
}

bool pump(int hid, int input, int output, bool l2cap,
          const std::string &controller, const std::string &adapter) {
    std::array<uint8_t, 672> packet{};
    unsigned long reports = 0;
    uint8_t sequence = 0;
    while (true) {
        pollfd fds[]{{input, POLLIN | POLLERR | POLLHUP}, {hid, POLLIN | POLLERR | POLLHUP}};
        const int ready = poll(fds, 2, 1000);
        if (ready < 0) {
            if (errno == EINTR) continue;
            log_line("poll failed: %s", strerror(errno));
            return false;
        }
        if (fds[0].revents & (POLLERR | POLLHUP)) return false;
        if (fds[0].revents & POLLIN) {
            const ssize_t length = read(input, packet.data(), packet.size());
            if (length <= 0) return false;
            if (packet[0] == 0xA1 && length > 1 &&
                !write_full(hid, packet.data() + 1, static_cast<size_t>(length - 1)))
                return false;
            if (++reports % 100 == 0) log_line("forwarded %lu reports", reports);
        }
        if (fds[1].revents & (POLLERR | POLLHUP)) return false;
        if (fds[1].revents & POLLIN) {
            const ssize_t length = read(hid, packet.data(), packet.size());
            if (length == 0) {
                if (!handle_get_report(hid, controller, adapter)) return false;
            } else if (length > 0) {
                if (l2cap) {
                    std::array<uint8_t, 78> bt_packet{};
                    bt_packet[0] = 0xA2;
                    bt_packet[1] = 0x31;
                    bt_packet[2] = static_cast<uint8_t>((sequence++ & 0x0f) << 4);
                    bt_packet[3] = 0x10;
                    const size_t body = std::min<size_t>(length, 74);
                    std::memcpy(bt_packet.data() + 4, packet.data(), body);
                    const uint32_t checksum = output_crc(bt_packet.data() + 1, 73);
                    std::memcpy(bt_packet.data() + 74, &checksum, sizeof(checksum));
                    if (!write_full(output, bt_packet.data(), bt_packet.size())) return false;
                } else if (!write_full(output, packet.data(), static_cast<size_t>(length))) {
                    return false;
                }
            } else if (length < 0 && errno != EAGAIN && errno != EINTR) {
                log_line("read hidg0 failed: %s", strerror(errno));
                return false;
            }
        }
    }
}

int main() {
    log_line("startup");
    Bus bus;
    int result = sd_bus_default_system(&bus.bus);
    if (result < 0) {
        log_line("system bus open failed: %s", strerror(-result));
        return 1;
    }
    sd_bus_set_method_call_timeout(bus.bus, 60ULL * 1000ULL * 1000ULL);
    std::string host_address;
    if (get_property(bus.bus, kAdapter, "org.bluez.Adapter1", "Address", host_address) < 0) return 1;
    if (!adapter_setup(bus)) return 1;
    unsigned backoff = 2;
    while (true) {
        Discovery discovery;
        if (!discover(bus, discovery) || !pair_connect(bus, discovery.device)) {
            log_line("retry in %u seconds", backoff);
            std::this_thread::sleep_for(std::chrono::seconds(backoff));
            backoff = std::min(backoff * 2u, 5u);
            continue;
        }
        backoff = 2;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::string raw;
        while (std::chrono::steady_clock::now() < deadline && (raw = hidraw_path()).empty())
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const int hid = open(kHidg, O_RDWR | O_NONBLOCK);
        if (hid < 0) {
            log_line("open %s failed: %s", kHidg, strerror(errno));
            continue;
        }
        int control = -1, input = -1, output = -1;
        if (!raw.empty()) {
            input = output = open(raw.c_str(), O_RDWR | O_NONBLOCK);
            if (input >= 0) log_line("using hidraw %s", raw.c_str());
        } else {
            log_line("no hidraw found; using L2CAP fallback");
            control = open_l2cap(discovery.device.address, kControlPsm);
            input = open_l2cap(discovery.device.address, kInterruptPsm);
            output = input;
        }
        if (control < 0 && raw.empty()) {
            log_line("L2CAP control channel unavailable; retrying");
        }
        if (input < 0 || output < 0 || (raw.empty() && control < 0)) {
            log_line("L2CAP/hidraw transport unavailable; retrying");
            if (control >= 0) close(control);
            if (input >= 0) close(input);
            if (output >= 0 && output != input) close(output);
            close(hid);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        pump(hid, input, output, raw.empty(), discovery.device.address, host_address);
        log_line("disconnect; closing transport");
        if (control >= 0) close(control);
        close(hid);
        close(input);
        if (output != input) close(output);
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>

#include <array>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <optional>
#include <poll.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>

#include "dualsense_protocol.h"

namespace {
constexpr uint16_t kControlPsm = 0x11;
constexpr uint16_t kInterruptPsm = 0x13;
constexpr const char *kControllerName = "DualSense Wireless Controller";
constexpr const char *kHidgPath = "/dev/hidg0";

std::string command(const std::string &line) {
    std::array<char, 256> buffer{};
    std::string output;
    FILE *pipe = popen(line.c_str(), "r");
    if (!pipe) return {};
    while (fgets(buffer.data(), buffer.size(), pipe)) output += buffer.data();
    pclose(pipe);
    return output;
}

std::optional<std::string> find_controller() {
    for (int attempt = 0; attempt < 30; ++attempt) {
        const std::string tree = command("busctl tree org.bluez /org/bluez/hci0 2>/dev/null");
        std::istringstream lines(tree);
        std::string line;
        while (std::getline(lines, line)) {
            const auto marker = line.find("/org/bluez/hci0/dev_");
            if (marker == std::string::npos) continue;
            const std::string path = line.substr(marker);
            const std::string name = command("busctl get-property org.bluez " + path +
                " org.bluez.Device1 Name 2>/dev/null");
            if (name.find(kControllerName) == std::string::npos) continue;
            command("busctl call org.bluez " + path + " org.bluez.Device1 Pair 2>/dev/null");
            command("busctl set-property org.bluez " + path +
                    " org.bluez.Device1 Trusted b true 2>/dev/null");
            command("busctl call org.bluez " + path + " org.bluez.Device1 Connect 2>/dev/null");
            std::string address = path.substr(path.find("dev_") + 4);
            for (char &character : address) if (character == '_') character = ':';
            return address;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return std::nullopt;
}

bool start_discovery() {
    command("busctl set-property org.bluez /org/bluez/hci0 org.bluez.Adapter1 Powered b true");
    const std::string result = command(
        "busctl call org.bluez /org/bluez/hci0 org.bluez.Adapter1 StartDiscovery 2>&1");
    return result.find("Failed") == std::string::npos;
}

int connect_l2cap(const bdaddr_t &address, uint16_t psm) {
    const int fd = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (fd < 0) return -1;
    sockaddr_l2 remote{};
    remote.l2_family = AF_BLUETOOTH;
    remote.l2_psm = htobs(psm);
    bacpy(&remote.l2_bdaddr, &address);
    if (connect(fd, reinterpret_cast<sockaddr *>(&remote), sizeof(remote)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool write_all(int fd, const uint8_t *data, size_t size) {
    while (size) {
        const ssize_t written = write(fd, data, size);
        if (written <= 0) return false;
        data += written;
        size -= static_cast<size_t>(written);
    }
    return true;
}

uint32_t crc32(const uint8_t *data, size_t size) {
    uint32_t crc = 0xFFFFFFFF;
    while (size--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
    }
    return ~crc ^ 0xA2D2A4A1u;
}

void forward_output(int bt_fd, int hid_fd, uint8_t &sequence) {
    std::array<uint8_t, 128> report{};
    const ssize_t length = read(hid_fd, report.data(), report.size());
    if (length <= 1 || report[0] != 0x02) return;
    std::array<uint8_t, 78> packet{};
    packet[0] = 0xA2;
    packet[1] = 0x31;
    packet[2] = static_cast<uint8_t>((sequence++ & 0x0F) << 4);
    packet[3] = 0x10;
    const size_t body = std::min<size_t>(length - 1, 47);
    std::memcpy(packet.data() + 4, report.data() + 1, body);
    const uint32_t checksum = crc32(packet.data() + 1, packet.size() - 5);
    std::memcpy(packet.data() + packet.size() - 4, &checksum, sizeof(checksum));
    write_all(bt_fd, packet.data(), packet.size());
}

bool bridge_connection(const std::string &address) {
    bdaddr_t bdaddr{};
    if (str2ba(address.c_str(), &bdaddr) < 0) return false;
    const int control = connect_l2cap(bdaddr, kControlPsm);
    const int interrupt = control < 0 ? -1 : connect_l2cap(bdaddr, kInterruptPsm);
    const int hid = open(kHidgPath, O_RDWR | O_NONBLOCK);
    if (control < 0 || interrupt < 0 || hid < 0) {
        if (control >= 0) close(control);
        if (interrupt >= 0) close(interrupt);
        if (hid >= 0) close(hid);
        return false;
    }
    std::cerr << "Connected to DualSense at " << address << '\n';
    uint8_t sequence = 0;
    std::array<uint8_t, 672> input{};
    while (true) {
        pollfd descriptors[]{{interrupt, POLLIN | POLLERR | POLLHUP},
                             {hid, POLLIN | POLLERR | POLLHUP}};
        if (poll(descriptors, 2, 1000) < 0 && errno != EINTR) break;
        if (descriptors[0].revents & (POLLERR | POLLHUP)) break;
        if (descriptors[0].revents & POLLIN) {
            const ssize_t length = read(interrupt, input.data(), input.size());
            if (length <= 0) break;
            if (length > 2 && input[0] == 0xA1)
                write_all(hid, input.data() + 1, static_cast<size_t>(length - 1));
        }
        if (descriptors[1].revents & (POLLERR | POLLHUP)) break;
        if (descriptors[1].revents & POLLIN) forward_output(interrupt, hid, sequence);
    }
    close(control);
    close(interrupt);
    close(hid);
    return true;
}
}

int main() {
    while (true) {
        if (!start_discovery()) {
            std::cerr << "BlueZ discovery could not be started; retrying\n";
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }
        const auto address = find_controller();
        command("busctl call org.bluez /org/bluez/hci0 org.bluez.Adapter1 StopDiscovery 2>/dev/null");
        if (address) bridge_connection(*address);
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

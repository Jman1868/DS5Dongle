#include "dualsense_protocol.h"

#include <cstring>

namespace ds5 {
bool parse_bluetooth_input(const uint8_t *packet, size_t length, InputReport &report) {
    if (!packet || length < 65 || packet[0] != 0xA1 || packet[1] != 0x01) return false;
    report.report_id = packet[1];
    std::memcpy(report.payload.data(), packet + 2, report.payload.size());
    return true;
}

static uint32_t crc32(const uint8_t *data, size_t length) {
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

std::vector<uint8_t> make_bluetooth_output(const OutputReport &report, uint8_t sequence) {
    std::vector<uint8_t> packet(78);
    packet[0] = 0xA2;
    packet[1] = 0x31;
    packet[2] = static_cast<uint8_t>((sequence & 0x0F) << 4);
    packet[3] = 0x10;
    packet[4] = report.report_id;
    std::memcpy(packet.data() + 5, report.payload.data(), report.payload.size());
    const uint32_t checksum = crc32(packet.data() + 1, packet.size() - 5);
    std::memcpy(packet.data() + packet.size() - 4, &checksum, sizeof(checksum));
    return packet;
}
}

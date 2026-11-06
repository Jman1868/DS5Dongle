#include "dualsense_protocol.h"

#include <algorithm>
#include <cstring>

namespace ds5 {
namespace {

constexpr uint32_t kOutputSeed = 0xEADA2D49;
constexpr uint32_t kFeatureSeed = 0x2060EFC3;

uint32_t crc32_table_entry(uint32_t value) {
    for (unsigned bit = 0; bit < 8; ++bit) {
        value = (value >> 1) ^ (0xEDB88320u & -(value & 1u));
    }
    return value;
}

const std::array<uint32_t, 256>& crc32_table() {
    static const auto table = [] {
        std::array<uint32_t, 256> result{};
        for (uint32_t i = 0; i < result.size(); ++i) {
            result[i] = crc32_table_entry(i);
        }
        return result;
    }();
    return table;
}

uint32_t crc32_with_seed(std::span<const uint8_t> data, uint32_t seed) {
    uint32_t crc = ~seed;
    const auto& table = crc32_table();
    for (const uint8_t byte : data) {
        crc = (crc >> 8) ^ table[(crc ^ byte) & 0xffu];
    }
    return ~crc;
}

void write_le32(uint8_t* destination, uint32_t value) {
    destination[0] = static_cast<uint8_t>(value);
    destination[1] = static_cast<uint8_t>(value >> 8);
    destination[2] = static_cast<uint8_t>(value >> 16);
    destination[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t read_le32(const uint8_t* source) {
    return static_cast<uint32_t>(source[0]) |
           (static_cast<uint32_t>(source[1]) << 8) |
           (static_cast<uint32_t>(source[2]) << 16) |
           (static_cast<uint32_t>(source[3]) << 24);
}

}  // namespace

uint32_t crc32_seeded(std::span<const uint8_t> data, uint32_t seed) {
    return crc32_with_seed(data, seed);
}

uint32_t output_crc(std::span<const uint8_t> data) {
    return crc32_with_seed(data, kOutputSeed);
}

uint32_t feature_crc(std::span<const uint8_t> data) {
    return crc32_with_seed(data, kFeatureSeed);
}

void fill_output_checksum(std::span<uint8_t> report) {
    if (report.size() < kOutputMinimumSize) {
        return;
    }
    write_le32(report.data() + report.size() - 4,
               output_crc(report.first(report.size() - 4)));
}

void fill_feature_checksum(std::span<uint8_t> report) {
    if (report.size() < kOutputMinimumSize) {
        return;
    }
    write_le32(report.data() + report.size() - 4,
               feature_crc(report.first(report.size() - 4)));
}

bool verify_output_checksum(std::span<const uint8_t> report) {
    return report.size() >= kOutputMinimumSize &&
           read_le32(report.data() + report.size() - 4) ==
               output_crc(report.first(report.size() - 4));
}

bool verify_feature_checksum(std::span<const uint8_t> report) {
    return report.size() >= kOutputMinimumSize &&
           read_le32(report.data() + report.size() - 4) ==
               feature_crc(report.first(report.size() - 4));
}

std::vector<uint8_t> frame_output_report(std::span<const uint8_t> report) {
    std::vector<uint8_t> framed;
    framed.reserve(report.size() + 1);
    framed.push_back(kBluetoothOutputPrefix);
    framed.insert(framed.end(), report.begin(), report.end());
    if (framed.size() >= kOutputMinimumSize) {
        fill_output_checksum(std::span<uint8_t>(framed).subspan(1));
    }
    return framed;
}

std::vector<uint8_t> make_feature_get(uint8_t report_id) {
    return {kBluetoothFeatureGet, report_id};
}

std::vector<uint8_t> frame_feature_set(uint8_t report_id,
                                       std::span<const uint8_t> payload) {
    std::vector<uint8_t> framed;
    framed.reserve(payload.size() + 6);
    framed.push_back(kBluetoothFeatureSet);
    framed.push_back(report_id);
    framed.insert(framed.end(), payload.begin(), payload.end());
    if (framed.size() >= kOutputMinimumSize) {
        fill_feature_checksum(std::span<uint8_t>(framed).subspan(1));
    }
    return framed;
}

bool parse_bluetooth_input(std::span<const uint8_t> packet, InputState& state) {
    if (packet.size() < 3 + kInputStateSize ||
        packet[1] != kBluetoothInputReport) {
        return false;
    }
    std::copy_n(packet.begin() + 3, kInputStateSize, state.bytes.begin());
    return true;
}

bool parse_feature_response(std::span<const uint8_t> packet,
                            uint8_t expected_report_id,
                            std::vector<uint8_t>& payload) {
    if (packet.size() < 2 ||
        packet[0] != kBluetoothFeatureResponse ||
        packet[1] != expected_report_id) {
        return false;
    }
    payload.assign(packet.begin() + 2, packet.end());
    return true;
}

}  // namespace ds5

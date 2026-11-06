#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ds5 {

constexpr std::size_t kInputStateSize = 63;
constexpr std::size_t kOutputMinimumSize = 4;
constexpr uint8_t kBluetoothInputReport = 0x31;
constexpr uint8_t kBluetoothOutputPrefix = 0xA2;
constexpr uint8_t kBluetoothFeatureGet = 0x43;
constexpr uint8_t kBluetoothFeatureSet = 0x53;
constexpr uint8_t kBluetoothFeatureResponse = 0xA3;

struct InputState {
    std::array<uint8_t, kInputStateSize> bytes{};
};

uint32_t crc32_seeded(std::span<const uint8_t> data, uint32_t seed);
uint32_t output_crc(std::span<const uint8_t> data);
uint32_t feature_crc(std::span<const uint8_t> data);

void fill_output_checksum(std::span<uint8_t> report);
void fill_feature_checksum(std::span<uint8_t> report);
bool verify_output_checksum(std::span<const uint8_t> report);
bool verify_feature_checksum(std::span<const uint8_t> report);

std::vector<uint8_t> frame_output_report(std::span<const uint8_t> report);
std::vector<uint8_t> make_feature_get(uint8_t report_id);
std::vector<uint8_t> frame_feature_set(uint8_t report_id,
                                        std::span<const uint8_t> payload);

bool parse_bluetooth_input(std::span<const uint8_t> packet, InputState& state);
bool parse_feature_response(std::span<const uint8_t> packet,
                            uint8_t expected_report_id,
                            std::vector<uint8_t>& payload);

}  // namespace ds5

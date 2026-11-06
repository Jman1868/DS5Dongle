#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ds5 {
struct USBGetStateData {
    std::array<uint8_t, 63> bytes{};
};

struct SetStateData {
    std::array<uint8_t, 47> bytes{};
};

static_assert(sizeof(USBGetStateData) == 63);
static_assert(sizeof(SetStateData) == 47);

struct InputReport {
    uint8_t report_id{};
    std::array<uint8_t, 63> payload{};
};

struct OutputReport {
    uint8_t report_id{};
    std::array<uint8_t, 47> payload{};
};

bool parse_bluetooth_input(const uint8_t *packet, size_t length, InputReport &report);
std::vector<uint8_t> make_bluetooth_output(const OutputReport &report, uint8_t sequence);
}

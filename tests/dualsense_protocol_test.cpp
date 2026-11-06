#include "dualsense_protocol.h"

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

using ds5::InputState;
using ds5::kBluetoothFeatureGet;
using ds5::kBluetoothFeatureResponse;
using ds5::kBluetoothFeatureSet;
using ds5::kBluetoothInputReport;
using ds5::kBluetoothOutputPrefix;
using ds5::kInputStateSize;

void test_output_crc() {
    // (a) The seed is copied from src/utils.h:127, where it is documented as
    // the 0xA2 seed. The CRC literal was independently computed with
    // Python zlib.crc32(data, 0xEADA2D49).
    const std::vector<uint8_t> data = {
        0x31, 0x10, 0x11, 0x12, 0x13, 0x14};
    assert(ds5::output_crc(data) == 0x6EAC8CB8u);

    // (c) The sentinel bytes are test data chosen by this test, not a
    // controller fixture. The checksum itself is independently fixed above.
    std::vector<uint8_t> report = {
        0x31, 0x00, 0x10, 0x02, 0x03, 0x04, 0, 0, 0, 0};
    ds5::fill_output_checksum(report);
    assert(ds5::verify_output_checksum(report));
    report[2] ^= 0x01;
    assert(!ds5::verify_output_checksum(report));
}

void test_output_framing() {
    // (a) 0xA2 is emitted by src/bt.cpp:902.
    // The CRC literal was independently computed with
    // Python zlib.crc32(data, 0xEADA2D49), over the six bytes before the
    // little-endian trailer.
    const std::vector<uint8_t> body = {
        0x31, 0x10, 0x11, 0x12, 0x13, 0x14, 0, 0, 0, 0};
    const auto framed_output = ds5::frame_output_report(body);
    const std::vector<uint8_t> expected = {
        0xA2, 0x31, 0x10, 0x11, 0x12, 0x13, 0x14,
        0xB8, 0x8C, 0xAC, 0x6E};
    assert(framed_output == expected);
    assert(framed_output.front() == kBluetoothOutputPrefix);
    assert(ds5::verify_output_checksum(
        std::span<const uint8_t>(framed_output).subspan(1)));
}

void test_feature_framing() {
    // (a) 0x43/report-id is sent by src/bt.cpp:930 and 0x53/report-id by
    // src/bt.cpp:993-994. The feature seed is copied from src/utils.h:141,
    // where it is documented as the 0x53 seed.
    const auto get = ds5::make_feature_get(0x20);
    assert((get == std::vector<uint8_t>{kBluetoothFeatureGet, 0x20}));

    // (a) The report-id and wrapper are from src/bt.cpp:993-994.
    // The CRC literal was independently computed with
    // Python zlib.crc32(data, 0x2060EFC3), over [report id + payload].
    const std::vector<uint8_t> payload = {1, 2, 3, 4, 5, 6, 0, 0, 0, 0};
    const auto feature = ds5::frame_feature_set(0x20, payload);
    const std::vector<uint8_t> expected = {
        0x53, 0x20, 1, 2, 3, 4, 5, 6, 0x57, 0xC4, 0x02, 0x41};
    assert(feature == expected);
    assert(feature[0] == kBluetoothFeatureSet);
    assert(ds5::verify_feature_checksum(
        std::span<const uint8_t>(feature).subspan(1)));
}

void test_feature_response_parsing() {
    // (a) 0xA3/report-id/payload is consumed by src/bt.cpp:714-717.
    // (c) Payload bytes 9 and 8 are arbitrary sentinels selected by this
    // test; they are not copied from a controller response fixture.
    std::vector<uint8_t> payload;
    const std::vector<uint8_t> response = {
        kBluetoothFeatureResponse, 0x20, 9, 8};
    assert(ds5::parse_feature_response(response, 0x20, payload));
    assert((payload == std::vector<uint8_t>{9, 8}));

    // (a) Rejecting a response for another report ID follows the matching
    // condition in src/bt.cpp:715.
    const std::vector<uint8_t> wrong_response = {
        kBluetoothFeatureResponse, 0x21, 9};
    assert(!ds5::parse_feature_response(wrong_response, 0x20, payload));
}

void test_bluetooth_input_parsing() {
    // (a) Report ID 0x31 and the data+3 offset are used by src/main.cpp:79
    // and src/main.cpp:102. The 63-byte size is src/utils.h:185.
    std::vector<uint8_t> input(3 + kInputStateSize, 0);
    input[1] = kBluetoothInputReport;

    // (c) Sentinel values are arbitrary test data chosen to verify both ends
    // of the copied state; they are not protocol fixtures.
    input[3] = 0xAA;
    input.back() = 0x55;
    InputState state;
    assert(ds5::parse_bluetooth_input(input, state));
    assert(state.bytes.front() == 0xAA);
    assert(state.bytes.back() == 0x55);

    // (a) A non-0x31 interrupt report is not the input layout handled by
    // src/main.cpp:79.
    input[1] = 0x32;
    assert(!ds5::parse_bluetooth_input(input, state));
}

}  // namespace

int main() {
    test_output_crc();
    test_output_framing();
    test_feature_framing();
    test_feature_response_parsing();
    test_bluetooth_input_parsing();
    return 0;
}

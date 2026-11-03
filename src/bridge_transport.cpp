#include "bridge_transport.h"

#include <Wire.h>

namespace bridge {

BridgeTransport bridge;

namespace {
constexpr uint8_t kRegId = 0x00;
constexpr uint8_t kRegStatus = 0x04;
constexpr uint8_t kRegRespRight = 0x10;
constexpr uint8_t kRegRespLeft = 0x11;
constexpr uint8_t kRegCmdRight = 0x20;
constexpr uint8_t kRegCmdLeft = 0x21;
constexpr uint8_t kRegKnobValues = 0x30;
constexpr uint8_t kRegKnobConfig = 0x40;
constexpr uint8_t kRegButtonStates = 0x80;
constexpr uint8_t kRegButtonConfig = 0x88;
constexpr uint8_t kRegCommit = 0xA0;
constexpr uint8_t kRegTestMode = 0xA1;

constexpr size_t kFrameMax = 66; // [board][len][line]
constexpr size_t kLineMax = 63;

void drainWire() {
    while (Wire1.available()) {
        Wire1.read();
    }
}
} // namespace

bool BridgeTransport::begin() {
    Wire1.setClock(400000);
    Wire1.setBufferSize(128);
    return probe();
}

bool BridgeTransport::probe() {
    uint8_t version = 0;
    if (!readId(version)) {
        hubOnline_ = false;
        return false;
    }
    hubOnline_ = true;
    lastOkMs_ = millis();
    return true;
}

bool BridgeTransport::readId(uint8_t& version) {
    uint8_t out[4] = {};
    if (!readRegister(kRegId, out, sizeof(out))) return false;
    if (out[0] != 'J' || out[1] != 'K') return false;
    version = out[2];
    return true;
}

bool BridgeTransport::readStatus(HubStatus& status) {
    uint8_t out[8] = {};
    if (!readRegister(kRegStatus, out, sizeof(out))) return false;

    status.rightAvailable = (uint16_t)out[0] | ((uint16_t)out[1] << 8);
    status.leftAvailable = (uint16_t)out[2] | ((uint16_t)out[3] << 8);
    status.rightCommandSpace = (uint16_t)out[4] | ((uint16_t)out[5] << 8);
    status.leftCommandSpace = (uint16_t)out[6] | ((uint16_t)out[7] << 8);
    return true;
}

bool BridgeTransport::readFrame(uint8_t board, char* line, size_t maxLen, size_t& outLen) {
    outLen = 0;
    uint8_t reg = (board == kBoardRight) ? kRegRespRight : kRegRespLeft;

    Wire1.beginTransmission(kAddress);
    Wire1.write(reg);
    if (Wire1.endTransmission(false) != 0) {
        hubOnline_ = false;
        return false;
    }

    uint8_t n = Wire1.requestFrom((uint16_t)kAddress, (uint8_t)kFrameMax);
    if (n < 2) {
        drainWire();
        return false;
    }

    uint8_t frameBoard = Wire1.read();
    uint8_t len = Wire1.read();
    if (frameBoard != board || len == 0 || n < (uint8_t)(len + 2)) {
        drainWire();
        return false;
    }

    size_t copy = len;
    if (copy >= maxLen) copy = maxLen - 1;
    for (size_t i = 0; i < copy; i++) {
        line[i] = (char)Wire1.read();
    }
    drainWire();
    line[copy] = '\0';
    outLen = copy;

    hubOnline_ = true;
    lastOkMs_ = millis();
    return true;
}

bool BridgeTransport::writeCommand(uint8_t board, const char* line) {
    if (line == nullptr) return false;
    size_t len = strlen(line);
    if (len == 0 || len > kLineMax) return false;

    uint8_t buf[kLineMax + 1];
    buf[0] = (uint8_t)len;
    memcpy(buf + 1, line, len);

    uint8_t reg = (board == kBoardRight) ? kRegCmdRight : kRegCmdLeft;
    return writeRegister(reg, buf, len + 1);
}

bool BridgeTransport::readKnobs(uint16_t* values) {
    uint8_t out[kKnobCount * 2] = {};
    if (!readRegister(kRegKnobValues, out, sizeof(out))) return false;
    for (uint8_t i = 0; i < kKnobCount; i++) {
        values[i] = (uint16_t)out[i * 2] | ((uint16_t)out[i * 2 + 1] << 8);
    }
    return true;
}

bool BridgeTransport::readKnobConfig(KnobConfig* config) {
    return readRegister(kRegKnobConfig, reinterpret_cast<uint8_t*>(config),
                        sizeof(KnobConfig) * kKnobCount);
}

bool BridgeTransport::writeKnobConfig(const KnobConfig* config) {
    return writeRegister(kRegKnobConfig, reinterpret_cast<const uint8_t*>(config),
                         sizeof(KnobConfig) * kKnobCount);
}

bool BridgeTransport::readButtons(uint8_t* states) {
    return readRegister(kRegButtonStates, states, kButtonCount);
}

bool BridgeTransport::readButtonConfig(ButtonConfig* config) {
    return readRegister(kRegButtonConfig, reinterpret_cast<uint8_t*>(config),
                        sizeof(ButtonConfig) * kButtonCount);
}

bool BridgeTransport::writeButtonConfig(const ButtonConfig* config) {
    return writeRegister(kRegButtonConfig, reinterpret_cast<const uint8_t*>(config),
                         sizeof(ButtonConfig) * kButtonCount);
}

bool BridgeTransport::commit() {
    uint8_t one = 1;
    return writeRegister(kRegCommit, &one, 1);
}

bool BridgeTransport::setTestMode(bool on) {
    uint8_t value = on ? 1 : 0;
    if (!writeRegister(kRegTestMode, &value, 1)) return false;
    testMode_ = on;
    return true;
}

bool BridgeTransport::readRegister(uint8_t reg, uint8_t* out, size_t len) {
    Wire1.beginTransmission(kAddress);
    Wire1.write(reg);
    if (Wire1.endTransmission(false) != 0) {
        hubOnline_ = false;
        return false;
    }

    size_t n = Wire1.requestFrom((uint16_t)kAddress, (uint8_t)len);
    if (n != len) {
        drainWire();
        hubOnline_ = false;
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        out[i] = Wire1.read();
    }

    hubOnline_ = true;
    lastOkMs_ = millis();
    return true;
}

bool BridgeTransport::writeRegister(uint8_t reg, const uint8_t* data, size_t len) {
    Wire1.beginTransmission(kAddress);
    Wire1.write(reg);
    Wire1.write(data, len);
    if (Wire1.endTransmission() != 0) {
        hubOnline_ = false;
        return false;
    }
    hubOnline_ = true;
    lastOkMs_ = millis();
    return true;
}

} // namespace bridge

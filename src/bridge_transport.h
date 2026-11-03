#pragma once

#include <Arduino.h>

// ESP32 master side of the control-hub I2C link (hub = Teensy 4.1, 0x42).
// Register/FIFO map must match src/hub/main.cpp in jaiba-hexa-drum-ui.
namespace bridge {

constexpr uint8_t kAddress = 0x42;
constexpr uint8_t kBoardRight = 0;
constexpr uint8_t kBoardLeft = 1;

constexpr uint8_t kKnobCount = 6;
constexpr uint8_t kButtonCount = 6;

// Same packed layout as the hub.
struct KnobConfig {
    uint16_t controlId;
    uint8_t cc;
    uint8_t channel;
    uint16_t minValue;
    uint16_t maxValue;
    uint8_t flags; // bit0 enabled, bit1 14-bit
    uint8_t reserved;
} __attribute__((packed));

struct ButtonConfig {
    uint8_t note;
    uint8_t channel;
    uint8_t mode;
    uint8_t flags;
} __attribute__((packed));

struct HubStatus {
    uint16_t rightAvailable = 0;
    uint16_t leftAvailable = 0;
    uint16_t rightCommandSpace = 0;
    uint16_t leftCommandSpace = 0;
};

class BridgeTransport {
public:
    bool begin();
    bool probe();
    bool readId(uint8_t& version);
    bool readStatus(HubStatus& status);
    bool readFrame(uint8_t board, char* line, size_t maxLen, size_t& outLen);
    bool writeCommand(uint8_t board, const char* line);
    bool readKnobs(uint16_t* values);
    bool readKnobConfig(KnobConfig* config);
    bool writeKnobConfig(const KnobConfig* config);
    bool readButtons(uint8_t* states);
    bool readButtonConfig(ButtonConfig* config);
    bool writeButtonConfig(const ButtonConfig* config);
    bool commit();
    bool setTestMode(bool on);

    bool hubOnline() const { return hubOnline_; }
    bool testMode() const { return testMode_; }
    uint32_t lastOkMs() const { return lastOkMs_; }

private:
    bool readRegister(uint8_t reg, uint8_t* out, size_t len);
    bool writeRegister(uint8_t reg, const uint8_t* data, size_t len);

    bool hubOnline_ = false;
    bool testMode_ = false;
    uint32_t lastOkMs_ = 0;
};

extern BridgeTransport bridge;

} // namespace bridge

#pragma once

#include <Arduino.h>
#include <queue>

namespace sensors {

constexpr uint8_t kBoardRight = 0;
constexpr uint8_t kBoardLeft = 1;
constexpr int kMaxPads = 9;

enum class PlayMode { UNKNOWN, BOTH, PIEZO_ONLY, VELOSTAT_ONLY };
enum class CalPhase { NONE, RESTING, WAITING_TOUCH, CAPTURING, PAD_DONE, COMPLETE, ABORTED };
enum class CommandResult { NONE, PENDING, SUCCESS, FAILED, TIMED_OUT };

struct PadState {
    int note = -1;
    int threshold = -1;
    int ceiling = -1;
    float curveExp = -1.0f;
    uint32_t lastHitMs = 0;
    uint8_t lastVelocity = 0;
    uint32_t hitCount = 0;
    uint32_t lastUpdated = 0;
};

struct CalState {
    bool piezo = false;
    CalPhase phase = CalPhase::NONE;
    int padIndex = -1;
    uint32_t lastUpdated = 0;
};

struct BoardState {
    uint8_t padCount = 0;
    PadState pads[kMaxPads];

    float crosstalkRatio = -1.0f;
    int crosstalkWindowMs = -1;
    uint32_t xtalkUpdated = 0;

    PlayMode playMode = PlayMode::UNKNOWN;
    uint32_t playModeUpdated = 0;

    CalState cal;

    uint32_t lastSeenMs = 0;
    uint32_t lastHitMs = 0;
    int lastHitPad = -1;
    int lastHitVelocity = 0;
    uint32_t hitCount = 0;

    CommandResult result = CommandResult::NONE;
    String pendingCommand;
    uint32_t pendingSentMs = 0;
    String failureReason;

    bool online() const {
        return lastSeenMs != 0 && (millis() - lastSeenMs) < 5000;
    }
};

class SensorLink {
public:
    void begin();
    void poll();

    void sendCommand(uint8_t board, const String& command);
    BoardState& board(uint8_t boardIndex) { return boards_[boardIndex]; }
    const BoardState& board(uint8_t boardIndex) const { return boards_[boardIndex]; }

    bool hubOnline() const;

private:
    void handleLine(uint8_t boardIndex, const char* line);
    void sendNextQueued();
    void checkAckTimeouts();

    BoardState boards_[2];
    std::queue<String> queues_[2];
    uint32_t lastGetMs_ = 0;
    uint32_t lastPollMs_ = 0;
};

extern SensorLink sensorLink;

} // namespace sensors

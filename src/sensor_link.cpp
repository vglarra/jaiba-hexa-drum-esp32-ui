#include "sensor_link.h"

#include "bridge_transport.h"

namespace sensors {

SensorLink sensorLink;

namespace {

String fieldAt(const String& args, int index) {
    int start = 0;
    for (int i = 0; i < index; i++) {
        int next = args.indexOf(',', start);
        if (next < 0) return "";
        start = next + 1;
    }
    int end = args.indexOf(',', start);
    return end < 0 ? args.substring(start) : args.substring(start, end);
}

int intArg(const String& args, int index) {
    return fieldAt(args, index).toInt();
}

float floatArg(const String& args, int index) {
    return fieldAt(args, index).toFloat();
}

String stringArg(const String& args, int index) {
    String s = fieldAt(args, index);
    s.trim();
    return s;
}

PlayMode parsePlayMode(const String& s) {
    if (s == "BOTH") return PlayMode::BOTH;
    if (s == "PIEZO_ONLY") return PlayMode::PIEZO_ONLY;
    if (s == "VELOSTAT_ONLY") return PlayMode::VELOSTAT_ONLY;
    return PlayMode::UNKNOWN;
}

CalPhase parseCalPhase(const String& s) {
    if (s == "RESTING") return CalPhase::RESTING;
    if (s == "WAITING_TOUCH") return CalPhase::WAITING_TOUCH;
    if (s == "CAPTURING") return CalPhase::CAPTURING;
    if (s == "PAD_DONE") return CalPhase::PAD_DONE;
    if (s == "COMPLETE") return CalPhase::COMPLETE;
    if (s == "ABORTED") return CalPhase::ABORTED;
    return CalPhase::NONE;
}

bool isPadInRange(const BoardState& board, int pad) {
    return pad >= 0 && pad < board.padCount;
}

} // namespace

void SensorLink::begin() {
    boards_[0].padCount = 9;
    boards_[1].padCount = 7;

    for (int i = 0; i < 9; i++) {
        boards_[0].pads[i].note = 60 + 2 * i;
    }
    for (int i = 0; i < 7; i++) {
        boards_[1].pads[i].note = 78 + 2 * i;
    }

    for (uint8_t b = 0; b < 2; b++) {
        queues_[b].push("GET_PLAY_MODE");
        queues_[b].push("GET_XTALK");
        for (int i = 0; i < boards_[b].padCount; i++) {
            queues_[b].push("GET_PAD," + String(i));
        }
    }
}

bool SensorLink::hubOnline() const {
    return bridge::bridge.hubOnline();
}

void SensorLink::sendCommand(uint8_t boardIndex, const String& command) {
    if (boardIndex > 1) return;
    if (command.length() == 0 || command.length() > 63) return;
    queues_[boardIndex].push(command);
}

void SensorLink::sendNextQueued() {
    bridge::HubStatus status;
    if (!bridge::bridge.readStatus(status)) return;

    for (uint8_t b = 0; b < 2; b++) {
        if (queues_[b].empty()) continue;

        uint16_t space = (b == kBoardRight) ? status.rightCommandSpace : status.leftCommandSpace;
        String command = queues_[b].front();
        if (command.length() + 1 > space) continue;

        if (!bridge::bridge.writeCommand(b, command.c_str())) continue;
        queues_[b].pop();

        if (command.startsWith("SET_") || command.startsWith("CAL_START")) {
            boards_[b].pendingCommand = command;
            boards_[b].pendingSentMs = millis();
            boards_[b].result = CommandResult::PENDING;
            boards_[b].failureReason = "";
        }
    }
}

void SensorLink::checkAckTimeouts() {
    for (uint8_t b = 0; b < 2; b++) {
        BoardState& board = boards_[b];
        if (board.result == CommandResult::PENDING &&
            board.pendingSentMs != 0 &&
            millis() - board.pendingSentMs > 1000) {
            board.result = CommandResult::TIMED_OUT;
            board.pendingCommand = "";
        }
    }
}

void SensorLink::poll() {
    // The hub/touch share Wire1. 20 ms (50 Hz) is plenty for hit events and
    // keeps the touch transactions short.
    if (lastPollMs_ != 0 && millis() - lastPollMs_ < 20) return;
    lastPollMs_ = millis();

    bridge::HubStatus status;
    if (!bridge::bridge.readStatus(status)) {
        checkAckTimeouts();
        return;
    }

    for (uint8_t b = 0; b < 2; b++) {
        uint16_t available = (b == kBoardRight) ? status.rightAvailable : status.leftAvailable;
        if (available == 0) continue;

        char line[80];
        size_t len = 0;
        if (bridge::bridge.readFrame(b, line, sizeof(line), len) && len > 0) {
            handleLine(b, line);
        }
    }

    sendNextQueued();

    if (lastGetMs_ == 0 || millis() - lastGetMs_ > 2000) {
        lastGetMs_ = millis();
        queues_[kBoardRight].push("GET_PLAY_MODE");
        queues_[kBoardLeft].push("GET_PLAY_MODE");
    }

    checkAckTimeouts();
}

void SensorLink::handleLine(uint8_t boardIndex, const char* line) {
    if (boardIndex > 1) return;
    BoardState& board = boards_[boardIndex];
    board.lastSeenMs = millis();

    String text(line);
    int comma = text.indexOf(',');
    String command = comma < 0 ? text : text.substring(0, comma);
    String args = comma < 0 ? "" : text.substring(comma + 1);

    if (command == "HIT") {
        int pad = intArg(args, 0);
        int velocity = intArg(args, 1);
        if (!isPadInRange(board, pad)) return;

        PadState& p = board.pads[pad];
        p.lastHitMs = millis();
        p.lastVelocity = (uint8_t)constrain(velocity, 0, 127);
        p.hitCount++;
        p.lastUpdated = millis();

        board.lastHitMs = p.lastHitMs;
        board.lastHitPad = pad;
        board.lastHitVelocity = p.lastVelocity;
        board.hitCount++;
        return;
    }

    if (command == "PADVAL") {
        int pad = intArg(args, 0);
        if (!isPadInRange(board, pad)) return;

        PadState& p = board.pads[pad];
        p.note = intArg(args, 1);
        p.threshold = intArg(args, 2);
        p.ceiling = intArg(args, 3);
        p.curveExp = floatArg(args, 4);
        p.lastUpdated = millis();
        return;
    }

    if (command == "CAL_STATE") {
        String sensor = stringArg(args, 0);
        String phase = stringArg(args, 1);

        board.cal.piezo = (sensor == "PIEZO");
        board.cal.phase = parseCalPhase(phase);
        board.cal.padIndex = -1;
        if (board.cal.phase == CalPhase::CAPTURING || board.cal.phase == CalPhase::PAD_DONE) {
            board.cal.padIndex = intArg(args, 2);
        }
        board.cal.lastUpdated = millis();
        return;
    }

    if (command == "XTALKVAL") {
        board.crosstalkRatio = floatArg(args, 0);
        board.crosstalkWindowMs = intArg(args, 1);
        board.xtalkUpdated = millis();
        return;
    }

    if (command == "PLAY_MODE") {
        board.playMode = parsePlayMode(stringArg(args, 0));
        board.playModeUpdated = millis();
        return;
    }

    if (command == "ACK") {
        String acked = args;
        acked.trim();

        // The sensor ACKs the command name, not the full argument string
        // (e.g. ACK,SET_PLAY_MODE for "SET_PLAY_MODE,BOTH").
        String pendingName = board.pendingCommand;
        int comma = pendingName.indexOf(',');
        if (comma >= 0) pendingName = pendingName.substring(0, comma);

        if (pendingName.length() > 0 && acked == pendingName) {
            board.result = CommandResult::SUCCESS;
            board.pendingCommand = "";
        }
        return;
    }

    if (command == "ERR") {
        board.result = CommandResult::FAILED;
        board.failureReason = args;
        board.pendingCommand = "";
        return;
    }
}

} // namespace sensors

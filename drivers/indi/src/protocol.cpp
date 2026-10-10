#include "protocol.h"
#include <connectionplugins/connectionserial.h>
#include <cerrno>
#include <charconv>
#include <stdexcept>
#include <unistd.h>

// Keep response type names concise in this file; protocol data structures live
// in namespace OSF and are declared in protocol.h.
using namespace OSF;

// The protocol object retains the file descriptor owned by INDI's serial
// connection plugin so every command uses the currently opened device port.
OSFprotocol::OSFprotocol(int fd) : m_fd(fd) {}

// Write one framed command, collect its matching response, and convert the
// payload into the typed value expected by the focuser driver.
OSF::CommandResponse OSFprotocol::sendCommand(const std::string &cmd)
{
    // A false bool is the failure sentinel used when no valid response can be
    // read or parsed.
    CommandResponse commandResponse = {false, {}};
    
    // A negative descriptor means the serial plugin has not opened a port.
    if (m_fd < 0) {
        return commandResponse;
    }

    // write() may transmit only part of a frame, so continue until the entire
    // command reaches the serial device.
    size_t bytesSent = 0;
    while (bytesSent < cmd.size()) {
        const ssize_t result = write(m_fd, cmd.data() + bytesSent, cmd.size() - bytesSent);
        if (result < 0) {
            // Interrupted system calls are retried; other write failures end
            // this transaction with the failure sentinel.
            if (errno == EINTR) {
                continue;
            }
            return commandResponse;
        }

        // A zero-byte write cannot advance the loop, so treat it as failure.
        if (result == 0) {
            return commandResponse;
        }

        // Accumulate progress so partial writes do not truncate the frame.
        bytesSent += static_cast<size_t>(result);
    }

    // Serial reads may contain debug frames and/or a partial response. Keep
    // accumulating until splitResponseFrames finds a complete ':' frame.
    std::string rawResponse;
    char buffer[256];
    std::string commandFrame;
    while (commandFrame.empty()) {
        const ssize_t bytesRead = read(m_fd, buffer, sizeof(buffer));
        if (bytesRead < 0) {
            // Retry only interrupted reads; other errors cannot complete this
            // command-response exchange.
            if (errno == EINTR) {
                continue;
            }
            return commandResponse;
        }

        // End-of-stream provides no response frame to parse.
        if (bytesRead == 0) {
            return commandResponse;
        }

        // Preserve all bytes because the closing '#' may arrive in a later
        // read, and extract diagnostic frames at the same time.
        rawResponse.append(buffer, static_cast<size_t>(bytesRead));
        splitResponseFrames(rawResponse, commandFrame, commandResponse.debugFrames);
    }

    // Only colon-prefixed, hash-terminated frames are protocol responses.
    if (commandFrame.size() < 2 || commandFrame.front() != ':' || commandFrame.back() != '#') {
        return commandResponse;
    }
    
    // Parsing below operates on the token and payload without frame markers.
    commandFrame = commandFrame.substr(1, commandFrame.size() - 2);

    // ACK and heartbeat replies have no payload and are represented as true.
    if (commandFrame == "ACK" || commandFrame == "PP") {
        commandResponse.response = true;
        return commandResponse;
    }

    // Every remaining response needs at least its two-character command token.
    if (commandFrame.size() < 2) {
        return commandResponse;
    }
    // The command index uses two-character tokens; the remainder is its data.
    const std::string token = commandFrame.substr(0, 2);
    const std::string payload = commandFrame.substr(2);

    // Preserve firmware errors as text so callers can distinguish them from
    // an empty or malformed response.
    if (token == "ER") {
        commandResponse.response = token + payload;
        return commandResponse;
    }
    // PF carries the movement label, position, and optional add-on fields.
    else if (token == "PF") {
        try {
            commandResponse.response = parseFocuserStatus(payload);
            return commandResponse;
        } catch (const std::invalid_argument &) {
            // Keep the default failure sentinel when payload fields are bad.
            return commandResponse;
        }
    } 
    // GL returns the minimum and maximum focuser travel positions.
    else if (token == "GL") {
        try {
            commandResponse.response = parseStepLimits(payload);
            return commandResponse;
        } catch (const std::invalid_argument &) {
            // Invalid limits must not be used to configure INDI ranges.
            return commandResponse;
        }
    }
    // TG is parsed for protocol completeness; the active focuser driver does
    // not currently expose its star-map target fields.
    else if (token == "TG") {
        try {
            commandResponse.response = parseTargetCoords(payload);
            return commandResponse;
        } catch (const std::invalid_argument &) {
            // Do not return partially parsed coordinates.
            return commandResponse;
        }
    }
    // PL represents a semicolon-separated list of presets.
    else if (token == "PL") {
        try {
            commandResponse.response = parsePresetList(payload);
            return commandResponse;
        } catch (const std::invalid_argument &) {
            // A malformed entry invalidates the typed list response.
            return commandResponse;
        }
    } 
    // PA and PR return one preset record with id, name, and step count.
    else if (token == "PA" || token == "PR") {
        try {
            commandResponse.response = parsePreset(payload);
            return commandResponse;
        } catch (const std::invalid_argument &) {
            // Keep the sentinel rather than exposing a partial preset.
            return commandResponse;
        }
    } 
    // AQ is a comma-separated list used to discover optional hardware add-ons.
    else if (token == "AQ") {
        try {
            commandResponse.response = parseAddonList(payload);
            return commandResponse;
        } catch (const std::invalid_argument &) {
            // The current parser accepts the payload as a list of names.
            return commandResponse;
        }
    }
    // TM returns the new motor-enabled state; store 0 and 1 as integers so
    // disabled (zero) remains distinguishable from parse failure.
    else if (token == "TM") {
        try {
            if (payload == "0") {
                commandResponse.response = int32_t{0};
                return commandResponse;
            } else if (payload == "1") {
                commandResponse.response = int32_t{1};
                return commandResponse;
            } else {
                // Any other spelling/value is outside the firmware contract.
                throw std::invalid_argument("Invalid TM payload");
            }
        } catch (const std::invalid_argument &) {
            // Leave the default sentinel to signal an invalid TM response.
            return commandResponse;
        }
    }
    // These query replies contain one signed decimal integer.
    else if (token == "GP" || token == "GS" || token == "GT" || token == "CI" || token == "CU") {
        try
        {
            commandResponse.response = parseInt32(payload);
            return commandResponse;
        }
        catch(const std::invalid_argument &)
        {
            // Parsing requires the entire payload to be a valid integer.
            return commandResponse;
        }
    }

    // For other tokens retain the raw payload. This permits untyped command
    // replies to remain visible while typed consumers still validate variants.
    commandResponse.response = payload;
    return commandResponse;
}
// Split raw response into command frame and debug frames
bool OSFprotocol::splitResponseFrames(const std::string &rawResponse, std::string &commandFrame, std::vector<std::string> &debugFrames)
{
    // Rebuild outputs from the complete accumulated byte stream on each call.
    commandFrame.clear();
    debugFrames.clear();

    size_t position = 0;
    // Firmware diagnostics start with '!' and end with '*'; command replies
    // start with ':' and end with '#'. Ignore incomplete trailing frames.
    while ((position = rawResponse.find_first_of("!:", position)) != std::string::npos) {
        const char start = rawResponse[position];
        const char terminator = start == '!' ? '*' : '#';
        const size_t end = rawResponse.find(terminator, position + 1);
        if (end == std::string::npos) {
            // The next serial read may supply the missing terminator.
            break;
        }

        // Copy one complete frame so later parsing is independent of buffers.
        const std::string frame = rawResponse.substr(position, end - position + 1);
        if (start == '!') {
            // Keep diagnostic frames separate from the typed command result.
            debugFrames.push_back(frame);
        } else if (commandFrame.empty()) {
            // The transaction uses the first complete command frame only.
            commandFrame = frame;
        }
        position = end + 1;
    }

    return !commandFrame.empty();
}
// Parse the leading status and position columns from :PF; later comma fields
// are reserved for optional add-on telemetry and are ignored here.
OSF::FocuserStatus OSFprotocol::parseFocuserStatus(const std::string &commandFrame) const
{
    const size_t separator = commandFrame.find(',');
    if (separator == std::string::npos || separator == 0) {
        // Both a status label and the separator before position are required.
        throw std::invalid_argument("Invalid focuser status frame");
    }

    const size_t additionalFields = commandFrame.find(',', separator + 1);
    const char *positionBegin = commandFrame.data() + separator + 1;
    const char *positionEnd = commandFrame.data() + (additionalFields == std::string::npos ? commandFrame.size() : additionalFields);
    int32_t position;
    const auto result = std::from_chars(positionBegin, positionEnd, position);
    if (result.ec != std::errc{} || result.ptr != positionEnd) {
        // from_chars must consume exactly the first numeric field.
        throw std::invalid_argument("Invalid focuser position");
    }

    return {commandFrame.substr(0, separator), position};
}
// Parse :GL's minimum and maximum step bounds as two complete int32 fields.
OSF::StepLimits OSFprotocol::parseStepLimits(const std::string &commandFrame) const
{
    const size_t separator = commandFrame.find(',');
    if (separator == std::string::npos || separator == 0) {
        // A missing comma or empty minimum makes the response unusable.
        throw std::invalid_argument("Invalid step limits frame");
    }

    const char *minBegin = commandFrame.data();
    const char *minEnd = commandFrame.data() + separator;
    int32_t minSteps;
    const auto minResult = std::from_chars(minBegin, minEnd, minSteps);
    if (minResult.ec != std::errc{} || minResult.ptr != minEnd) {
        // Reject partial parses and non-decimal minimum values.
        throw std::invalid_argument("Invalid minimum step limit");
    }

    const char *maxBegin = commandFrame.data() + separator + 1;
    const char *maxEnd = commandFrame.data() + commandFrame.size();
    int32_t maxSteps;
    const auto maxResult = std::from_chars(maxBegin, maxEnd, maxSteps);
    if (maxResult.ec != std::errc{} || maxResult.ptr != maxEnd) {
        // Reject partial parses and non-decimal maximum values.
        throw std::invalid_argument("Invalid maximum step limit");
    }

    return {minSteps, maxSteps};
}

// Parse :TG's RA, Dec, and target name; coordinates must each occupy one
// complete comma-delimited field before the remaining name text.
OSF::TargetCoords OSFprotocol::parseTargetCoords(const std::string &commandFrame) const
{
    const size_t firstSeparator = commandFrame.find(',');
    const size_t secondSeparator = commandFrame.find(',', firstSeparator + 1);
    if (firstSeparator == std::string::npos || secondSeparator == std::string::npos || firstSeparator == 0 || secondSeparator == firstSeparator + 1) {
        // Require non-empty RA and Dec fields plus the second delimiter.
        throw std::invalid_argument("Invalid target coordinates frame");
    }

    const char *raBegin = commandFrame.data();
    const char *raEnd = commandFrame.data() + firstSeparator;
    double ra;
    const auto raResult = std::from_chars(raBegin, raEnd, ra);
    if (raResult.ec != std::errc{} || raResult.ptr != raEnd) {
        // Do not accept trailing junk after the RA value.
        throw std::invalid_argument("Invalid RA value");
    }

    const char *decBegin = commandFrame.data() + firstSeparator + 1;
    const char *decEnd = commandFrame.data() + secondSeparator;
    double dec;
    const auto decResult = std::from_chars(decBegin, decEnd, dec);
    if (decResult.ec != std::errc{} || decResult.ptr != decEnd) {
        // Do not accept trailing junk after the declination value.
        throw std::invalid_argument("Invalid Dec value");
    }

    const std::string name = commandFrame.substr(secondSeparator + 1);
    if (name.empty()) {
        // The final field identifies which target the coordinates describe.
        throw std::invalid_argument("Invalid target name");
    }

    return {ra, dec, name};
}

// Parse one preset record in the protocol's id,name,steps format.
OSF::Preset OSFprotocol::parsePreset(const std::string &presetFrame) const
{
    const size_t firstSeparator = presetFrame.find(',');
    const size_t secondSeparator = presetFrame.find(',', firstSeparator + 1);
    if (firstSeparator == std::string::npos || secondSeparator == std::string::npos || firstSeparator == 0 || secondSeparator == firstSeparator + 1) {
        // A record needs an id, a non-empty name, and a second delimiter.
        throw std::invalid_argument("Invalid preset frame");
    }

    const char *idBegin = presetFrame.data();
    const char *idEnd = presetFrame.data() + firstSeparator;
    int32_t id;
    const auto idResult = std::from_chars(idBegin, idEnd, id);
    if (idResult.ec != std::errc{} || idResult.ptr != idEnd) {
        // Require the complete first field to be an integer identifier.
        throw std::invalid_argument("Invalid preset ID");
    }

    const std::string name = presetFrame.substr(firstSeparator + 1, secondSeparator - firstSeparator - 1);
    if (name.empty()) {
        // Empty preset labels cannot identify a saved position to clients.
        throw std::invalid_argument("Invalid preset name");
    }

    const char *stepsBegin = presetFrame.data() + secondSeparator + 1;
    const char *stepsEnd = presetFrame.data() + presetFrame.size();
    int32_t steps;
    const auto stepsResult = std::from_chars(stepsBegin, stepsEnd, steps);
    if (stepsResult.ec != std::errc{} || stepsResult.ptr != stepsEnd) {
        // Reject partial or malformed step counts.
        throw std::invalid_argument("Invalid preset steps");
    }

    return {id, name, steps};
}

// Split a :PL payload on semicolons and parse each resulting preset record.
std::vector<OSF::Preset> OSFprotocol::parsePresetList(const std::string &commandFrame) const
{
    std::vector<OSF::Preset> presets;
    size_t start = 0;
    size_t end = commandFrame.find(';');
    while (end != std::string::npos) {
        // Parse every complete record before the next list delimiter.
        presets.push_back(parsePreset(commandFrame.substr(start, end - start)));
        start = end + 1;
        end = commandFrame.find(';', start);
    }
    if (start < commandFrame.size()) {
        // The final record has no trailing semicolon in the wire format.
        presets.push_back(parsePreset(commandFrame.substr(start)));
    }
    return presets;
}

// Parse :AQ's comma-separated add-on names while omitting empty fields.
std::vector<std::string> OSFprotocol::parseAddonList(const std::string &commandFrame) const
{
    std::vector<std::string> addons;
    size_t start = 0;
    size_t end = commandFrame.find(',');
    while (end != std::string::npos) {
        // Preserve each non-empty name before advancing past its delimiter.
        const std::string addon = commandFrame.substr(start, end - start);
        if (!addon.empty()) {
            addons.push_back(addon);
        }
        start = end + 1;
        end = commandFrame.find(',', start);
    }
    if (start < commandFrame.size()) {
        // Process the last name, which is not followed by another comma.
        const std::string addon = commandFrame.substr(start);
        if (!addon.empty()) {
            addons.push_back(addon);
        }
    }
    return addons;
}

// Parse one signed 32-bit decimal payload and require complete consumption so
// protocol values with suffixes cannot silently pass as valid numbers.
int32_t OSFprotocol::parseInt32(const std::string &str) const
{
    const char *begin = str.data();
    const char *end = str.data() + str.size();
    int32_t value;
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        // Surface conversion and trailing-character failures to the caller.
        throw std::invalid_argument("Invalid int32 value");
    }
    return value;
}
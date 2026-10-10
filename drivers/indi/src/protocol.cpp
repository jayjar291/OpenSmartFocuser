#include "protocol.h"
#include <connectionplugins/connectionserial.h>
#include <cerrno>
#include <charconv>
#include <stdexcept>
#include <unistd.h>

using namespace OSF;
OSFprotocol::OSFprotocol(int fd) : m_fd(fd) {}

// Send command string and receive typed response
OSF::CommandResponse OSFprotocol::sendCommand(const std::string &cmd)
{
    // Initialize command response with default false values
    CommandResponse commandResponse = {false, {}};
    
    // Return early if the file descriptor is invalid
    if (m_fd < 0) {
        return commandResponse;
    }
    // Send the command string to the device
    size_t bytesSent = 0;
    while (bytesSent < cmd.size()) {
        const ssize_t result = write(m_fd, cmd.data() + bytesSent, cmd.size() - bytesSent);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            // Return early on write error
            return commandResponse;
        }
        if (result == 0) {
            // Return early if write returns 0, indicating no progress
            return commandResponse;
        }
        bytesSent += static_cast<size_t>(result);
    }
    // Read response from the device until a complete command frame is obtained
    std::string rawResponse;
    char buffer[256];
    std::string commandFrame;
    while (commandFrame.empty()) {
        const ssize_t bytesRead = read(m_fd, buffer, sizeof(buffer));
        if (bytesRead < 0) {
            if (errno == EINTR) {
                continue;
            }
            // Return early on read error
            return commandResponse;
        }
        if (bytesRead == 0) {
            // Return early if read returns 0, indicating end of stream
            return commandResponse;
        }
        rawResponse.append(buffer, static_cast<size_t>(bytesRead));
        // Attempt to split raw response into command and debug frames
        splitResponseFrames(rawResponse, commandFrame, commandResponse.debugFrames);
    }

    // Validate command frame format
    if (commandFrame.size() < 2 || commandFrame.front() != ':' || commandFrame.back() != '#') {
        return commandResponse;
    }
    
    // Strip leading ':' and trailing '#' from the command frame
    commandFrame = commandFrame.substr(1, commandFrame.size() - 2);

    // Handle special cases for ACK and PP responses
    if (commandFrame == "ACK" || commandFrame == "PP") {
        commandResponse.response = true;
        return commandResponse;
    }

    // Handle other command frames based on their token
    if (commandFrame.size() < 2) {
        return commandResponse;
    }
    // Extract token and payload from the command frame
    const std::string token = commandFrame.substr(0, 2);
    const std::string payload = commandFrame.substr(2);

    // Handle ER response token
    if (token == "ER") {
        commandResponse.response = token + payload;
        return commandResponse;
    }
    // Handle PF response token
    else if (token == "PF") {
        try {
            commandResponse.response = parseFocuserStatus(payload);
            return commandResponse;
        } catch (const std::invalid_argument &) {
            return commandResponse;
        }
    } 
    // Handle GL response token
    else if (token == "GL") {
        try {
            commandResponse.response = parseStepLimits(payload);
            return commandResponse;
        } catch (const std::invalid_argument &) {
            return commandResponse;
        }
    }
    // Handle TG response token
    else if (token == "TG") {
        try {
            commandResponse.response = parseTargetCoords(payload);
            return commandResponse;
        } catch (const std::invalid_argument &) {
            return commandResponse;
        }
    }
    // Handle PL response token
    else if (token == "PL") {
        try {
            commandResponse.response = parsePresetList(payload);
            return commandResponse;
        } catch (const std::invalid_argument &) {
            return commandResponse;
        }
    } 
    // Handle PA and PR response token
    else if (token == "PA" || token == "PR") {
        try {
            commandResponse.response = parsePreset(payload);
            return commandResponse;
        } catch (const std::invalid_argument &) {
            return commandResponse;
        }
    } 
    // Handle AQ response token
    else if (token == "AQ") {
        try {
            commandResponse.response = parseAddonList(payload);
            return commandResponse;
        } catch (const std::invalid_argument &) {
            return commandResponse;
        }
    }
    // Handle TM response token
    else if (token == "TM") {
        try {
            if (payload == "0") {
                commandResponse.response = int32_t{0};
                return commandResponse;
            } else if (payload == "1") {
                commandResponse.response = int32_t{1};
                return commandResponse;
            } else {
                throw std::invalid_argument("Invalid TM payload");
            }
        } catch (const std::invalid_argument &) {
            return commandResponse;
        }
    }
    // Handle integer response tokens (GP, GS, GT, CI, CU)
    else if (token == "GP" || token == "GS" || token == "GT" || token == "CI" || token == "CU") {
        try
        {
            commandResponse.response = parseInt32(payload);
            return commandResponse;
        }
        catch(const std::invalid_argument &)
        {
            return commandResponse;
        }
    }
    commandResponse.response = payload;
    return commandResponse;
}
// Split raw response into command frame and debug frames
bool OSFprotocol::splitResponseFrames(const std::string &rawResponse, std::string &commandFrame, std::vector<std::string> &debugFrames)
{
    commandFrame.clear();
    debugFrames.clear();

    size_t position = 0;
    while ((position = rawResponse.find_first_of("!:", position)) != std::string::npos) {
        const char start = rawResponse[position];
        const char terminator = start == '!' ? '*' : '#';
        const size_t end = rawResponse.find(terminator, position + 1);
        if (end == std::string::npos) {
            break;
        }

        const std::string frame = rawResponse.substr(position, end - position + 1);
        if (start == '!') {
            debugFrames.push_back(frame);
        } else if (commandFrame.empty()) {
            commandFrame = frame;
        }
        position = end + 1;
    }

    return !commandFrame.empty();
}
// Parse focuser status from the command frame
OSF::FocuserStatus OSFprotocol::parseFocuserStatus(const std::string &commandFrame) const
{
    const size_t separator = commandFrame.find(',');
    if (separator == std::string::npos || separator == 0) {
        throw std::invalid_argument("Invalid focuser status frame");
    }

    const size_t additionalFields = commandFrame.find(',', separator + 1);
    const char *positionBegin = commandFrame.data() + separator + 1;
    const char *positionEnd = commandFrame.data() + (additionalFields == std::string::npos ? commandFrame.size() : additionalFields);
    int32_t position;
    const auto result = std::from_chars(positionBegin, positionEnd, position);
    if (result.ec != std::errc{} || result.ptr != positionEnd) {
        throw std::invalid_argument("Invalid focuser position");
    }

    return {commandFrame.substr(0, separator), position};
}
// Parse step limits from the command frame
OSF::StepLimits OSFprotocol::parseStepLimits(const std::string &commandFrame) const
{
    const size_t separator = commandFrame.find(',');
    if (separator == std::string::npos || separator == 0) {
        throw std::invalid_argument("Invalid step limits frame");
    }

    const char *minBegin = commandFrame.data();
    const char *minEnd = commandFrame.data() + separator;
    int32_t minSteps;
    const auto minResult = std::from_chars(minBegin, minEnd, minSteps);
    if (minResult.ec != std::errc{} || minResult.ptr != minEnd) {
        throw std::invalid_argument("Invalid minimum step limit");
    }

    const char *maxBegin = commandFrame.data() + separator + 1;
    const char *maxEnd = commandFrame.data() + commandFrame.size();
    int32_t maxSteps;
    const auto maxResult = std::from_chars(maxBegin, maxEnd, maxSteps);
    if (maxResult.ec != std::errc{} || maxResult.ptr != maxEnd) {
        throw std::invalid_argument("Invalid maximum step limit");
    }

    return {minSteps, maxSteps};
}

// Parse target coordinates from the command frame
OSF::TargetCoords OSFprotocol::parseTargetCoords(const std::string &commandFrame) const
{
    const size_t firstSeparator = commandFrame.find(',');
    const size_t secondSeparator = commandFrame.find(',', firstSeparator + 1);
    if (firstSeparator == std::string::npos || secondSeparator == std::string::npos || firstSeparator == 0 || secondSeparator == firstSeparator + 1) {
        throw std::invalid_argument("Invalid target coordinates frame");
    }

    const char *raBegin = commandFrame.data();
    const char *raEnd = commandFrame.data() + firstSeparator;
    double ra;
    const auto raResult = std::from_chars(raBegin, raEnd, ra);
    if (raResult.ec != std::errc{} || raResult.ptr != raEnd) {
        throw std::invalid_argument("Invalid RA value");
    }

    const char *decBegin = commandFrame.data() + firstSeparator + 1;
    const char *decEnd = commandFrame.data() + secondSeparator;
    double dec;
    const auto decResult = std::from_chars(decBegin, decEnd, dec);
    if (decResult.ec != std::errc{} || decResult.ptr != decEnd) {
        throw std::invalid_argument("Invalid Dec value");
    }

    const std::string name = commandFrame.substr(secondSeparator + 1);
    if (name.empty()) {
        throw std::invalid_argument("Invalid target name");
    }

    return {ra, dec, name};
}

// Parse preset from the preset frame
OSF::Preset OSFprotocol::parsePreset(const std::string &presetFrame) const
{
    const size_t firstSeparator = presetFrame.find(',');
    const size_t secondSeparator = presetFrame.find(',', firstSeparator + 1);
    if (firstSeparator == std::string::npos || secondSeparator == std::string::npos || firstSeparator == 0 || secondSeparator == firstSeparator + 1) {
        throw std::invalid_argument("Invalid preset frame");
    }

    const char *idBegin = presetFrame.data();
    const char *idEnd = presetFrame.data() + firstSeparator;
    int32_t id;
    const auto idResult = std::from_chars(idBegin, idEnd, id);
    if (idResult.ec != std::errc{} || idResult.ptr != idEnd) {
        throw std::invalid_argument("Invalid preset ID");
    }

    const std::string name = presetFrame.substr(firstSeparator + 1, secondSeparator - firstSeparator - 1);
    if (name.empty()) {
        throw std::invalid_argument("Invalid preset name");
    }

    const char *stepsBegin = presetFrame.data() + secondSeparator + 1;
    const char *stepsEnd = presetFrame.data() + presetFrame.size();
    int32_t steps;
    const auto stepsResult = std::from_chars(stepsBegin, stepsEnd, steps);
    if (stepsResult.ec != std::errc{} || stepsResult.ptr != stepsEnd) {
        throw std::invalid_argument("Invalid preset steps");
    }

    return {id, name, steps};
}

// Parse preset list from the command frame
std::vector<OSF::Preset> OSFprotocol::parsePresetList(const std::string &commandFrame) const
{
    std::vector<OSF::Preset> presets;
    size_t start = 0;
    size_t end = commandFrame.find(';');
    while (end != std::string::npos) {
        presets.push_back(parsePreset(commandFrame.substr(start, end - start)));
        start = end + 1;
        end = commandFrame.find(';', start);
    }
    if (start < commandFrame.size()) {
        presets.push_back(parsePreset(commandFrame.substr(start)));
    }
    return presets;
}

// Parse addon list from the command frame
std::vector<std::string> OSFprotocol::parseAddonList(const std::string &commandFrame) const
{
    std::vector<std::string> addons;
    size_t start = 0;
    size_t end = commandFrame.find(',');
    while (end != std::string::npos) {
        const std::string addon = commandFrame.substr(start, end - start);
        if (!addon.empty()) {
            addons.push_back(addon);
        }
        start = end + 1;
        end = commandFrame.find(',', start);
    }
    if (start < commandFrame.size()) {
        const std::string addon = commandFrame.substr(start);
        if (!addon.empty()) {
            addons.push_back(addon);
        }
    }
    return addons;
}

// Parse int32 value from the string
int32_t OSFprotocol::parseInt32(const std::string &str) const
{
    const char *begin = str.data();
    const char *end = str.data() + str.size();
    int32_t value;
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        throw std::invalid_argument("Invalid int32 value");
    }
    return value;
}
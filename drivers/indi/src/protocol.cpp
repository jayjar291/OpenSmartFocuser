#include "protocol.h"
#include <connectionplugins/connectionserial.h>
#include <cerrno>
#include <charconv>
#include <stdexcept>
#include <unistd.h>

using namespace OSF;
OSFprotocol::OSFprotocol(int fd) : m_fd(fd) {}

OSF::FocuserStatus OSFprotocol::parseFocuserStatus(const std::string &commandFrame) const
{
    const size_t separator = commandFrame.find(',');
    if (separator == std::string::npos || separator == 0) {
        throw std::invalid_argument("Invalid focuser status frame");
    }

    const char *positionBegin = commandFrame.data() + separator + 1;
    const char *positionEnd = commandFrame.data() + commandFrame.size();
    int32_t position;
    const auto result = std::from_chars(positionBegin, positionEnd, position);
    if (result.ec != std::errc{} || result.ptr != positionEnd) {
        throw std::invalid_argument("Invalid focuser position");
    }

    return {commandFrame.substr(0, separator), position};
}

OSF::CommandResponse OSFprotocol::sendCommand(const std::string &cmd)
{
    CommandResponse commandResponse = {false, {}};
    if (m_fd < 0) {
        return commandResponse;
    }
    size_t bytesSent = 0;
    while (bytesSent < cmd.size()) {
        const ssize_t result = write(m_fd, cmd.data() + bytesSent, cmd.size() - bytesSent);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return commandResponse;
        }
        if (result == 0) {
            return commandResponse;
        }
        bytesSent += static_cast<size_t>(result);
    }

    std::string rawResponse;
    char buffer[256];
    std::string commandFrame;
    while (commandFrame.empty()) {
        const ssize_t bytesRead = read(m_fd, buffer, sizeof(buffer));
        if (bytesRead < 0) {
            if (errno == EINTR) {
                continue;
            }
            return commandResponse;
        }
        if (bytesRead == 0) {
            return commandResponse;
        }
        rawResponse.append(buffer, static_cast<size_t>(bytesRead));
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

    const std::string token = commandFrame.substr(0, 2);
    const std::string payload = commandFrame.substr(2);

    if (token == "PF") {
        try {
            commandResponse.response = parseFocuserStatus(payload);
        } catch (const std::invalid_argument &) {
            return commandResponse;
        }
    }
    return commandResponse;
}

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
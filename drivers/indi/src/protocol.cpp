#include "protocol.h"
#include <connectionplugins/connectionserial.h>
#include <cerrno>
#include <unistd.h>

using namespace OSF;
OSFprotocol::OSFprotocol(int fd) : m_fd(fd) {}

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

    commandResponse.response = commandFrame;
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
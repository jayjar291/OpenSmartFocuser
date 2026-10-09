#include "protocol.h"

using namespace OSF;
OSFprotocol::OSFprotocol(int fd) : m_fd(fd) {}

OSF::ResponseData OSFprotocol::sendCommand(const std::string &cmd)
{
    // Implementation for sending command and receiving response goes here
}
#include <string>
#include <variant>
#include <cstdint>
#include <vector>

namespace OSF {

// Structures for commands returning multiple fields
struct StepLimits {
    int32_t minSteps;
    int32_t maxSteps;
};

struct TargetCoords {
    double ra;
    double dec;
    std::string name;
};

struct Preset {
    int32_t id;
    std::string name;
    int32_t steps;
};

struct FocuserStatus {
    std::string status;
    int32_t position;
    int16_t dustCapPosition = -1;  // -1 if unused
    int16_t brightness = -1;       // -1 if unused
};

// Response payload variant covering all protocol command types
using ResponseData = std::variant<
    bool,                   // ACK (:ACK# -> true, error/NAK -> false)
    int32_t,                // Single integer (:GP, :GS, :CI, :CU, :TM)
    std::string,            // Enum/version string (:GM, :VF)
    StepLimits,             // Step boundary limits (:GL)
    TargetCoords,           // Starmap target (:TG)
    FocuserStatus,          // Poll status (:PF)
    Preset,                 // Single preset (:PR, :PA)
    std::vector<Preset>,    // Preset list (:PL)
    std::vector<std::string>// Add-on list (:AQ)
>;

} // namespace OSF

class OSFprotocol {
public:
    explicit OSFprotocol(int fd);

    // Send command string and receive typed response
    OSF::ResponseData sendCommand(const std::string &cmd);

private:
    int m_fd;
    // Parser helpers
    OSF::ResponseData parseResponse(const std::string &rawResponse);
    OSF::StepLimits parseLimits(const std::string &payload);
};
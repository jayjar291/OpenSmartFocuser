#include <string>
#include <variant>
#include <cstdint>
#include <vector>

namespace OSF {

// Structures for commands returning multiple fields

// Structure representing step boundary limits for the focuser
struct StepLimits {
    int32_t minSteps;
    int32_t maxSteps;
};

// Structure representing target coordinates for the starmap
struct TargetCoords {
    double ra;
    double dec;
    std::string name;
};

// Structure representing a preset for the focuser
struct Preset {
    int32_t id;
    std::string name;
    int32_t steps;
};
// Structure representing the status of the focuser
struct FocuserStatus {
    std::string status;
    int32_t position;
    //int16_t dustCapPosition = -1;  // -1 if unused
    //int16_t brightness = -1;       // -1 if unused
};

// Response payload variant covering all protocol command types
using ResponseData = std::variant<
    bool,                   // ACK (:ACK# -> true, error/NAK -> false)
    int32_t,                // Single integer 
    std::string,            // Enum/version string 
    StepLimits,             // Step boundary limits 
    TargetCoords,           // Starmap target 
    FocuserStatus,          // Poll status 
    Preset,                 // Single preset 
    std::vector<Preset>,    // Preset list 
    std::vector<std::string>// Add-on list
>;
// Structure representing a command response, including the typed response and debug frames
struct CommandResponse {
    ResponseData response;
    std::vector<std::string> debugFrames;
};

} // namespace OSF

class OSFprotocol {
public:
    explicit OSFprotocol(int fd);

    // Send command string and receive typed response
    OSF::CommandResponse sendCommand(const std::string &cmd);

private:
    int m_fd;
    
    // Parse focuser status from the command frame
    OSF::FocuserStatus parseFocuserStatus(const std::string &commandFrame) const;
    // Split raw response into command frame and debug frames
    static bool splitResponseFrames(const std::string &rawResponse, std::string &commandFrame, std::vector<std::string> &debugFrames);
};
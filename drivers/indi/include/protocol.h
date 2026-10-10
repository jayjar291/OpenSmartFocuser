#include <string>
#include <variant>
#include <cstdint>
#include <vector>

namespace OSF {

// Typed values returned by protocol commands. Keeping response shapes explicit
// lets the driver distinguish a valid payload from a transport/parse failure.

// Hardware bounds supplied by :GL and used to constrain INDI position inputs.
struct StepLimits {
    int32_t minSteps;
    int32_t maxSteps;
};

// :TG payload fields; retained for the protocol parser although the active
// focuser driver does not currently expose star-map controls.
struct TargetCoords {
    double ra;
    double dec;
    std::string name;
};

// One preset record used by the preset response parser.
struct Preset {
    int32_t id;
    std::string name;
    int32_t steps;
};
// Leading fields of :PF; trailing optional add-on values are currently ignored.
struct FocuserStatus {
    std::string status;
    int32_t position;
    //int16_t dustCapPosition = -1;  // -1 if unused
    //int16_t brightness = -1;       // -1 if unused
};

// Exactly one response shape is active at a time. bool represents ACK/NAK,
// integer and aggregate alternatives represent parsed command-specific data.
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
// Couples the parsed command result with any separate !...* firmware
// diagnostic frames received in the same serial read.
struct CommandResponse {
    ResponseData response;
    std::vector<std::string> debugFrames;
};

} // namespace OSF

class OSFprotocol {
public:
    // Bind all later command exchanges to the serial file descriptor.
    explicit OSFprotocol(int fd);

    // Send one framed command and return its typed response plus diagnostics.
    OSF::CommandResponse sendCommand(const std::string &cmd);

private:
    // Non-owning descriptor from Connection::Serial; its owner controls lifetime.
    int m_fd;

    // Convert comma-delimited response payloads into typed records; each parser
    // throws invalid_argument when a required field is absent or malformed.
    OSF::FocuserStatus parseFocuserStatus(const std::string &commandFrame) const;
    OSF::StepLimits parseStepLimits(const std::string &commandFrame) const;
    OSF::TargetCoords parseTargetCoords(const std::string &commandFrame) const;
    OSF::Preset parsePreset(const std::string &presetFrame) const;
    std::vector<OSF::Preset> parsePresetList(const std::string &commandFrame) const;
    std::vector<std::string> parseAddonList(const std::string &commandFrame) const;
    int32_t parseInt32(const std::string &commandFrame) const;

    // Separate complete ':' command frames from '!' diagnostic frames while
    // leaving incomplete trailing bytes available for a subsequent read.
    static bool splitResponseFrames(const std::string &rawResponse, std::string &commandFrame, std::vector<std::string> &debugFrames);
};
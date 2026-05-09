#ifndef AWELC_CONTROLLER_H
#define AWELC_CONTROLLER_H

#include <vector>
#include <string>
#include <cstdint>
#include "transport/hid_transport.h"

class AWELCController {
public:
    explicit AWELCController(HIDTransport& transport);

    // Query commands
    std::string getFirmware();
    std::pair<uint16_t, int> getConfig(); // Returns (platform_id, zone_count)

    // Color control — safe path with response wait + inter-chunk pause.
    // Use for one-shot state changes and config where reliability matters.
    void setColorZones(uint8_t r, uint8_t g, uint8_t b, const std::vector<int>& zone_ids);
    void setZoneColor(int zone_id, uint8_t r, uint8_t g, uint8_t b);
    void setAllColor(uint8_t r, uint8_t g, uint8_t b, int zone_count = 101);
    void allOff(int zone_count = 101);

    // Fire-and-forget color write for animation frames. No response read,
    // minimal inter-chunk pause. Dropped frames are acceptable at 30-60 Hz.
    void setColorZonesFast(uint8_t r, uint8_t g, uint8_t b, const std::vector<int>& zone_ids);

    // Brightness
    void dim(uint8_t percent);

private:
    HIDTransport& transport_;
};

#endif // AWELC_CONTROLLER_H
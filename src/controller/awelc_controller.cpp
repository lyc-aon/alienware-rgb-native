#include "controller/awelc_controller.h"
#include "protocol.h"
#include <thread>
#include <chrono>

AWELCController::AWELCController(HIDTransport& transport) : transport_(transport) {}

std::string AWELCController::getFirmware() {
    // Send REPORT + FIRMWARE subcommand
    std::vector<uint8_t> cmd = {REPORT_ID, PROTOCOL_BYTE, CMD_REPORT, REPORT_FIRMWARE};
    auto resp = transport_.sendAndRead(cmd);
    
    if (resp.size() < 4) {
        return "";
    }
    
    // Response bytes after header contain firmware string
    std::string fw(resp.begin(), resp.end());
    size_t null_pos = fw.find('\0');
    if (null_pos != std::string::npos) {
        fw = fw.substr(0, null_pos);
    }
    return fw;
}

std::pair<uint16_t, int> AWELCController::getConfig() {
    // Send REPORT + CONFIG subcommand
    std::vector<uint8_t> cmd = {REPORT_ID, PROTOCOL_BYTE, CMD_REPORT, REPORT_CONFIG};
    auto resp = transport_.sendAndRead(cmd);
    
    if (resp.size() < 7) {
        return {0, 0};
    }
    
    // Response structure: [REPORT_ID, PROTOCOL_BYTE, ACK, ?, platform_hi, platform_lo, zone_count]
    // Byte 4-5: platform ID (big-endian)
    uint16_t platform = (static_cast<uint16_t>(resp[4]) << 8) | resp[5];
    // Byte 6: zone count
    int zones = resp[6];
    
    return {platform, zones};
}

void AWELCController::setColorZones(uint8_t r, uint8_t g, uint8_t b, const std::vector<int>& zone_ids) {
    // Chunk into packets of max 25 zones
    for (size_t i = 0; i < zone_ids.size(); i += MAX_ZONES_PER_PACKET) {
        size_t chunk_size = std::min(size_t(MAX_ZONES_PER_PACKET), zone_ids.size() - i);
        
        // Build command: [R, G, B, count_hi, count_lo, zone0, zone1, ...]
        std::vector<uint8_t> cmd = {
            REPORT_ID,
            PROTOCOL_BYTE,
            CMD_SET_COLOR,
            r, g, b,
            static_cast<uint8_t>(chunk_size >> 8),
            static_cast<uint8_t>(chunk_size & 0xFF)
        };
        
        for (size_t j = 0; j < chunk_size; ++j) {
            cmd.push_back(static_cast<uint8_t>(zone_ids[i + j]));
        }
        
        transport_.sendAndRead(cmd);
        
        // Extra pause between batches
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void AWELCController::setColorZonesFast(uint8_t r, uint8_t g, uint8_t b,
                                        const std::vector<int>& zone_ids) {
    // Same protocol as setColorZones but:
    //  - no response read (sendNoAck)
    //  - no inter-chunk sleep (just the time sendFeatureReport takes)
    // Typical latency: ~2ms per chunk, ~10ms for all 101 zones. Enables
    // animation at 30-60 Hz.
    for (size_t i = 0; i < zone_ids.size(); i += MAX_ZONES_PER_PACKET) {
        size_t chunk_size = std::min(size_t(MAX_ZONES_PER_PACKET), zone_ids.size() - i);
        std::vector<uint8_t> cmd = {
            REPORT_ID, PROTOCOL_BYTE, CMD_SET_COLOR,
            r, g, b,
            static_cast<uint8_t>(chunk_size >> 8),
            static_cast<uint8_t>(chunk_size & 0xFF),
        };
        for (size_t j = 0; j < chunk_size; ++j) {
            cmd.push_back(static_cast<uint8_t>(zone_ids[i + j]));
        }
        transport_.sendNoAck(cmd);
    }
}

void AWELCController::setZoneColor(int zone_id, uint8_t r, uint8_t g, uint8_t b) {
    setColorZones(r, g, b, {zone_id});
}

void AWELCController::setAllColor(uint8_t r, uint8_t g, uint8_t b, int zone_count) {
    std::vector<int> zones(zone_count);
    for (int i = 0; i < zone_count; ++i) {
        zones[i] = i;
    }
    setColorZones(r, g, b, zones);
}

void AWELCController::allOff(int zone_count) {
    setAllColor(0, 0, 0, zone_count);
}

void AWELCController::dim(uint8_t percent) {
    std::vector<uint8_t> cmd = {
        REPORT_ID,
        PROTOCOL_BYTE,
        CMD_DIM,
        std::max<uint8_t>(0, std::min<uint8_t>(100, percent))
    };
    transport_.sendAndRead(cmd);
}
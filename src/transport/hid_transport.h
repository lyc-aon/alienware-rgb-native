#ifndef HID_TRANSPORT_H
#define HID_TRANSPORT_H

#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include <hidapi.h>

class HIDTransport {
public:
    HIDTransport();
    ~HIDTransport();

    bool open();
    void close();
    bool isConnected() const { return device_ != nullptr; }

    // Send feature report and read response (with built-in delay). Use for
    // query commands (firmware, config) and for one-shot state changes.
    std::vector<uint8_t> sendAndRead(const std::vector<uint8_t>& data);

    // Fire-and-forget write for animation frames: no response read, no
    // INTER_CMD_DELAY_MS sleep. Returns bytes sent, or <0 on error.
    int sendNoAck(const std::vector<uint8_t>& data);

    std::string error() const;

private:
    int sendFeatureReport(const std::vector<uint8_t>& data);
    std::vector<uint8_t> getFeatureReport();

    hid_device* device_;
};

#endif // HID_TRANSPORT_H
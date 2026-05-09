#include "transport/hid_transport.h"
#include "protocol.h"
#include <hidapi.h>
#include <thread>
#include <chrono>

HIDTransport::HIDTransport() : device_(nullptr) {
    hid_init();
}

HIDTransport::~HIDTransport() {
    close();
    hid_exit();
}

bool HIDTransport::open() {
    if (device_) {
        return true;
    }
    
    device_ = hid_open(VENDOR_ID, PRODUCT_ID, nullptr);
    return device_ != nullptr;
}

void HIDTransport::close() {
    if (device_) {
        hid_close(device_);
        device_ = nullptr;
    }
}

int HIDTransport::sendFeatureReport(const std::vector<uint8_t>& data) {
    if (!device_) {
        return -1;
    }
    
    // hidapi expects: report_id (1 byte) + data bytes = total REPORT_SIZE (33)
    // The 'data' parameter already includes REPORT_ID as byte 0
    uint8_t buf[HIDAPI_BUF_SIZE] = {0};
    
    size_t copy_len = (data.size() < REPORT_SIZE) ? data.size() : REPORT_SIZE;
    for (size_t i = 0; i < copy_len; ++i) {
        buf[i] = data[i];
    }
    
    // Pass REPORT_SIZE (33), not HIDAPI_BUF_SIZE (34)
    return hid_send_feature_report(device_, buf, REPORT_SIZE);
}

std::vector<uint8_t> HIDTransport::getFeatureReport() {
    std::vector<uint8_t> result;
    
    if (!device_) {
        return result;
    }
    
    uint8_t buf[HIDAPI_BUF_SIZE] = {0};
    buf[0] = REPORT_ID;  // Initialize with report ID for hid_get_feature_report
    
    // Pass REPORT_SIZE (33) to match Python implementation
    int ret = hid_get_feature_report(device_, buf, REPORT_SIZE);
    if (ret < 0) {
        return result;
    }
    
    // hidapi returns report_id + data, Python returns bytes(buf[:n]) which includes everything
    result.assign(buf, buf + ret);
    return result;
}

std::vector<uint8_t> HIDTransport::sendAndRead(const std::vector<uint8_t>& data) {
    int sent = sendFeatureReport(data);
    if (sent < 0) {
        return {};
    }

    // Wait for protocol delay
    std::this_thread::sleep_for(std::chrono::milliseconds(INTER_CMD_DELAY_MS));

    return getFeatureReport();
}

int HIDTransport::sendNoAck(const std::vector<uint8_t>& data) {
    // No sleep, no response read — animator frames are fire-and-forget.
    // Dropped frames are acceptable at 30-60 Hz; next frame is a few ms away.
    return sendFeatureReport(data);
}

std::string HIDTransport::error() const {
    if (!device_) {
        return "Not connected";
    }
    
    const wchar_t* error_msg = hid_error(device_);
    if (error_msg) {
        // Convert wchar_t to std::string
        std::wstring wstr(error_msg);
        return std::string(wstr.begin(), wstr.end());
    }
    return "";
}
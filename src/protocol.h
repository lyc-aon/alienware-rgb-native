#ifndef PROTOCOL_H
#define PROTOCOL_H

// USB device identifiers
constexpr unsigned short VENDOR_ID = 0x187C;
constexpr unsigned short PRODUCT_ID = 0x0551;

// Packet structure
constexpr size_t REPORT_SIZE = 33;
constexpr size_t HIDAPI_BUF_SIZE = REPORT_SIZE + 1; // hidapi includes report_id
constexpr unsigned char REPORT_ID = 0x00;
constexpr unsigned char PROTOCOL_BYTE = 0x03;
constexpr unsigned char ACK_BYTE = 0x83;

// Commands
constexpr unsigned char CMD_REPORT = 0x20;
constexpr unsigned char CMD_DIM = 0x26;
constexpr unsigned char CMD_SET_COLOR = 0x27;

// Report subcommands
constexpr unsigned char REPORT_FIRMWARE = 0x00;
constexpr unsigned char REPORT_CONFIG = 0x02;

// Platform and zone constants
constexpr unsigned short PLATFORM_0812 = 0x0812;
constexpr int DEFAULT_ZONE_COUNT = 101;
constexpr int MAX_ZONES_PER_PACKET = 25; // 33 - 8 header bytes

// Timing (milliseconds)
constexpr int INTER_CMD_DELAY_MS = 70;

#endif // PROTOCOL_H
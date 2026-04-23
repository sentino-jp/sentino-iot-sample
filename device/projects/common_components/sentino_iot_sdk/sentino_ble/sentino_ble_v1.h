#ifndef __SENTINO_BLE_V1_H__
#define __SENTINO_BLE_V1_H__

#include <stdint.h>
#include <stddef.h>

/*
 * Sentino Rlink BLE V1 Packet Framing Protocol
 *
 * Frame layout (max 128 bytes):
 *   HEAD(0xFF) + TYPE(0x01) + SN(2B) + TOTAL(2B) + LEN(2B) + C_LEN(1B) + DATA(N) + CRC(1B)
 *   Header = 9 bytes, max payload per packet = 118 bytes
 *   CRC = SUM(TYPE..DATA) & 0xFF
 *
 * GATT Service: 0x1910
 *   Write char:  0x2B11 (App→Device)
 *   Notify char: 0x2B10 (Device→App)
 *
 * Advertising: Service UUID 0xA101 for device discovery
 */

#define V1_HEAD             0xFF
#define V1_TYPE             0x01
#define V1_HEADER_SIZE      9
#define V1_MAX_PAYLOAD      118
#define V1_MAX_PACKET       128

/* Provisioning status codes (sent as BLE Notify during provisioning) */
#define V1_STATUS_JSON_PARSED       1701
#define V1_STATUS_JSON_PARSE_FAIL   1700
#define V1_STATUS_WIFI_CONNECTED    1006
#define V1_STATUS_MQTT_CONNECTED    1703
#define V1_STATUS_SERVER_FAIL       1702
#define V1_STATUS_BIND_SUCCESS      1801
#define V1_STATUS_PKT_HEAD_ERR      1501
#define V1_STATUS_PKT_SN_ERR        1502
#define V1_STATUS_PKT_CRC_ERR       1503

/* V1 packet assembler state */
typedef struct {
    uint8_t *buffer;        /* reassembly buffer (allocated on first packet) */
    uint16_t received_len;
    uint16_t expected_total;
    uint16_t expected_len;
    uint16_t next_sn;
} v1_assembler_t;

/**
 * Encode a JSON string into V1 protocol packets.
 * @param json_str   Null-terminated JSON string
 * @param out_packets Array of packet buffers (caller must free each)
 * @param out_count   Number of packets generated
 * @return 0 on success
 */
int v1_encode(const char *json_str, uint8_t ***out_packets, int *out_sizes, int *out_count);

/** Free encoded packets */
void v1_encode_free(uint8_t **packets, int count);

/**
 * Feed one received V1 packet into the assembler.
 * @param assembler  Assembler state
 * @param packet     Raw packet data
 * @param packet_len Length of packet
 * @param out_json   If complete message assembled, points to JSON string (caller must free)
 * @return 0 if incomplete, 1 if complete message in out_json, -1 on error
 */
int v1_assembler_feed(v1_assembler_t *assembler, const uint8_t *packet, int packet_len, char **out_json);

/** Reset assembler state */
void v1_assembler_reset(v1_assembler_t *assembler);

#endif /* __SENTINO_BLE_V1_H__ */

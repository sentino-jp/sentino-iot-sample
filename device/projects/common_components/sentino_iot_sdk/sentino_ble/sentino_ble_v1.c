#include <string.h>
#include <stdlib.h>
#include <os/os.h>
#include <os/mem.h>
#include <components/log.h>

#include "sentino_ble_v1.h"

#define TAG "ble_v1"

static uint8_t v1_compute_crc(const uint8_t *buf, int start, int end)
{
    uint32_t sum = 0;
    for (int i = start; i < end; i++) {
        sum += buf[i];
    }
    return (uint8_t)(sum & 0xFF);
}

int v1_encode(const char *json_str, uint8_t ***out_packets, int *out_sizes, int *out_count)
{
    int data_len = strlen(json_str);
    int total_packets = (data_len + V1_MAX_PAYLOAD - 1) / V1_MAX_PAYLOAD;

    uint8_t **packets = os_malloc(total_packets * sizeof(uint8_t *));
    if (!packets) return -1;

    /* out_sizes is used as an array of ints */
    int *sizes = os_malloc(total_packets * sizeof(int));
    if (!sizes) { os_free(packets); return -1; }

    for (int sn = 0; sn < total_packets; sn++) {
        int offset = sn * V1_MAX_PAYLOAD;
        int chunk_len = data_len - offset;
        if (chunk_len > V1_MAX_PAYLOAD) chunk_len = V1_MAX_PAYLOAD;
        int pkt_len = V1_HEADER_SIZE + chunk_len + 1; /* +1 for CRC */

        uint8_t *pkt = os_malloc(pkt_len);
        if (!pkt) {
            /* cleanup on failure */
            for (int j = 0; j < sn; j++) os_free(packets[j]);
            os_free(packets);
            os_free(sizes);
            return -1;
        }

        pkt[0] = V1_HEAD;
        pkt[1] = V1_TYPE;
        pkt[2] = (sn >> 8) & 0xFF;
        pkt[3] = sn & 0xFF;
        pkt[4] = (total_packets >> 8) & 0xFF;
        pkt[5] = total_packets & 0xFF;
        pkt[6] = (data_len >> 8) & 0xFF;
        pkt[7] = data_len & 0xFF;
        pkt[8] = (uint8_t)chunk_len;
        memcpy(&pkt[V1_HEADER_SIZE], json_str + offset, chunk_len);
        pkt[pkt_len - 1] = v1_compute_crc(pkt, 1, pkt_len - 1);

        packets[sn] = pkt;
        sizes[sn] = pkt_len;
    }

    *out_packets = packets;
    *out_count = total_packets;
    /* Store sizes array pointer in out_sizes (reinterpret as int*) */
    memcpy(out_sizes, &sizes, sizeof(int *));
    return 0;
}

void v1_encode_free(uint8_t **packets, int count)
{
    if (!packets) return;
    for (int i = 0; i < count; i++) {
        if (packets[i]) os_free(packets[i]);
    }
    os_free(packets);
}

int v1_assembler_feed(v1_assembler_t *assembler, const uint8_t *packet, int packet_len, char **out_json)
{
    *out_json = NULL;

    if (packet_len < V1_HEADER_SIZE + 1) {
        BK_LOGW(TAG, "packet too short: %d\n", packet_len);
        return -1;
    }

    if (packet[0] != V1_HEAD) {
        BK_LOGW(TAG, "invalid HEAD: 0x%02x\n", packet[0]);
        v1_assembler_reset(assembler);
        return -1;
    }

    uint16_t sn = (packet[2] << 8) | packet[3];
    uint16_t total = (packet[4] << 8) | packet[5];
    uint16_t data_len = (packet[6] << 8) | packet[7];
    uint8_t chunk_len = packet[8];

    /* Verify CRC */
    int crc_pos = V1_HEADER_SIZE + chunk_len;
    if (crc_pos >= packet_len) {
        BK_LOGW(TAG, "CRC position out of bounds\n");
        v1_assembler_reset(assembler);
        return -1;
    }

    uint8_t expected_crc = v1_compute_crc(packet, 1, crc_pos);
    uint8_t actual_crc = packet[crc_pos];
    if (expected_crc != actual_crc) {
        BK_LOGW(TAG, "CRC mismatch: expected 0x%02x, got 0x%02x\n", expected_crc, actual_crc);
        v1_assembler_reset(assembler);
        return -1;
    }

    /* Verify sequence */
    if (sn != assembler->next_sn) {
        BK_LOGW(TAG, "SN mismatch: expected %d, got %d\n", assembler->next_sn, sn);
        v1_assembler_reset(assembler);
        return -1;
    }

    /* First packet: allocate buffer */
    if (sn == 0) {
        if (assembler->buffer) os_free(assembler->buffer);
        assembler->buffer = os_malloc(data_len + 1); /* +1 for null terminator */
        if (!assembler->buffer) {
            BK_LOGE(TAG, "alloc reassembly buffer failed\n");
            return -1;
        }
        assembler->received_len = 0;
        assembler->expected_total = total;
        assembler->expected_len = data_len;
    }

    /* Copy payload */
    if (assembler->buffer && (assembler->received_len + chunk_len <= assembler->expected_len)) {
        memcpy(assembler->buffer + assembler->received_len,
               &packet[V1_HEADER_SIZE], chunk_len);
    }
    assembler->received_len += chunk_len;
    assembler->next_sn = sn + 1;

    /* Last packet? */
    if (sn == total - 1) {
        if (assembler->received_len != assembler->expected_len) {
            BK_LOGW(TAG, "length mismatch: expected %d, got %d\n",
                    assembler->expected_len, assembler->received_len);
            v1_assembler_reset(assembler);
            return -1;
        }
        assembler->buffer[assembler->received_len] = '\0';
        *out_json = (char *)assembler->buffer;
        assembler->buffer = NULL; /* caller takes ownership */
        v1_assembler_reset(assembler);
        return 1; /* complete */
    }

    return 0; /* incomplete */
}

void v1_assembler_reset(v1_assembler_t *assembler)
{
    if (assembler->buffer) {
        os_free(assembler->buffer);
        assembler->buffer = NULL;
    }
    assembler->received_len = 0;
    assembler->expected_total = 0;
    assembler->expected_len = 0;
    assembler->next_sn = 0;
}

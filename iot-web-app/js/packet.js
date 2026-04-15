// V1 BLE Framing Protocol (ref-ble.md Section 4)
// Frame: HEAD(0xFF) + TYPE(0x01) + SN(2B) + TOTAL(2B) + LEN(2B) + C_LEN(1B) + DATA(N) + CRC(1B)

const HEAD = 0xff;
const TYPE = 0x01;
const HEADER_SIZE = 9; // HEAD(1) + TYPE(1) + SN(2) + TOTAL(2) + LEN(2) + C_LEN(1)
const MAX_PAYLOAD = 118;

function computeCRC(buf, start, end) {
  let sum = 0;
  for (let i = start; i < end; i++) sum += buf[i];
  return sum & 0xff;
}

/** Encode a JSON string into V1 protocol packets */
export function encode(jsonString) {
  const data = new TextEncoder().encode(jsonString);
  const totalLen = data.length;
  const totalPackets = Math.ceil(totalLen / MAX_PAYLOAD);
  const packets = [];

  for (let sn = 0; sn < totalPackets; sn++) {
    const offset = sn * MAX_PAYLOAD;
    const chunkLen = Math.min(MAX_PAYLOAD, totalLen - offset);
    const pktLen = HEADER_SIZE + chunkLen + 1; // +1 for CRC
    const pkt = new Uint8Array(pktLen);

    pkt[0] = HEAD;
    pkt[1] = TYPE;
    pkt[2] = (sn >> 8) & 0xff;
    pkt[3] = sn & 0xff;
    pkt[4] = (totalPackets >> 8) & 0xff;
    pkt[5] = totalPackets & 0xff;
    pkt[6] = (totalLen >> 8) & 0xff;
    pkt[7] = totalLen & 0xff;
    pkt[8] = chunkLen;
    pkt.set(data.subarray(offset, offset + chunkLen), HEADER_SIZE);

    pkt[pktLen - 1] = computeCRC(pkt, 1, pktLen - 1); // TYPE through DATA
    packets.push(pkt);
  }
  return packets;
}

/** Reassemble received V1 packets into a complete JSON string */
export class PacketAssembler {
  constructor() {
    this.reset();
  }

  reset() {
    this.buffer = null;
    this.receivedLen = 0;
    this.expectedTotal = 0;
    this.nextSN = 0;
  }

  /** Feed one received packet. Returns complete JSON string when done, null otherwise. */
  feed(packet) {
    const buf = packet instanceof Uint8Array ? packet : new Uint8Array(packet);

    if (buf[0] !== HEAD) {
      console.warn('[Packet] Invalid HEAD:', buf[0]);
      return null;
    }

    const sn = (buf[2] << 8) | buf[3];
    const total = (buf[4] << 8) | buf[5];
    const dataLen = (buf[6] << 8) | buf[7];
    const chunkLen = buf[8];

    // Verify CRC
    const expectedCRC = computeCRC(buf, 1, HEADER_SIZE + chunkLen);
    const actualCRC = buf[HEADER_SIZE + chunkLen];
    if (expectedCRC !== actualCRC) {
      console.warn(`[Packet] CRC mismatch: expected ${expectedCRC}, got ${actualCRC}`);
      this.reset();
      return null;
    }

    // Verify sequence
    if (sn !== this.nextSN) {
      console.warn(`[Packet] Sequence error: expected ${this.nextSN}, got ${sn}`);
      this.reset();
      return null;
    }

    // First packet: allocate buffer
    if (sn === 0) {
      this.buffer = new Uint8Array(dataLen);
      this.receivedLen = 0;
      this.expectedTotal = total;
    }

    // Copy payload
    this.buffer.set(buf.subarray(HEADER_SIZE, HEADER_SIZE + chunkLen), this.receivedLen);
    this.receivedLen += chunkLen;
    this.nextSN = sn + 1;

    // Last packet
    if (sn === total - 1) {
      if (this.receivedLen !== dataLen) {
        console.warn(`[Packet] Length mismatch: expected ${dataLen}, got ${this.receivedLen}`);
        this.reset();
        return null;
      }
      const result = new TextDecoder().decode(this.buffer);
      this.reset();
      return result;
    }

    return null;
  }
}

/** Format a packet as hex string for debugging */
export function toHex(packet) {
  return Array.from(packet).map(b => b.toString(16).padStart(2, '0')).join(' ');
}

// Web Bluetooth wrapper for Sentino BLE protocol (ref-ble.md)

import { encode, PacketAssembler, toHex } from './packet.js';

// BLE UUIDs
// Advertising uses 0xA101, but GATT service is 0x1910
const ADV_SERVICE_UUID = 0xa101;                                    // for scanning filter
const GATT_SERVICE_UUID = '00001910-0000-1000-8000-00805f9b34fb';   // actual GATT service
const WRITE_CHAR_UUID = '00002b11-0000-1000-8000-00805f9b34fb';     // write / write-without-response
const NOTIFY_CHAR_UUID = '00002b10-0000-1000-8000-00805f9b34fb';    // notify / indicate
const PACKET_DELAY_MS = 20;

const SENTINO_COMPANY_ID = 0x0000;

let device = null;
let server = null;
let writeChar = null;
let notifyChar = null;
let deviceUuid = null; // extracted from manufacturer data
const assembler = new PacketAssembler();
const messageHandlers = [];
let logFn = console.log;

/** Set external log function */
export function setLogger(fn) {
  logFn = fn;
}

function log(msg) {
  logFn(`[BLE] ${msg}`);
}

/** Scan, connect, and discover GATT service */
export async function scanAndConnect() {
  if (!navigator.bluetooth) {
    throw new Error('Web Bluetooth not supported. Please use Chrome.');
  }

  log('Requesting BLE device...');
  device = await navigator.bluetooth.requestDevice({
    filters: [{ services: [ADV_SERVICE_UUID] }],
    optionalServices: [GATT_SERVICE_UUID],
    optionalManufacturerData: [{ companyIdentifier: SENTINO_COMPANY_ID }],
  });
  log(`Device selected: ${device.name || device.id}`);
  deviceUuid = null;

  device.addEventListener('gattserverdisconnected', () => {
    log('Device disconnected');
    writeChar = null;
    notifyChar = null;
    server = null;
  });

  log('Connecting GATT...');
  server = await device.gatt.connect();

  log(`Discovering GATT service ${GATT_SERVICE_UUID}...`);
  const service = await server.getPrimaryService(GATT_SERVICE_UUID);
  log('Service found');

  log('Getting characteristics...');
  writeChar = await service.getCharacteristic(WRITE_CHAR_UUID);
  log(`Write char: ${WRITE_CHAR_UUID}`);
  notifyChar = await service.getCharacteristic(NOTIFY_CHAR_UUID);
  log(`Notify char: ${NOTIFY_CHAR_UUID}`);

  if (!writeChar) throw new Error('No writable characteristic found');
  if (!notifyChar) throw new Error('No notify characteristic found');

  // Start notifications
  await notifyChar.startNotifications();
  notifyChar.addEventListener('characteristicvaluechanged', onNotify);
  log('Notifications started. Ready.');

  // Try to extract device UUID from advertisement manufacturer data
  tryExtractUuid();

  return { device, server };
}

/** Try watchAdvertisements to extract UUID from manufacturer data */
function tryExtractUuid() {
  if (!device || !device.watchAdvertisements) {
    log('watchAdvertisements not available — enter UUID manually');
    return;
  }
  try {
    device.addEventListener('advertisementreceived', (event) => {
      if (deviceUuid) return; // already extracted
      for (const [companyId, dataView] of event.manufacturerData) {
        // Structure: ConfigFLAG(1) + ProtoVer(1) + EncryptMethod(1) + CommAbility(2) + IDType(1) + UUID/MAC(N)
        if (dataView.byteLength > 6) {
          const bytes = new Uint8Array(dataView.buffer, dataView.byteOffset, dataView.byteLength);
          const idType = bytes[5];
          const idBytes = bytes.slice(6);
          if (idType === 0) {
            // UUID — try decode as UTF-8 text
            deviceUuid = new TextDecoder().decode(idBytes);
            log(`UUID from advertisement: ${deviceUuid}`);
          } else {
            // MAC
            deviceUuid = Array.from(idBytes.slice(0, 6)).map(b => b.toString(16).padStart(2, '0')).join(':');
            log(`MAC from advertisement: ${deviceUuid}`);
          }
        }
      }
    });
    device.watchAdvertisements();
    log('Watching advertisements for UUID...');
  } catch (e) {
    log(`watchAdvertisements failed: ${e.message}`);
  }
}

function onNotify(event) {
  const value = new Uint8Array(event.target.value.buffer);
  log(`RX: ${toHex(value)}`);

  const result = assembler.feed(value);
  if (result !== null) {
    log(`RX complete: ${result}`);
    try {
      const msg = JSON.parse(result);
      for (const handler of messageHandlers) handler(msg);
    } catch (e) {
      log(`JSON parse error: ${e.message}`);
    }
  }
}

/** Register a handler for incoming messages */
export function onMessage(callback) {
  messageHandlers.push(callback);
}

/** Remove a message handler */
export function offMessage(callback) {
  const idx = messageHandlers.indexOf(callback);
  if (idx >= 0) messageHandlers.splice(idx, 1);
}

/** Send a JSON message via BLE (handles V1 framing + packet delays) */
export async function sendMessage(jsonObject) {
  if (!writeChar) throw new Error('Not connected');

  const jsonStr = JSON.stringify(jsonObject);
  log(`TX: ${jsonStr}`);
  const packets = encode(jsonStr);

  for (let i = 0; i < packets.length; i++) {
    log(`TX packet ${i + 1}/${packets.length}: ${toHex(packets[i])}`);
    if (writeChar.properties.writeWithoutResponse) {
      await writeChar.writeValueWithoutResponse(packets[i]);
    } else {
      await writeChar.writeValue(packets[i]);
    }
    if (i < packets.length - 1) {
      await new Promise((r) => setTimeout(r, PACKET_DELAY_MS));
    }
  }
}

/** Send a message and wait for a response of a specific type */
export function request(jsonObject, expectedResponseType, timeoutMs = 10000) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      offMessage(handler);
      reject(new Error(`Timeout waiting for ${expectedResponseType}`));
    }, timeoutMs);

    function handler(msg) {
      if (msg.type === expectedResponseType) {
        clearTimeout(timer);
        offMessage(handler);
        resolve(msg);
      }
    }

    onMessage(handler);
    sendMessage(jsonObject).catch((err) => {
      clearTimeout(timer);
      offMessage(handler);
      reject(err);
    });
  });
}

/** Get the current BluetoothDevice */
export function getDevice() {
  return device;
}

/** Get device UUID extracted from advertisement manufacturer data */
export function getDeviceUuid() {
  return deviceUuid;
}

/** Check if connected */
export function isConnected() {
  return server !== null && server.connected;
}

/** Disconnect from device */
export function disconnect() {
  if (server && server.connected) {
    server.disconnect();
  }
  writeChar = null;
  notifyChar = null;
  server = null;
  device = null;
  deviceUuid = null;
  assembler.reset();
}

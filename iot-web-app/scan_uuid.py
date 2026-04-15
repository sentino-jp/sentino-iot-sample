#!/usr/bin/env python3
"""Scan BLE advertisement to extract device UUID from manufacturer data.

Manufacturer data structure (from ref-ble.md §2.2):
  Company ID (2B, key) | Config FLAG (1B) | Proto Version (1B) |
  Encrypt Method (1B)  | Comm Ability (2B) | ID Type (1B)      |
  UUID or MAC (max 19B)

ID Type: 0 = UUID, 1 = MAC
Encrypt Method: 0x00 = AES-CBC encrypted UUID, 0x01 = plaintext MAC, 0x02 = plaintext
"""

import asyncio
from bleak import BleakScanner

ADV_SERVICE_UUID = "0000a101-0000-1000-8000-00805f9b34fb"


def parse_manufacturer_data(company_id, data: bytes):
    """Parse Sentino manufacturer data and extract device UUID."""
    print(f"  Company ID: 0x{company_id:04X}")
    print(f"  Raw bytes ({len(data)}): {data.hex(' ')}")

    if len(data) < 7:
        print("  Too short to parse")
        return None

    config_flag = data[0]
    proto_ver = data[1]
    encrypt_method = data[2]
    comm_ability = (data[3] << 8) | data[4]
    id_type = data[5]
    id_bytes = data[6:]

    print(f"  Config FLAG:    0x{config_flag:02X}")
    print(f"    - Provisioning: {'no' if config_flag & 0x80 else 'yes'}")
    print(f"    - Bound:        {'yes' if config_flag & 0x40 else 'no'}")
    print(f"    - WiFi:         {'connected' if config_flag & 0x20 else 'disconnected'}")
    print(f"  Proto Version:  0x{proto_ver:02X}")
    print(f"  Encrypt Method: 0x{encrypt_method:02X} ({'auth key+device id' if encrypt_method == 0 else 'ECB' if encrypt_method == 1 else 'plaintext' if encrypt_method == 2 else 'unknown'})")
    print(f"  Comm Ability:   0x{comm_ability:04X}")
    print(f"  ID Type:        {id_type} ({'UUID' if id_type == 0 else 'MAC'})")

    if id_type == 0:
        # UUID
        if encrypt_method == 0x00:
            print(f"  UUID (encrypted): {id_bytes.hex(' ')}")
            # Try decoding as text anyway
            try:
                uuid_str = id_bytes.decode('utf-8', errors='replace')
                print(f"  UUID (as text):   {uuid_str}")
            except:
                pass
            return id_bytes.hex()
        else:
            uuid_str = id_bytes.decode('utf-8', errors='replace')
            print(f"  UUID: {uuid_str}")
            return uuid_str
    else:
        mac = ':'.join(f'{b:02X}' for b in id_bytes[:6])
        print(f"  MAC: {mac}")
        return mac


async def main():
    print("Scanning for Sentino BLE devices (Service UUID 0xA101)...")
    print("Make sure device is in provisioning mode.\n")

    devices = await BleakScanner.discover(
        timeout=10.0,
        return_adv=True,
    )

    found = False
    for device, adv_data in devices.values():
        # Check if this is a Sentino device (0xA101 in service_uuids OR service_data)
        service_uuids = [str(u).lower() for u in (adv_data.service_uuids or [])]
        service_data_uuids = [str(u).lower() for u in (adv_data.service_data or {}).keys()]
        is_sentino = ADV_SERVICE_UUID in service_uuids or ADV_SERVICE_UUID in service_data_uuids

        if not is_sentino:
            continue

        found = True
        print(f"Found Sentino device: {device.name or '(no name)'}")
        print(f"  Address: {device.address}")
        print(f"  RSSI: {adv_data.rssi} dBm")
        print(f"  Service UUIDs: {adv_data.service_uuids}")

        if adv_data.manufacturer_data:
            for company_id, data in adv_data.manufacturer_data.items():
                print(f"\n  Manufacturer Data:")
                uuid = parse_manufacturer_data(company_id, bytes(data))
                if uuid:
                    print(f"\n  >>> Device identifier: {uuid}")
                    print(f"  >>> Use this for checkBindResult polling")
        else:
            print("  No manufacturer data found")

        if adv_data.service_data:
            for uuid, data in adv_data.service_data.items():
                print(f"\n  Service Data [{uuid}]: {bytes(data).hex(' ')}")
                # Service data: UUID(2B LE) + Flag(1B) + PID(NB)
                raw = bytes(data)
                if len(raw) > 3:
                    pid = raw[3:].decode('utf-8', errors='replace')
                    print(f"    PID: {pid}")

        print()

    if not found:
        print("No Sentino devices found. Ensure device is in provisioning mode.")


if __name__ == "__main__":
    asyncio.run(main())

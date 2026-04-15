#!/usr/bin/env python3
"""Scan BLE for Sentino devices (0xA101)."""
import asyncio
from bleak import BleakScanner

async def scan():
    print("Scanning 10s...")
    devices = await BleakScanner.discover(timeout=10.0, return_adv=True)
    found = False
    for device, adv in devices.values():
        name = device.name or ""
        svc = [str(u).lower() for u in (adv.service_uuids or [])]
        sd = {str(k).lower(): bytes(v).hex() for k, v in (adv.service_data or {}).items()}
        has_a101 = any("a101" in s for s in svc) or any("a101" in k for k in sd)
        if has_a101 or any(p in name for p in ["R1", "X1", "RY"]):
            found = True
            print(f"FOUND: {name}  rssi={adv.rssi}")
            print(f"  svc_uuids: {svc}")
            print(f"  svc_data:  {sd}")
            mfr = {hex(k): bytes(v).hex() for k, v in adv.manufacturer_data.items()} if adv.manufacturer_data else {}
            print(f"  mfr_data:  {mfr}")
    if not found:
        print("No Sentino device found. Long press S1 to enter provisioning mode.")
    print(f"Total: {len(devices)} devices")

if __name__ == "__main__":
    asyncio.run(scan())

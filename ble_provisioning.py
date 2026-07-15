#!/usr/bin/env python

import asyncio
import argparse
from bleak import BleakClient, BleakScanner
from bleak.backends.characteristic import BleakGATTCharacteristic


class Args(argparse.Namespace):
    ssid: str
    password: str
    ap: bool


async def main(args: Args) -> None:
    device = await BleakScanner.find_device_by_name("esp32-cam")
    if device is None:
        print("ble scanner not found esp32-cam")
        return

    async with BleakClient(device) as client:
        value = (args.ssid + " " + args.password).encode("utf-8")
        await client.write_gatt_char(
            "b8a8da87-e141-3a8f-374d-0642b3ad54bf", value, response=False
        )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "-s",
        "--ssid",
        required=True,
        type=str,
    )
    parser.add_argument(
        "-p",
        "--password",
        required=True,
        type=str,
    )

    parser.add_argument("--ap", action="store_true")

    args = parser.parse_args(namespace=Args())

    print(f"{args.ssid} {args.password}")

    asyncio.run(main(args))

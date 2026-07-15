#!/usr/bin/env python
import socket
from struct import unpack
from PIL import Image, ImageTk
import tkinter as tk
from io import BytesIO

# Format constants matching esp32-camera pixformat_t enum
FMT_RGB565 = 0
FMT_YUV422 = 1
FMT_YUV420 = 2
FMT_GRAYSCALE = 3
FMT_JPEG = 4
FMT_RGB888 = 5

HEADER_SIZE = 9  # length(4B) + width(2B) + height(2B) + format(1B)


def rgb565_to_image(data, width, height):
    """Convert RGB565 byte data to PIL Image."""
    rgb_pixels = bytearray(width * height * 3)
    for i in range(0, len(data), 2):
        pixel = (data[i] << 8) | data[i + 1]  # big-endian

        idx = (i // 2) * 3
        rgb_pixels[idx] = ((pixel >> 11) & 0x1F) << 3  # R
        rgb_pixels[idx + 1] = ((pixel >> 5) & 0x3F) << 2  # G
        rgb_pixels[idx + 2] = (pixel & 0x1F) << 3  # B

    return Image.frombytes("RGB", (width, height), bytes(rgb_pixels))


def decode_frame(width, height, fmt, payload):
    """Decode image payload based on format."""
    if fmt == FMT_JPEG:
        return Image.open(BytesIO(payload))
    elif fmt == FMT_RGB565:
        return rgb565_to_image(payload, width, height)
    elif fmt == FMT_RGB888:
        return Image.frombytes("RGB", (width, height), payload)
    elif fmt == FMT_GRAYSCALE:
        return Image.frombytes("L", (width, height), payload)
    else:
        raise ValueError(f"Unsupported format: {fmt}")


def extract_latest_frame(buf):
    """Extract all complete frames from buf, return the latest one and leftover bytes.
    Returns (width, height, fmt, payload, remaining_buf) or None if no complete frame.
    """
    latest = None
    while len(buf) >= HEADER_SIZE:
        length, width, height, fmt = unpack(">IHHB", buf[:HEADER_SIZE])
        frame_total = HEADER_SIZE + length
        if len(buf) < frame_total:
            break  # incomplete frame, wait for more data
        payload = bytes(buf[HEADER_SIZE:frame_total])
        latest = (width, height, fmt, payload)
        buf = buf[frame_total:]  # discard old frame, keep scanning
    return latest, buf


def connect_and_receive_image(ip, port, label):
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        client_socket.connect((ip, port))
        print("Connected")

        buffer = bytearray()

        while True:
            # ---- 1) Non-blocking: drain all available data into buffer ----
            client_socket.setblocking(False)
            try:
                while True:
                    chunk = client_socket.recv(65536)
                    if not chunk:
                        break
                    buffer.extend(chunk)
            except BlockingIOError:
                pass
            finally:
                client_socket.setblocking(True)

            # ---- 2) If no complete frame in buffer, do ONE blocking read ----
            if len(buffer) < HEADER_SIZE:
                chunk = client_socket.recv(HEADER_SIZE - len(buffer))
                if not chunk:
                    raise ConnectionError("Connection closed")
                buffer.extend(chunk)

            # ---- 3) Try to extract latest frame ----
            result, buffer = extract_latest_frame(buffer)

            # If we have a header but not full payload, block until we get the rest
            if result is None and len(buffer) >= HEADER_SIZE:
                length = unpack(">I", buffer[:4])[0]
                needed = HEADER_SIZE + length - len(buffer)
                while needed > 0:
                    chunk = client_socket.recv(needed)
                    if not chunk:
                        raise ConnectionError("Connection closed")
                    buffer.extend(chunk)
                    needed -= len(chunk)
                result, buffer = extract_latest_frame(buffer)

            # ---- 4) Display latest frame ----
            if result:
                width, height, fmt, payload = result
                img = decode_frame(width, height, fmt, payload)
                print(
                    f"\rFrame: {width}x{height} fmt={fmt} size={len(payload):6d}",
                    end="",
                    flush=True,
                )

                tk_img = ImageTk.PhotoImage(img)
                label.config(image=tk_img)
                label.image = tk_img

            # Safety: if buffer grows too large (e.g. corrupted stream), reset
            if len(buffer) > 1_000_000:
                print("\nBuffer overflow, resetting")
                buffer = bytearray()

    except Exception as e:
        print(f"\nError: {e}")
    finally:
        client_socket.close()


if __name__ == "__main__":
    root = tk.Tk()
    root.wm_geometry("320x240")
    root.title("ESP32-CAM")
    label = tk.Label(root)
    label.pack()
    ip = ""
    try:
        ip = socket.gethostbyname("esp32-cam.local")
        print(f"esp32-cam.local -> {ip}")
    except socket.gaierror as e:
        print(f"Unable to resolve esp32-cam.local: {e}")

    from threading import Thread

    thread = Thread(target=connect_and_receive_image, args=(ip, 3488, label))
    thread.daemon = True
    thread.start()

    root.mainloop()

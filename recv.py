import socket
import time
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

FRAME_HEADER_SIZE = 9   # length(4B) + width(2B) + height(2B) + format(1B)
CHUNK_HEADER_SIZE = 8   # frame_id(4B) + chunk_seq(2B) + total_chunks(2B)

# Maximum frames to keep in reassembly buffer (prevent memory leak)
MAX_PENDING_FRAMES = 4


def rgb565_to_image(data, width, height):
    """Convert RGB565 byte data to PIL Image."""
    rgb_pixels = bytearray(width * height * 3)
    for i in range(0, len(data), 2):
        pixel = (data[i] << 8) | data[i + 1]  # big-endian

        idx = (i // 2) * 3
        rgb_pixels[idx]     = ((pixel >> 11) & 0x1F) << 3   # R
        rgb_pixels[idx + 1] = ((pixel >> 5)  & 0x3F) << 2   # G
        rgb_pixels[idx + 2] = (pixel         & 0x1F) << 3   # B

    return Image.frombytes('RGB', (width, height), bytes(rgb_pixels))


def decode_frame(width, height, fmt, payload):
    """Decode image payload based on format."""
    if fmt == FMT_JPEG:
        return Image.open(BytesIO(payload))
    elif fmt == FMT_RGB565:
        return rgb565_to_image(payload, width, height)
    elif fmt == FMT_RGB888:
        return Image.frombytes('RGB', (width, height), payload)
    elif fmt == FMT_GRAYSCALE:
        return Image.frombytes('L', (width, height), payload)
    else:
        raise ValueError(f"Unsupported format: {fmt}")


def connect_and_receive_image(ip, port, label):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('', 0))  # bind to any available port so we can receive
    local_port = sock.getsockname()[1]
    print(f"Local UDP port: {local_port}")

    # Send "hello" so ESP32 learns our address
    sock.sendto("hello".encode("utf-8"), (ip, port))
    print(f"Sent hello to {ip}:{port}")

    pending = {}          # frame_id -> {'total': N, 'chunks': {seq: bytes}}
    last_displayed = -1  # last frame_id we displayed

    def store_chunk(raw):
        """Parse and store a chunk datagram. Returns frame_id if chunk stored."""
        if len(raw) < CHUNK_HEADER_SIZE:
            return None
        frame_id, chunk_seq, total_chunks = unpack('>IHH', raw[:CHUNK_HEADER_SIZE])
        chunk_data = raw[CHUNK_HEADER_SIZE:]

        if frame_id <= last_displayed:
            return None

        if frame_id not in pending:
            pending[frame_id] = {'total': total_chunks, 'chunks': {}, 'time': time.time()}
        pending[frame_id]['chunks'][chunk_seq] = chunk_data
        pending[frame_id]['time'] = time.time()
        return frame_id

    def try_reassemble():
        """Check for complete frames, return list of (fid, width, height, fmt, payload)."""
        completed = []
        for fid in sorted(pending.keys()):
            info = pending[fid]
            if len(info['chunks']) == info['total']:
                parts = [info['chunks'][seq] for seq in range(info['total'])]
                full_data = b''.join(parts)

                if len(full_data) >= FRAME_HEADER_SIZE:
                    length, width, height, fmt = unpack('>IHHB', full_data[:FRAME_HEADER_SIZE])
                    payload = full_data[FRAME_HEADER_SIZE:FRAME_HEADER_SIZE + length]
                    completed.append((fid, width, height, fmt, payload))
        return completed

    while True:
        # ---- 1) Non-blocking: drain all available datagrams ----
        sock.setblocking(False)
        try:
            while True:
                dgram, _ = sock.recvfrom(65536)
                store_chunk(dgram)
        except BlockingIOError:
            pass
        finally:
            sock.setblocking(True)

        # ---- 2) If frame incomplete, block until we get enough chunks ----
        completed = try_reassemble()
        if not completed and pending:
            sock.settimeout(0.5)  # ESP32 sends all chunks within ~15ms
            deadline = time.time() + 0.5
            try:
                while time.time() < deadline:
                    dgram = sock.recv(65536)
                    store_chunk(dgram)
                    completed = try_reassemble()
                    if completed:
                        break
            except socket.timeout:
                pass
            sock.settimeout(None)

        # ---- 3) Display the latest complete frame ----
        if completed:
            for fid, width, height, fmt, payload in completed:
                if fid > last_displayed:
                    try:
                        img = decode_frame(width, height, fmt, payload)
                        nchunks = pending.get(fid, {}).get('total', '?')
                        print(f"\rFrame #{fid}: {width}x{height} fmt={fmt} size={len(payload):6d} chunks={nchunks}  ",
                              end='', flush=True)

                        tk_img = ImageTk.PhotoImage(img)
                        label.config(image=tk_img)
                        label.image = tk_img
                        last_displayed = fid
                    except Exception as e:
                        print(f"\nDecode error: {e}")

            for fid, *_ in completed:
                pending.pop(fid, None)

        # ---- 4) Clean up stale / old frames ----
        now = time.time()
        stale = [fid for fid, info in pending.items()
                 if now - info['time'] > 2.0]
        for fid in stale:
            print(f"\nDropping stale frame #{fid} ({len(pending[fid]['chunks'])}/{pending[fid]['total']} chunks)")
            pending.pop(fid)

        while len(pending) > MAX_PENDING_FRAMES:
            oldest = min(pending.keys())
            pending.pop(oldest)


if __name__ == '__main__':
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

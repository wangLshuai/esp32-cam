import socket
from struct import unpack
from PIL import Image, ImageTk
import tkinter as tk
import time

def connect_and_receive_image(ip, port, label):
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        client_socket.connect((ip, port))
        print("Connected")

        while True:  # 循环接收图像
            width, height  = 320,240
            
            total_bytes = width * height * 2
            image_data = bytearray()
            while len(image_data) < total_bytes:
                chunk = client_socket.recv(total_bytes - len(image_data))
                if not chunk:
                    break
                image_data.extend(chunk)

            if len(image_data) != total_bytes:
                raise Exception(f"Expected {total_bytes}, got {len(image_data)}")

            # 转换 RGB565 → RGB888
            rgb_pixels = []
            for i in range(0, len(image_data), 2):
                b1 = image_data[i]      # 高字节
                b0 = image_data[i + 1]  # 低字节
                pixel = (b1 << 8) | b0  # 大端组合

                r = ((pixel >> 11) & 0x1F) << 3
                g = ((pixel >> 5)  & 0x3F) << 2
                b = (pixel        & 0x1F) << 3

                rgb_pixels.extend([r, g, b])

            img = Image.frombytes('RGB', (width, height), bytes(rgb_pixels))
            
            # 使用ImageTk转换为适合tkinter的格式
            tk_img = ImageTk.PhotoImage(img)
            label.config(image=tk_img)
            label.image = tk_img  # 保持引用以防止被垃圾回收器删除

            # 简单的延迟避免过快更新
            time.sleep(0.1)

    except Exception as e:
        print(f"Error: {e}")
    finally:
        client_socket.close()

if __name__ == '__main__':
    root = tk.Tk()
    label = tk.Label(root)
    label.pack()
    ip = ""
    try:
        ip = socket.gethostbyname("esp32-cam.local")
        print(f"esp32-cam.local 的 IPv4 地址是: {ip}")
    except socket.gaierror as e:
        print(f"无法解析域名 esp32-cam.local: {e}")

    # 在新线程中运行接收函数，以免阻塞tkinter主循环
    from threading import Thread
    thread = Thread(target=connect_and_receive_image, args=(ip, 3488, label))
    thread.daemon = True  # 设置为守护线程，以便关闭窗口时能退出程序
    thread.start()

    root.mainloop()  # 开始tkinter的事件循环

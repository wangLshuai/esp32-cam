# GOOUUU esp32-s3-cam 图传

摄像头是 OV2640

# pin
![img](./imgs/ESP32S3CAM-Pin.jpg)

# build
source ${IDF_PATH}/export.sh
idf.py menuconfig 去设置 SSID 和 passwd，可以键入 ‘/’ 去搜索配置
idf.py build

# format
git config core.hooksPath .githooks

# run
idf.py -p PORT flash
python view.py
![img](./imgs/view.png)
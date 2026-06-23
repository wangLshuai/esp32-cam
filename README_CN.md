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

* wifi 配置
如果没有编译前配置默认的 CONFIG_WIFI_PASSWORD 和 CONFIG_WIFI_SSID, 需要使用 esp ble prov app 蓝牙配网,设备前缀默认是 esp32-cam_ ,proof of possession pin 默认是 “abcd1234”,
长按按键 CONFIG_WIFI_PROV_BUTTON_GPIO 5s ，则重启进入配网模式
python view.py
![img](./imgs/view.png)
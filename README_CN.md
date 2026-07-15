# GOOUUU esp32-s3-cam 图传

摄像头是 OV2640

# pin
![img](./imgs/ESP32S3CAM-Pin.jpg)

# build
```
source ${IDF_PATH}/export.sh
idf.py menuconfig 
idf.py build
```

# format
git config core.hooksPath .githooks

# run
idf.py -p PORT flash

* wifi 配置
如果没有编译前配置默认的 CONFIG_WIFI_PASSWORD 和 CONFIG_WIFI_SSID, 需要用 ble 去配置网络,
安装依赖的 python module
```
pip install bleak
```

ble_provisioning.py 脚本去设置 wifi ssid 和 password ，ssid 和 password 的总长度不能超过62.
```
./ble_provisioning.py -s mysid -p mypassword
```



```
python view.py
```
![img](./imgs/view.png)
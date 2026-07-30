# ESP8266 MQTT 远程RGB灯带控制系统
## 📖 项目介绍
基于ESP8266(ESP-12F)、WS2812幻彩灯带、EMQX Cloud MQTT服务搭建的物联网灯光控制系统。
公网MQTT通信，支持HTML网页、MQTTX客户端远程下发指令控制灯带颜色；搭载OLED屏幕实时显示当前灯光颜色名称。

## 🛠️ 硬件清单
- ESP8266 ESP-12F 开发板
- WS2812 / WS2815 5V幻彩灯带
- 5V直流电源（灯带长度较长建议独立供电）
- 0.96寸 I2C OLED显示屏（可选）
- 330Ω信号电阻（信号优化，减少颜色错乱）
- 1000μF电解电容（电源滤波）

## 🔌 硬件接线
1. WS2812 数据引脚 → ESP8266 DATA_PIN
2. WS2812 GND 必须与ESP开发板 **共地**（重中之重，避免只亮第一颗灯）
3. OLED SDA → D2，SCL → D1
> ⚠️ 长灯带建议灯带单独外接5V电源，不要依靠开发板供电

## ⚙️ 服务端环境
MQTT平台：EMQX Cloud Serverless
- MQTT接入地址：`i11884f9.ala.cn-hangzhou.emqxsl.cn`
- MQTT端口(mqtts)：8883（单片机、MQTTX使用）
- WebSocket端口(wss)：8084（HTML网页前端使用）
- 通信主题：`light/rgb/cmd`

## 📡 通信协议说明
采用纯文本字符串下发RGB指令，格式：
`R,G,B`
示例：

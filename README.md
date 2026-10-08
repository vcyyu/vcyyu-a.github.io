# ESP8266 MQTT 远程RGB灯带控制系统（LightMQTT）
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
1. WS2812 数据引脚 → ESP8266 DATA_PIN（GPIO15）
2. WS2812 GND 必须与ESP开发板 **共地**
3. OLED SDA → D2（GPIO4），SCL → D1（GPIO5）
4. 按键KEY_PIN → ESP8266 D6(GPIO12)
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
if (r == 0 && g == 0 && b == 0) return "OFF";
  if (r == 255 && g == 0 && b == 0) return "RED";
  if (r == 255 && g == 50 && b == 0) return "ORANGE";
  if (r == 255 && g == 255 && b == 0) return "YELLOW";
  if (r == 0 && g == 255 && b == 0) return "GREEN";
  if (r == 0 && g == 255 && b == 250) return "CYAN";
  if (r == 0 && g == 0 && b == 255) return "BLUE";
  if (r == 255 && g == 0 && b == 255) return "PURPLE";
  if (r == 166 && g == 166 && b == 166) return "WHITE";
  if (r == 255 && g == 35 && b == 30) return "PINK";
  if (r == 40 && g == 180 && b == 220) return "AZURE";
  if (r == 0 && g == 255 && b == 55) return "SPRING";
  if (r == 110 && g == 120 && b == 127) return "COOLWHITE";
  return "UNKNOWN";
<img width="733" height="801" alt="06ac054c-7894-4b51-a977-3d68c208d6d0" src="https://github.com/user-attachments/assets/79bc385b-69d8-45ee-814e-e2889f27b42a" />
<img width="1080" height="1440" alt="微信图片_20261008185249_194_314" src="https://github.com/user-attachments/assets/28b01a79-3935-4a08-a7f1-5aac5bf9089f" />
<img width="1080" height="1440" alt="微信图片_20261008185246_193_314" src="https://github.com/user-attachments/assets/a7e9b9bb-6541-40da-ab62-820282744808" />
<img width="1440" height="1080" alt="微信图片_20261008191622_196_314" src="https://github.com/user-attachments/assets/bc143de0-b7b8-4fae-bbd8-65220e54d697" />

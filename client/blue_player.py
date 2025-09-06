import asyncio
import numpy as np
import sounddevice as sd
from bleak import BleakClient, BleakScanner

# ESP32 的 BLE 服务和特征值 UUID（需与设备端一致）
AUDIO_SERVICE_UUID = "00001234-0000-1000-8000-00805f9b34fb"
AUDIO_CHAR_UUID = "00005678-0000-1000-8000-00805f9b34fb"

# 音频配置（需与 ESP32 采样率一致）
SAMPLE_RATE = 16000
audio_buffer = []

# BLE 数据接收回调
def handle_audio_data(sender, data):
    # 将接收到的字节数据转换为 16bit 整数数组
    audio_chunk = np.frombuffer(data, dtype=np.int16)
    audio_buffer.extend(audio_chunk)

# 播放音频的协程
async def play_audio():
    sd.default.samplerate = SAMPLE_RATE
    sd.default.channels = 1
    while True:
        if len(audio_buffer) >= 1024:  # 积累到一定数据量再播放，避免卡顿
            chunk = np.array(audio_buffer[:1024], dtype=np.int16)
            sd.play(chunk)
            del audio_buffer[:1024]
        await asyncio.sleep(0.01)

# 主函数：扫描并连接 ESP32
async def main():
    print("扫描 ESP32-Audio 设备...")
    devices = await BleakScanner.discover()
    esp32_device = None
    for d in devices:
        if d.name == "ESP32-Audio":
            esp32_device = d
            break
    if not esp32_device:
        print("未找到 ESP32-Audio 设备")
        return

    print(f"连接到 {esp32_device.address}...")
    async with BleakClient(esp32_device.address) as client:
        print("连接成功，开始接收音频...")
        # 启用特征值通知
        await client.start_notify(AUDIO_CHAR_UUID, handle_audio_data)
        # 启动音频播放协程
        await play_audio()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("退出程序")
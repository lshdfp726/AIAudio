import sounddevice as sd

# 打印所有可用音频设备（重点看“输出”设备）
print(sd.query_devices()) 
import numpy as np
import sounddevice as sd
import queue
import threading
from lshWebsocket import ESP32AudioStream
import logging
import time
from collections import deque
from audioDispose import AudioDispose

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# 配置参数（需与ESP32匹配）
ESP32_IP = "192.168.3.62"  # ESP32的IP地址（在ESP32串口日志中查看）
ESP32_PORT = 12345
SAMPLE_RATE = 16000
CHANNELS = 1
BUFFER_SIZE = 1024  # 与ESP32发送的缓冲区大小一致

TARGET_BUFFER_DURATION = 0.3  # 目标缓冲区时长(秒)
MIN_BUFFER_DURATION = 0.1     # 最小缓冲区时长(秒)

class PlayAudio:
    def __init__(self, sample_rate=16000, channel=1):
        self.start_time = None
        self.total_samples = 0
        self.sample_rate = sample_rate
        self.channel = channel
        self.dtype = np.int16
        #创建音频处理工具类
        self.aDispose = AudioDispose(SAMPLE_RATE)
        self.adjustment_step = 0.05  #每次调整步长
        self.max_target = 1.0        # 最大 目标缓冲区时长
        self.min_target = 0.2        # 最小 目标缓冲区时长
        self.max_min = 0.3           # 最小缓冲区里面的 最大时长
        self.min_min = 0.05          # 最小缓冲区里面的 最小时长
        self.max_target_buffer_duration = TARGET_BUFFER_DURATION
        self.min_target_buffer_duration = MIN_BUFFER_DURATION
        self.last_play_time = 0.0  # 上一段音频结束时间
        self.buffer_history = deque(maxlen=20)  # 记录最近20次缓冲区时长
        
        self.ideal_buffer_duration = 0.3  # 理想缓冲时长（平衡延迟和稳定性）
        self.buffer_tolerance = 0.1  # 允许的缓冲偏差

        # 队列管理
        self.raw_queue = deque()  # 原始音频队列（未处理）
        self.processed_queue = queue.Queue()  # 处理后音频队列（待播放）

    def start_audio_playback(self, device_id=1):
        """在独立线程中启动音频播放"""
        event = threading.Event()

        def audio_thread():
            try:
                with sd.OutputStream(
                    samplerate=self.sample_rate,
                    channels=self.channel,
                    callback=self.audio_playback_callback,
                    dtype=np.int16,
                    blocksize=BUFFER_SIZE//2,
                    device=device_id
                ):
                    logger.info("音频播放已启动")
                    event.wait()  # 等待事件触发退出
            except Exception as e:
                logger.error(f"音频播放错误: {e}")

        thread = threading.Thread(target=audio_thread)
        thread.daemon = True
        thread.start()

        return event

    def get_current_time(self):
        """计算当前音频播放时间（秒），类似 Web Audio API 的 currentTime"""
        if not self.start_time:
            return 0.0
        return self.total_samples / self.sample_rate

    def get_buffer_duration(self):
        total_samples = sum(len(chunk) for chunk in self.raw_queue)
        return total_samples/self.sample_rate

    def adjust_thresholds(self):
        #记录历史音频数据播放时长
        if len(self.buffer_history) < 10: #
            return
        
        #计算历史数据播放时长平均值
        avg_duration = sum(self.buffer_history) / len(self.buffer_history)
        #调整最大缓冲区时常， 历史数据长期大于目标时长
        if avg_duration > self.max_target_buffer_duration * 1.3:
            #历史数据长期大于目标时长，增大目标阈值
            new_target = min(self.max_target_buffer_duration + self.adjustment_step, self.max_target)
            if new_target != self.max_target_buffer_duration:
                self.max_target_buffer_duration = new_target
                print(f"动态调整目标缓冲区至: {self.max_target_buffer_duration:.2f}s")
        elif avg_duration < self.max_target_buffer_duration * 0.7:
            # 缓冲区长期低于目标，减小目标阈值
            new_target = max(self.max_target_buffer_duration - self.adjustment_step, self.min_target)
            if new_target != self.max_target_buffer_duration:
                self.max_target_buffer_duration = new_target
                print(f"动态调整目标缓冲区至: {self.max_target_buffer_duration:.2f}s")

        # 调整最小缓冲区阈值（保持为目标阈值的1/3 ~ 1/2）
        ideal_min = self.min_target_buffer_duration * 0.3
        new_min = max(min(ideal_min, self.max_min), self.min_min)
        if abs(new_min - self.min_target_buffer_duration) > 0.01:
            self.min_target_buffer_duration = new_min
            print(f"动态调整最小缓冲区至: {self.min_target_buffer_duration:.2f}s")

    def adjust_thresholds_based_on_lag(self):
        """根据数据超前/滞后调整阈值"""
        current_time = self.get_current_time()
        # 计算当前缓冲数据的理论时长（已处理但未播放的部分）
        buffered_duration = self.last_play_time - current_time
        
        # 数据滞后（缓冲不足）：降低最小阈值，更早触发处理
        if buffered_duration < (self.ideal_buffer_duration - self.buffer_tolerance):
            self.min_target_buffer_duration = max(
                self.min_target_buffer_duration - 0.02,
                self.min_min  # 最低0.05s
            )
            print(f"缓冲不足，降低最小阈值至 {self.MIN_BUFFER_DURATION:.2f}s")
        
        # 数据超前（缓冲过多）：提高目标阈值，减少处理频率
        elif buffered_duration > (self.ideal_buffer_duration + self.buffer_tolerance):
            self.max_target_buffer_duration = min(
                self.max_target_buffer_duration + 0.02,
                self.max_target  # 最高1.0s
            )
            print(f"缓冲过多，提高目标阈值至 {self.max_target_buffer_duration:.2f}s")
            
    #音频块处理
    def process_audio(self):
        total_length = sum(len(chunk) for chunk in self.raw_queue)
        if total_length == 0:
            return 0.0
        #合并
        merged = np.concatenate(list(self.raw_queue), axis=0).astype(self.dtype)
        self.raw_queue.clear()
        
        filtered = self.aDispose.process_audio(merged)
        
        self.processed_queue.put(filtered)
        
        # 记录缓冲区历史用于动态调整
        processed_duration = len(filtered) / self.sample_rate
        self.buffer_history.append(processed_duration)
        self.adjust_thresholds()        # 记录缓冲区历史用于动态调整

        #self.adjust_thresholds_based_on_lag(), 先验证不加的效果
        return processed_duration

    ### 音频播放 ###
    def audio_playback_callback(self, outdata, frames, pa_time, status):
        # 初始化时间跟踪
        if self.start_time is None:
            self.start_time = time.time()
            self.last_play_time = 0.0
        
        #满足设置的最大最小目标缓冲区时常才会进行数据处理播放流程
        buffer_duration = self.get_buffer_duration()
        if buffer_duration >= self.max_target_buffer_duration or buffer_duration < self.min_target_buffer_duration:
            processed_duration = self.process_audio()
            if processed_duration > 0:
                self.last_play_time = max(self.get_current_time(), self.last_play_time)
                self.last_play_time += processed_duration

        #播放调整后的数据，如果没进上面处理逻辑 ，self.processed_queue 里面的数据是空的，下面就直接填0变成静音了
        try:
            processed_data = self.processed_queue.get_nowait()
            if len(processed_data) < frames:
                outdata[:len(processed_data), 0] = processed_data #实际数据不足，先填充前半部分
                outdata[len(processed_data), 0] = 0 #后半部分填充0，静音
            else:
                outdata[:, 0] = processed_data[:frames] #填满
                remaining = processed_data[frames:] #剩余的塞回去。。。
                if len(remaining) > 0:
                    self.processed_queue.put(remaining)
                
                self.total_samples += frames
        except queue.Empty:
            outdata.fill(0)

    
    def open_callback(self):
        self.start_time = time.time()
        self.last_play_time = 0.0
    
    # 设置音频回调函数
    def audio_callback(self, data: bytes):
        try:
            # 假设是16位有符号整数，小端字节序
            chunk_np = np.frombuffer(data, dtype=np.int16)
            self.raw_queue.append(chunk_np)
        except Exception as e:
            print(f"音频块格式错误: {e}")

    # 设置错误回调函数
    def error_callback(self,error_msg: str):
        logger.error(f"错误: {error_msg}")


### 主函数 ###
def main():
    WS_URL = f"ws://{ESP32_IP}:{ESP32_PORT}/"

    # 创建音频流处理器
    stream = ESP32AudioStream(ws_url=WS_URL)

    playAudio = PlayAudio(SAMPLE_RATE, CHANNELS)

    stream.set_audio_callback(playAudio.audio_callback)
    stream.set_error_callback(playAudio.error_callback)
    stream.set_open_callback(playAudio.open_callback)

    try:
        # 启动音频流
        stream.start()
        logger.info("按Ctrl+C停止录音...")

        stop_event = playAudio.start_audio_playback()

        while True:
            time.sleep(1)

    except KeyboardInterrupt:
        # 停止音频流并保存文件
        stream.stop()
        stop_event.set()  # 通知播放线程退出


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("程序已停止")
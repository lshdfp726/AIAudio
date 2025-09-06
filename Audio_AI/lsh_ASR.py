import numpy as np
import logging
import whisper
from scipy.signal import resample, butter, lfilter  # 用于滤波和重采样
from scipy.io import wavfile  # 用于保存调试音频

import librosa
import matplotlib.pyplot as plt

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

class ASRTransform:
    def __init__(self, language="zh"):
        self.language = language
        # 1. 改用small模型（平衡识别精度与速度，比base更擅长处理模糊语音）
        self.model = whisper.load_model("small")
        logger.info("Whisper small模型加载完成")

    def butter_bandpass_filter(self, data, lowcut, highcut, fs, order=5):
        """带通滤波器：保留人声频率（300-3400Hz，人类语音核心频率范围），过滤高低频噪音"""
        nyq = 0.5 * fs  # 奈奎斯特频率（采样率的一半）
        low = lowcut / nyq
        high = highcut / nyq
        b, a = butter(order, [low, high], btype='band')  # 设计带通滤波器
        filtered_data = lfilter(b, a, data)
        return filtered_data

    def normalize_audio(self, data):
        """音频归一化：统一音量范围，避免音量波动导致识别误差"""
        # 计算音频的峰值
        peak = np.max(np.abs(data))
        if peak == 0:
            return data
        # 将峰值归一化到0.9（避免削波失真，保留少量余量）
        return data * 0.9 / peak

    def remove_silence(self, data, fs, threshold=0.01, min_silence_duration=0.01):
        """去除静音段：减少无意义静音对模型的干扰"""
        data = data.astype(np.float32)
        # 计算每个采样点的能量（音量）
        energy = np.square(data)
        # 滑动窗口判断静音（窗口大小=0.02秒，约320个采样点@16kHz）
        window_size = int(fs * 0.02)
        energy_windowed = np.convolve(energy, np.ones(window_size)/window_size, mode='same')
        
        # 标记非静音段（能量超过阈值）
        non_silence_mask = energy_windowed > threshold
        # 找到所有非静音段的起始和结束位置, 相邻元素差分计算，结果长度少1
        diff = np.diff(non_silence_mask.astype(int))

        # 2. 提取状态变化点：1=静音→非静音（起始），-1=非静音→静音（结束）
        rise_edges = np.where(diff == 1)[0]  # 上升沿（静音→非静音）的索引（diff的索引）
        fall_edges = np.where(diff == -1)[0] # 下降沿（非静音→静音）的索引（diff的索引）
        
        # 3. 补全边界：处理“开头是非静音”或“结尾是非静音”的情况
        start_indices = []
        end_indices = []

        # 情况A：音频开头就是非静音（non_silence_mask[0]为True）→ 补第一个起始点为0
        if non_silence_mask[0]:
            start_indices.append(0)

        # 情况B：添加正常的起始和结束（上升沿对应start，下降沿对应end）
        # 注意：diff的索引i对应non_silence_mask的i和i+1，所以实际位置要+1
        start_indices.extend(rise_edges + 1)
        end_indices.extend(fall_edges + 1)

        # 情况C：音频结尾是非静音（non_silence_mask[-1]为True）→ 补最后一个结束点为数据长度
        if non_silence_mask[-1]:
            end_indices.append(len(data))

        # 4. 关键：确保start和end长度一致（防止错位）
        # 若长度仍不一致（极端情况），取较短的长度截断（避免zip配对错误）
        min_len = min(len(start_indices), len(end_indices))
        start_indices = start_indices[:min_len]
        end_indices = end_indices[:min_len]

        # 过滤过短的静音段（避免误删语音中的短暂停顿）
        if len(start_indices) == 0:
            return data  # 全是静音，返回原始数据
        if len(end_indices) == 0:
            end_indices = [len(data)]
        
        # 合并非静音段
        # 5. 过滤过短的非静音段
        non_silence_data = []
        for i, (start, end) in enumerate(zip(start_indices, end_indices)):
            # 安全校验：防止start > end（极端情况）
            if start >= end:
                logger.warning(f"跳过异常片段{i+1}：start={start} ≥ end={end}")
                continue
            # 计算片段时长
            segment_duration = (end - start) / fs
            # logger.info(f"非静音段{i+1}：起始={start}，结束={end}，时长={segment_duration:.3f}秒")
            # 保留超过最小时长的片段
            if segment_duration > min_silence_duration:
                non_silence_data.append(data[start:end])
            else:
                pass
                # logger.info(f"非静音段{i+1}因时长过短（{segment_duration:.3f}秒）被过滤")
        
        if not non_silence_data:
            return data  # 无有效非静音段，返回原始数据
        return np.concatenate(non_silence_data)

    def pcmToText(self, pcm_stream, sample_rate=8000, sample_width=2, channels=1):
        try:
            # -------------------------- 1. 基础PCM转换 --------------------------
            # 将PCM字节流转换为16位整数数组（原始音频数据）
            audio_int16 = np.frombuffer(pcm_stream, dtype=np.int16)
            logger.info(f"原始音频长度：{len(audio_int16)} 采样点，采样率：{sample_rate}Hz")

            # -------------------------- 2. 多声道转单声道 --------------------------
            if channels > 1:
                logger.info(f"将{channels}声道转换为单声道")
                # 重塑为（采样组数，声道数），再对声道求平均
                audio_int16 = audio_int16.reshape(-1, channels).mean(axis=1).astype(np.int16)

            # -------------------------- 3. 重采样到16kHz（Whisper最优输入） --------------------------
            if sample_rate != 16000:
                logger.info(f"从{sample_rate}Hz重采样到16000Hz")
                # 计算重采样后的采样点数（保证时长不变）
                num_samples = int(len(audio_int16) * 16000 / sample_rate)
                audio_resampled = resample(audio_int16, num_samples)
            else:
                audio_resampled = audio_int16.astype(np.float32)

            # -------------------------- 4. 核心音频增强（关键步骤） --------------------------
            # 4.1 带通滤波：保留人声频率（300-3400Hz），过滤低频噪音（如电流声）和高频噪音（如尖锐杂音）
            audio_filtered = self.butter_bandpass_filter(audio_resampled, 300, 3400, 16000)
            # 4.2 归一化：统一音量，避免忽大忽小
            audio_normalized = self.normalize_audio(audio_filtered)
            # # 4.3 去除静音段：减少无意义静音对模型的干扰
            # audio_cleaned = self.remove_silence(audio_normalized, 16000)

            # -------------------------- 5. 保存调试音频（验证处理效果） --------------------------
            # 保存处理后的音频为WAV文件，手动听是否清晰
            # wavfile.write("debug_cleaned_audio.wav", 16000, audio_cleaned.astype(np.float32))
            # logger.info("处理后的音频已保存到：debug_cleaned_audio.wav")

            audio_data = audio_normalized.astype(np.float32)

            # -------------------------- 6. Whisper识别参数调优 --------------------------
            result = self.model.transcribe(
                audio_data,
                language=self.language,
                fp16=False,  # 非NVIDIA显卡禁用
                no_speech_threshold=0.3,  # 降低“无语音”判断阈值（更灵敏捕捉弱语音）
                temperature=0.1,  # 降低随机性（减少模型对“lalala”这类无意义结果的偏好）
                # initial_prompt="这是一段中文语音，内容可能包含日常对话、天气预报等，请注意识别准确的中文词汇。",  # 给模型提示，引导正确识别
                word_timestamps=False  # 关闭词级时间戳，加快识别速度
            )

            # -------------------------- 7. 结果处理 --------------------------
            text = result["text"].strip()
            logger.info(f"最终识别结果：{text}")
            # 若结果仍为“lalala”，提示检查原始音频
            if text.lower() in ["lalala", "la la", "啦啦啦", ""]:
                return "识别结果异常（可能原始音频噪音过大或语音不清晰），请先检查 debug_cleaned_audio.wav 是否清晰"
            return text

        except Exception as e:
            logger.error(f"pcmToText error: {str(e)}", exc_info=True)
            return f"处理错误: {str(e)}"


    def mel(self, pcmData, sample=16000,n_mels=80):
        n_fft = 512 #傅立叶变换的单位片段采样点数
        hop_length = 160 #分帧： 10ms in 16000
        mes_spectrogram = librosa.feature.melspectrogram(
            y = pcmData,
            sr = sample,
            n_fft = n_fft,
            hop_length = hop_length,
            n_mels = n_mels
        )
        #ref=np.max 表示以这段音频中最大能量最为0db点的参考值
        # db = 10 * log10(P/Pref) , P表示当前信号了，Pref 人为定义的参考量
        mel_spectrogram_db = librosa.power_to_db(mes_spectrogram, ref=np.max)
        
        #可视化
        plt.figure(figsize=(10, 4))
        librosa.display.specshow(mel_spectrogram_db, sr=sample,hop_length=hop_length,x_axis='time',y_axis='mel')
        plt.colorbar(format='%2.0f dB')
        plt.title('Mel Spectrogram')
        plt.show()

# 测试代码
if __name__ == '__main__':
    pcm_data = np.fromfile("./demo/Audio_AI/test.pcm", dtype=np.int16)
    audio = pcm_data.astype(np.float32) / 32768.0
    transform = ASRTransform(language="zh")
    transform.mel(audio)

    import time
    while True:
        time.sleep(3)
    # 替换为你的PCM文件路径
    # pcm_file = "./demo/Audio_AI/test.pcm"
    # with open(pcm_file, "rb") as f:
    #     pcm_binary = f.read()

    # transform = ASRTransform(language="zh")
    # result = transform.pcmToText(pcm_binary, sample_rate=16000, sample_width=2, channels=1)
    # print(f"最终转换结果：{result}")






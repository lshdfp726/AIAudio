import numpy as np
import logging
import time
import scipy.signal as signal
import soundfile as sf
import noisereduce as nr

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# 音频处理参数
DC_ALPHA = 0.05 #直流偏移系数，直流偏移是指音频信号整体偏离零电平（横轴）的现象
NOISE_THRESHOLD = 600
"""
若信号动态范围大（如音乐）：建议设为 25000 ~ 28000，保留更多动态空间。
若信号以语音为主（动态范围小）：可设为 28000 ~ 30000，提升音量的同时不易失真。
若设备对过载敏感：可降低至 20000 ~ 25000，优先保证稳定性。
"""
TARGET_PEAK = 28000
dc_offset = 0.0  # 全局直流偏移变量

class AudioDispose:
    def __init__(self, sample_rate, highpass_cutoff=80, lowpass_cutoff=17000, q=1.0):
        self.sample_rate = sample_rate
        self.noise_baseline = 30  # 初始基线
        self.baseline_update_rate = 0.01  # 基线更新速率（缓慢跟踪）
        self.dc_offset = 0.0
        self.calibrating = True  # 校准标志
        self.calibrate_count = 0
        self.calibrate_max = int(sample_rate * 0.1)  # 100ms校准数据
        self.nyquist = 0.5 * sample_rate  #奈奎斯特采样，2倍采样频率系数
        self.highpass_b, self.highpass_a = self._design_biquad_filter(highpass_cutoff, 'highpass', q)
        self.lowpass_b, self.lowpass_a = self._design_biquad_filter(lowpass_cutoff, 'lowpass', q)
        self.highpass_state = np.zeros(max(len(self.highpass_a), len(self.highpass_b)) - 1)
        self.lowpass_state = np.zeros(max(len(self.lowpass_a), len(self.lowpass_b)) - 1)
        
        self.noise_update_ratio = 2  # 新噪声样本的权重（0~1）
        self.noise_sample = None  # 动态更新的噪声样本
        self.bit_depth = 16


    def _design_biquad_filter(self, cutoff, filter_type, q):
        normalized_cutoff =  cutoff/ self.nyquist
        if filter_type == 'highpass':
            w0 = 2 * np.pi * normalized_cutoff
            alpha = np.sin(w0) / (2 * q)
            b0 = (1 + np.cos(w0)) / 2
            b1 = -(1 + np.cos(w0))
            b2 = (1 + np.cos(w0)) / 2
            a0 = 1 + alpha
            a1 = -2 * np.cos(w0)
            a2 = 1 - alpha
        elif filter_type == 'lowpass':
            w0 = 2 * np.pi * normalized_cutoff
            alpha = np.sin(w0) / (2 * q)
            b0 = (1 - np.cos(w0)) / 2
            b1 = 1 - np.cos(w0)
            b2 = (1 - np.cos(w0)) / 2
            a0 = 1 + alpha
            a1 = -2 * np.cos(w0)
            a2 = 1 - alpha
        else:
            raise ValueError("滤波器类型必须是 'highpass' 或 'lowpass'")

        b = np.array([b0, b1, b2]) / a0
        a = np.array([1, a1/a0, a2/a0])

        return b, a


    def _process_filter(self, audio_chunk):
        filter_high, self.highpass_state = signal.lfilter(
            self.highpass_b, self.highpass_a,
            audio_chunk,
            zi=self.highpass_state
        )
        
        filter_final, self.lowpass_state = signal.lfilter(
            self.lowpass_b, self.lowpass_a,
            filter_high,
            zi=self.lowpass_state
        )
        
        return filter_final
        
    ### 音频处理函数（与之前相同）###
    def _remove_dc_offset(self, samples):
        if self.calibrating:
            # 校准阶段：用前100ms数据计算初始偏移
            self.dc_offset = np.mean(samples)
            self.calibrate_count += len(samples)
            if self.calibrate_count >= self.calibrate_max:
                self.calibrating = False
            return samples - self.dc_offset
        # 正常阶段：平滑更新偏移
        self.dc_offset = (1 - DC_ALPHA) * self.dc_offset + DC_ALPHA * np.mean(samples)
        return samples - self.dc_offset

    def _reduce_noise(self, samples):
        # 动态更新噪声基线（仅当信号较小时，认为是噪声）
        is_noise = np.abs(samples) < self.noise_baseline * 1.5  # 1.5倍基线以下视为纯噪声
        if np.any(is_noise):
            self.noise_baseline = (1 - self.baseline_update_rate) * self.noise_baseline + \
                                self.baseline_update_rate * np.mean(np.abs(samples[is_noise]))
        # 阈值设为基线的2倍（可根据需要调整）
        threshold = self.noise_baseline * 2
        # 低于阈值的信号直接置0（避免非线性失真）
        samples[np.abs(samples) < threshold] = 0
        return samples.astype(np.int16)

    def _boost_volume(self, samples):
        """
        动态统计每一帧数据最大峰值，在拿目标峰值计算得出比例系数
        在对当前采样点每一帧进行增益缩放，并且约束在16位有符号范围内，避免溢出
        """
        current_peak = np.max(np.abs(samples))
        if current_peak == 0:
            return samples
        gain = TARGET_PEAK / current_peak
        return np.clip(samples * gain, -32768, 32767).astype(np.int16)

    def _smooth_signal(self, samples, window_size=5):
        # 简单滑动平均（窗口大小5，可调整）
        return np.convolve(samples, np.ones(window_size)/window_size, mode='same').astype(np.int16)
    

    def _detect_silent_segment(self, audio_data, threshold=0.05, min_len=0.5):
        """检测音频中的静音片段（能量低于阈值的部分）"""
        # 计算能量（振幅平方）
        energy = np.square(audio_data)
        # 找能量低于阈值的索引
        silent_indices = np.where(energy < threshold)[0]
        if len(silent_indices) == 0:
            return None  # 未找到静音片段
        
        # 取最长的静音片段（至少min_len秒）
        min_samples = int(min_len * self.sample_rate)
        silent_runs = []
        current_run = [silent_indices[0]]
        for idx in silent_indices[1:]:
            if idx == current_run[-1] + 1:
                current_run.append(idx)
            else:
                silent_runs.append(current_run)
                current_run = [idx]
        silent_runs.append(current_run)
        
        # 筛选出足够长的静音片段
        for run in silent_runs:
            if len(run) >= min_samples:
                return audio_data[run[:min_samples]]  # 返回前min_samples个样本
        return None
    # def process_segment(self, segment):
    #     """处理单段音频，动态更新噪声样本"""
    #     # 1. 预处理：归一化音频
    #     audio_data = segment.copy().astype(np.float32)
    #     if np.max(np.abs(audio_data)) > 1.0:
    #         audio_data /= (2 **(self.bit_depth - 1))
        
    #     # 2. 检测当前段的静音片段，用于更新噪声样本
    #     current_noise = self._detect_silent_segment(audio_data)
    #     if current_noise is not None:
    #         if self.noise_sample is None:
    #             # 首次初始化噪声样本
    #             self.noise_sample = current_noise
    #         else:
    #             # 动态更新（加权平均，平滑噪声变化）
    #             self.noise_sample = (
    #                 self.noise_sample * (1 - self.noise_update_ratio) +
    #                 current_noise * self.noise_update_ratio
    #             )
        
    #     # 3. 若仍无噪声样本（如第一段无静音），退而求其次用开头片段
    #     if self.noise_sample is None:
    #         noise_len = int(0.5 * self.sample_rate)  # 用0.5秒作为临时噪声
    #         self.noise_sample = audio_data[:noise_len].copy()
        
    #     # 4. 降噪
    #     denoised = nr.reduce_noise(
    #         y=audio_data,
    #         sr=self.sample_rate,
    #         y_noise=self.noise_sample
    #     )
        
    #     # 5. 转换回原始格式
    #     denoised = denoised * (2** (self.bit_depth - 1) - 1)
    #     return denoised.astype(segment.dtype)
    # def denoise_audio_from_memory(self, merged_audio, sample_rate=16000, bit_depth=16,
    #                          noise_sample_duration=1.0):
    #     """
    #     对内存中的音频数据进行降噪处理
        
    #     参数:
    #     merged_audio (np.ndarray): 内存中的音频数据（如merged变量）
    #     sample_rate (int): 采样率(Hz)
    #     bit_depth (int): 位深
    #     noise_sample_duration (float): 用于提取噪声样本的时长(秒)
    #     """
    #     # 复制原始数据以避免修改源数据
    #     audio_data = merged_audio.copy()
        
    #     # 归一化到[-1, 1]范围（如果尚未归一化）
    #     if np.max(np.abs(audio_data)) > 1.0:
    #         audio_data = audio_data.astype(np.float32) / (2 **(bit_depth - 1))
        
    #     # 如果是立体声，转为单声道处理
    #     if len(audio_data.shape) > 1:
    #         audio_data = np.mean(audio_data, axis=1)
        
    #     # 提取噪声样本（取音频开头的一段作为噪声参考）
    #     noise_sample_length = int(noise_sample_duration * sample_rate)
    #     # 确保噪声样本长度不超过音频总长度
    #     noise_sample_length = min(noise_sample_length, len(audio_data))
    #     noise_sample = audio_data[:noise_sample_length]
        
    #     # 进行降噪处理
    #     denoised_audio = nr.reduce_noise(
    #         y=audio_data,           # 音频数据
    #         sr=sample_rate,         # 采样率
    #         y_noise=noise_sample    # 噪声样本
    #     )
        
    #     # 将降噪后的音频转换回原始位深
    #     denoised_audio = denoised_audio * (2** (bit_depth - 1) - 1)
    #     denoised_audio = denoised_audio.astype(merged_audio.dtype)
            
    #     return denoised_audio

    def process_audio(self, samples):

        samples = self._remove_dc_offset(samples)
        samples = self._reduce_noise(samples)
        # samples = self._boost_volume(samples)
        samples = self._process_filter(samples)
        
        # samples = self.process_segment(samples)
        return samples
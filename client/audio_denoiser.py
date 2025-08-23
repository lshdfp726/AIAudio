import numpy as np
import soundfile as sf
import noisereduce as nr
import matplotlib.pyplot as plt

def read_pcm_file(file_path, sample_rate=16000, bit_depth=16, channels=1):
    """读取PCM格式音频文件"""
    # 根据位深确定数据类型
    dtype = {
        8: np.uint8,
        16: np.int16,
        32: np.int32,
        64: np.int64
    }.get(bit_depth, np.int16)
    
    # 读取PCM原始数据
    with open(file_path, 'rb') as f:
        pcm_data = np.frombuffer(f.read(), dtype=dtype)
    
    # 归一化到[-1, 1]范围
    pcm_data = pcm_data.astype(np.float32) / (2 **(bit_depth - 1))
    
    # 如果是多声道，进行处理
    if channels > 1:
        pcm_data = pcm_data.reshape(-1, channels)
    
    return pcm_data, sample_rate

def write_pcm_file(audio_data, file_path, bit_depth=16):
    """将音频数据写入PCM文件"""
    # 将归一化数据转换回原始位深
    dtype = {
        8: np.uint8,
        16: np.int16,
        32: np.int32,
        64: np.int64
    }.get(bit_depth, np.int16)
    
    # 反归一化
    audio_data = audio_data * (2** (bit_depth - 1) - 1)
    audio_data = audio_data.astype(dtype)
    
    # 写入文件
    with open(file_path, 'wb') as f:
        f.write(audio_data.tobytes())

def denoise_audio(input_file, output_file, is_pcm=False, 
                 sample_rate=16000, bit_depth=16, channels=1,
                 noise_sample_duration=1.0):
    """
    对音频文件进行降噪处理，支持PCM和常见音频格式
    
    参数:
    input_file (str): 输入音频文件路径
    output_file (str): 输出降噪后音频文件路径
    is_pcm (bool): 是否为PCM格式
    sample_rate (int): 采样率(Hz)，PCM文件需要指定
    bit_depth (int): 位深，PCM文件需要指定
    channels (int): 声道数，PCM文件需要指定
    noise_sample_duration (float): 用于提取噪声样本的时长(秒)
    """
    # 读取音频文件
    if is_pcm:
        audio_data, sample_rate = read_pcm_file(
            input_file, sample_rate, bit_depth, channels
        )
    else:
        audio_data, sample_rate = sf.read(input_file)
    
    # 如果是立体声，转为单声道处理
    if len(audio_data.shape) > 1:
        audio_data = np.mean(audio_data, axis=1)
    
    # 提取噪声样本（取音频开头的一段作为噪声参考）
    noise_sample_length = int(noise_sample_duration * sample_rate)
    noise_sample = audio_data[:noise_sample_length]
    
    # 进行降噪处理 - 适配当前版本的参数名称y_noise
    denoised_audio = nr.reduce_noise(
        y=audio_data,           # 音频数据
        sr=sample_rate,         # 新增采样率参数（根据你的函数定义需要此参数）
        y_noise=noise_sample,   # 噪声样本（使用当前版本的参数名称）
    )
    
    # 保存降噪后的音频
    if is_pcm:
        write_pcm_file(denoised_audio, output_file, bit_depth)
    else:
        sf.write(output_file, denoised_audio, sample_rate)
    
    print(f"降噪完成，已保存至 {output_file}")
    
    # 绘制原始音频和降噪后音频的波形图对比
    plot_waveforms(audio_data, denoised_audio, sample_rate)

def plot_waveforms(original, denoised, sample_rate):
    """绘制原始音频和降噪后音频的波形图"""
    time = np.linspace(0, len(original)/sample_rate, len(original))
    
    plt.figure(figsize=(12, 8))
    
    plt.subplot(2, 1, 1)
    plt.plot(time, original)
    plt.title('原始音频')
    plt.xlabel('时间 (秒)')
    plt.ylabel('振幅')
    
    plt.subplot(2, 1, 2)
    plt.plot(time, denoised)
    plt.title('降噪后音频')
    plt.xlabel('时间 (秒)')
    plt.ylabel('振幅')
    
    plt.tight_layout()
    plt.show()

if __name__ == "__main__":
    # 处理PCM文件示例
    denoise_audio(
        input_file="input.pcm",
        output_file="output_denoised.pcm",
        is_pcm=True,
        sample_rate=16000,  # 根据实际PCM文件的采样率设置
        bit_depth=16,       # 根据实际PCM文件的位深设置
        channels=1,         # 根据实际PCM文件的声道数设置
        noise_sample_duration=0.2
    )
    
    # 处理普通音频文件示例（如WAV）
    # denoise_audio(
    #     input_file="input.wav",
    #     output_file="output_denoised.wav",
    #     is_pcm=False
    # )
    
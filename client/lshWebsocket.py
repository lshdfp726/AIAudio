import websocket
import threading
import time
import wave
import logging
import numpy as np
from typing import Callable, Optional
import json

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

class ESP32AudioStream:
    """ESP32S3音频流处理模块"""
    
    def __init__(self, ws_url: str, sample_rate: int = 16000, 
                 channels: int = 1, sample_width: int = 2):
        """
        初始化音频流处理器
        
        Args:
            ws_url: ESP32S3的WebSocket服务器地址
            sample_rate: 采样率(Hz)
            channels: 声道数
            sample_width: 采样宽度(字节)
        """
        self.ws_url = ws_url
        self.sample_rate = sample_rate
        self.channels = channels
        self.sample_width = sample_width
        self.ws = None
        self.is_running = False
        self.audio_callback = None
        self.error_callback = None
        self._connection_lock = threading.Lock()
        self._reconnect_attempts = 0
        self._max_reconnect_attempts = 10
        
        # websocket.enableTrace(True)
        
    def set_audio_callback(self, callback: Callable[[bytes], None]):
        """
        设置音频数据回调函数
        
        Args:
            callback: 音频数据回调函数，接收bytes类型的音频数据
        """
        self.audio_callback = callback
        
    def set_error_callback(self, callback: Callable[[str], None]):
        """
        设置错误回调函数
        
        Args:
            callback: 错误回调函数，接收错误信息字符串
        """
        self.error_callback = callback
    
    def set_open_callback(self, callback: Callable[[str], None]):
        """_summary_
        websocket 连接上回调
        Args:
            callback 连接建立回调
        """
        self.open_callback = callback
        
    def start(self):
        """启动音频流接收"""
        with self._connection_lock:
            if not self.is_running:
                self.is_running = True
                self._connect()
                logger.info(f"音频流已启动，连接到: {self.ws_url}")
                
    def stop(self):
        """停止音频流接收"""
        with self._connection_lock:
            self.is_running = False
            if self.ws:
                try:
                    self.ws.close()
                except Exception as e:
                    logger.error(f"关闭WebSocket连接失败: {e}")
                self.ws = None
            logger.info("音频流已停止")
            
    def _connect(self):
        """建立WebSocket连接"""
        try:
            self.ws = websocket.WebSocketApp(
                self.ws_url,
                on_open=self._on_open,
                on_message=self._on_message,
                on_error=self._on_error,
                on_close=self._on_close
            )
                # 启动时配置自动Ping：每30秒发一次Ping，等待10秒超时
            wst = threading.Thread(
                target=lambda: self.ws.run_forever(
                    ping_interval=30,  # 每隔30秒发送一次Ping
                    ping_timeout=10    # 等待Pong的超时时间（秒）
                )
            )
            
            # wst = threading.Thread(target=self.ws.run_forever)
            wst.daemon = True
            wst.start()
            
            self._reconnect_attempts = 0
            
        except Exception as e:
            self._handle_error(f"连接失败: {e}")
            self._schedule_reconnect()
            
    def _on_open(self, ws):
        """连接建立回调"""
        logger.info("WebSocket连接已建立")
        self._reconnect_attempts = 0
        if self.open_callback:
            self.open_callback()
        # 发送启动音频流命令
        # self._send_command("start_audio")
        
    def _on_message(self, ws, message):
        """消息接收回调"""
        if isinstance(message, bytes):
            # 处理二进制音频数据
            if self.audio_callback:
                try:
                    self.audio_callback(message)
                except Exception as e:
                    self._handle_error(f"处理音频数据失败: {e}")
            else:
                logger.warning("未设置音频回调函数，丢弃数据")
        else:
            # 处理文本消息
            logger.info(f"收到文本消息: {message}")
            
    def _on_error(self, ws, error):
        """错误回调"""
        self._handle_error(f"WebSocket错误: {error}")
        
    def _on_close(self, ws, close_status_code, close_msg):
        """连接关闭回调"""
        logger.info(f"WebSocket连接已关闭: {close_status_code} {close_msg}")
        self.ws = None
        if self.is_running:
            self._schedule_reconnect()
            
    def _handle_error(self, error_msg: str):
        """处理错误并触发回调"""
        logger.error(error_msg)
        if self.error_callback:
            self.error_callback(error_msg)
            
    def _schedule_reconnect(self):
        """安排重连"""
        self._reconnect_attempts += 1
        if self._reconnect_attempts <= self._max_reconnect_attempts:
            delay = min(2 ** self._reconnect_attempts, 30)  # 指数退避
            logger.info(f"{delay}秒后尝试重连 ({self._reconnect_attempts}/{self._max_reconnect_attempts})")
            threading.Timer(delay, self._connect).start()
        else:
            self._handle_error("达到最大重连次数，停止尝试")
            self.is_running = False
            
    def _send_command(self, command: str, params: Optional[dict] = None):
        if self.ws and self.ws.sock and self.ws.sock.connected:
            try:
                # 改为发送纯文本命令
                self.ws.send(command)  # 直接发送字符串
                logger.debug(f"发送命令: {command}")
                return True
            except Exception as e:
                self._handle_error(f"发送命令失败: {e}")
                return False
        else:
            self._handle_error("WebSocket未连接，命令发送失败")
            return False

# 使用示例
if __name__ == "__main__":
   pass
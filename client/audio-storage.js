// 音频数据存储工具类
export class AudioStorage {
  constructor() {
    this.dbName = 'AudioDatabase';
    this.storeName = 'audioRecords';
    this.version = 1;
    this.db = null;
    
    // 初始化数据库
    this.initDB();
  }
  
  // 初始化IndexedDB数据库
  initDB() {
    return new Promise((resolve, reject) => {
      const request = indexedDB.open(this.dbName, this.version);
      
      // 数据库版本升级时调用
      request.onupgradeneeded = (event) => {
        this.db = event.target.result;
        
        // 如果存储对象不存在则创建
        if (!this.db.objectStoreNames.contains(this.storeName)) {
          const objectStore = this.db.createObjectStore(this.storeName, { 
            keyPath: 'id',
            autoIncrement: true 
          });
          
          // 创建索引，便于查询
          objectStore.createIndex('timestamp', 'timestamp', { unique: false });
        }
      };
      
      request.onsuccess = (event) => {
        this.db = event.target.result;
        resolve(this.db);
      };
      
      request.onerror = (event) => {
        console.error('IndexedDB初始化错误:', event.target.error);
        reject(event.target.error);
      };
    });
  }
  
  // 存储音频数据
  storeAudioData(data, metadata = {}) {
    if (!this.db) {
      return this.initDB().then(() => this.storeAudioData(data, metadata));
    }
    
    return new Promise((resolve, reject) => {
      // 创建事务
      const transaction = this.db.transaction([this.storeName], 'readwrite');
      const store = transaction.objectStore(this.storeName);
      
      // 准备要存储的数据
      const audioRecord = {
        data: data,          // 音频数据
        timestamp: Date.now(), // 时间戳
        metadata: metadata   // 附加元数据（如采样率、格式等）
      };
      
      // 存储记录
      const request = store.add(audioRecord);
      
      request.onsuccess = () => {
        console.log('音频数据存储成功，ID:', request.result);
        resolve(request.result);
      };
      
      request.onerror = () => {
        console.error('音频数据存储失败:', request.error);
        reject(request.error);
      };
    });
  }
  
  // 获取所有音频记录
  getAllAudioRecords() {
    if (!this.db) {
      return this.initDB().then(() => this.getAllAudioRecords());
    }
    
    return new Promise((resolve, reject) => {
      const transaction = this.db.transaction([this.storeName], 'readonly');
      const store = transaction.objectStore(this.storeName);
      const request = store.getAll();
      
      request.onsuccess = () => {
        resolve(request.result);
      };
      
      request.onerror = () => {
        console.error('获取音频记录失败:', request.error);
        reject(request.error);
      };
    });
  }
  
  // 根据ID获取音频记录
  getAudioRecordById(id) {
    if (!this.db) {
      return this.initDB().then(() => this.getAudioRecordById(id));
    }
    
    return new Promise((resolve, reject) => {
      const transaction = this.db.transaction([this.storeName], 'readonly');
      const store = transaction.objectStore(this.storeName);
      const request = store.get(id);
      
      request.onsuccess = () => {
        resolve(request.result);
      };
      
      request.onerror = () => {
        console.error('获取音频记录失败:', request.error);
        reject(request.error);
      };
    });
  }
  
  // 删除音频记录
  deleteAudioRecord(id) {
    if (!this.db) {
      return this.initDB().then(() => this.deleteAudioRecord(id));
    }
    
    return new Promise((resolve, reject) => {
      const transaction = this.db.transaction([this.storeName], 'readwrite');
      const store = transaction.objectStore(this.storeName);
      const request = store.delete(id);
      
      request.onsuccess = () => {
        console.log(`ID为${id}的音频记录已删除`);
        resolve(true);
      };
      
      request.onerror = () => {
        console.error('删除音频记录失败:', request.error);
        reject(request.error);
      };
    });
  }
  
  // 清空所有音频记录
  clearAllAudioRecords() {
    if (!this.db) {
      return this.initDB().then(() => this.clearAllAudioRecords());
    }
    
    return new Promise((resolve, reject) => {
      const transaction = this.db.transaction([this.storeName], 'readwrite');
      const store = transaction.objectStore(this.storeName);
      const request = store.clear();
      
      request.onsuccess = () => {
        console.log('所有音频记录已清空');
        resolve(true);
      };
      
      request.onerror = () => {
        console.error('清空音频记录失败:', request.error);
        reject(request.error);
      };
    });
  }
}

// 使用示例
// 初始化存储工具
const audioStorage = new AudioStorage();

// 假设这是你的WebSocket接收处理
function setupWebSocketAudioHandler(webSocket) {
  // 存储音频片段的缓冲区
  let audioBuffer = [];
  
  webSocket.onmessage = (event) => {
    // 假设接收到的是ArrayBuffer格式的音频数据
    if (event.data instanceof ArrayBuffer) {
      // 可以直接存储单个片段
      audioStorage.storeAudioData(event.data, {
        type: 'audio_chunk',
        size: event.data.byteLength
      });
      
      // 或者先缓存，积累到一定大小再存储
      audioBuffer.push(event.data);
      
      // 例如：当缓冲区大小超过1MB时存储
      const totalSize = audioBuffer.reduce((sum, chunk) => sum + chunk.byteLength, 0);
      if (totalSize > 1024 * 1024) {
        // 合并缓冲区并存储
        const combined = new Uint8Array(totalSize);
        let offset = 0;
        audioBuffer.forEach(chunk => {
          combined.set(new Uint8Array(chunk), offset);
          offset += chunk.byteLength;
        });
        
        audioStorage.storeAudioData(combined.buffer, {
          type: 'audio_segment',
          size: totalSize,
          chunks: audioBuffer.length
        });
        
        // 清空缓冲区
        audioBuffer = [];
      }
    }
  };
}

// 后续分析音频数据的示例方法
async function analyzeAudioData() {
  // 获取所有音频记录
  const records = await audioStorage.getAllAudioRecords();
  
  // 处理每条记录
  records.forEach(record => {
    console.log(`分析音频记录 (ID: ${record.id})`);
    console.log('元数据:', record.metadata);
    console.log('数据大小:', record.data.byteLength, '字节');
    
    // 这里可以添加你的音频分析逻辑
    // 例如：将ArrayBuffer转换为Float32Array进行音频处理
    const audioData = new Float32Array(record.data);
    // ... 分析代码 ...
  });
}

export default AudioStorage ;
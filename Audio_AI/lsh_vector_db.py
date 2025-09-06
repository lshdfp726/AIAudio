from langchain.text_splitter import CharacterTextSplitter
from langchain.embeddings import OpenAIEmbeddings
from langchain.vectorstores import FAISS

import logging
# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


class BuildVectorDB:
    def __init__(self, chunk_size=100, chunk_overlap=20,separator="。") -> None:
        """
        文本构建为向量数据库
        chunk_size: 每个段落分割字符数
        chunk_overlap: 片段重叠部分(保证上下文连贯？)
        separator: 分隔符
        """
        text_splitter = CharacterTextSplitter(
            chunk_size=100,
            chunk_overlap=20,
            separator=separator
        )
        self.spliter = text_splitter
        self.embeddings = OpenAIEmbeddings()

    def buildWithText(self, text):
        try:
            chunks = self.spliter.split_text(text) #使用openAI的嵌入模型
            db = FAISS.from_texts(chunks, self.embeddings) #构建向量库
        except Exception as e:
            logger.error(f"buildWithText error: {str(e)}")
            return None

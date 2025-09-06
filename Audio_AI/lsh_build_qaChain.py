from langchain.llms import HuggingFacePipeline
from transformers import AutoTokenizer, AutoModelForCausalLM, pipeline
from langchain.chains import RetrievalQA

from Audio_AI.lsh_ASR import ASRTransform
from Audio_AI.lsh_vector_db import BuildVectorDB

import logging
# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)



class BuildAudioQAChain:
    def __init__(self, llv_modelname="", temperature=0) -> None:
        model_path = "THUDM/chatglm3-6b" #本地模型路径
        tokenizer = AutoTokenizer.from_pretrained(model_path, trust_remote_code=True)
        model = AutoModelForCausalLM.from_pretrained(model_path, trust_remote_code=True).quantize(4).cuda() #4-bit 量化
        pipe = pipeline("text-generation",model=model, tokenizer=tokenizer, max_new_tokens=512)
        llm = HuggingFacePipeline(pipeline=pipe)

        self.llm = llm

    def buildQAChian(self, db):
        try:
            qa_chain = RetrievalQA.from_chain_type(
                llm=self.llm,
                chain_type="stuff", # 将检索到的文本"填充"到提示词中
                retriever=db.as_retriever(search_kwargs={"k":3}), #检索最相关的3个片段
                return_source_documents=True #返回用于生产回答的源文本
            )

            return qa_chain
        except Exception as e:
            logger.error(f"buildQAChian error {str(e)}")


if __name__ == "__main__":
    # 1. 音频转文本
    audio_path = "meeting_recording.mp3"  # 你的音频文件路径
    audio_text = audio_to_text(audio_path)
    
    # 2. 构建向量数据库
    vector_db = BuildVectorDB().buildWithText(audio_text)
    
    # 3. 构建问答链
    qa_chain = BuildAudioQAChain().buildQAChian(vector_db)
    
    # 4. 提问并获取答案
    question = "音频中提到的项目截止日期是什么时候？"
    result = qa_chain({"query": question})
    
    print(f"问题：{question}")
    print(f"回答：{result['result']}")
    print("\n参考文本：")
    for doc in result["source_documents"]:
        print(f"- {doc.page_content}")
import logging
import base64
import io
import os
import re
import time
import uuid
from pathlib import Path
from typing import List, Literal, Optional

from dotenv import load_dotenv
from fastapi import FastAPI, Header, Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field

from langchain_core.messages import AIMessage, HumanMessage, SystemMessage
from langchain_openai import ChatOpenAI

# 加载 .env 文件 (优先查找项目根目录)
_dotenv_path = Path(__file__).resolve().parent.parent / ".env"
load_dotenv(dotenv_path=_dotenv_path, override=False)

LOG_LEVEL = os.getenv("LOG_LEVEL", "INFO").upper()
logging.basicConfig(level=LOG_LEVEL, format="%(asctime)s %(levelname)s %(message)s")
logger = logging.getLogger("langchain_server")

SERVER_API_KEY = os.getenv("LANGCHAIN_API_KEY", "")
DEFAULT_MODEL = os.getenv("LANGCHAIN_MODEL_DEFAULT", "deepseek/deepseek-v4-pro")
TEMPERATURE = float(os.getenv("LANGCHAIN_TEMPERATURE", "0.7"))
MAX_TOKENS = os.getenv("LANGCHAIN_MAX_TOKENS", "")
UPSTREAM_API_KEY = (
    os.getenv("OPENROUTER_API_KEY", "")
    or os.getenv("OPENAI_API_KEY", "")
    or os.getenv("LLM_API_KEY", "")
)
UPSTREAM_BASE_URL = (
    os.getenv("OPENROUTER_BASE_URL", "")
    or os.getenv("OPENAI_BASE_URL", "")
    or os.getenv("LLM_BASE_URL", "")
    or "https://openrouter.ai/api/v1"
)
OPENROUTER_HTTP_REFERER = os.getenv("OPENROUTER_HTTP_REFERER", "")
OPENROUTER_APP_TITLE = os.getenv("OPENROUTER_APP_TITLE", "AI-Chat")

MODEL_ALIASES = {
    "deepseek-chat": "deepseek/deepseek-v4-pro",
    "deepseek-v4-pro": "deepseek/deepseek-v4-pro",
    "gpt-oss-20b": "openai/gpt-oss-20b:free",
    "gpt-oss-120b": "openai/gpt-oss-120b:free",
    "gemma-2-27b": "google/gemma-2-27b-it",
    "gemma-3-4b": "google/gemma-3-4b-it",
    "gemma-3-12b": "google/gemma-3-12b-it",
    "gemma-3-27b": "google/gemma-3-27b-it",
}

app = FastAPI()


class ChatMessage(BaseModel):
    role: Literal["system", "user", "assistant"]
    content: str = Field(min_length=1)


class ChatAttachment(BaseModel):
    name: str = ""
    content_type: str = ""
    size: Optional[int] = None
    data_url: str = ""


class ChatRequest(BaseModel):
    request_id: Optional[str] = None
    session_id: Optional[str] = None
    username: Optional[str] = None
    model: Optional[str] = None
    messages: List[ChatMessage]
    attachments: List[ChatAttachment] = Field(default_factory=list)


def _normalize_model(model: str) -> str:
    return MODEL_ALIASES.get(model, model)


def _build_llm(model: str) -> ChatOpenAI:
    kwargs = {"model": model, "temperature": TEMPERATURE}
    if UPSTREAM_API_KEY:
        kwargs["api_key"] = UPSTREAM_API_KEY
    if UPSTREAM_BASE_URL:
        kwargs["base_url"] = UPSTREAM_BASE_URL
    headers = {}
    if OPENROUTER_HTTP_REFERER:
        headers["HTTP-Referer"] = OPENROUTER_HTTP_REFERER
    if OPENROUTER_APP_TITLE:
        headers["X-Title"] = OPENROUTER_APP_TITLE
    if headers:
        kwargs["default_headers"] = headers
    if MAX_TOKENS:
        try:
            kwargs["max_tokens"] = int(MAX_TOKENS)
        except ValueError:
            pass
    return ChatOpenAI(**kwargs)


def _safe_filename(name: str) -> str:
    name = (name or "attachment").strip()
    name = name.replace("\\", "_").replace("/", "_")
    return name[:180] or "attachment"


def _split_data_url(data_url: str) -> tuple[str, bytes]:
    match = re.match(r"^data:([^;,]+)?(;base64)?,(.*)$", data_url or "", re.S)
    if not match:
        return "", b""
    content_type = match.group(1) or ""
    is_base64 = bool(match.group(2))
    payload = match.group(3) or ""
    try:
        if is_base64:
            return content_type, base64.b64decode(payload, validate=False)
        return content_type, payload.encode("utf-8", errors="replace")
    except Exception:
        return content_type, b""


def _trim_text(text: str, limit: int) -> str:
    if len(text) <= limit:
        return text
    return text[:limit] + "\n...[内容过长，已截断]"


def _extract_document_text(name: str, content_type: str, raw: bytes, limit: int) -> str:
    suffix = Path(name).suffix.lower()
    text_like_suffixes = {
        ".txt", ".md", ".csv", ".json", ".log", ".py", ".cc", ".cpp", ".c",
        ".h", ".hpp", ".js", ".ts", ".html", ".css", ".sql", ".xml", ".yaml", ".yml",
    }
    if content_type.startswith("text/") or suffix in text_like_suffixes:
        return _trim_text(raw.decode("utf-8", errors="replace"), limit)

    if content_type == "application/pdf" or suffix == ".pdf":
        try:
            from pypdf import PdfReader

            reader = PdfReader(io.BytesIO(raw))
            pages = []
            for page in reader.pages[:12]:
                pages.append(page.extract_text() or "")
            return _trim_text("\n\n".join(pages).strip(), limit)
        except ModuleNotFoundError:
            return "PDF 文本提取依赖缺失，请在 langchain_server 环境安装 pypdf。"
        except Exception as exc:
            return f"PDF 文本提取失败: {exc}"

    if (
        content_type == "application/vnd.openxmlformats-officedocument.wordprocessingml.document"
        or suffix == ".docx"
    ):
        try:
            from docx import Document

            document = Document(io.BytesIO(raw))
            text = "\n".join(p.text for p in document.paragraphs if p.text)
            return _trim_text(text, limit)
        except ModuleNotFoundError:
            return "DOCX 文本提取依赖缺失，请在 langchain_server 环境安装 python-docx。"
        except Exception as exc:
            return f"DOCX 文本提取失败: {exc}"

    return "暂不支持直接提取该类型文档内容。"


def _model_supports_images(model: str) -> bool:
    model_lower = model.lower()
    return (
        model_lower.startswith("google/gemma-3-")
        or "vision" in model_lower
        or "vl" in model_lower
    )


def _build_attachment_context(attachments: List[ChatAttachment], include_images: bool) -> tuple[str, list[dict]]:
    max_files = int(os.getenv("LANGCHAIN_MAX_ATTACHMENTS", "4"))
    max_bytes = int(os.getenv("LANGCHAIN_MAX_ATTACHMENT_BYTES", str(6 * 1024 * 1024)))
    max_chars = int(os.getenv("LANGCHAIN_MAX_ATTACHMENT_TEXT_CHARS", "16000"))

    text_sections = []
    image_blocks = []

    for index, attachment in enumerate(attachments[:max_files], start=1):
        name = _safe_filename(attachment.name)
        declared_type = attachment.content_type or ""
        content_type, raw = _split_data_url(attachment.data_url)
        content_type = declared_type or content_type or "application/octet-stream"
        size = attachment.size or len(raw)

        if not raw:
            text_sections.append(f"[附件 {index}: {name}] 文件内容为空或无法读取。")
            continue
        if len(raw) > max_bytes:
            text_sections.append(f"[附件 {index}: {name}] 文件过大，已跳过。")
            continue

        if content_type.startswith("image/"):
            if include_images:
                image_blocks.append(
                    {
                        "type": "image_url",
                        "image_url": {"url": attachment.data_url},
                    }
                )
            else:
                text_sections.append(
                    f"[图片 {index}: {name}] 当前模型不支持视觉输入。请切换到 Google Gemma 3 系列后再让模型分析图片内容。"
                )
                continue
            text_sections.append(f"[图片 {index}: {name}, 类型: {content_type}, 大小: {size} bytes]")
            continue

        extracted = _extract_document_text(name, content_type, raw, max_chars)
        text_sections.append(
            f"[文档 {index}: {name}, 类型: {content_type}, 大小: {size} bytes]\n{extracted}"
        )

    if len(attachments) > max_files:
        text_sections.append(f"[系统提示] 本次只处理前 {max_files} 个附件。")

    if not text_sections:
        return "", image_blocks

    return "用户上传的附件如下：\n\n" + "\n\n".join(text_sections), image_blocks


def _make_trace_id(request_id: Optional[str]) -> str:
    if request_id:
        return request_id
    return str(uuid.uuid4())


def _error_response(trace_id: str, error: str, reply: str) -> JSONResponse:
    return JSONResponse(
        status_code=200,
        content={"success": False, "error": error, "reply": reply, "trace_id": trace_id},
    )


def _is_authorized(auth_header: Optional[str]) -> bool:
    if not SERVER_API_KEY:
        return True
    if not auth_header:
        return False
    parts = auth_header.split()
    if len(parts) == 2 and parts[0].lower() == "bearer" and parts[1] == SERVER_API_KEY:
        return True
    return False


@app.exception_handler(RequestValidationError)
async def validation_exception_handler(request: Request, exc: RequestValidationError):
    trace_id = str(uuid.uuid4())
    logger.warning("validation error: %s", exc)
    return _error_response(trace_id, "invalid_request", "Request validation failed")


@app.exception_handler(Exception)
async def unhandled_exception_handler(request: Request, exc: Exception):
    trace_id = str(uuid.uuid4())
    logger.exception("unhandled error")
    return _error_response(trace_id, "internal_error", "Internal server error")


@app.post("/llm/chat")
async def llm_chat(payload: ChatRequest, authorization: Optional[str] = Header(default=None)):
    trace_id = _make_trace_id(payload.request_id)

    if not _is_authorized(authorization):
        logger.warning("unauthorized request, trace_id=%s", trace_id)
        return _error_response(trace_id, "unauthorized", "Unauthorized")

    if not payload.messages:
        return _error_response(trace_id, "empty_messages", "Messages are required")

    model = _normalize_model(payload.model or DEFAULT_MODEL)
    attachment_context, image_blocks = _build_attachment_context(
        payload.attachments,
        include_images=_model_supports_images(model),
    )
    langchain_messages = []
    last_user_index = -1
    for idx, message in enumerate(payload.messages):
        if message.role == "user":
            last_user_index = idx

    if attachment_context:
        langchain_messages.append(SystemMessage(content=attachment_context))

    for idx, message in enumerate(payload.messages):
        if message.role == "system":
            langchain_messages.append(SystemMessage(content=message.content))
        elif message.role == "user":
            if image_blocks and idx == last_user_index:
                content = [{"type": "text", "text": message.content}] + image_blocks
                langchain_messages.append(HumanMessage(content=content))
            else:
                langchain_messages.append(HumanMessage(content=message.content))
        elif message.role == "assistant":
            langchain_messages.append(AIMessage(content=message.content))

    logger.info(
        "request start trace_id=%s session_id=%s username=%s model=%s messages=%d attachments=%d",
        trace_id,
        payload.session_id or "",
        payload.username or "",
        model,
        len(langchain_messages),
        len(payload.attachments),
    )

    start = time.time()
    try:
        llm = _build_llm(model)
        result = llm.invoke(langchain_messages)
        reply = getattr(result, "content", None)
        if not reply:
            reply = str(result)
    except Exception as exc:
        logger.exception("langchain invoke failed trace_id=%s", trace_id)
        return _error_response(trace_id, "upstream_error", str(exc))

    latency_ms = int((time.time() - start) * 1000)
    logger.info("request end trace_id=%s latency_ms=%d", trace_id, latency_ms)

    return {
        "success": True,
        "reply": reply,
        "model": model,
        "trace_id": trace_id,
    }

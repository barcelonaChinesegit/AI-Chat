# AI-Chat

[English](README.md) | 中文

AI-Chat 是一个面向普通用户的多模型 AI 聊天平台。项目起源于一个很实际的观察：身边不少非计算机专业同学希望使用优秀的大模型完成学习、写作、资料整理和日常问答，但往往不了解 API、代理配置、模型选择和部署细节。因此，本项目尝试把复杂的大模型接入、用户系统和公网部署封装成一个可直接访问的 Web 服务。

在线访问：

- 首页：http://ai.chenzijian.com/
- 聊天页：http://ai.chenzijian.com/AIChat.html

> 说明：线上服务依赖个人服务器和第三方模型中转服务，若访问受限，请以代码和部署说明为准。

## 项目亮点

- **自研 C++ HTTP 服务**：实现静态资源服务、请求解析、路由分发、登录注册、会话管理和聊天记录持久化。
- **多模型统一接入**：通过 Python FastAPI + LangChain 接入 OpenRouter，支持 DeepSeek、Qwen、GPT OSS、Gemma 等模型。
- **完整聊天体验**：支持用户登录、会话列表、新建/切换/重命名/删除会话、历史消息加载、多模型选择。
- **附件能力**：支持上传文本、Markdown、CSV、JSON、PDF、DOCX、代码文件和图片；后端可抽取文档内容并传入模型。
- **工程化部署**：提供 Nginx 反向代理配置、正式环境重启脚本、项目内日志目录和敏感信息忽略规则。
- **真实问题修复**：处理过 API 兼容差异、模型地区不可用、端口占用、后台进程守护、GitHub secret scanning 等实际部署问题。

## 技术栈

| 模块 | 技术 |
| --- | --- |
| C++ 服务 | C++17, socket, fork 并发处理, libcurl, jsoncpp, MySQL C API |
| LLM 后端 | Python, FastAPI, LangChain, langchain-openai |
| 数据库 | MySQL |
| 前端 | HTML, CSS, JavaScript |
| 部署 | Linux, Nginx, shell script |
| 模型入口 | OpenRouter 兼容 OpenAI Chat Completions API |

## 系统架构

```text
Browser
  |
  |  http://ai.chenzijian.com/
  v
Nginx :80
  |
  |  reverse proxy
  v
C++ HTTP Server :8999
  |-- serves wwwroot/*.html and assets
  |-- handles login/register/session/chat APIs
  |-- reads/writes MySQL
  |
  |  POST /llm/chat
  v
Python FastAPI + LangChain :8000
  |
  |  OpenRouter-compatible API
  v
LLM Providers
```

## 目录结构

```text
AI-Chat/
├── Main.cc                    # C++ 业务入口：用户系统、会话管理、AI 聊天接口
├── Http.hpp                   # HTTP 请求解析、静态资源响应、路由分发
├── Socket.hpp                 # TCP socket 封装，本地反代监听
├── TcpServer.hpp              # C++ HTTP 服务并发处理
├── Log.hpp                    # 日志模块，输出到 logs/
├── Session.hpp                # 会话 ID 等工具
├── mysql_util.hpp             # MySQL 连接与执行封装
├── Makefile                   # C++ 服务编译
├── langchain_server/
│   ├── app.py                 # FastAPI + LangChain 模型调用服务
│   └── requirements.txt       # Python 依赖
├── wwwroot/
│   ├── index.html             # 项目首页
│   ├── AIChat.html            # AI 聊天主界面
│   ├── Login.html             # 登录页
│   ├── Register.html          # 注册页
│   ├── UserCenter.html        # 用户中心
│   └── image/                 # 首页展示图片
├── init_database.sql          # 数据库初始化 SQL
├── create_sessions_table.sql  # 会话表相关 SQL
├── nginx-ai-chat.conf         # Nginx 反向代理配置模板
├── restart_prod_site.sh       # 停旧服务并重启正式服务
├── Nginx配置.md               # 部署说明
└── .gitignore                 # 忽略 API Key、日志、上传文件和编译产物
```

## 核心功能

### 用户与会话

- 用户注册、登录、登出
- 当前用户状态检测
- 用户中心信息展示与更新
- 多轮会话管理
- 会话创建、切换、重命名、删除
- 聊天历史持久化到 MySQL

### 多模型聊天

前端可在聊天页选择不同模型，C++ 服务负责组装用户消息、历史上下文和附件信息，并转发给 Python LangChain 服务。Python 后端会把模型 ID 统一映射到 OpenRouter 兼容 API。

当前保留的模型类型包括：

- DeepSeek
- Qwen
- OpenAI GPT OSS
- Google Gemma

### 附件上传与处理

支持的附件类型包括：

- 文本文档：`.txt`, `.md`, `.csv`, `.json`, `.log`
- 代码文件：`.py`, `.cpp`, `.cc`, `.hpp`, `.h`, `.js`, `.ts`, `.html`, `.css`, `.sql`, `.xml`, `.yaml`, `.yml`
- 文档：`.pdf`, `.docx`
- 图片：`image/*`

Python 后端会对文本、PDF、DOCX 等文件进行内容抽取，并将附件上下文加入模型输入。对于支持视觉输入的模型，会将图片作为多模态内容传入。

## 快速开始

### 1. 安装依赖

C++ 编译依赖：

```bash
sudo apt update
sudo apt install -y g++ make libcurl4-openssl-dev libjsoncpp-dev libmysqlclient-dev libssl-dev
```

Python 依赖：

```bash
cd langchain_server
pip install -r requirements.txt
```

### 2. 配置环境变量

复制 `.env.example` 并填写自己的配置：

```bash
cp .env.example .env
```

核心配置示例：

```bash
LANGCHAIN_API_KEY=your_internal_service_key
LANGCHAIN_API_URL=http://127.0.0.1:8000/llm/chat
LANGCHAIN_CONNECT_TIMEOUT_MS=5000
LANGCHAIN_TIMEOUT_MS=60000

LANGCHAIN_MODEL_DEFAULT=deepseek/deepseek-v4-pro
LANGCHAIN_TEMPERATURE=0.7
LANGCHAIN_MAX_TOKENS=

OPENROUTER_API_KEY=your_openrouter_api_key
OPENROUTER_BASE_URL=https://openrouter.ai/api/v1
OPENROUTER_APP_TITLE=AI-Chat

MYSQL_HOST=127.0.0.1
MYSQL_USER=your_mysql_user
MYSQL_PASS=your_mysql_password
MYSQL_DBNAME=http_service
MYSQL_PORT=3306
```

`.env` 已被 `.gitignore` 忽略，请不要提交真实 API Key。

### 3. 初始化数据库

根据 `init_database.sql` 和 `create_sessions_table.sql` 初始化 MySQL 表结构。

```bash
mysql -u your_user -p your_database < init_database.sql
mysql -u your_user -p your_database < create_sessions_table.sql
```

数据库连接参数从 `.env` 中读取，部署到其他环境时只需要修改 `MYSQL_HOST`、`MYSQL_USER`、`MYSQL_PASS`、`MYSQL_DBNAME` 和 `MYSQL_PORT`。

### 4. 启动 Python LLM 后端

```bash
cd langchain_server
python -m uvicorn app:app --host 0.0.0.0 --port 8000
```

### 5. 编译并启动 C++ 服务

```bash
make
./myhttp_ai 8999
```

本项目正式部署时让 C++ 服务监听 `127.0.0.1:8999`，由 Nginx 对外反向代理。

## 生产部署

项目提供正式环境重启脚本：

```bash
./restart_prod_site.sh
```

脚本会执行：

- 停止旧目录 `/home/czj/ai_chat` 的 `8999` 服务
- 停止当前项目测试端口 `18080` 服务
- 停止并重启当前项目的 Python LangChain 服务
- 重新编译 C++ 服务
- 启动当前项目到 `127.0.0.1:8999`

Nginx 配置模板见 `nginx-ai-chat.conf`：

```bash
sudo cp nginx-ai-chat.conf /etc/nginx/sites-available/ai-chat
sudo ln -sf /etc/nginx/sites-available/ai-chat /etc/nginx/sites-enabled/ai-chat
sudo nginx -t
sudo systemctl reload nginx
```

## 安全与仓库说明

- `.env`、日志、上传文件和编译产物已加入 `.gitignore`
- 不要提交真实 API Key、数据库密码或服务器私密配置
- 如果 API Key 曾经被提交到 Git 历史，需要重新生成 Key，并重写 Git 历史后再推送
- 当前仓库保留了部署模板，但线上真实环境变量仅保存在服务器本地

## 项目收获

这个项目不是只停留在课堂练习，而是完整经历了从需求产生、系统设计、前后端实现、模型接入、数据库持久化，到公网部署和安全修复的过程。通过实现 AI-Chat，我对 Web 服务架构、LLM API 工程化接入、Linux 进程管理、Nginx 反向代理、日志与密钥管理有了更具体的理解，也更清楚一个面向真实用户的 AI 应用需要同时关注功能、稳定性和可维护性。

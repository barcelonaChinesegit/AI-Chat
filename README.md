# AI-Chat

English | [中文](README-zh.md)

AI-Chat is a multi-model AI chat platform designed for everyday users. The project started from a practical observation: many classmates from non-computer-science backgrounds wanted to use high-quality large language models for studying, writing, information organization, and daily Q&A, but they often had no stable and low-barrier way to access these models. They also did not want to deal with API keys, proxy settings, model IDs, or deployment details. AI-Chat wraps these engineering details into a web service that can be accessed directly through a browser.

Live demo:

- Home page: http://ai.chenzijian.com/
- Chat page: http://ai.chenzijian.com/AIChat.html

> Note: the live service depends on a personal server and third-party model gateway availability. If the demo is temporarily unavailable, please refer to the source code and deployment instructions.

## Highlights

- **Self-built C++ HTTP service**: handles static file serving, HTTP request parsing, route dispatching, authentication, session management, and chat history persistence.
- **Unified multi-model access**: uses Python FastAPI + LangChain to call OpenRouter-compatible APIs, supporting DeepSeek, Qwen, GPT OSS, Gemma, and other models.
- **Complete chat workflow**: supports login, session list, new/switch/rename/delete sessions, history loading, and model selection.
- **Attachment support**: supports text, Markdown, CSV, JSON, PDF, DOCX, source code files, and images. The backend extracts document content and passes it into the model context.
- **Production-oriented deployment**: includes Nginx reverse proxy configuration, a production restart script, local log management, and `.gitignore` rules for secrets and runtime files.
- **Real deployment problem solving**: addressed API compatibility differences, region-unavailable models, port conflicts, daemon/background process handling, and GitHub secret scanning issues.

## Tech Stack

| Module | Technologies |
| --- | --- |
| C++ service | C++17, socket, fork-based concurrent handling, libcurl, jsoncpp, MySQL C API |
| LLM backend | Python, FastAPI, LangChain, langchain-openai |
| Database | MySQL |
| Frontend | HTML, CSS, JavaScript |
| Deployment | Linux, Nginx, shell script |
| Model gateway | OpenRouter-compatible OpenAI Chat Completions API |

## Architecture

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

## Project Structure

```text
AI-Chat/
├── Main.cc                    # C++ business entry: users, sessions, AI chat API
├── Http.hpp                   # HTTP parsing, static responses, route dispatching
├── Socket.hpp                 # TCP socket wrapper for local reverse-proxy serving
├── TcpServer.hpp              # Concurrent C++ HTTP server
├── Log.hpp                    # Logging module, writes to logs/
├── Session.hpp                # Session ID utilities
├── mysql_util.hpp             # MySQL connection and execution helpers
├── Makefile                   # C++ build script
├── langchain_server/
│   ├── app.py                 # FastAPI + LangChain model invocation service
│   └── requirements.txt       # Python dependencies
├── wwwroot/
│   ├── index.html             # Home page
│   ├── AIChat.html            # Main AI chat interface
│   ├── Login.html             # Login page
│   ├── Register.html          # Registration page
│   ├── UserCenter.html        # User center
│   └── image/                 # Home page images
├── init_database.sql          # Database initialization SQL
├── create_sessions_table.sql  # Session table SQL
├── nginx-ai-chat.conf         # Nginx reverse proxy template
├── restart_prod_site.sh       # Stop old service and restart production service
├── Nginx配置.md               # Chinese deployment notes
└── .gitignore                 # Ignores API keys, logs, uploads, and build artifacts
```

## Core Features

### Users and Sessions

- User registration, login, and logout
- Current user status checking
- User profile display and update
- Multi-turn session management
- Session creation, switching, renaming, and deletion
- Chat history persistence in MySQL

### Multi-Model Chat

The frontend allows users to select a model on the chat page. The C++ service assembles the user message, recent conversation history, selected model, and attachments, then forwards the request to the Python LangChain backend. The Python backend normalizes model IDs and calls the OpenRouter-compatible API.

Currently supported model families include:

- DeepSeek
- Qwen
- OpenAI GPT OSS
- Google Gemma

### Attachment Upload and Processing

Supported attachment types include:

- Text documents: `.txt`, `.md`, `.csv`, `.json`, `.log`
- Source code files: `.py`, `.cpp`, `.cc`, `.hpp`, `.h`, `.js`, `.ts`, `.html`, `.css`, `.sql`, `.xml`, `.yaml`, `.yml`
- Documents: `.pdf`, `.docx`
- Images: `image/*`

The Python backend extracts content from text, PDF, and DOCX files and adds the extracted content to the model input. For models with vision capability, images can be passed as multimodal input.

## Quick Start

### 1. Install Dependencies

C++ build dependencies:

```bash
sudo apt update
sudo apt install -y g++ make libcurl4-openssl-dev libjsoncpp-dev libmysqlclient-dev libssl-dev
```

Python dependencies:

```bash
cd langchain_server
pip install -r requirements.txt
```

### 2. Configure Environment Variables

Copy `.env.example` and fill in your own credentials:

```bash
cp .env.example .env
```

Core configuration example:

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

`.env` is ignored by `.gitignore`. Do not commit real API keys or server credentials.

### 3. Initialize the Database

Use `init_database.sql` and `create_sessions_table.sql` to initialize MySQL tables.

```bash
mysql -u your_user -p your_database < init_database.sql
mysql -u your_user -p your_database < create_sessions_table.sql
```

Database connection parameters are read from `.env`. For a new environment, update `MYSQL_HOST`, `MYSQL_USER`, `MYSQL_PASS`, `MYSQL_DBNAME`, and `MYSQL_PORT`.

### 4. Start the Python LLM Backend

```bash
cd langchain_server
python -m uvicorn app:app --host 0.0.0.0 --port 8000
```

### 5. Build and Start the C++ Service

```bash
make
./myhttp_ai 8999
```

In production, the C++ service listens on `127.0.0.1:8999` and is exposed through Nginx reverse proxy.

## Production Deployment

The project provides a production restart script:

```bash
./restart_prod_site.sh
```

The script will:

- Stop the old `8999` service under `/home/czj/ai_chat`
- Stop the current project's test service on port `18080`
- Stop and restart the Python LangChain backend
- Rebuild the C++ service
- Start the current project on `127.0.0.1:8999`

Nginx reverse proxy template:

```bash
sudo cp nginx-ai-chat.conf /etc/nginx/sites-available/ai-chat
sudo ln -sf /etc/nginx/sites-available/ai-chat /etc/nginx/sites-enabled/ai-chat
sudo nginx -t
sudo systemctl reload nginx
```

## Security Notes

- `.env`, logs, uploaded files, and build artifacts are ignored by `.gitignore`
- Do not commit real API keys, database passwords, or private server configuration
- If an API key was ever committed to Git history, rotate the key and rewrite Git history before pushing
- The repository keeps deployment templates only; real production environment variables stay on the server

## What I Learned

This project goes beyond a classroom demo. It covers the full path from identifying a real user need, designing the system, implementing frontend and backend modules, integrating LLM APIs, persisting data, deploying to a public domain, and fixing security issues discovered during GitHub publishing. Through AI-Chat, I gained a more concrete understanding of web service architecture, LLM API engineering, Linux process management, Nginx reverse proxy deployment, logging, and secret management. It also helped me understand that a real user-facing AI application needs to balance functionality, stability, maintainability, and operational safety.

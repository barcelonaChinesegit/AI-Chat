#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OLD_ROOT_DIR="${OLD_ROOT_DIR:-/home/czj/ai_chat}"
PYTHON_BIN="${PYTHON_BIN:-/home/czj/anaconda3/envs/langchain_server/bin/python}"
HTTP_PORT="${HTTP_PORT:-8999}"
LLM_PORT="${LLM_PORT:-8000}"
LOG_DIR="$ROOT_DIR/logs"
PY_LOG="$LOG_DIR/langchain_server.log"
CXX_LOG="$LOG_DIR/myhttp_ai.log"

mkdir -p "$LOG_DIR"

wait_port_free() {
    local port="$1"
    local label="$2"
    for _ in {1..20}; do
        if ! ss -ltn | grep -q ":$port\\b"; then
            return 0
        fi
        sleep 0.5
    done
    echo "[prod] warning: $label port $port is still in use"
    ss -ltnp | grep -E ":$port\\b" || true
}

wait_http_ready() {
    local url="$1"
    local label="$2"
    for _ in {1..30}; do
        if curl -sS --max-time 1 "$url" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.5
    done
    echo "[prod] warning: $label did not become ready: $url"
    return 1
}

stop_myhttp_on_port() {
    local port="$1"
    local label="$2"
    shift 2
    local allowed_roots=("$@")

    echo "[prod] stopping $label on port $port"
    while read -r pid cwd; do
        [ -z "${pid:-}" ] && continue
        for allowed in "${allowed_roots[@]}"; do
            if [ "$cwd" = "$allowed" ]; then
                echo "[prod] kill myhttp_ai pid=$pid cwd=$cwd"
                kill "$pid" 2>/dev/null || true
                break
            fi
        done
    done < <(
        for pid in $(pgrep -f "^./myhttp_ai $port" || true); do
            cwd="$(readlink -f "/proc/$pid/cwd" 2>/dev/null || true)"
            printf '%s %s\n' "$pid" "$cwd"
        done
    )
}

echo "[prod] root: $ROOT_DIR"
stop_myhttp_on_port "$HTTP_PORT" "production myhttp_ai" "$ROOT_DIR" "$OLD_ROOT_DIR"
stop_myhttp_on_port 18080 "test myhttp_ai" "$ROOT_DIR"

sleep 1
wait_port_free "$HTTP_PORT" "myhttp_ai"

echo "[prod] stopping langchain server on port $LLM_PORT for this project"
while read -r pid cwd; do
    [ -z "${pid:-}" ] && continue
    if [ "$cwd" = "$ROOT_DIR/langchain_server" ]; then
        echo "[prod] kill uvicorn pid=$pid"
        kill "$pid" 2>/dev/null || true
    fi
done < <(
    for pid in $(pgrep -f "uvicorn app:app --host .* --port $LLM_PORT" || true); do
        cwd="$(readlink -f "/proc/$pid/cwd" 2>/dev/null || true)"
        printf '%s %s\n' "$pid" "$cwd"
    done
)

sleep 1
wait_port_free "$LLM_PORT" "langchain server"

echo "[prod] building C++ server"
make -C "$ROOT_DIR"

echo "[prod] starting langchain server on port $LLM_PORT"
(
    cd "$ROOT_DIR/langchain_server"
    if command -v setsid >/dev/null 2>&1; then
        nohup setsid "$PYTHON_BIN" -m uvicorn app:app --host 0.0.0.0 --port "$LLM_PORT" < /dev/null >> "$PY_LOG" 2>&1 &
    else
        nohup "$PYTHON_BIN" -m uvicorn app:app --host 0.0.0.0 --port "$LLM_PORT" < /dev/null >> "$PY_LOG" 2>&1 &
    fi
)

wait_http_ready "http://127.0.0.1:$LLM_PORT/docs" "langchain server" || tail -n 40 "$PY_LOG" || true

echo "[prod] starting myhttp_ai on port $HTTP_PORT"
(
    cd "$ROOT_DIR"
    ./myhttp_ai "$HTTP_PORT"
)

sleep 1
wait_http_ready "http://127.0.0.1:$HTTP_PORT/AIChat.html" "myhttp_ai" || tail -n 40 "$CXX_LOG" || true

echo "[prod] status"
ss -ltnp | grep -E ":($HTTP_PORT|$LLM_PORT)\b" || true
echo "[prod] C++ log: $CXX_LOG"
echo "[prod] Python log: $PY_LOG"
echo "[prod] open: http://ai.chenzijian.com/"

# AI-Chat Nginx 配置

当前正式服务结构：

- Nginx: `http://ai.chenzijian.com/`
- 首页: `http://ai.chenzijian.com/`
- 聊天页: `http://ai.chenzijian.com/AIChat.html`
- C++ HTTP 服务: `127.0.0.1:8999`
- Python LangChain 服务: `0.0.0.0:8000`
- 项目目录: `/home/czj/master/AI-Chat`
- C++ 日志: `/home/czj/master/AI-Chat/logs/myhttp_ai.log`
- Python 日志: `/home/czj/master/AI-Chat/logs/langchain_server.log`

## 重启正式服务

```bash
cd /home/czj/master/AI-Chat
./restart_prod_site.sh
```

脚本会停止旧目录 `/home/czj/ai_chat` 的 `8999` 服务，停止当前项目的测试 `18080` 服务，然后把当前项目启动到 `127.0.0.1:8999`。

## 安装或更新 Nginx 配置

配置模板已写在当前目录的 `nginx-ai-chat.conf`。

```bash
cd /home/czj/master/AI-Chat
sudo cp nginx-ai-chat.conf /etc/nginx/sites-available/ai-chat
sudo ln -sf /etc/nginx/sites-available/ai-chat /etc/nginx/sites-enabled/ai-chat
sudo nginx -t
sudo systemctl reload nginx
```

验证：

```bash
curl -I http://ai.chenzijian.com/
curl -s http://ai.chenzijian.com/ | grep 'AI智能助手'
```

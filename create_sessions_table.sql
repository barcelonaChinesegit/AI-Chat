-- 创建会话管理相关表
USE http_service;

-- 1. 创建sessions表
CREATE TABLE IF NOT EXISTS sessions (
    session_id VARCHAR(64) PRIMARY KEY COMMENT '会话ID(UUID)',
    session_name VARCHAR(200) DEFAULT '新对话' COMMENT '会话名称',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '最后更新时间',
    message_count INT DEFAULT 0 COMMENT '消息数量',
    INDEX idx_created_at (created_at),
    INDEX idx_updated_at (updated_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='会话表';

-- 2. 修改chat_history表，添加session_id字段
ALTER TABLE chat_history ADD COLUMN session_id VARCHAR(64) DEFAULT NULL COMMENT '会话ID' AFTER id;
ALTER TABLE chat_history ADD INDEX idx_session_id (session_id);

-- 3. 为现有数据创建默认session
INSERT INTO sessions (session_id, session_name, created_at) 
VALUES ('default-session', '默认会话', NOW())
ON DUPLICATE KEY UPDATE session_id=session_id;

-- 4. 更新现有聊天记录到默认session
UPDATE chat_history SET session_id = 'default-session' WHERE session_id IS NULL;

-- 5. 查看表结构
DESC sessions;
DESC chat_history;

-- 6. 查看数据
SELECT '=== Sessions 表 ===' as info;
SELECT * FROM sessions;

SELECT '=== Chat History 统计 ===' as info;
SELECT session_id, COUNT(*) as count FROM chat_history GROUP BY session_id;

SELECT '会话表创建成功！' AS message;


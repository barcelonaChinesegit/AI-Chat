-- MySQL数据库初始化脚本
-- 数据库名: http_service
-- 字符集: utf8 (适用于MySQL 5.7)

-- 创建数据库（如果不存在）
CREATE DATABASE IF NOT EXISTS http_service DEFAULT CHARSET=utf8 COLLATE=utf8_general_ci;

-- 使用数据库
USE http_service;

-- 创建用户表
CREATE TABLE IF NOT EXISTS users (
    id INT AUTO_INCREMENT PRIMARY KEY COMMENT '用户ID',
    username VARCHAR(50) NOT NULL UNIQUE COMMENT '用户名',
    password VARCHAR(64) NOT NULL COMMENT '密码(SHA256加密)',
    email VARCHAR(100) DEFAULT NULL COMMENT '邮箱',
    is_vip TINYINT(1) DEFAULT 0 COMMENT '是否VIP用户(0:否, 1:是)',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    last_login TIMESTAMP NULL DEFAULT NULL COMMENT '最后登录时间',
    INDEX idx_username (username),
    INDEX idx_email (email)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='用户表';

-- 插入测试数据
-- 密码: 123456 (SHA256: 8d969eef6ecad3c29a3a629280e686cf0c3f5d5a86aff3ca12020c923adc6c92)
INSERT INTO users (username, password, email, is_vip) VALUES 
('test', '8d969eef6ecad3c29a3a629280e686cf0c3f5d5a86aff3ca12020c923adc6c92', 'test@example.com', 0),
('vipuser', '8d969eef6ecad3c29a3a629280e686cf0c3f5d5a86aff3ca12020c923adc6c92', 'vip@example.com', 1)
ON DUPLICATE KEY UPDATE username=username; -- 如果已存在则忽略

-- 查看表结构
DESC users;

-- 查看已有用户
SELECT id, username, email, is_vip, created_at, last_login FROM users;

-- 常用SQL命令示例：

-- 1. 查询所有用户
-- SELECT * FROM users;

-- 2. 查询VIP用户
-- SELECT * FROM users WHERE is_vip=1;

-- 3. 将用户设为VIP
-- UPDATE users SET is_vip=1 WHERE username='test';

-- 4. 取消VIP
-- UPDATE users SET is_vip=0 WHERE username='test';

-- 5. 删除用户
-- DELETE FROM users WHERE username='test';

-- 6. 查询最近登录的用户
-- SELECT username, last_login FROM users WHERE last_login IS NOT NULL ORDER BY last_login DESC LIMIT 10;

-- 7. 统计用户数量
-- SELECT COUNT(*) as total_users, 
--        SUM(is_vip) as vip_users,
--        COUNT(*) - SUM(is_vip) as normal_users 
-- FROM users;

-- 8. 清空所有用户（谨慎使用！）
-- TRUNCATE TABLE users;


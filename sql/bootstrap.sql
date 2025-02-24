create database zener_test;

USE zener_test;
CREATE TABLE IF NOT EXISTS user (
    username char(50) NULL,
    password char(50) NULL
)ENGINE=InnoDB;

INSERT INTO user(username, password) VALUES('name', 'password');

-- CREATE TABLE IF NOT EXISTS 'users' (
--     `id` BIGINT(20) NOT NULL COMMENT '用户唯一标识'
--     `name` VARCHAR(255) NOT NULL COMMENT '用户昵称'
--     `email` VARCHAR(255) NULL COMMENT '邮箱'
--     `avatar` VARCHAR(255) NULL COMMENT '头像'
--     `create_time` DATATIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间'
--     `updated_time` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '更新时间'
--     `is_deleted` TINYINT(1) NOT NULL DEFAULT 0 COMMENT '是否删除'
-- )
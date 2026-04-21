create database zener;

USE zener;
CREATE TABLE IF NOT EXISTS user (
    username char(50) NULL,
    password char(50) NULL
)ENGINE=InnoDB;

INSERT INTO user(username, password) VALUES('evan', '123456');


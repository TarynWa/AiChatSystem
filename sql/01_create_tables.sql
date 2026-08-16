-- ============================================================
-- chat 数据库建表脚本
-- 项目：基于muduo C++ Reactor模型的Linux集群版聊天室
-- 设计原则：
--   1. 所有主键使用BIGINT，预留分布式ID生成（如雪花算法）扩展性
--   2. 字符集统一utf8mb4，支持emoji与多语言
--   3. 关键查询字段建立索引，避免全表扫描
--   4. 时间字段统一DATETIME，由MySQL维护默认值
--   5. 状态字段使用枚举字符串，便于跨节点一致
--   6. 表结构适配集群架构，节点间无状态共享，DB通过连接池访问
-- 执行：mysql -uroot -p < 01_create_tables.sql
-- ============================================================

CREATE DATABASE IF NOT EXISTS chat DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE chat;

-- ============================================================
-- 1. 用户表 User
--    存储用户基本信息，登录/注册核心表
--    password字段存储加盐SHA256哈希值（64位hex），禁止明文入库
--    salt字段独立存储盐值（32位hex），便于密码校验与未来算法升级
--    state字段标识在线状态，集群下需配合Redis共享（后续扩展）
-- ============================================================
DROP TABLE IF EXISTS `User`;
CREATE TABLE `User` (
    `id`         BIGINT       NOT NULL AUTO_INCREMENT COMMENT '用户ID，集群下可替换为雪花ID',
    `name`       VARCHAR(64)  NOT NULL COMMENT '用户名，全局唯一',
    `password`   VARCHAR(128) NOT NULL COMMENT '密码哈希值（SHA256(salt+password)），禁止明文存储',
    `salt`       VARCHAR(64)  NOT NULL DEFAULT '' COMMENT '密码盐值，注册时随机生成',
    `state`      VARCHAR(16)  NOT NULL DEFAULT 'offline' COMMENT '在线状态：online/offline',
    `created_at` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '注册时间',
    `updated_at` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_user_name` (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户基础信息表';

-- ============================================================
-- 2. 好友关系表 Friend
--    记录用户之间的好友关系（单向存储，应用层维护双向一致性）
--    主键(userid, friendid)保证同一对关系不重复
--    idx_friendid 索引支持反向查询某用户被哪些人加为好友
--    预留扩展：可增加 status 字段支持好友请求审核流程
-- ============================================================
DROP TABLE IF EXISTS `Friend`;
CREATE TABLE `Friend` (
    `userid`     BIGINT       NOT NULL COMMENT '发起方用户ID',
    `friendid`   BIGINT       NOT NULL COMMENT '被添加好友ID',
    `created_at` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '建立好友关系时间',
    PRIMARY KEY (`userid`, `friendid`),
    KEY `idx_friendid` (`friendid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='好友关系表';

-- ============================================================
-- 3. 群信息表 GroupInfo
--    群组基本信息，群主通过 creator_id 关联 User
--    id使用BIGINT AUTO_INCREMENT，集群下可替换为分布式ID
-- ============================================================
DROP TABLE IF EXISTS `GroupInfo`;
CREATE TABLE `GroupInfo` (
    `id`         BIGINT       NOT NULL AUTO_INCREMENT COMMENT '群ID',
    `group_name` VARCHAR(128) NOT NULL COMMENT '群名称',
    `group_desc` VARCHAR(256) NOT NULL DEFAULT '' COMMENT '群描述',
    `creator_id` BIGINT       NOT NULL COMMENT '创建者用户ID',
    `created_at` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`id`),
    KEY `idx_creator_id` (`creator_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='群组信息表';

-- ============================================================
-- 4. 群成员表 GroupMember
--    群与用户的关联，role 区分群主与普通成员
--    主键(groupid, userid)保证同一用户在同一群内不重复
--    idx_userid 索引支持查询某用户加入的所有群
-- ============================================================
DROP TABLE IF EXISTS `GroupMember`;
CREATE TABLE `GroupMember` (
    `groupid`    BIGINT       NOT NULL COMMENT '群ID',
    `userid`     BIGINT       NOT NULL COMMENT '用户ID',
    `role`       VARCHAR(16)  NOT NULL DEFAULT 'normal' COMMENT '群内角色：creator/normal',
    `join_time`  DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '加入时间',
    PRIMARY KEY (`groupid`, `userid`),
    KEY `idx_userid` (`userid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='群成员表';

-- ============================================================
-- 5. 离线消息表 OfflineMessage
--    存储用户离线期间收到的私聊与群聊消息，上线后拉取
--    msg_type 区分消息来源：private(私聊)/group(群聊)
--    idx_userid 索引支持按用户ID批量拉取离线消息
--    设计为追加写表，拉取后由应用层删除
--    msg_id 字段：客户端生成的全局唯一幂等键，用于跨节点/跨连接去重
--    seq 字段：每发送方单调递增的顺序键，接收方按seq重排
--    uk_from_msg 唯一索引：防止跨节点 PUBLISH 导致同一消息被多个节点重复入库
-- ============================================================
DROP TABLE IF EXISTS `OfflineMessage`;
CREATE TABLE `OfflineMessage` (
    `id`         BIGINT       NOT NULL AUTO_INCREMENT COMMENT '自增主键',
    `userid`     BIGINT       NOT NULL COMMENT '接收方用户ID（离线消息归属者）',
    `from_id`    BIGINT       NOT NULL DEFAULT 0 COMMENT '发送方用户ID',
    `msg_type`   VARCHAR(16)  NOT NULL DEFAULT 'private' COMMENT '消息类型：private/group',
    `content`    TEXT         NOT NULL COMMENT '消息内容',
    `msg_id`     BIGINT       NOT NULL DEFAULT 0 COMMENT '幂等键：客户端生成的全局唯一ID',
    `seq`        INT          NOT NULL DEFAULT 0 COMMENT '顺序键：每发送方单调递增',
    `created_at` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '消息生成时间',
    PRIMARY KEY (`id`),
    KEY `idx_userid` (`userid`),
    UNIQUE KEY `uk_from_msg` (`from_id`, `msg_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='离线消息表';

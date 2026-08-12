#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
============================================================
 MySQL 聊天室数据库初始化脚本（Python 版）
 项目：基于muduo C++ Reactor模型的Linux集群版聊天室
 作用：执行 01_create_tables.sql 创建 chat 数据库及全部业务表
 用法：
   ./init_db.py                                  # 使用默认配置
   ./init_db.py -u root -p 20050610 -h 127.0.0.1 -P 3306
   MYSQL_PASSWORD=xxx ./init_db.py -u root       # 通过环境变量传密码
 默认配置对齐 chatsystem/mysql.hpp（root/20050610/localhost:3306）
 依赖：系统 mysql 客户端（无需 pip 安装任何 Python 包）
============================================================
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

# ---------- 默认配置（与 chatsystem/mysql.hpp 保持一致） ----------
DEFAULT_USER = os.environ.get("MYSQL_USER", "root")
DEFAULT_PASSWORD = os.environ.get("MYSQL_PASSWORD", "20050610")
DEFAULT_HOST = os.environ.get("MYSQL_HOST", "localhost")
DEFAULT_PORT = int(os.environ.get("MYSQL_PORT", "3306"))

# 期望创建的5张表（与 01_create_tables.sql 一致）
EXPECTED_TABLES = ["User", "Friend", "GroupInfo", "GroupMember", "OfflineMessage"]

# SQL 文件路径（与本脚本同目录）
SQL_FILE = Path(__file__).resolve().parent / "01_create_tables.sql"


def parse_args():
    """解析命令行参数，默认值与 mysql.hpp 保持一致"""
    parser = argparse.ArgumentParser(
        description="MySQL 聊天室数据库初始化脚本",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("-u", "--user", default=DEFAULT_USER, help="MySQL 用户名")
    parser.add_argument("-p", "--password", default=DEFAULT_PASSWORD, help="MySQL 密码")
    parser.add_argument("-H", "--host", default=DEFAULT_HOST, help="MySQL 主机")
    parser.add_argument("-P", "--port", type=int, default=DEFAULT_PORT, help="MySQL 端口")
    return parser.parse_args()


def check_prerequisites():
    """前置检查：SQL文件存在、mysql 客户端可用"""
    if not SQL_FILE.is_file():
        print(f"[ERROR] 建表SQL文件不存在: {SQL_FILE}")
        sys.exit(1)
    try:
        subprocess.run(
            ["mysql", "--version"],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("[ERROR] 未找到 mysql 客户端，请先安装 mysql-client")
        sys.exit(1)


def run_mysql(args, sql_input=None, capture=False):
    """
    调用 mysql 客户端执行 SQL
    通过 MYSQL_PWD 环境变量传密码，避免命令行参数泄露（ps 可见）
    sql_input: 通过 stdin 传入的 SQL 文本；为 None 时仅连接测试
    capture: 是否捕获输出
    """
    env = os.environ.copy()
    env["MYSQL_PWD"] = args.password

    cmd = [
        "mysql",
        f"-h{args.host}",
        f"-P{args.port}",
        f"-u{args.user}",
    ]
    if capture:
        cmd.extend(["-sN"])  # 静默、无列名，便于解析

    return subprocess.run(
        cmd,
        input=sql_input.encode("utf-8") if sql_input else None,
        env=env,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE,
    )


def execute_create_sql(args):
    """执行 01_create_tables.sql 建表"""
    print("=" * 60)
    print(" MySQL 聊天室数据库初始化")
    print("=" * 60)
    print(f" 主机        : {args.host}:{args.port}")
    print(f" 用户        : {args.user}")
    print(f" SQL文件     : {SQL_FILE}")
    print("=" * 60)

    with open(SQL_FILE, "r", encoding="utf-8") as f:
        sql_content = f.read()

    result = run_mysql(args, sql_input=sql_content)
    if result.returncode == 0:
        print("[OK] 建表SQL执行成功")
    else:
        print("[ERROR] 建表SQL执行失败")
        if result.stderr:
            print(result.stderr.decode("utf-8", errors="replace"))
        sys.exit(1)


def verify_tables(args):
    """验证 chat 数据库中期望的表是否齐全"""
    print()
    print("=" * 60)
    print(" 验证 chat 数据库表结构")
    print("=" * 60)

    # 通过 stdin 执行 SHOW TABLES，避免 SQL 注入
    result = run_mysql(args, sql_input="USE chat; SHOW TABLES;", capture=True)
    if result.returncode != 0:
        print("[ERROR] 查询表列表失败")
        if result.stderr:
            print(result.stderr.decode("utf-8", errors="replace"))
        sys.exit(1)

    existing_tables = [
        line.strip() for line in result.stdout.decode("utf-8").splitlines() if line.strip()
    ]

    if not existing_tables:
        print("[ERROR] chat 数据库中未找到任何表，请检查建表日志")
        sys.exit(1)

    all_ok = True
    for tbl in EXPECTED_TABLES:
        if tbl in existing_tables:
            print(f"  [OK]   表 {tbl} 存在")
        else:
            print(f"  [MISS] 表 {tbl} 缺失")
            all_ok = False

    print()
    if all_ok:
        print("=" * 60)
        print(" [SUCCESS] 全部 5 张表初始化完成，可启动聊天室服务")
        print("=" * 60)
        sys.exit(0)
    else:
        print("=" * 60)
        print(" [WARN] 部分表缺失，请检查 01_create_tables.sql")
        print("=" * 60)
        sys.exit(1)


def main():
    args = parse_args()
    check_prerequisites()
    execute_create_sql(args)
    verify_tables(args)


if __name__ == "__main__":
    main()

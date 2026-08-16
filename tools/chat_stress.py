#!/usr/bin/env python3
# ============================================================
# chatsystem 功能测试 + 并发压测脚本
# ============================================================
# 协议说明（与 chatsystem/chatserver.cpp 对齐）：
#   - 帧格式：纯 protobuf 二进制，不加任何长度前缀
#   - 外层信封：chat.BaseMessage（type=消息类型枚举, payload=内层消息序列化字节）
#   - 消息类型枚举（对应 proto 中的 EnMsgType）：
#       LOGIN_MSG=1,  LOGIN_MSG_ACK=2,  REG_MSG=4, REG_MSG_ACK=5
#       ONE_CHAT_MSG=6, ADD_FRIEND_MSG=8, ADD_FRIEND_MSG_ACK=9
#       DEL_FRIEND_MSG=10, CREATE_GROUP_MSG=12, ADD_GROUP_MSG=14
#       GROUP_CHAT_MSG=18
#
# protobuf 轻量级手工编码器（仅实现脚本所需的子集）：
#   - 每个字段 = (field_id << 3 | wire_type) varint + 负载
#   - wire_type: 0=varint, 2=length-delimited(string/bytes/message)
#   - int64/int32: wire 0, varint 编码（64位按原样截断）
#   - string/bytes: wire 2, varint(len) + 字节
# ============================================================
import socket
import struct
import time
import threading
import random
import hashlib
import sys
import os
from concurrent.futures import ThreadPoolExecutor, as_completed
from collections import defaultdict, Counter

# ============================================================
# 配置
# ============================================================
SERVER_ADDR = ("127.0.0.1", 8080)    # 默认走 nginx TCP 负载均衡
# 如果想直接打后端节点，换成： ("127.0.0.1", 6000) / ("127.0.0.1", 6001)
TEST_USER_PREFIX = "stuser_"          # 压测用户名前缀（会拼上 _<unix>_<i>）
TEST_PASSWORD = "Pass1234"            # 统一测试密码
DEFAULT_TIMEOUT = 5.0                 # socket 读写超时（秒）


# ============================================================
# 极简 protobuf varint 编解码器
# ============================================================
def _encode_varint(value):
    """uint64 -> varint 字节串"""
    out = bytearray()
    value &= 0xFFFFFFFFFFFFFFFF
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def _decode_varint(data, pos=0):
    """(uint64, next_pos)"""
    shift = 0
    result = 0
    while True:
        if pos >= len(data):
            raise ValueError("varint 截断")
        b = data[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        shift += 7
        if not (b & 0x80):
            return result & 0xFFFFFFFFFFFFFFFF, pos


# ============================================================
# protobuf 字段编码辅助函数
# ============================================================
def _field_varint(field_id, value):
    """int32/int64/uint64/uint32/enum 字段（wire_type=0）"""
    return _encode_varint((field_id << 3) | 0) + _encode_varint(value)


def _field_length(field_id, payload):
    """string/bytes/embedded message 字段（wire_type=2）"""
    if isinstance(payload, str):
        payload = payload.encode("utf-8")
    return _encode_varint((field_id << 3) | 2) + _encode_varint(len(payload)) + bytes(payload)


# ============================================================
# 业务消息编码（仅实现脚本用到的最小集合）
# ============================================================
def encode_register_request(username, password, new_id=0):
    """REG_MSG (4) payload: RegisterRequest { username=1, password=2, id=3 }"""
    body = (
        _field_length(1, username)
        + _field_length(2, password)
        + _field_varint(3, new_id)
    )
    return body


def encode_login_request(username, password, uid=0, last_ack_seq=0):
    """LOGIN_MSG (1) payload: LoginRequest { username=1, password=2, id=3, last_ack_seq=4 }"""
    body = (
        _field_length(1, username)
        + _field_length(2, password)
        + _field_varint(3, uid)
        + _field_varint(4, last_ack_seq)
    )
    return body


def encode_one_chat(from_id, to_id, content, ts=0, msg_id=0, seq=0):
    """ONE_CHAT_MSG (6) payload: OneChatRequest {from=1,to=2,content=3,ts=4,msg_id=5,seq=6}"""
    return (
        _field_varint(1, from_id)
        + _field_varint(2, to_id)
        + _field_length(3, content)
        + _field_varint(4, ts)
        + _field_varint(5, msg_id)
        + _field_varint(6, seq)
    )


def encode_group_chat(from_id, group_id, content, ts=0, msg_id=0, seq=0):
    """GROUP_CHAT_MSG (18) payload: GroupChatRequest {from=1,group=2,content=3,ts=4,msg_id=5,seq=6}"""
    return (
        _field_varint(1, from_id)
        + _field_varint(2, group_id)
        + _field_length(3, content)
        + _field_varint(4, ts)
        + _field_varint(5, msg_id)
        + _field_varint(6, seq)
    )


def encode_add_friend(from_id, to_id, msg_id=0, seq=0):
    """ADD_FRIEND_MSG (8): AddFriendRequest {from=1, to=2, msg_id=3, seq=4}"""
    return (
        _field_varint(1, from_id)
        + _field_varint(2, to_id)
        + _field_varint(3, msg_id)
        + _field_varint(4, seq)
    )


def encode_del_friend(from_id, to_id, msg_id=0, seq=0):
    """DEL_FRIEND_MSG (10): DelFriendRequest {from=1, to=2, msg_id=3, seq=4}"""
    return (
        _field_varint(1, from_id)
        + _field_varint(2, to_id)
        + _field_varint(3, msg_id)
        + _field_varint(4, seq)
    )


def encode_base_message(msg_type, payload):
    """
    外层信封 BaseMessage
      field 1: EnMsgType type (enum -> varint wire 0)
      field 2: bytes payload (wire 2)
    外加 4 字节大端长度前缀（与服务端 onMessage 分帧逻辑对齐，解决 TCP 粘包）：
      [4-byte len][BaseMessage bytes]
    """
    inner = _field_varint(1, msg_type) + _field_length(2, payload)
    return struct.pack('>I', len(inner)) + inner


# ============================================================
# 解码响应（按需提取关键字段：id/code/msg 等）
# ============================================================
def decode_scalar_fields(data):
    """
    把未知 protobuf 消息按 tag-id 解出字段。
    返回 dict: {field_id: [(wire_type, raw_bytes_or_int), ...]}
    对于 length-delimited 字段，仅返回 bytes 原始负载不递归解码。
    调用方按需再 decode 特定嵌套 message。
    """
    fields = defaultdict(list)
    pos = 0
    while pos < len(data):
        tag, pos = _decode_varint(data, pos)
        wire = tag & 0x7
        fid = tag >> 3
        if wire == 0:
            val, pos = _decode_varint(data, pos)
            fields[fid].append((0, val))
        elif wire == 2:
            ln, pos = _decode_varint(data, pos)
            payload = data[pos : pos + ln]
            pos += ln
            fields[fid].append((2, payload))
        else:
            # 1 / 5 / 3 / 4 在本脚本中不出现，跳过
            if wire == 1:
                pos += 8
            elif wire == 5:
                pos += 4
            else:
                break
    return fields


def extract_int_field(fields, field_id, default=0):
    """从 decode_scalar_fields 结果里取某个 int/enum 字段首值"""
    vals = fields.get(field_id, [])
    if vals and vals[0][0] == 0:
        return int(vals[0][1])
    return default


def extract_str_field(fields, field_id, default=""):
    """取某个 string/bytes 字段首值（utf-8 解码失败则返回空串）"""
    vals = fields.get(field_id, [])
    if vals and vals[0][0] == 2:
        try:
            return vals[0][1].decode("utf-8")
        except Exception:
            return ""
    return default


def extract_bytes_field(fields, field_id, default=b""):
    vals = fields.get(field_id, [])
    if vals and vals[0][0] == 2:
        return vals[0][1]
    return default


def parse_base_message(data):
    """把收到的字节当作 BaseMessage 解码，返回 (type_int, payload_bytes)"""
    fields = decode_scalar_fields(data)
    tp = extract_int_field(fields, 1, 0)
    py = extract_bytes_field(fields, 2, b"")
    return tp, py


# ============================================================
# socket 收发工具
# ============================================================
def recv_all(sock, timeout=DEFAULT_TIMEOUT):
    """
    读一个完整的 protobuf 响应。
    服务端 muduo retrieveAllAsString 一帧就是一个完整消息，
    所以单次 recv 通常能拿到整个 protobuf。
    这里循环读最多 1 秒后返回累积数据。
    """
    sock.settimeout(timeout)
    chunks = []
    start = time.time()
    total = 0
    while time.time() - start < timeout:
        try:
            d = sock.recv(65536)
        except socket.timeout:
            if chunks:
                break
            continue
        except OSError:
            break
        if not d:
            break
        chunks.append(d)
        total += len(d)
        # 只要拿到数据，就按 muduo 的一次 send 一次 onMessage 语义提前返回
        # （除非 10ms 内还能读到更多，说明多个 ACK 粘包）
        time.sleep(0.005)
        try:
            sock.settimeout(0.005)
            d2 = sock.recv(65536)
            if d2:
                chunks.append(d2)
                total += len(d2)
        except Exception:
            pass
        break
    return b"".join(chunks)


def send_and_recv(sock, msg_type, payload_bytes, timeout=DEFAULT_TIMEOUT):
    full = encode_base_message(msg_type, payload_bytes)
    sock.sendall(full)
    return recv_all(sock, timeout=timeout)


def try_split_multi(data):
    """
    按 4 字节大端长度前缀切分粘包数据，与服务端 sendFrame 配对。
    每帧格式：[4-byte len][BaseMessage bytes]
    返回 list[BaseMessage_bytes]（不含长度前缀）
    """
    out = []
    pos = 0
    while pos + 4 <= len(data):
        frame_len = struct.unpack('>I', data[pos:pos + 4])[0]
        pos += 4
        if frame_len <= 0 or pos + frame_len > len(data):
            break  # 帧不完整，丢弃
        out.append(bytes(data[pos:pos + frame_len]))
        pos += frame_len
    return out or ([data] if data else [])


# ============================================================
# 功能测试（单线程串行，验证每个业务）
# ============================================================
class Tester:
    def __init__(self):
        self.registered = []  # [(uid, username, password)]
        self.base = None      # 注册/登录 socket（每次新建）
        self._seq_counter = 0  # 功能测试也维护单调 seq
        self._msg_id_counter = 0

    def _next_msg_id_seq(self):
        """生成下一组 (msg_id, seq)"""
        self._seq_counter += 1
        self._msg_id_counter += 1
        # 功能测试固定 worker_index = 0x70000000，避免与压测 worker 冲突
        msg_id = (0x70000000 << 32) | (self._msg_id_counter & 0xFFFFFFFF)
        return msg_id, self._seq_counter

    def _new_sock(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(DEFAULT_TIMEOUT)
        s.connect(SERVER_ADDR)
        return s

    def register(self, username, password):
        """注册一个用户，返回 (success, uid, msg)"""
        s = self._new_sock()
        try:
            payload = encode_register_request(username, password)
            resp = send_and_recv(s, 4, payload, timeout=30)  # reg 走 DB 异步，给足够时间
            if not resp:
                return False, -1, "空响应"
            # 可能粘包多条消息，找 REG_MSG_ACK(5)
            for frame in try_split_multi(resp):
                tp, py = parse_base_message(frame)
                if tp == 5:
                    fields = decode_scalar_fields(py)
                    code = extract_int_field(fields, 1, 0)
                    msg = extract_str_field(fields, 2, "")
                    uid = extract_int_field(fields, 3, -1)
                    ok = code == 0 and uid > 0  # code=0 表示成功（proto3 省略 0 值，字段缺失=0）
                    return ok, uid, msg
            return False, -1, f"未找到 REG_MSG_ACK，原始响应 {len(resp)} 字节"
        finally:
            try:
                s.close()
            except Exception:
                pass

    def login(self, uid, username, password):
        """登录。注意服务端用 id + name + password 三者校验。"""
        s = self._new_sock()
        payload = encode_login_request(username, password, uid)
        resp = send_and_recv(s, 1, payload, timeout=10)
        if not resp:
            return False, s, "空响应"
        for frame in try_split_multi(resp):
            tp, py = parse_base_message(frame)
            if tp == 2:  # LOGIN_MSG_ACK
                fields = decode_scalar_fields(py)
                rid = extract_int_field(fields, 3, -1)
                # 登录成功：rid == uid；失败：rid == -1
                if rid == uid:
                    return True, s, f"登录成功 uid={rid}"
                else:
                    try:
                        s.close()
                    except Exception:
                        pass
                    return False, None, f"登录失败 ACK id={rid}"
        return False, None, f"未找到 LOGIN_MSG_ACK，原始响应 {len(resp)} 字节"

    def one_chat(self, sock, from_id, to_id, content, wait_ack=True):
        msg_id, seq = self._next_msg_id_seq()
        payload = encode_one_chat(from_id, to_id, content, int(time.time() * 1000), msg_id, seq)
        sock.sendall(encode_base_message(6, payload))
        if not wait_ack:
            return True, ""
        # 等 ONE_CHAT_MSG_ACK (7)
        resp = recv_all(sock, timeout=5)
        for frame in try_split_multi(resp):
            tp, py = parse_base_message(frame)
            if tp == 7:
                fields = decode_scalar_fields(py)
                code = extract_int_field(fields, 1, 0)
                msg = extract_str_field(fields, 2, "")
                return code == 0, msg
        return True, "(无 ACK，发送已完成)"

    def group_chat(self, sock, from_id, group_id, content, wait_ack=True):
        msg_id, seq = self._next_msg_id_seq()
        payload = encode_group_chat(from_id, group_id, content, int(time.time() * 1000), msg_id, seq)
        sock.sendall(encode_base_message(18, payload))
        if not wait_ack:
            return True, ""
        resp = recv_all(sock, timeout=5)
        for frame in try_split_multi(resp):
            tp, py = parse_base_message(frame)
            if tp == 19:
                fields = decode_scalar_fields(py)
                code = extract_int_field(fields, 1, 0)
                msg = extract_str_field(fields, 2, "")
                return code == 0, msg
        return True, "(无 ACK，发送已完成)"

    def add_friend(self, sock, from_id, to_id):
        msg_id, seq = self._next_msg_id_seq()
        payload = encode_add_friend(from_id, to_id, msg_id, seq)
        resp = send_and_recv(sock, 8, payload, timeout=8)
        for frame in try_split_multi(resp):
            tp, py = parse_base_message(frame)
            if tp == 9:
                fields = decode_scalar_fields(py)
                code = extract_int_field(fields, 1, 0)
                msg = extract_str_field(fields, 2, "")
                return code == 0, msg
        return False, "未找到 ADD_FRIEND_MSG_ACK"

    def del_friend(self, sock, from_id, to_id):
        msg_id, seq = self._next_msg_id_seq()
        payload = encode_del_friend(from_id, to_id, msg_id, seq)
        resp = send_and_recv(sock, 10, payload, timeout=8)
        for frame in try_split_multi(resp):
            tp, py = parse_base_message(frame)
            if tp == 11:
                fields = decode_scalar_fields(py)
                code = extract_int_field(fields, 1, 0)
                msg = extract_str_field(fields, 2, "")
                return code == 0, msg
        return False, "未找到 DEL_FRIEND_MSG_ACK"


def run_functional_tests():
    """串行跑完：注册×2 → 登录×2 → 加好友 → 私聊 → 删好友 → 群聊"""
    print("=" * 60)
    print("【功能测试】开始（串行）")
    print("=" * 60)
    tester = Tester()
    run_suffix = str(int(time.time()))
    results = []

    def case(name, success, detail=""):
        results.append((name, success, detail))
        mark = "✅" if success else "❌"
        print(f"  {mark} {name}: {detail}")

    # 1. 注册 userA
    nameA = f"{TEST_USER_PREFIX}{run_suffix}_A"
    ok, uidA, msg = tester.register(nameA, TEST_PASSWORD)
    case(f"注册 {nameA}", ok, f"uid={uidA} msg={msg}")
    if ok:
        tester.registered.append((uidA, nameA, TEST_PASSWORD))

    # 2. 注册 userB
    nameB = f"{TEST_USER_PREFIX}{run_suffix}_B"
    ok, uidB, msg = tester.register(nameB, TEST_PASSWORD)
    case(f"注册 {nameB}", ok, f"uid={uidB} msg={msg}")
    if ok:
        tester.registered.append((uidB, nameB, TEST_PASSWORD))

    # 3. 重复注册 userA → 应失败（用户名已存在）
    ok2, _uid, msg = tester.register(nameA, TEST_PASSWORD)
    case("重复注册应失败", not ok2, f"msg={msg}")

    if len(tester.registered) < 2:
        print("  ⚠️  至少需要 2 个用户才能继续好友/聊天测试，提前结束")
        return results

    # 4. 登录 userA
    ok, sA, msg = tester.login(uidA, nameA, TEST_PASSWORD)
    case(f"登录 uid={uidA} ({nameA})", ok, msg)
    if not ok:
        return results

    # 5. 登录 userB
    ok, sB, msg = tester.login(uidB, nameB, TEST_PASSWORD)
    case(f"登录 uid={uidB} ({nameB})", ok, msg)
    if not ok:
        try:
            sA.close()
        except Exception:
            pass
        return results

    # 6. A 加 B 好友
    ok, msg = tester.add_friend(sA, uidA, uidB)
    case(f"A({uidA}) 加 B({uidB}) 好友", ok, msg)

    # 7. A 给 B 发私聊（服务端 ONE_CHAT 不要求必须是好友，直接投递）
    ok, msg = tester.one_chat(sA, uidA, uidB, f"hello from uid={uidA} at {run_suffix}", wait_ack=True)
    case("A→B 私聊", ok, msg)

    # 8. B 给 A 回私聊
    ok, msg = tester.one_chat(sB, uidB, uidA, f"hi back from uid={uidB}", wait_ack=True)
    case("B→A 私聊回", ok, msg)

    # 8.5 排空 sA 上残留的 B→A 私聊广播，避免干扰后续 ACK 接收
    try:
        sA.settimeout(0.2)
        while True:
            _ = sA.recv(65536)
    except Exception:
        pass

    # 9. A 删 B 好友
    ok, msg = tester.del_friend(sA, uidA, uidB)
    case(f"A({uidA}) 删 B({uidB}) 好友", ok, msg)

    # 10. A 在 B 上线状态下给 B 再发一条私聊（即使删好友也能投递）
    ok, msg = tester.one_chat(sA, uidA, uidB, f"after unfriend uid={uidA}", wait_ack=True)
    case("删好友后仍可发私聊", ok, msg)

    # 11. 关闭 sockets
    try:
        sA.close()
        sB.close()
    except Exception:
        pass

    print()
    passed = sum(1 for _, ok, _ in results if ok)
    print(f"功能测试汇总：{passed}/{len(results)} 用例通过")
    return results


# ============================================================
# 并发压测（阶梯加压，找性能拐点）
# ============================================================
class StressWorker:
    def __init__(self, worker_index, uid, username, password):
        self.uid = uid
        self.username = username
        self.password = password
        self.worker_index = worker_index
        # 客户端维护 msg_id 与 seq（断线重连后不重置，持续累加）
        # msg_id：worker_index 高 32 位 + 自增计数低 32 位，保证全局唯一
        self._seq_counter = 0
        self._msg_id_counter = 0

    def next_msg_id_seq(self):
        """生成下一组 (msg_id, seq)，保证单调递增"""
        self._seq_counter += 1
        self._msg_id_counter += 1
        msg_id = (self.worker_index << 32) | (self._msg_id_counter & 0xFFFFFFFF)
        return msg_id, self._seq_counter

    def connect_and_login(self, last_ack_seq=0):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(DEFAULT_TIMEOUT)
        s.connect(SERVER_ADDR)
        # 登录，回传 last_ack_seq 水位（断线重连时使用）
        payload = encode_login_request(self.username, self.password, self.uid, last_ack_seq)
        s.sendall(encode_base_message(1, payload))
        resp = recv_all(s, timeout=10)
        ok = False
        for frame in try_split_multi(resp):
            tp, py = parse_base_message(frame)
            if tp == 2:
                fields = decode_scalar_fields(py)
                rid = extract_int_field(fields, 3, -1)
                if rid == self.uid:
                    ok = True
                break
        if not ok:
            try:
                s.close()
            except Exception:
                pass
            return None
        return s

    def send_one_round_chat(self, sock, target_uid, round_idx):
        content = f"W{self.worker_index} R{round_idx} {random.randint(0, 10**9)}"
        msg_id, seq = self.next_msg_id_seq()
        payload = encode_one_chat(self.uid, target_uid, content, int(time.time() * 1000), msg_id, seq)
        sock.sendall(encode_base_message(6, payload))

    def send_one_round_register(self, reg_name):
        """建立新 socket，注册，关闭"""
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(DEFAULT_TIMEOUT)
            s.connect(SERVER_ADDR)
            payload = encode_register_request(reg_name, TEST_PASSWORD)
            s.sendall(encode_base_message(4, payload))
            resp = recv_all(s, timeout=10)
            for frame in try_split_multi(resp):
                tp, py = parse_base_message(frame)
                if tp == 5:
                    fields = decode_scalar_fields(py)
                    code = extract_int_field(fields, 1, 0)
                    s.close()
                    return code == 0
            s.close()
        except Exception:
            try:
                s.close()
            except Exception:
                pass
        return False

    # ===== 复用已登录连接的高频业务压测接口 =====
    # 以下方法均假设 sock 已完成 LOGIN，可直接发业务消息。
    def send_one_round_add_friend(self, sock, target_uid):
        """单次加好友：发 ADD_FRIEND_MSG(8) → 循环读直到 ADD_FRIEND_MSG_ACK(9)"""
        msg_id, seq = self.next_msg_id_seq()
        payload = encode_add_friend(self.uid, target_uid, msg_id, seq)
        sock.sendall(encode_base_message(8, payload))
        end = time.time() + 8
        while time.time() < end:
            resp = recv_all(sock, timeout=2)
            for frame in try_split_multi(resp):
                tp, py = parse_base_message(frame)
                if tp == 9:
                    fields = decode_scalar_fields(py)
                    code = extract_int_field(fields, 1, 0)
                    return code == 0
        return False

    def send_one_round_del_friend(self, sock, target_uid):
        """单次删好友：发 DEL_FRIEND_MSG(10) → 循环读直到 DEL_FRIEND_MSG_ACK(11)"""
        msg_id, seq = self.next_msg_id_seq()
        payload = encode_del_friend(self.uid, target_uid, msg_id, seq)
        sock.sendall(encode_base_message(10, payload))
        end = time.time() + 8
        while time.time() < end:
            resp = recv_all(sock, timeout=2)
            for frame in try_split_multi(resp):
                tp, py = parse_base_message(frame)
                if tp == 11:
                    fields = decode_scalar_fields(py)
                    code = extract_int_field(fields, 1, 0)
                    return code == 0
        return False

    def send_one_round_one_chat(self, sock, target_uid, round_idx):
        """单次私聊：发 ONE_CHAT_MSG(6) → 循环读直到 ONE_CHAT_MSG_ACK(7)
        与 connect_chat 模式不同：本方法假定 sock 已经登录，只压"消息发送"环节"""
        content = f"W{self.worker_index} R{round_idx} {random.randint(0, 10**9)}"
        msg_id, seq = self.next_msg_id_seq()
        payload = encode_one_chat(self.uid, target_uid, content, int(time.time() * 1000), msg_id, seq)
        sock.sendall(encode_base_message(6, payload))
        end = time.time() + 5
        while time.time() < end:
            resp = recv_all(sock, timeout=2)
            for frame in try_split_multi(resp):
                tp, py = parse_base_message(frame)
                if tp == 7:
                    fields = decode_scalar_fields(py)
                    code = extract_int_field(fields, 1, 0)
                    return code == 0
        return True  # 无 ACK 也算发送完成

    def send_one_round_group_chat(self, sock, group_id, round_idx):
        """单次群聊：发 GROUP_CHAT_MSG(18) → 循环读直到拿到 GROUP_CHAT_MSG_ACK(19)
        群聊场景下 socket 会收到大量其他成员的广播消息(18)，需跳过它们找到 ACK(19)。"""
        content = f"G{group_id} W{self.worker_index} R{round_idx} {random.randint(0, 10**9)}"
        msg_id, seq = self.next_msg_id_seq()
        payload = encode_group_chat(self.uid, group_id, content, int(time.time() * 1000), msg_id, seq)
        sock.sendall(encode_base_message(18, payload))
        end = time.time() + 8
        while time.time() < end:
            resp = recv_all(sock, timeout=2)
            for frame in try_split_multi(resp):
                tp, py = parse_base_message(frame)
                if tp == 19:
                    fields = decode_scalar_fields(py)
                    code = extract_int_field(fields, 1, 0)
                    return code == 0
                # tp == 18 是其他成员的群聊广播，跳过
        return False

    # ===== Fire-and-forget 批量模式：发完所有请求后统一收 ACK =====
    # 用于群聊/加好友这种 ACK 慢的业务，避免"发一条等一条"导致的 socket buffer 堆积。
    def send_batch_group_chat_fire(self, sock, group_id, n_iters):
        """一次性把 N 条群聊消息发出去，最后统一读 ACK。
        返回 (sent_count, ack_ok_count)。"""
        sent = 0
        for i in range(n_iters):
            content = f"G{group_id} W{self.worker_index} R{i} {random.randint(0, 10**9)}"
            msg_id, seq = self.next_msg_id_seq()
            payload = encode_group_chat(self.uid, group_id, content, int(time.time() * 1000), msg_id, seq)
            try:
                sock.sendall(encode_base_message(18, payload))
                sent += 1
            except Exception:
                break
        # 统一读 ACK
        ack_ok = 0
        end = time.time() + 8
        while time.time() < end and ack_ok < sent:
            try:
                d = sock.recv(65536)
                if not d:
                    break
                for frame in try_split_multi(d):
                    tp, py = parse_base_message(frame)
                    if tp == 19:
                        fields = decode_scalar_fields(py)
                        code = extract_int_field(fields, 1, 0)
                        if code == 0:
                            ack_ok += 1
            except socket.timeout:
                continue
        return sent, ack_ok

    def send_batch_add_friend_fire(self, sock, target_uids):
        """批量加好友：对 target_uids 列表中每个 uid 发一次 ADD_FRIEND_MSG。
        最后统一读 ACK。返回 (sent, ack_ok)。"""
        sent = 0
        for tgt in target_uids:
            msg_id, seq = self.next_msg_id_seq()
            payload = encode_add_friend(self.uid, tgt, msg_id, seq)
            try:
                sock.sendall(encode_base_message(8, payload))
                sent += 1
            except Exception:
                break
        ack_ok = 0
        end = time.time() + 8
        while time.time() < end and ack_ok < sent:
            try:
                d = sock.recv(65536)
                if not d:
                    break
                for frame in try_split_multi(d):
                    tp, py = parse_base_message(frame)
                    if tp == 9:
                        fields = decode_scalar_fields(py)
                        code = extract_int_field(fields, 1, 0)
                        if code == 0:
                            ack_ok += 1
            except socket.timeout:
                continue
        return sent, ack_ok


def _task(typ, worker, n_iters, target_uid, run_suffix, latencies, counter, lock, all_uids=None):
    """单个 worker 的单轮任务。
    typ ∈ {"connect_chat", "register", "one_chat", "add_friend", "del_friend", "group_chat"}
    all_uids: 已注册用户 uid 列表（add_friend 模式下用于挑选有效目标，避免命中不存在的用户）
    """
    lat_list = []
    ok, err = 0, 0
    try:
        if typ == "connect_chat":
            t0 = time.perf_counter()
            sock = worker.connect_and_login()
            conn_lat = (time.perf_counter() - t0) * 1000
            if sock is None:
                err = n_iters
                raise RuntimeError("connect+login 失败")
            lat_list.append(("login", conn_lat))
            for i in range(n_iters):
                t1 = time.perf_counter()
                worker.send_one_round_chat(sock, target_uid, i)
                chat_lat = (time.perf_counter() - t1) * 1000
                lat_list.append(("chat", chat_lat))
                ok += 1
            try:
                sock.close()
            except Exception:
                pass
        elif typ == "register":
            for i in range(n_iters):
                t0 = time.perf_counter()
                name = f"{TEST_USER_PREFIX}{run_suffix}_S{worker.worker_index}_I{i}_{random.randint(0, 10**9)}"
                good = worker.send_one_round_register(name)
                lat = (time.perf_counter() - t0) * 1000
                lat_list.append(("reg", lat))
                if good:
                    ok += 1
                else:
                    err += 1
        elif typ in ("one_chat", "add_friend", "del_friend", "group_chat"):
            # 复用已登录 socket 的高频业务压测
            sock = worker.connect_and_login()
            if sock is None:
                err = n_iters
                raise RuntimeError("connect+login 失败")
            # 不同模式有不同的目标 id 和延迟 key
            lat_key = {
                "one_chat": "chat",
                "add_friend": "add",
                "del_friend": "del",
                "group_chat": "grp",
            }[typ]

            if typ == "group_chat":
                # 同步模式：发一条→等 ACK→发下一条。
                # 不用 Fire-and-forget：服务端 onMessage 调 retrieveAllAsString()
                # 一次性把整个缓冲区当 1 条 protobuf 解析，多条消息粘包会被整体丢弃。
                for i in range(n_iters):
                    t0 = time.perf_counter()
                    good = worker.send_one_round_group_chat(sock, target_uid, i)
                    lat = (time.perf_counter() - t0) * 1000
                    lat_list.append((lat_key, lat))
                    if good:
                        ok += 1
                    else:
                        err += 1
            elif typ == "add_friend":
                # 同步模式：发一条→等 ACK→发下一条。
                # 不用 Fire-and-forget：服务端 onMessage 调 retrieveAllAsString()
                # 一次性把整个缓冲区当 1 条 protobuf 解析，多条消息粘包会被整体丢弃。
                if all_uids and len(all_uids) > 1:
                    candidates = [u for u in all_uids if u != worker.uid]
                else:
                    candidates = []
                for i in range(n_iters):
                    if candidates:
                        tgt = candidates[(worker.worker_index * 7 + i) % len(candidates)]
                    else:
                        tgt = target_uid
                    t0 = time.perf_counter()
                    good = worker.send_one_round_add_friend(sock, tgt)
                    lat = (time.perf_counter() - t0) * 1000
                    lat_list.append((lat_key, lat))
                    if good:
                        ok += 1
                    else:
                        err += 1
            else:
                # one_chat / del_friend 用单条同步模式（业务本身 ACK 快）
                for i in range(n_iters):
                    t0 = time.perf_counter()
                    if typ == "one_chat":
                        good = worker.send_one_round_one_chat(sock, target_uid, i)
                    else:  # del_friend
                        good = worker.send_one_round_del_friend(sock, target_uid)
                    lat = (time.perf_counter() - t0) * 1000
                    lat_list.append((lat_key, lat))
                    if good:
                        ok += 1
                    else:
                        err += 1
            try:
                sock.close()
            except Exception:
                pass
    except Exception as ex:
        err = max(err, 1)
        lat_list.append(("error", 0.0))
        with lock:
            counter[str(ex)] += 1
    with lock:
        for k, v in lat_list:
            latencies[k].append(v)
        counter["ok"] += ok
        counter["err"] += err


def percentile(values, p):
    if not values:
        return 0.0
    vs = sorted(values)
    k = (len(vs) - 1) * (p / 100.0)
    f = int(k)
    c = f + 1 if f + 1 < len(vs) else f
    if f == c:
        return vs[f]
    return vs[f] + (vs[c] - vs[f]) * (k - f)


def _do_concurrency_step(typ, workers, per_worker_iters, target_uid, run_suffix, step_name, all_uids=None):
    """一个并发级别下执行所有 worker，汇总耗时、延迟、成功率"""
    latencies = defaultdict(list)
    counter = Counter({"ok": 0, "err": 0})
    lock = threading.Lock()

    t_start = time.perf_counter()
    with ThreadPoolExecutor(max_workers=len(workers)) as ex:
        futs = []
        for w in workers:
            futs.append(ex.submit(_task, typ, w, per_worker_iters, target_uid, run_suffix, latencies, counter, lock, all_uids))
        for f in as_completed(futs):
            try:
                f.result()
            except Exception as ex:
                with lock:
                    counter[str(ex)] += 1
                    counter["err"] += 1
    elapsed = time.perf_counter() - t_start
    total_ops = counter["ok"] + counter["err"]
    succ_rate = (counter["ok"] / total_ops * 100.0) if total_ops else 0.0
    total_ops_ok = counter["ok"]
    rps = total_ops_ok / elapsed if elapsed > 0 else 0

    # 延迟统计 key（不同模式对应不同业务延迟维度）
    key = {
        "connect_chat": "chat",
        "register": "reg",
        "one_chat": "chat",
        "add_friend": "add",
        "del_friend": "del",
        "group_chat": "grp",
    }.get(typ, "chat")
    vals = latencies.get(key, [])
    lat_p50 = percentile(vals, 50)
    lat_p95 = percentile(vals, 95)
    lat_p99 = percentile(vals, 99)
    login_p95 = percentile(latencies.get("login", []), 95)

    print(
        f"  [{step_name}] workers={len(workers):>4} | per_worker={per_worker_iters} | time={elapsed:5.2f}s "
        f"| success={counter['ok']}/{total_ops} ({succ_rate:5.1f}%) | RPS={rps:7.0f} "
        f"| P50/95/99-{key}={lat_p50:5.1f}/{lat_p95:5.1f}/{lat_p99:5.1f}ms"
        + (f" | login-P95={login_p95:5.1f}ms" if typ == "connect_chat" and login_p95 else "")
    )
    return {
        "step": step_name,
        "workers": len(workers),
        "per_worker_iters": per_worker_iters,
        "elapsed": elapsed,
        "success": counter["ok"],
        "total": total_ops,
        "success_rate": succ_rate,
        "rps": rps,
        "lat_p50": lat_p50,
        "lat_p95": lat_p95,
        "lat_p99": lat_p99,
        "login_p95": login_p95,
        "errors": {k: v for k, v in counter.items() if k not in ("ok", "err")},
    }


def run_stress_test(staircase, mode="connect_chat"):
    """
    阶梯式并发压测：
      staircase: [(并发用户数, 每用户操作次数)]  列表
      mode:
        "connect_chat" —— 每个 worker: 建连→登录→N 次私聊（含建连成本）
        "register"     —— 每个 worker: N 次独立注册（含建连成本）
        "one_chat"     —— 每个 worker: 建连→登录→N 次私聊（只压消息发送环节）
        "add_friend"   —— 每个 worker: 建连→登录→N 次加好友（异步 DB 写）
        "del_friend"   —— 每个 worker: 建连→登录→N 次删好友（异步 DB 写）
        "group_chat"   —— 每个 worker: 建连→登录→N 次群聊（含 Redis 跨节点/离线存储）
    """
    run_suffix = str(int(time.time()))
    print()
    print("=" * 60)
    print(f"【并发压测】模式={mode} 阶梯={staircase}")
    print(f"【目标】Server={SERVER_ADDR[0]}:{SERVER_ADDR[1]}")
    print("=" * 60)

    all_users = []   # [(uid, name, pw)]
    # 先批量注册足够的用户（串行，避免并发注册导致锁冲突）
    total_users_needed = max(n for n, _ in staircase)
    print(f"  [前置] 批量注册 {total_users_needed} 个测试用户（串行）… ", end="", flush=True)
    start = time.time()
    base = Tester()
    for i in range(total_users_needed):
        name = f"{TEST_USER_PREFIX}{run_suffix}_U{i}"
        ok, uid, msg = base.register(name, TEST_PASSWORD)
        if not ok and "用户名已存在" in msg:
            # 撞名了（同前缀前次测试残留），改名重试
            name = f"{TEST_USER_PREFIX}{run_suffix}_Z{i}_{random.randint(0, 10**6)}"
            ok, uid, msg = base.register(name, TEST_PASSWORD)
        if ok:
            all_users.append((uid, name, TEST_PASSWORD))
    print(f"成功注册 {len(all_users)}/{total_users_needed}，用时 {time.time()-start:.1f}s")

    if len(all_users) < 2:
        print("  ❌ 注册用户太少，无法压测。检查 DB 是否正常 / 用户是否已被批量占用。")
        return None

    # 选第一个用户作为所有 worker 的私聊/加好友/删好友 接收方 target
    target_uid = all_users[0][0]

    # 群聊模式特殊：需要先创建一个群组并让所有 worker 用户加入
    if mode == "group_chat":
        # 限制群规模为 16 人（真实业务群一般 10~50 人，太大时单条消息广播会拖垮 ACK）
        group_size = min(16, len(all_users))
        print(f"  [前置] 创建测试群组（规模={group_size}）并让用户加群 … ", end="", flush=True)
        g_start = time.time()
        # 用第一个用户登录创建群
        s = socket.socket(); s.settimeout(DEFAULT_TIMEOUT); s.connect(SERVER_ADDR)
        payload = encode_login_request(all_users[0][1], all_users[0][2], all_users[0][0])
        s.sendall(encode_base_message(1, payload))
        recv_all(s, timeout=5)
        # 构造 CreateGroupRequest {creator_id=1, group_name=2, group_desc=3}
        grp_body = (
            _field_varint(1, all_users[0][0])
            + _field_length(2, f"StressGroup_{run_suffix}")
            + _field_length(3, "for stress test")
        )
        s.sendall(encode_base_message(12, grp_body))
        resp = recv_all(s, timeout=8)
        group_id = -1
        for frame in try_split_multi(resp):
            tp, py = parse_base_message(frame)
            if tp == 13:  # CREATE_GROUP_MSG_ACK
                fields = decode_scalar_fields(py)
                group_id = extract_int_field(fields, 3, -1)
                break
        s.close()
        if group_id <= 0:
            print(f"❌ 创建群组失败")
            return None
        # 让 group_size 个用户加群（串行，避免 DB 锁冲突）
        added = 1  # 创建者自动入群
        for uid, name, pw in all_users[1:group_size]:
            s2 = socket.socket(); s2.settimeout(DEFAULT_TIMEOUT); s2.connect(SERVER_ADDR)
            lp = encode_login_request(name, pw, uid)
            s2.sendall(encode_base_message(1, lp))
            recv_all(s2, timeout=5)
            # AddGroupRequest {user_id=1, group_id=2}
            ag_body = _field_varint(1, uid) + _field_varint(2, group_id)
            s2.sendall(encode_base_message(14, ag_body))
            recv_all(s2, timeout=5)
            s2.close()
            added += 1
        print(f"群组 id={group_id}，{added} 人入群，用时 {time.time()-g_start:.1f}s")
        # group_chat 模式下 target_uid 复用为 group_id
        target_uid = group_id

    steps_out = []
    # 记录前一级别 RPS，若连续 2 级不再增长，认为到拐点
    peak_rps = 0.0
    flat_count = 0

    for idx, (n_workers, n_iters) in enumerate(staircase):
        # 取已注册用户分配给 workers
        users = all_users[:n_workers]
        workers = [
            StressWorker(i, users[i % len(users)][0], users[i % len(users)][1], users[i % len(users)][2])
            for i in range(n_workers)
        ]
        # add_friend 模式需要完整 uid 列表用于挑选有效目标
        all_uids = [u[0] for u in all_users] if mode == "add_friend" else None
        step_result = _do_concurrency_step(
            mode, workers, n_iters, target_uid, run_suffix,
            f"S{idx+1} {mode}", all_uids
        )
        steps_out.append(step_result)

        if step_result["rps"] > peak_rps + 50:  # 至少涨 50 才算有进展
            peak_rps = step_result["rps"]
            flat_count = 0
        else:
            flat_count += 1
            if flat_count >= 2:
                print(f"  ℹ️  连续 {flat_count} 级 RPS 无增长，认为已达拐点，提前停止。")
                break
        if step_result["success_rate"] < 70:
            print(f"  ⚠️  成功率 < 70% ({step_result['success_rate']:.1f}%)，提前停止。")
            break

    # === 输出报告 ===
    print()
    print("=" * 60)
    print("压测报告")
    print("=" * 60)
    header = f"  {'step':<12} {'workers':>6} {'RPS':>8} {'succ%':>6} {'P50ms':>7} {'P95ms':>7} {'P99ms':>7} {'login95':>8}"
    print(header)
    print("  " + "-" * (len(header) - 2))
    for s in steps_out:
        print(
            f"  {s['step']:<12} {s['workers']:>6} {s['rps']:>8.0f} "
            f"{s['success_rate']:>6.1f} {s['lat_p50']:>7.1f} {s['lat_p95']:>7.1f} "
            f"{s['lat_p99']:>7.1f} {s.get('login_p95', 0):>8.1f}"
        )
    if steps_out:
        best = max(steps_out, key=lambda x: x["rps"])
        print(f"\n  🚀 峰值 RPS: {best['rps']:.0f}（在 {best['step']} workers={best['workers']}）")
        last = steps_out[-1]
        if last["success_rate"] < 70 or flat_count >= 2:
            print(f"  🧨 并发拐点: 约 {best['workers']} 并发（超过后 RPS 停止增长或成功率暴跌）")
        else:
            print(f"  ℹ️  还未到拐点，可加大 staircase 列表最后一级继续往上测。")

        # 打印非 ok/err 类的错误汇总（仅展示 top5）
        err_counter = Counter()
        for s in steps_out:
            for k, v in s["errors"].items():
                if v:
                    err_counter[k] += v
        if err_counter:
            print("\n  错误分布 TOP5:")
            for k, v in err_counter.most_common(5):
                print(f"     ×{v:<5} {k[:120]}")

    return steps_out


# ============================================================
# CLI 入口
# ============================================================
def usage():
    name = sys.argv[0]
    print(f"""用法：
  python3 {name} functional                         # 功能测试（注册/登录/加删好友/私聊）
  python3 {name} stress_chat [阶梯]                # 连接+登录+私聊压测（含建连成本）
  python3 {name} stress_reg  [阶梯]                # 注册压测（每请求独立建连，DB写）
  python3 {name} stress_one_chat [阶梯]             # 已登录后纯私聊压测（只测消息投递）
  python3 {name} stress_add_friend [阶梯]           # 加好友压测（异步DB双向插入）
  python3 {name} stress_del_friend [阶梯]           # 删好友压测（异步DB双向删除）
  python3 {name} stress_group_chat [阶梯]           # 群聊压测（含建群+加群+群广播）
  python3 {name} all                               # 功能测试 + chat 压测 + reg 压测

阶梯格式（可选）：并发数:每用户操作次数[,并发数:每用户操作次数…]
  例：
    python3 {name} stress_one_chat
        → 使用默认阶梯 16:20,32:20,64:20,128:20,256:20
    python3 {name} stress_group_chat 16:10,32:10,64:10
""")


def parse_stair(s):
    out = []
    for part in s.split(","):
        part = part.strip()
        if not part:
            continue
        a, _, b = part.partition(":")
        a = int(a.strip())
        b = int(b.strip()) if b else 10
        out.append((a, b))
    return out or [(32, 10), (64, 10), (128, 10), (256, 10), (512, 10), (768, 10), (1024, 10)]


def main():
    args = sys.argv[1:]
    mode = args[0] if args else "all"

    global SERVER_ADDR
    env_host = os.environ.get("CHAT_HOST")
    env_port = os.environ.get("CHAT_PORT")
    if env_host and env_port:
        SERVER_ADDR = (env_host, int(env_port))
    env_timeout = os.environ.get("CHAT_TIMEOUT")
    if env_timeout:
        global DEFAULT_TIMEOUT
        DEFAULT_TIMEOUT = float(env_timeout)

    print(f"目标服务器: {SERVER_ADDR[0]}:{SERVER_ADDR[1]}  timeout={DEFAULT_TIMEOUT}s")

    if mode == "-h" or mode == "--help":
        usage()
        return

    if mode == "functional":
        run_functional_tests()
    elif mode == "stress_chat":
        stair = parse_stair(args[1]) if len(args) > 1 else parse_stair("")
        run_stress_test(stair, mode="connect_chat")
    elif mode == "stress_reg":
        stair = parse_stair(args[1]) if len(args) > 1 else parse_stair("16:3,32:3,64:3,96:3,128:3")
        run_stress_test(stair, mode="register")
    elif mode == "stress_one_chat":
        stair = parse_stair(args[1]) if len(args) > 1 else parse_stair("16:20,32:20,64:20,128:20,256:20")
        run_stress_test(stair, mode="one_chat")
    elif mode == "stress_add_friend":
        stair = parse_stair(args[1]) if len(args) > 1 else parse_stair("16:5,32:5,64:5,128:5")
        run_stress_test(stair, mode="add_friend")
    elif mode == "stress_del_friend":
        stair = parse_stair(args[1]) if len(args) > 1 else parse_stair("16:5,32:5,64:5,128:5")
        run_stress_test(stair, mode="del_friend")
    elif mode == "stress_group_chat":
        stair = parse_stair(args[1]) if len(args) > 1 else parse_stair("16:10,32:10,64:10,128:10")
        run_stress_test(stair, mode="group_chat")
    elif mode == "all":
        print("=" * 70)
        print("一、功能测试")
        print("=" * 70)
        run_functional_tests()
        print()
        print("=" * 70)
        print("二、连接+私聊压测（含建连登录成本）")
        print("=" * 70)
        run_stress_test(parse_stair(""), mode="connect_chat")
        print()
        print("=" * 70)
        print("三、注册压测（每个请求独立建连，对 DB 压力更大）")
        print("=" * 70)
        run_stress_test(parse_stair("16:3,32:3,64:3,96:3,128:3"), mode="register")
    else:
        usage()


if __name__ == "__main__":
    main()

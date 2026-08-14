/* ============================================================
   app.js - Tauri 聊天室前端逻辑（原生 JS，不引入框架）
   ============================================================
   分层：
     UI 层   →  DOM 操作 / 渲染 / 表单提交
     IPC 层  →  invoke() 发送 Tauri Command 到 Rust 后端
     事件层  →  listen() 订阅 Rust 层推送的 Tauri 事件

   交互链路（详细）：
     1. 用户点击登录按钮
         onLogin() → invoke('login', username, password)
                 → Rust main.rs:login()
                 → TCP.send_msg(1=LOGIN_MSG, LoginRequest.encode())
                 → 4B 长度前缀 + protobuf → Muduo 服务端
     2. 服务端响应
         Muduo → 4B 长度 + BaseMessage(LOGIN_MSG_ACK)
             → Rust tcp.rs reader_loop recv()
             → try_parse_frame() 解码 → dispatch()
             → app.emit('chat.login_ack', payload)
             → 前端 app.js listen('chat.login_ack')
             → handleLoginAck() 渲染结果 / 切换主页面

   事件名（与 Rust tcp.rs 中的常量保持一致）：
     chat.login_ack      登录响应       { ok, id, username, msg, need_relogin? }
     chat.reg_ack        注册响应       { ok, code, msg, id }
     chat.one_chat       收到私聊消息   { from_id, to_id, content, timestamp_ms, is_self }
     chat.group_chat     收到群聊消息   { from_id, group_id, content, timestamp_ms, is_self }
     chat.one_chat_ack   私聊发送回执   { ok, code, msg }
     chat.group_chat_ack 群聊发送回执   { ok, code, msg }
     chat.add_friend_ack 添加好友响应   { ok, code, msg }
     chat.del_friend_ack 删除好友响应   { ok, code, msg }
     chat.friend_list    好友列表推送（预留）
     chat.loginout_ack   注销响应       { ok }
     chat.net_status     网络状态变化   { status: connected/disconnected/reconnecting, addr? }
   ============================================================ */

// ============================================================
// 全局状态
// ============================================================
const Store = {
    me: null,              // 当前登录用户 { id, name }
    friends: new Map(),    // 好友 Map<id, {id, name, state}>
    groups: new Map(),     // 群组 Map<id, {id, name, desc, members:[]}>
    activeChat: null,      // 当前会话 { type: 'friend'|'group', id, name }
    messages: [],          // 当前会话聊天消息列表
    pending: new Set(),    // 正在等待响应的请求（用于防止重复提交）
};

// ============================================================
// Tauri IPC 封装：兼容 Tauri 1.x / 2.x / 浏览器调试（mock）
// ============================================================
// 兼容链路（按优先级）：
//   1. Tauri 2.x ESM (@tauri-apps/api) —— import('@tauri-apps/api/core')
//      （若有网络或有本地安装）Tauri 2.x 推荐做法，不依赖全局注入
//   2. Tauri 2.x 全局注入 —— window.__TAURI__.core.invoke
//      （需 tauri.conf.json 中 app.withGlobalTauri = true）
//   3. Tauri 1.x 全局注入 —— window.__TAURI__.invoke
//   4. 浏览器调试 Mock（兜底）
// 为避免 module 动态 import 与同步脚本的时序问题，采用懒初始化：
//   Tauri.ready() 统一触发初始化，并缓存结果。
// ============================================================
const Tauri = (() => {
    // ---------- 内部缓存 ----------
    let _coreApi = null;   // @tauri-apps/api/core（ESM 导入结果）
    let _eventApi = null;  // @tauri-apps/api/event
    let _initPromise = null;
    const _mockListeners = {};

    // ---------- 初始化入口（懒加载） ----------
    function ready() {
        if (_initPromise) return _initPromise;
        _initPromise = (async () => {
            try {
                // 优先级 1：@tauri-apps/api/core ESM（Tauri 2.x 推荐）
                try {
                    // Tauri 2.x 的 WebView 自带 IPC 协议解析，裸 import 路径会重定向到
                    // tauri:// 特殊协议；失败则回退到 CDN
                    const mod = await import('@tauri-apps/api/core').catch(async () => {
                        return await import('https://unpkg.com/@tauri-apps/api@2.1.0/dist/core.min.js');
                    });
                    const evMod = await import('@tauri-apps/api/event').catch(async () => {
                        return await import('https://unpkg.com/@tauri-apps/api@2.1.0/dist/event.min.js');
                    });
                    if (mod && typeof mod.invoke === 'function') {
                        _coreApi = mod;
                        _eventApi = evMod || mod;
                        return 'esm';
                    }
                } catch (_) { /* ESM 失败，继续尝试全局对象 */ }

                // 优先级 2 / 3：window.__TAURI__ 全局对象
                const g = window.__TAURI__;
                if (g) {
                    if (g.core && typeof g.core.invoke === 'function') return 'global-2x';
                    if (typeof g.invoke === 'function') return 'global-1x';
                }
            } catch (_) { /* ignore */ }
            // 优先级 4：mock 模式（浏览器调试或没有 Tauri）
            return 'mock';
        })();
        return _initPromise;
    }

    // ---------- invoke 实现 ----------
    async function invoke(cmd, args) {
        const mode = await ready();
        if (mode === 'esm') {
            return _coreApi.invoke(cmd, args || {});
        }
        const g = window.__TAURI__;
        if (g && g.core && typeof g.core.invoke === 'function') {
            return g.core.invoke(cmd, args || {});
        }
        if (g && typeof g.invoke === 'function') {
            return g.invoke(cmd, args || {});
        }
        // -------- mock --------
        console.warn('[Tauri Mock] invoke', cmd, args);
        return new Promise((res) => {
            setTimeout(() => {
                if (cmd === 'register') {
                    const a = args || {};
                    if (a.username && a.username.startsWith('dup')) {
                        emitMock('chat.reg_ack', { ok:false, code:2, msg:'用户名已存在', id:-1 });
                    } else {
                        emitMock('chat.reg_ack', { ok:true, code:0, msg:'注册成功', id: 1000 + Math.floor(Math.random()*1000) });
                    }
                    res('mock');
                } else if (cmd === 'login') {
                    const a = args || {};
                    const uid = 2000 + Math.floor(Math.random()*100);
                    emitMock('chat.login_ack', { ok:true, id:uid, username: a.username, msg:'登录成功' });
                    res('mock');
                } else if (cmd === 'logout') {
                    emitMock('chat.loginout_ack', { ok:true });
                    res('mock');
                } else if (cmd === 'get_net_status') {
                    res({ connected: false, server_addr: '' });
                } else {
                    res('mock');
                }
            }, 300);
        });
    }

    // ---------- listen 实现 ----------
    async function listen(ev, cb) {
        const mode = await ready();
        if (mode === 'esm') {
            const l = _eventApi.listen || (_eventApi.on ? (n, fn) => _eventApi.on(n, { handler: fn }) : null);
            if (typeof l === 'function') {
                return l(ev, (e) => cb(e && (e.payload || e)));
            }
        }
        const g = window.__TAURI__;
        if (g && typeof g.event === 'object') {
            const fn = g.event;
            const l = fn.listen || fn.on;
            if (typeof l === 'function') return l(ev, (e) => cb(e && (e.payload || e)));
        }
        // -------- mock --------
        (_mockListeners[ev] ||= []).push(cb);
        return () => {
            const arr = _mockListeners[ev] || [];
            const i = arr.indexOf(cb);
            if (i >= 0) arr.splice(i, 1);
        };
    }

    // mock emit
    function emitMock(ev, payload) {
        const arr = _mockListeners[ev];
        if (arr) arr.forEach(cb => cb(payload));
    }

    return { ready, invoke, listen };
})();

// ============================================================
// 页面初始化：绑定事件监听 + 订阅 Tauri 事件
// ============================================================
window.addEventListener('DOMContentLoaded', async () => {
    // ---- 绑定 Tab 切换（登录/注册/服务器） ----
    document.querySelectorAll('#page-login .tab-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            const tab = btn.dataset.tab;
            document.querySelectorAll('#page-login .tab-btn').forEach(b => b.classList.toggle('active', b === btn));
            document.querySelectorAll('#page-login .auth-form').forEach(f => {
                f.classList.toggle('active', f.id === 'form-' + tab);
            });
        });
    });
    // ---- 绑定群组对话框 Tab ----
    document.querySelectorAll('#dlg-group .tab-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            const gtab = btn.dataset.gtab;
            document.querySelectorAll('#dlg-group .tab-btn').forEach(b => b.classList.toggle('active', b === btn));
            document.querySelectorAll('#dlg-group .gtab').forEach(g => {
                g.classList.toggle('active', g.id === 'gtab-' + gtab);
            });
        });
    });

    // ---- 订阅 Rust 层推送的 Tauri 事件 ----
    Tauri.listen('chat.login_ack', handleLoginAck);
    Tauri.listen('chat.reg_ack', handleRegAck);
    Tauri.listen('chat.one_chat', handleOneChat);
    Tauri.listen('chat.group_chat', handleGroupChat);
    Tauri.listen('chat.one_chat_ack', (p) => ackShow('私聊', p));
    Tauri.listen('chat.group_chat_ack', (p) => ackShow('群聊', p));
    Tauri.listen('chat.add_friend_ack', handleAddFriendAck);
    Tauri.listen('chat.del_friend_ack', handleDelFriendAck);
    Tauri.listen('chat.friend_list', handleFriendList);
    Tauri.listen('chat.loginout_ack', handleLogoutAck);
    Tauri.listen('chat.net_status', handleNetStatus);

    // ---- 初始网络状态 ----
    try {
        const st = await Tauri.invoke('get_net_status');
        updateNetDot(st && st.connected);
    } catch(e) { /* ignore */ }
});

// ============================================================
// Toast 工具
// ============================================================
function toast(msg, type = 'info', duration = 2500) {
    const c = document.getElementById('toast-container');
    const el = document.createElement('div');
    el.className = 'toast ' + type;
    el.textContent = msg;
    c.appendChild(el);
    requestAnimationFrame(() => el.classList.add('show'));
    setTimeout(() => {
        el.classList.remove('show');
        setTimeout(() => el.remove(), 400);
    }, duration);
}

// ============================================================
// 调试日志（右下角调试面板显示）
// ============================================================
let _debugLines = [];
function debugLog(msg, tag) {
    const ts = new Date().toLocaleTimeString();
    const t = tag ? `[${tag}] ` : '';
    _debugLines.push(`${ts} ${t}${String(msg)}`);
    if (_debugLines.length > 200) _debugLines.shift();
    const el = document.getElementById('debug-log');
    if (el) {
        el.textContent = _debugLines.join('\n');
        el.scrollTop = el.scrollHeight;
    }
    if (tag === 'error' || tag === 'warn') {
        console[tag === 'error' ? 'error' : 'warn']('[debug]', msg);
    } else {
        console.log('[debug]', msg);
    }
}

// ============================================================
// 初始化诊断入口：页面加载立即执行，不依赖 DOMContentLoaded
// 目的：尽早检测 window.__TAURI__ 是否存在
// ============================================================
(function bootDiag() {
    const g = typeof window !== 'undefined' ? window.__TAURI__ : undefined;
    debugLog(`window.__TAURI__ 存在: ${g ? 'YES' : 'NO'}`, 'boot');
    if (g) {
        debugLog(`  __TAURI__.core: ${g.core ? 'YES' : 'NO'}`, 'boot');
        debugLog(`  __TAURI__.core.invoke: ${g.core && typeof g.core.invoke === 'function' ? 'YES' : 'NO'}`, 'boot');
        debugLog(`  __TAURI__.invoke: ${typeof g.invoke === 'function' ? 'YES' : 'NO'}`, 'boot');
        debugLog(`  __TAURI__.event: ${typeof g.event === 'object' ? 'YES' : 'NO'}`, 'boot');
    }
})();

// ============================================================
// Tab / Modal 工具函数
// ============================================================
function closeModal(m) { m.classList.remove('active'); m.style.display = 'none'; }
function showAddFriend() {
    const d = document.getElementById('dlg-add-friend');
    d.style.display = 'flex'; d.classList.add('active');
    document.getElementById('add-friend-id').value = '';
}
function showGroupDialog() {
    const d = document.getElementById('dlg-group');
    d.style.display = 'flex'; d.classList.add('active');
}

// ============================================================
// 注册流程
//   前端校验 → invoke('register') → Rust 发送 REG_MSG
//   → Rust 收到 REG_MSG_ACK → emit('chat.reg_ack') → handleRegAck
// ============================================================
function onRegister(e) {
    e.preventDefault();
    const u = document.getElementById('reg-username').value.trim();
    const p = document.getElementById('reg-password').value;
    const p2 = document.getElementById('reg-password2').value;
    const statusEl = document.getElementById('reg-status');
    statusEl.textContent = '';
    debugLog(`注册按钮点击 username=${u}`, 'click');

    if (u.length < 1) { statusEl.textContent = '请输入用户名'; debugLog('校验失败：用户名空', 'warn'); return false; }
    if (p.length < 6) { statusEl.textContent = '密码至少 6 位'; debugLog('校验失败：密码 < 6 位', 'warn'); return false; }
    if (p !== p2)    { statusEl.textContent = '两次密码不一致'; debugLog('校验失败：两次密码不一致', 'warn'); return false; }

    const btn = document.getElementById('btn-register');
    btn.disabled = true; btn.textContent = '提交中...';
    Tauri.ready().then(mode => debugLog(`Tauri 运行模式: ${mode}`, 'tauri'))
        .then(() => Tauri.invoke('register', { username: u, password: p }))
        .then(r => { debugLog(`invoke(register) => ${r}`, 'tauri'); })
        .catch(err => {
            statusEl.textContent = err;
            debugLog(`invoke(register) 错误: ${err}`, 'error');
        })
        .finally(() => { btn.disabled = false; btn.textContent = '注 册'; });
    return false;
}
function handleRegAck(p) {
    const statusEl = document.getElementById('reg-status');
    if (p.ok) {
        statusEl.textContent = '';
        toast(`注册成功！您的用户 ID：${p.id}，请使用该 ID 登录`, 'success', 3500);
        // 自动切到登录 tab，并把用户名填进去
        document.querySelector('#page-login .tab-btn[data-tab="login"]').click();
        document.getElementById('login-username').value = document.getElementById('reg-username').value;
        document.getElementById('reg-username').value = '';
        document.getElementById('reg-password').value = '';
        document.getElementById('reg-password2').value = '';
    } else {
        statusEl.textContent = `[${p.code}] ${p.msg}`;
        toast('注册失败：' + p.msg, 'error');
    }
}

// ============================================================
// 登录流程
// ============================================================
function onLogin(e) {
    e.preventDefault();
    const u = document.getElementById('login-username').value.trim();
    const p = document.getElementById('login-password').value;
    const statusEl = document.getElementById('login-status');
    statusEl.textContent = '';
    debugLog(`登录按钮点击 username=${u}`, 'click');
    if (!u || !p) {
        statusEl.textContent = '请输入用户名和密码';
        debugLog('校验失败：用户名或密码空', 'warn');
        return false;
    }

    const btn = document.getElementById('btn-login');
    btn.disabled = true; btn.textContent = '登录中...';
    Tauri.ready().then(mode => debugLog(`Tauri 运行模式: ${mode}`, 'tauri'))
        .then(() => Tauri.invoke('login', { username: u, password: p }))
        .then(r => { debugLog(`invoke(login) => ${r}`, 'tauri'); })
        .catch(err => {
            statusEl.textContent = err;
            debugLog(`invoke(login) 错误: ${err}`, 'error');
        })
        .finally(() => { btn.disabled = false; btn.textContent = '登 录'; });
    return false;
}
function handleLoginAck(p) {
    const statusEl = document.getElementById('login-status');
    if (p.ok) {
        Store.me = { id: p.id, name: p.username };
        statusEl.textContent = '';
        toast(`欢迎回来，${p.username}`, 'success');
        enterMainPage();
    } else {
        const msg = p.msg || '登录失败';
        // 断线重连后需要重新登录
        if (p.need_relogin) {
            toast('连接已恢复，请重新登录', 'warn', 4000);
        }
        statusEl.textContent = msg;
        toast(msg, 'error');
        // 切换回登录页
        document.getElementById('page-login').classList.add('active');
        document.getElementById('page-main').classList.remove('active');
    }
}

// ============================================================
// 登出流程
// ============================================================
async function doLogout() {
    if (!confirm('确定要注销吗？')) return;
    try { await Tauri.invoke('logout'); } catch(e) { /* ignore */ }
}
function handleLogoutAck() {
    Store.me = null;
    Store.friends.clear();
    Store.groups.clear();
    Store.activeChat = null;
    Store.messages = [];
    // 回到登录页
    document.getElementById('page-login').classList.add('active');
    document.getElementById('page-main').classList.remove('active');
    document.getElementById('top-username').textContent = '未登录';
    toast('已注销', 'info');
}

// ============================================================
// 进入主页面（登录成功后）
// ============================================================
function enterMainPage() {
    document.getElementById('page-login').classList.remove('active');
    document.getElementById('page-main').classList.add('active');
    document.getElementById('page-main').style.display = 'flex';
    document.getElementById('top-username').textContent = Store.me.name + ` (ID:${Store.me.id})`;

    // 加载初始好友列表（先从 Rust get_friends，再用客户端缓存兜底）
    Tauri.invoke('get_friends').then(list => {
        (list || []).forEach(f => Store.friends.set(f.id, f));
        renderFriendList();
        renderGroupList();
    });
}

// ============================================================
// 好友列表渲染 & 交互
// ============================================================
function renderFriendList() {
    const ul = document.getElementById('friend-list');
    ul.innerHTML = '';
    if (Store.friends.size === 0) {
        const li = document.createElement('li');
        li.style.color = '#aaa'; li.style.justifyContent = 'center';
        li.style.padding = '18px 14px';
        li.textContent = '暂无好友，点击 ＋ 添加';
        ul.appendChild(li);
        return;
    }
    Store.friends.forEach(f => {
        const li = document.createElement('li');
        li.dataset.id = f.id;
        if (Store.activeChat && Store.activeChat.type === 'friend' && Store.activeChat.id === f.id) {
            li.classList.add('active');
        }
        const av = document.createElement('div');
        av.className = 'avatar';
        av.textContent = (f.name || `U${f.id}`).slice(0, 2).toUpperCase();
        const info = document.createElement('div');
        info.className = 'friend-info';
        info.innerHTML = `
            <div class="friend-name">${escapeHtml(f.name || `用户${f.id}`)}</div>
            <div class="friend-meta">
                <span class="state-tag ${f.state === 'online' ? 'online' : 'offline'}"></span>
                ${f.state === 'online' ? '在线' : '离线'}
            </div>`;
        const del = document.createElement('button');
        del.className = 'del-btn';
        del.textContent = '删除';
        del.onclick = (ev) => { ev.stopPropagation(); confirmDeleteFriend(f); };
        li.appendChild(av); li.appendChild(info); li.appendChild(del);
        li.onclick = () => openFriendChat(f);
        ul.appendChild(li);
    });
}

// ============================================================
// 群列表渲染 & 交互
// ============================================================
function renderGroupList() {
    const ul = document.getElementById('group-list');
    ul.innerHTML = '';
    if (Store.groups.size === 0) {
        const li = document.createElement('li');
        li.style.color = '#aaa'; li.style.justifyContent = 'center';
        li.style.padding = '18px 14px';
        li.textContent = '暂无群组';
        ul.appendChild(li);
        return;
    }
    Store.groups.forEach(g => {
        const li = document.createElement('li');
        li.dataset.id = g.id;
        if (Store.activeChat && Store.activeChat.type === 'group' && Store.activeChat.id === g.id) {
            li.classList.add('active');
        }
        const av = document.createElement('div');
        av.className = 'avatar green';
        av.textContent = (g.name || `G${g.id}`).slice(0, 2);
        const info = document.createElement('div');
        info.className = 'friend-info';
        info.innerHTML = `
            <div class="friend-name">${escapeHtml(g.name || `群${g.id}`)}</div>
            <div class="friend-meta">${g.members ? g.members.length + ' 人' : '群聊'}</div>`;
        li.appendChild(av); li.appendChild(info);
        li.onclick = () => openGroupChat(g);
        ul.appendChild(li);
    });
}

// ============================================================
// 添加 / 删除好友
// ============================================================
function doAddFriend() {
    const idInput = document.getElementById('add-friend-id');
    const id = parseInt(idInput.value, 10);
    if (!id || id <= 0) { toast('请输入有效的用户 ID', 'warn'); return; }
    if (id === Store.me.id) { toast('不能添加自己为好友', 'warn'); return; }
    Tauri.invoke('add_friend', { toId: id })
        .then(() => toast('添加请求已发送', 'info'))
        .catch(e => toast('添加失败：' + e, 'error'));
    closeModal(document.getElementById('dlg-add-friend'));
}
function handleAddFriendAck(p) {
    if (p.ok) {
        toast('添加好友成功', 'success');
        // 本地同步添加一个占位条目（若服务端有好友查询消息可刷新）
        const fakeName = `用户${p.id || '?'}`;
        Store.friends.set(p.id || Date.now(), { id: p.id || Date.now(), name: fakeName, state: 'offline' });
        renderFriendList();
    } else {
        toast(`添加好友失败：${p.msg}`, 'error');
    }
}
function confirmDeleteFriend(f) {
    if (!confirm(`确定删除好友 ${f.name}？`)) return;
    Tauri.invoke('del_friend', { toId: f.id })
        .then(() => toast('删除请求已发送', 'info'))
        .catch(e => toast('删除失败：' + e, 'error'));
}
function handleDelFriendAck(p) {
    if (p.ok) {
        toast('删除好友成功', 'success');
        // 前端需要找到刚才删除的对象 → 这里简单重绘即可
        // 如果 activeChat 是该好友，关闭当前会话
        if (Store.activeChat && Store.activeChat.type === 'friend') {
            Store.activeChat = null;
            Store.messages = [];
            renderMessages();
            renderDetail(null);
            document.getElementById('chat-title').textContent = '请选择会话';
        }
        renderFriendList();
    } else {
        toast(`删除失败：${p.msg}`, 'error');
    }
}

// ============================================================
// 创建 / 加入群聊（预留接口占位）
// ============================================================
function doCreateGroup() {
    const name = document.getElementById('create-group-name').value.trim();
    const desc = document.getElementById('create-group-desc').value.trim();
    if (!name) { toast('请输入群名称', 'warn'); return; }
    toast('创建群组功能：服务端 CREATE_GROUP_MSG 接口需后端配合，此处仅做前端演示', 'info');
    closeModal(document.getElementById('dlg-group'));
}
function doJoinGroup() {
    const id = parseInt(document.getElementById('join-group-id').value, 10);
    if (!id || id <= 0) { toast('请输入有效的群 ID', 'warn'); return; }
    toast(`加入群组 ID=${id}：服务端 ADD_GROUP_MSG 接口需后端配合，此处仅做前端演示`, 'info');
    closeModal(document.getElementById('dlg-group'));
}

// ============================================================
// 打开私聊 / 群聊 会话
// ============================================================
function openFriendChat(f) {
    Store.activeChat = { type: 'friend', id: f.id, name: f.name };
    Store.messages = loadLocalMsgs(Store.activeChat);
    renderFriendList();
    renderGroupList();
    document.getElementById('chat-title').textContent = `私聊：${f.name} (ID:${f.id})`;
    renderMessages();
    renderDetail({ kind: 'friend', friend: f });
}
function openGroupChat(g) {
    Store.activeChat = { type: 'group', id: g.id, name: g.name };
    Store.messages = loadLocalMsgs(Store.activeChat);
    renderFriendList();
    renderGroupList();
    document.getElementById('chat-title').textContent = `群聊：${g.name} (ID:${g.id})`;
    renderMessages();
    renderDetail({ kind: 'group', group: g });
}

// ============================================================
// 发送消息（私聊 / 群聊）
// ============================================================
function onInputKey(e) {
    if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault();
        doSend();
    }
}
function doSend() {
    const txt = document.getElementById('chat-input');
    const content = txt.value;
    if (!content.trim()) return;
    if (!Store.activeChat) { toast('请先选择会话', 'warn'); return; }

    const ts = Date.now();
    if (Store.activeChat.type === 'friend') {
        // 先本地显示一条自己发送的消息（乐观显示）
        appendLocalMsg({
            msg_id: `self_${ts}`,
            from_id: Store.me.id, from_name: Store.me.name,
            target_id: Store.activeChat.id, target_type: 'private',
            content: content.trim(), timestamp_ms: ts, is_self: true,
        });
        Tauri.invoke('send_private', { toId: Store.activeChat.id, content: content.trim() })
            .catch(e => toast('发送失败：' + e, 'error'));
    } else {
        appendLocalMsg({
            msg_id: `self_${ts}`,
            from_id: Store.me.id, from_name: Store.me.name,
            target_id: Store.activeChat.id, target_type: 'group',
            content: content.trim(), timestamp_ms: ts, is_self: true,
        });
        Tauri.invoke('send_group', { groupId: Store.activeChat.id, content: content.trim() })
            .catch(e => toast('发送失败：' + e, 'error'));
    }
    txt.value = '';
    // 保存到 localStorage
    saveLocalMsgs(Store.activeChat, Store.messages);
}
function ackShow(label, p) {
    if (p.ok) toast(`${label}已送达`, 'success', 1500);
    else toast(`${label}失败：${p.msg}`, 'error');
}

// ============================================================
// 处理收到的私聊消息（Rust reader → emit → 这里）
// ============================================================
function handleOneChat(p) {
    const msg = {
        msg_id: `r_${p.timestamp_ms || Date.now()}_${p.from_id}`,
        from_id: p.from_id,
        from_name: p.from_name || `用户${p.from_id}`,
        target_id: p.to_id,
        target_type: 'private',
        content: p.content,
        timestamp_ms: p.timestamp_ms || Date.now(),
        is_self: !!p.is_self,
    };
    // 如果当前在跟该好友聊天 → 立即显示
    if (Store.activeChat && Store.activeChat.type === 'friend' &&
        (Store.activeChat.id === p.from_id || Store.activeChat.id === p.to_id)) {
        appendLocalMsg(msg);
        saveLocalMsgs(Store.activeChat, Store.messages);
    } else {
        // 存到对应会话的本地缓存
        const key = `friend:${p.is_self ? p.to_id : p.from_id}`;
        const other = p.is_self ? p.to_id : p.from_id;
        const conv = JSON.parse(localStorage.getItem(`chat_conv_${key}`) || '[]');
        conv.push(msg);
        localStorage.setItem(`chat_conv_${key}`, JSON.stringify(conv));
        // 通知栏提示
        toast(`新私聊消息：来自 ${msg.from_name}`, 'info', 3000);
        // 确保好友列表有该好友
        if (!Store.friends.has(other)) {
            Store.friends.set(other, { id: other, name: msg.from_name, state: 'online' });
            renderFriendList();
        }
    }
}

// ============================================================
// 处理收到的群聊消息
// ============================================================
function handleGroupChat(p) {
    const msg = {
        msg_id: `g_${p.timestamp_ms || Date.now()}_${p.group_id}_${p.from_id}`,
        from_id: p.from_id,
        from_name: p.from_name || `用户${p.from_id}`,
        target_id: p.group_id,
        target_type: 'group',
        content: p.content,
        timestamp_ms: p.timestamp_ms || Date.now(),
        is_self: !!p.is_self,
    };
    if (Store.activeChat && Store.activeChat.type === 'group' && Store.activeChat.id === p.group_id) {
        appendLocalMsg(msg);
        saveLocalMsgs(Store.activeChat, Store.messages);
    } else {
        const conv = JSON.parse(localStorage.getItem(`chat_conv_group:${p.group_id}`) || '[]');
        conv.push(msg);
        localStorage.setItem(`chat_conv_group:${p.group_id}`, JSON.stringify(conv));
        toast(`新群聊消息：群 ${p.group_id} - ${msg.from_name}`, 'info', 3000);
        // 若群不存在于本地列表，创建占位
        if (!Store.groups.has(p.group_id)) {
            Store.groups.set(p.group_id, { id: p.group_id, name: `群${p.group_id}`, desc: '', members: [] });
            renderGroupList();
        }
    }
}

// ============================================================
// 好友列表推送（预留）
// ============================================================
function handleFriendList(list) {
    (list || []).forEach(f => Store.friends.set(f.id, f));
    renderFriendList();
}

// ============================================================
// 渲染聊天消息列表
// ============================================================
function renderMessages() {
    const el = document.getElementById('chat-messages');
    el.innerHTML = '';
    if (Store.messages.length === 0) {
        el.innerHTML = `<div style="text-align:center;color:#bbb;padding:60px 0;font-size:13px;">暂无消息，发送第一条消息吧~</div>`;
        return;
    }
    Store.messages.forEach(m => el.appendChild(makeMsgDom(m)));
    el.scrollTop = el.scrollHeight;
}
function appendLocalMsg(m) {
    Store.messages.push(m);
    const el = document.getElementById('chat-messages');
    el.appendChild(makeMsgDom(m));
    el.scrollTop = el.scrollHeight;
}
function makeMsgDom(m) {
    const wrap = document.createElement('div');
    wrap.className = 'msg' + (m.is_self ? ' self' : '');
    const av = document.createElement('div');
    av.className = 'avatar' + (m.target_type === 'group' ? ' orange' : '');
    av.textContent = (m.from_name || '?').slice(0, 2).toUpperCase();
    const body = document.createElement('div');
    body.style.minWidth = '0';
    body.innerHTML = `
        <div class="msg-meta">${escapeHtml(m.from_name)} · ${fmtTime(m.timestamp_ms)}</div>
        <div class="msg-bubble">${escapeHtml(m.content)}</div>`;
    wrap.appendChild(av);
    wrap.appendChild(body);
    return wrap;
}

// ============================================================
// 渲染右侧详情面板
// ============================================================
function renderDetail(info) {
    const panel = document.getElementById('detail-panel');
    if (!info) {
        panel.innerHTML = `<div class="empty-detail">请选择左侧会话</div>`;
        return;
    }
    if (info.kind === 'friend') {
        const f = info.friend;
        panel.innerHTML = `
            <div class="detail-content">
                <div style="width:64px;height:64px;border-radius:50%;margin:0 auto 16px;background:linear-gradient(135deg,#667eea,#764ba2);color:#fff;display:flex;align-items:center;justify-content:center;font-size:24px;font-weight:600;">${escapeHtml((f.name||'U').slice(0,2))}</div>
                <h3 style="text-align:center;">${escapeHtml(f.name || `用户${f.id}`)}</h3>
                <div class="sub" style="text-align:center;">ID: ${f.id}</div>
                <div class="detail-row"><span class="k">在线状态</span><span class="v">${f.state === 'online' ? '🟢 在线' : '⚪ 离线'}</span></div>
                <div class="detail-row"><span class="k">好友关系</span><span class="v">已添加</span></div>
            </div>`;
    } else if (info.kind === 'group') {
        const g = info.group;
        panel.innerHTML = `
            <div class="detail-content">
                <div style="width:64px;height:64px;border-radius:50%;margin:0 auto 16px;background:linear-gradient(135deg,#11998e,#38ef7d);color:#fff;display:flex;align-items:center;justify-content:center;font-size:24px;font-weight:600;">${escapeHtml((g.name||'G').slice(0,2))}</div>
                <h3 style="text-align:center;">${escapeHtml(g.name || `群${g.id}`)}</h3>
                <div class="sub" style="text-align:center;">ID: ${g.id}</div>
                <div class="detail-row"><span class="k">群描述</span><span class="v">${escapeHtml(g.desc || '无')}</span></div>
                <div class="detail-row"><span class="k">群成员</span><span class="v">${(g.members||[]).length} 人</span></div>
            </div>`;
    }
}

// ============================================================
// 服务器设置
// ============================================================
function onSetServer(e) {
    e.preventDefault();
    const addr = document.getElementById('server-addr').value.trim();
    if (!addr) return false;
    Tauri.invoke('set_server', { addr })
        .then(r => {
            document.getElementById('server-status').textContent = '';
            toast(r || '连接成功', 'success');
        })
        .catch(err => {
            document.getElementById('server-status').textContent = err;
            toast('连接失败：' + err, 'error');
        });
    return false;
}

// ============================================================
// 网络状态变化
// ============================================================
function handleNetStatus(p) {
    let flag = 'bad', text = '未连接';
    if (p.status === 'connected')    { flag = 'ok';   text = '已连接 ' + (p.addr || ''); }
    else if (p.status === 'reconnected') { flag = 'ok'; text = '已重连'; }
    else if (p.status === 'reconnecting'){ flag = 'warn'; text = '重连中...'; }
    else if (p.status === 'disconnected'){
        flag = 'bad';
        text = '断开' + (p.reason ? `：${p.reason}` : '');
    }
    updateNetDotByFlag(flag, text);
    // 异常提示
    if (p.status === 'disconnected' && Store.me) {
        toast('网络已断开，正在尝试自动重连...', 'warn', 4000);
    }
    if (p.status === 'reconnected') {
        toast('网络已恢复', 'success');
    }
}
function updateNetDot(connected) {
    updateNetDotByFlag(connected ? 'ok' : 'bad', connected ? '已连接' : '未连接');
}
function updateNetDotByFlag(flag, text) {
    // 登录页
    const d1 = document.getElementById('net-dot');
    const t1 = document.getElementById('net-text');
    if (d1) { d1.className = 'dot ' + flag; }
    if (t1) { t1.textContent = text; }
    // 主页面
    const d2 = document.getElementById('main-net-dot');
    const t2 = document.getElementById('main-net-text');
    if (d2) { d2.className = 'dot ' + flag; }
    if (t2) { t2.textContent = text; }
}

// ============================================================
// 本地消息持久化（localStorage）：离线消息不依赖浏览器存储，仅作为 UI 缓存
// ============================================================
function convKey(c) { return (c ? c.type : 'none') + ':' + (c ? c.id : '0'); }
function saveLocalMsgs(conv, msgs) {
    if (!conv) return;
    try { localStorage.setItem('chat_conv_' + convKey(conv), JSON.stringify(msgs.slice(-200))); } catch(e) {}
}
function loadLocalMsgs(conv) {
    if (!conv) return [];
    try { return JSON.parse(localStorage.getItem('chat_conv_' + convKey(conv)) || '[]'); } catch(e) { return []; }
}

// ============================================================
// 小工具：HTML 转义 / 时间格式化
// ============================================================
function escapeHtml(s) {
    return (s == null ? '' : String(s))
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}
function fmtTime(ms) {
    if (!ms) return '';
    const d = new Date(ms);
    const hh = String(d.getHours()).padStart(2, '0');
    const mm = String(d.getMinutes()).padStart(2, '0');
    const today = new Date();
    const sameDay = d.toDateString() === today.toDateString();
    if (sameDay) return `${hh}:${mm}`;
    const mo = String(d.getMonth() + 1).padStart(2, '0');
    const dd = String(d.getDate()).padStart(2, '0');
    return `${mo}-${dd} ${hh}:${mm}`;
}

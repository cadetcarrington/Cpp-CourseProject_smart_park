/* SmartPark H5 用户端（M1）
 * 纯 vanilla JS，无构建依赖；由服务端 --web-root 同源伺服，
 * fetch 与 WebSocket 均走同源地址（扫码即用的部署形态）。
 */
'use strict';

/* ---------- 状态与基础设施 ---------- */

// localStorage 里的值可能损坏（例如某次写入的是字符串 "undefined"）。
// 这里在模块顶层执行，直接 JSON.parse 会抛异常让整个应用起不来——
// 用户只能手动清存储才能恢复。所以解析失败就当作未登录并清掉脏值。
function readStoredUser() {
  try {
    const raw = localStorage.getItem('sp_user');
    if (!raw || raw === 'undefined' || raw === 'null') return null;
    return JSON.parse(raw);
  } catch (_) {
    localStorage.removeItem('sp_user');
    return null;
  }
}

const state = {
  token: localStorage.getItem('sp_token') || '',
  user: readStoredUser(),
  meta: null,
  ws: null,
  wsOk: false,
  wsHeartbeat: null,
  wsRetryTimer: null,
  wsRetry: 0,
  lastPlate: localStorage.getItem('sp_plate') || '',
  plates: [],
};

// 同源部署：页面由 REST 网关伺服，API 与页面同源，不使用任何可配置外部地址。
const API_BASE = '';

const $app = document.getElementById('app');

// 所有拼进 innerHTML 的动态文本都必须过这里。
// 车牌来自输入框并写入 localStorage，错误信息来自服务端，二者都不能
// 直接当 HTML 用——否则 `"><img src=x onerror=...>` 就是一个持久化注入点。
function esc(v) {
  return String(v == null ? '' : v).replace(/[&<>"']/g, (c) => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
  }[c]));
}

function toast(text, ms) {
  let wrap = document.querySelector('.toastWrap');
  if (!wrap) {
    wrap = document.createElement('div');
    wrap.className = 'toastWrap';
    document.body.appendChild(wrap);
  }
  const item = document.createElement('div');
  item.className = 'toastItem';
  item.textContent = text;
  wrap.appendChild(item);
  setTimeout(() => item.remove(), ms || 2600);
}

function money(v) {
  return '¥' + (Math.round((Number(v) || 0) * 100) / 100).toFixed(2);
}

function fmtTime(ms) {
  if (!ms) return '—';
  const d = new Date(Number(ms));
  const p = (n) => String(n).padStart(2, '0');
  return `${d.getMonth() + 1}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`;
}

// 拍照识牌上传前的压缩：最长边缩到 maxSide，转 JPEG base64（不含 data: 前缀）。
function fileToJpegBase64(file, maxSide) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onerror = () => reject({ message: '读取图片失败' });
    reader.onload = () => {
      const img = new Image();
      img.onerror = () => reject({ message: '图片解码失败' });
      img.onload = () => {
        const scale = Math.min(1, maxSide / Math.max(img.width, img.height));
        const canvas = document.createElement('canvas');
        canvas.width = Math.round(img.width * scale);
        canvas.height = Math.round(img.height * scale);
        canvas.getContext('2d').drawImage(img, 0, 0, canvas.width, canvas.height);
        const dataUrl = canvas.toDataURL('image/jpeg', 0.85);
        resolve(dataUrl.slice(dataUrl.indexOf(',') + 1));
      };
      img.src = reader.result;
    };
    reader.readAsDataURL(file);
  });
}

async function api(method, path, body) {
  const headers = {};
  if (body !== undefined) headers['Content-Type'] = 'application/json';
  if (state.token) headers['Authorization'] = 'Bearer ' + state.token;
  let res;
  try {
    res = await fetch(API_BASE + '/api/v1' + path, {
      method,
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
    });
  } catch (e) {
    throw { code: 'NETWORK', message: '无法连接服务器' };
  }
  const data = await res.json().catch(() => ({}));
  if (res.status === 401 && state.token && path !== '/auth/login') {
    doLogout(true);
    throw { code: 'AUTH_EXPIRED', message: '登录已过期，请重新登录' };
  }
  if (!res.ok) throw data;
  return data;
}

// 扫码绑定过的车牌：预约/缴费/寻车优先用它预填，省得每次手输。
async function loadPlates() {
  if (!state.token) { state.plates = []; return []; }
  try {
    const data = await api('GET', '/me/plates');
    state.plates = data.plates || [];
  } catch (_) {
    state.plates = [];
  }
  return state.plates;
}

// 车牌输入框的默认值：最近用过的 > 已绑定的第一辆。
function plateHint() {
  return state.lastPlate || (state.plates && state.plates[0] && state.plates[0].plate) || '';
}

function saveAuth(token, user) {
  state.token = token || '';
  state.user = user || null;
  if (state.token) localStorage.setItem('sp_token', state.token);
  else localStorage.removeItem('sp_token');
  if (state.user) localStorage.setItem('sp_user', JSON.stringify(state.user));
  else localStorage.removeItem('sp_user');
}

function doLogout(quiet) {
  if (state.token && !quiet) {
    api('POST', '/auth/logout').catch(() => {});
  }
  state.token = '';
  state.user = null;
  localStorage.removeItem('sp_token');
  localStorage.removeItem('sp_user');
  closeWs();
  if (!quiet) { toast('已退出登录'); location.hash = '#/login'; }
}

/* ---------- WebSocket 实时事件 ---------- */

function closeWs() {
  if (state.wsHeartbeat) { clearInterval(state.wsHeartbeat); state.wsHeartbeat = null; }
  if (state.wsRetryTimer) { clearTimeout(state.wsRetryTimer); state.wsRetryTimer = null; }
  if (state.ws) { state.ws.onclose = null; state.ws.close(); state.ws = null; }
  state.wsOk = false;
}

// 断线后退避重连（1s→2s→4s…上限 15s）。
// 移动端切网络、息屏、进出电梯都会断，没有重连的话「实时」就永久是红的。
function scheduleWsReconnect() {
  if (state.wsRetryTimer || !state.token) return;
  const delay = Math.min(15000, 1000 * Math.pow(2, state.wsRetry++));
  state.wsRetryTimer = setTimeout(() => {
    state.wsRetryTimer = null;
    connectWs();
  }, delay);
}

function connectWs() {
  closeWs();
  if (!state.token || !state.meta || !state.meta.wsUrl) return;
  const ws = new WebSocket(state.meta.wsUrl);
  state.ws = ws;
  ws.onopen = () => {
    state.wsRetry = 0;
    ws.send(JSON.stringify({ type: 'auth', token: state.token }));
  };
  ws.onmessage = (ev) => {
    let frame;
    try { frame = JSON.parse(ev.data || '{}'); } catch (_) { return; }
    if (frame.type === 'auth' && frame.ok) {
      state.wsOk = true;
      refreshWsBadge();
      toast('实时连接已恢复', 1600);
    } else if (frame.type === 'event') {
      handleEvent(frame.event, frame.payload);
    } else if (frame.type === 'error') {
      // 服务端明确拒绝（token 失效/被踢）：重连也没用，引导重新登录。
      state.wsOk = false;
      refreshWsBadge();
      if (frame.code === 'AUTH_REQUIRED') {
        doLogout(true);
        toast('登录已失效，请重新登录');
        nav('#/login');
      }
    }
  };
  ws.onclose = () => { state.wsOk = false; refreshWsBadge(); scheduleWsReconnect(); };
  ws.onerror = () => { state.wsOk = false; refreshWsBadge(); };
  // 心跳保活（服务端 60 秒无帧踢除）
  state.wsHeartbeat = setInterval(() => {
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({ type: 'ping', ts: Date.now() }));
    }
  }, 20000);
}

function handleEvent(name, payload) {
  // 车位状态可能已变：布局缓存必须失效，否则车位图/路线会一直停在首次加载的画面。
  layoutCache = null;
  const plate = payload && payload.plate;
  if (name === 'parking.entered') toast(`🚗 ${plate || ''} 入场，车位 ${payload.spotId || ''}`);
  else if (name === 'parking.exited') toast(`🚗 ${plate || ''} 离场，费用 ${money(payload.fee)}`);
  else if (name === 'reservation.created') toast(`📅 ${plate || ''} 预约车位 ${payload.spotId || ''}`);
  else if (name === 'reservation.cancelled') toast(`📅 ${plate || ''} 取消预约`);
  else if (name === 'reservation.checkin') toast(`📅 ${plate || ''} 到场核销`);
  else if (name === 'payment.paid') {
    toast(payload.frictionless
      ? `⚡ 无感支付扣费 ${money(payload.amount)}（${plate || ''}）`
      : `💰 订单已支付 ${money(payload.amount)}`);
  }
  else if (name === 'gate.replayed') toast(`📥 补报完成 ${payload.applied || 0} 条`);
  if (location.hash === '' || location.hash === '#/home') render();
}

function refreshWsBadge() {
  const dot = document.querySelector('[data-ws-dot]');
  if (dot) {
    dot.classList.toggle('on', state.wsOk);
    const tip = dot.parentElement;
    if (tip) tip.title = state.wsOk ? '实时连接正常' : '实时连接断开';
  }
}

/* ---------- 路由 ---------- */

// location.hash 形如 "#/claim?t=xxx"：路由匹配只看 ? 之前的部分。
function currentRoute() {
  const raw = location.hash || '#/home';
  const q = raw.indexOf('?');
  return q >= 0 ? raw.slice(0, q) : raw;
}

function hashQuery(name) {
  const raw = location.hash || '';
  const q = raw.indexOf('?');
  if (q < 0) return '';
  return new URLSearchParams(raw.slice(q + 1)).get(name) || '';
}

const routes = {
  '#/login': viewLogin,
  '#/register': viewLogin,
  '#/home': viewHome,
  '#/reserve': viewReserve,
  '#/pay': viewPay,
  '#/records': viewRecords,
  '#/more': viewMore,
  '#/claim': viewClaim,
};

function render() {
  const route = currentRoute();
  const onLogin = route.startsWith('#/login') || route.startsWith('#/register');
  // #/claim 是扫码入口：未登录也要能进，否则用户看不到「扫的是哪个车场」。
  const onClaim = route.startsWith('#/claim');
  if (!state.token && !onLogin && !onClaim) { location.hash = '#/login'; return; }
  // 已登录还停在登录页才跳首页；停在 claim 页要让它继续走绑定流程。
  if (state.token && onLogin) { location.hash = '#/home'; return; }
  const view = onLogin ? viewLogin : (routes[route] || viewHome);
  $app.innerHTML = '';
  view($app);
  if (state.token && !onLogin && !onClaim) renderTabbar(route);
  refreshWsBadge();
}

function nav(hash) { location.hash = hash; }

const TABS = [
  ['#/home', '首页'],
  ['#/reserve', '预约'],
  ['#/pay', '缴费'],
  ['#/records', '记录'],
  ['#/more', '更多'],
];

function renderTabbar(current) {
  const bar = document.createElement('div');
  bar.className = 'tabbar';
  bar.innerHTML = TABS.map(([hash, label]) =>
    `<a href="${hash}" class="${hash === current ? 'on' : ''}">${label}</a>`
  ).join('');
  $app.appendChild(bar);
}

function header(sub) {
  return `<div class="header"><div class="row">
    <div><h1>SmartPark</h1><div class="sub">${sub || ''}</div></div>
    <div class="muted" style="color:rgba(255,255,255,.6)"><span class="dot" data-ws-dot></span>实时</div>
  </div></div>`;
}

/* ---------- 登录 / 注册 ---------- */

function viewLogin(root) {
  const mode = location.hash.startsWith('#/register') ? 'reg' : 'login';
  root.innerHTML = `
  <div class="auth">
    <div class="logo"><h1>SmartPark</h1><p>智慧停车 · 扫码即用</p></div>
    <div class="seg">
      <a href="#/login" class="${mode === 'login' ? 'on' : ''}">登录</a>
      <a href="#/register" class="${mode === 'reg' ? 'on' : ''}">注册</a>
    </div>
    <div class="field"><label>账号</label>
      <input id="f-user" placeholder="2-24 个字符" autocomplete="username"></div>
    <div class="field"><label>密码</label>
      <input id="f-pass" type="password" placeholder="6-64 个字符" autocomplete="current-password"></div>
    <button class="btn" id="f-go">${mode === 'login' ? '登 录' : '注 册 并 登 录'}</button>
    <p class="muted" style="text-align:center;margin-top:14px">
      ${mode === 'login' ? '没有账号？<a href="#/register">去注册</a>' : '已有账号？<a href="#/login">去登录</a>'}</p>
  </div>`;
  const go = root.querySelector('#f-go');
  const submit = async () => {
    const username = root.querySelector('#f-user').value.trim();
    const password = root.querySelector('#f-pass').value;
    go.disabled = true;
    try {
      if (mode === 'reg') {
        await api('POST', '/auth/register', { username, password });
        toast('注册成功，自动登录');
      }
      const data = await api('POST', '/auth/login', { username, password });
      saveAuth(data.token, data.user);
      loadPlates().catch(() => {});
      connectWs();
      toast(`欢迎，${data.user.username}`);
      // 扫码进来的话回到 claim 继续走绑定，否则进首页。
      nav(sessionStorage.getItem('sp_claim_ticket') ? '#/claim' : '#/home');
    } catch (e) {
      toast(e.message || '操作失败');
      go.disabled = false;
    }
  };
  go.onclick = submit;
  root.querySelector('#f-pass').onkeydown = (e) => { if (e.key === 'Enter') submit(); };
}

/* ---------- 首页：余位概览 ---------- */

async function viewHome(root) {
  root.innerHTML = header(state.user ? '@' + state.user.username : '')
      + '<div class="card"><div class="empty">加载中…</div></div>';
  let status;
  try { status = await api('GET', '/parking/status'); }
  catch (e) { root.querySelector('.card').innerHTML = `<div class="empty">${esc(e.message || '加载失败')}</div>`; return; }
  const zones = (status.zones || []).map((z) => {
    const free = Math.max(0, z.total - z.occupied);
    const load = z.total ? Math.round((z.occupied / z.total) * 100) : 0;
    return `<div class="zone"><div class="zl"><span>${esc(z.zone)}</span><span>空 ${free} / ${z.total}</span></div>
      <div class="bar"><i style="width:${load}%"></i></div></div>`;
  }).join('');
  // 移除占位卡，再追加数据卡
  const placeholder = root.querySelector('.card');
  if (placeholder) placeholder.remove();
  const card = document.createElement('div');
  card.innerHTML = `
    <div class="card">
      <div style="text-align:center;padding:4px 0 2px">
        <div class="value-xl" style="color:var(--brand)">${status.available}</div>
        <div class="muted">当前可用车位 / 共 ${status.capacity} 个</div>
      </div>
      <div class="stat-grid">
        <div class="s"><b>${status.occupied}</b><span>占用</span></div>
        <div class="s"><b>${status.reserved}</b><span>预约</span></div>
        <div class="s"><b>${status.capacity - status.occupied - status.reserved}</b><span>空闲</span></div>
        <div class="s"><b>${status.capacity}</b><span>总数</span></div>
      </div>
    </div>
    <div class="card"><h3>分区负载</h3>${zones || '<div class="empty">暂无分区数据</div>'}</div>
    <div class="card"><h3>车位图</h3>
      <canvas class="map" id="home-canvas" width="460" height="340"></canvas>
      ${LEGEND_HTML}
    </div>
    <div class="card">
      <h3>快捷操作</h3>
      <div class="row2">
        <button class="btn ghost" id="q-enter">入场登记</button>
        <button class="btn ghost" id="q-find">反向寻车</button>
      </div>
      <div class="row2" style="margin-top:10px">
        <button class="btn ghost" id="q-pay">离场缴费</button>
        <button class="btn ghost" id="q-reserve">预约车位</button>
      </div>
    </div>`;
  root.appendChild(card);
  card.querySelector('#q-enter').onclick = quickEnter;
  card.querySelector('#q-find').onclick = () => nav('#/more#guide');
  card.querySelector('#q-pay').onclick = () => nav('#/pay');
  card.querySelector('#q-reserve').onclick = () => nav('#/reserve');
  // 车位图：布局加载失败时静默隐藏卡片（余位数字不受影响）。
  loadLayout().then(() => {
    const canvas = card.querySelector('#home-canvas');
    if (canvas) drawLayout(canvas, {});
  }).catch(() => {
    const mapCard = card.querySelector('#home-canvas');
    if (mapCard) mapCard.closest('.card').style.display = 'none';
  });
}

async function quickEnter() {
  const plate = (state.lastPlate || '').trim();
  const input = window.prompt('输入车牌号（演示：模拟入场登记）', plate || '京A12345');
  if (!input) return;
  const p = input.trim();
  state.lastPlate = p;
  localStorage.setItem('sp_plate', p);
  try {
    const r = await api('POST', '/parking/enter', { plate: p, vehicleType: 'car' });
    toast(`入场成功！分配车位 ${r.spotId}`);
    render();
  } catch (e) {
    toast(e.message || '入场失败');
  }
}

/* ---------- 预约 ---------- */

// 与服务端 ReservationRule 对齐：至少提前 30 分钟，最长未来 7 天，最短 30 分钟。
const RESERVE_MIN_LEAD_MS = 30 * 60 * 1000;
const RESERVE_MAX_ADVANCE_MS = 7 * 24 * 60 * 60 * 1000;

function toLocalInput(ms) {
  const d = new Date(ms);
  const pad = (n) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

function viewReserve(root) {
  // 默认开场：45 分钟后向上取整到 15 分钟格，保证始终满足提前量。
  const snap = 15 * 60 * 1000;
  const defaultStart = toLocalInput(Math.ceil((Date.now() + 45 * 60 * 1000) / snap) * snap);
  const minStart = toLocalInput(Date.now() + RESERVE_MIN_LEAD_MS);
  root.innerHTML = header('未来 7 天时段预约 · 定金即时收取') + `
    <div class="card">
      <div class="field"><label>车牌号（不能是在场车辆）</label>
        <input id="r-plate" placeholder="如 京A12345" value="${esc(plateHint())}"></div>
      <div class="row2">
        <div class="field"><label>车型</label>
          <select id="r-type">
            <option value="car">轿车</option><option value="electric">新能源</option>
            <option value="motorcycle">摩托车</option><option value="truck">货车</option>
          </select></div>
        <div class="field"><label>时长（最短 30 分钟）</label>
          <select id="r-dur">
            <option value="30">30 分钟</option><option value="60">1 小时</option>
            <option value="120" selected>2 小时</option><option value="180">3 小时</option>
            <option value="240">4 小时</option>
          </select></div>
      </div>
      <div class="field"><label>到场时间（至少提前 30 分钟，7 天内）</label>
        <input id="r-start" type="datetime-local" value="${defaultStart}" min="${minStart}"></div>
      <label style="display:flex;align-items:center;gap:8px;font-size:13px;color:var(--ink-2)">
        <input type="checkbox" id="r-acc" style="width:16px"> 无障碍车位（免定金）</label>
      <button class="btn" id="r-go">创建预约（付定金 ¥20）</button>
      <p class="muted" style="margin-top:10px">到场核销后定金抵扣停车费；取消全额退款；超时未到场没收定金。</p>
    </div>
    <div class="card"><h3>我的未结束预约 <a class="more" id="r-refresh">刷新</a></h3>
      <div id="r-list"><div class="empty">加载中…</div></div></div>`;
  root.querySelector('#r-go').onclick = createReservation;
  root.querySelector('#r-refresh').onclick = loadMyReservations;
  loadMyReservations();

  function showResult(html, ok) {
    const box = root.querySelector('#r-list');
    const banner = document.createElement('div');
    banner.className = ok ? 'banner ok' : 'banner bad';
    banner.innerHTML = html;
    box.prepend(banner);
  }

  async function createReservation() {
    const plate = root.querySelector('#r-plate').value.trim();
    const vehicleType = root.querySelector('#r-type').value;
    const durationMin = parseInt(root.querySelector('#r-dur').value, 10);
    const accessible = root.querySelector('#r-acc').checked;
    const startValue = root.querySelector('#r-start').value;
    if (!plate) { toast('请输入车牌号'); return; }
    const startMs = new Date(startValue).getTime();
    const now = Date.now();
    if (!startMs || startMs < now + RESERVE_MIN_LEAD_MS) {
      showResult(`⏰ 预约需<b>至少提前 30 分钟</b>，请调整到场时间（当前选择了 ${esc(startValue.replace('T', ' ') || '空')}）`, false);
      toast('到场时间太近，需至少提前 30 分钟');
      return;
    }
    if (startMs > now + RESERVE_MAX_ADVANCE_MS) {
      showResult('📅 只能预约<b>未来 7 天内</b>的时间段', false);
      toast('超出 7 天预约范围');
      return;
    }
    state.lastPlate = plate;
    localStorage.setItem('sp_plate', plate);
    const btn = root.querySelector('#r-go');
    btn.disabled = true;
    try {
      const r = await api('POST', '/reservations',
        { plate, vehicleType, startMs, durationMin, accessible });
      toast(`预约成功，车位 ${r.spotId}`);
      loadMyReservations();
      showResult(`✅ 已确认：车位 <b>${esc(r.spotId)}</b> · 定金 ${money(r.deposit)} 已收` +
        (r.order ? `（凭证 ${esc(r.order.outTradeNo)}）` : '') +
        `<br><span class="muted">步行约 ${esc(Math.round(r.entryRoute.distanceM))} 米 / ${esc(r.entryRoute.turns)} 个转弯 · 到场后定金抵扣停车费</span>`, true);
    } catch (e) {
      // 服务端规则错误（提前量/冲突/已在场内等）直接展示原文，方便排障。
      showResult(`❌ ${esc(e.message || '预约失败')}`, false);
      toast(e.message || '预约失败');
    } finally {
      btn.disabled = false;
    }
  }

  async function loadMyReservations() {
    const plate = root.querySelector('#r-plate').value.trim() || state.lastPlate;
    const box = root.querySelector('#r-list');
    if (!plate) { box.innerHTML = '<div class="empty">先在上方输入车牌，或从首页快捷进入</div>'; return; }
    try {
      const data = await api('GET', `/reservations?plate=${encodeURIComponent(plate)}&state=open`);
      if (!data.reservations.length) {
        box.innerHTML = '<div class="empty">该车牌暂无未结束预约</div>';
        return;
      }
      box.innerHTML = data.reservations.map((r) => `
        <div class="item">
          <div><div class="t">${esc(r.spotId)} · ${fmtTime(r.startMs)} 起 ${Math.round((r.endMs - r.startMs) / 60000)} 分钟</div>
            <div class="d">定金 ${money(r.deposit)}（${r.depositState === 'pending' ? '待结算' : '已收'}）</div></div>
          <button class="btn danger sm" data-id="${esc(r.reservationId)}">取消</button>
        </div>`).join('');
      box.querySelectorAll('button[data-id]').forEach((b) => {
        b.onclick = async () => {
          if (!window.confirm('确定取消该预约？定金将全额退回。')) return;
          try {
            await api('POST', `/reservations/${b.dataset.id}/cancel`, {});
            toast('已取消，定金退回');
            loadMyReservations();
          } catch (e) { toast(e.message || '取消失败'); }
        };
      });
    } catch (e) {
      box.innerHTML = `<div class="empty">${esc(e.message || '加载失败')}</div>`;
    }
  }
}

/* ---------- 缴费（模拟收银台）---------- */

function viewPay(root) {
  root.innerHTML = header('离场缴费 · 模拟支付') + `
    <div class="card" id="p-step1">
      <div class="field"><label>车牌号</label>
        <input id="p-plate" placeholder="如 京A12345" value="${esc(plateHint())}"></div>
      <button class="btn" id="p-query">查询在场车辆</button>
    </div>
    <div id="p-next"></div>
    <div class="card"><h3>最近缴费单 <a class="more" id="p-refresh">刷新</a></h3>
      <div id="p-orders"><div class="empty">加载中…</div></div></div>`;
  root.querySelector('#p-query').onclick = queryActive;
  root.querySelector('#p-refresh').onclick = loadMyOrders;
  loadMyOrders();

  async function loadMyOrders() {
    const box = root.querySelector('#p-orders');
    try {
      const data = await api('GET', '/payments/orders?pageSize=8');
      if (!data.orders.length) {
        box.innerHTML = '<div class="empty">暂无缴费记录</div>';
        return;
      }
      box.innerHTML = data.orders.map((o) => `
        <div class="item">
          <div>
            <div class="t">${esc(o.plate)} <span class="tag ${o.status === 'paid' ? 'ok' : o.status === 'pending' ? 'warn' : 'info'}">${
              { paid: '已支付', pending: '待支付', expired: '已超时',
                refunded: '已退款', cancelled: '已关闭' }[o.status] || o.status}</span></div>
            <div class="d">${fmtTime(o.paidAtMs || o.createdAtMs)} · ${{ deposit: '预约定金', parking_fee: '停车费' }[o.kind] || o.kind}</div>
          </div>
          <b>${money(o.amount)}</b>
        </div>`).join('');
    } catch (e) {
      box.innerHTML = `<div class="empty">${esc(e.message || '加载失败')}</div>`;
    }
  }

  async function queryActive() {
    const plate = root.querySelector('#p-plate').value.trim();
    if (!plate) { toast('请输入车牌号'); return; }
    state.lastPlate = plate;
    localStorage.setItem('sp_plate', plate);
    const box = root.querySelector('#p-next');
    box.innerHTML = '<div class="card"><div class="empty">查询中…</div></div>';
    try {
      const rec = await api('GET', `/records/${encodeURIComponent(plate)}/active`);
      box.innerHTML = `
        <div class="card">
          <h3>在场车辆 ${esc(rec.plate)}</h3>
          <div class="kv"><span>车位</span><b>${esc(rec.spotId)}</b></div>
          <div class="kv"><span>入场时间</span><b>${fmtTime(rec.entryTimeMs)}</b></div>
          <div class="kv"><span>已停时长</span><b>${Math.round(rec.durationMin)} 分钟</b></div>
          <div class="kv"><span>预计费用</span><b>${money(rec.estimateFee)}</b></div>
          <button class="btn" id="p-create">生成缴费单</button>
        </div>`;
      box.querySelector('#p-create').onclick = () => createOrder(plate, box);
    } catch (e) {
      box.innerHTML = `<div class="banner bad">${esc(e.message || '该车牌当前不在场内')}</div>`;
    }
  }

  async function createOrder(plate, box) {
    box.innerHTML = '<div class="card"><div class="empty">下单中…</div></div>';
    try {
      const order = await api('POST', '/payments/orders',
        { kind: 'parking_fee', plate });
      showCashier(order, box);
    } catch (e) {
      box.innerHTML = `<div class="banner bad">${esc(e.message || '下单失败')}</div>`;
    }
  }

  function showCashier(order, box) {
    const deadline = order.expireAtMs;
    box.innerHTML = `
      <div class="card cashier">
        <div class="muted">模拟收银台</div>
        <div class="amount">${order.amount.toFixed(2)}</div>
        <div class="muted">停车费 · ${esc(order.plate)}</div>
        <div class="qrbox">
          <img alt="支付二维码" width="150" height="150"
               src="/api/v1/qr?text=${encodeURIComponent('SMARTPARK-PAY:' + order.outTradeNo)}">
          <div class="otn">${esc(order.outTradeNo)}</div>
        </div>
        <div class="count" id="c-count">支付剩余 --:--</div>
        <button class="btn" id="c-pay">确认支付（模拟）</button>
        <p class="muted" style="margin-top:10px">v1 为模拟网关：真实场景此处为微信/支付宝支付二维码。支付成功即自动结算离场。</p>
      </div>`;
    const timer = setInterval(() => {
      const left = deadline - Date.now();
      const el = box.querySelector('#c-count');
      if (!el) { clearInterval(timer); return; }
      if (left <= 0) {
        el.textContent = '订单已超时关闭';
        clearInterval(timer);
        const btn = box.querySelector('#c-pay');
        if (btn) { btn.disabled = true; }
        return;
      }
      const m = Math.floor(left / 60000);
      const s = Math.floor((left % 60000) / 1000);
      el.textContent = `支付剩余 ${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
    }, 500);
    box.querySelector('#c-pay').onclick = async () => {
      const btn = box.querySelector('#c-pay');
      btn.disabled = true;
      try {
        const r = await api('POST', `/payments/orders/${order.orderId}/confirm`, {});
        clearInterval(timer);
        const leave = r.leave || {};
        box.innerHTML = `
          <div class="card cashier">
            <div style="font-size:44px">✅</div>
            <div style="font-size:18px;font-weight:700;margin:6px 0">支付成功，已放行</div>
            <div class="kv"><span>实付金额</span><b>${money(r.order.amount)}</b></div>
            <div class="kv"><span>停用车位</span><b>${esc(leave.spotId || '—')}</b></div>
            <div class="kv"><span>本次时长</span><b>${Math.round(leave.durationMin || 0)} 分钟</b></div>
            <a class="btn ghost" style="text-align:center;text-decoration:none" href="#/records">查看停车记录</a>
          </div>`;
        toast('缴费成功，一路顺风！');
      } catch (e) {
        toast(e.message || '支付失败');
        btn.disabled = false;
      }
    };
  }
}

/* ---------- 扫码进入 / 设置账户 ---------- */

// 扫码流程：确认点位 → （未登录先注册/登录）→ 绑定车牌 → 进首页。
// 票据存在 sessionStorage：注册/登录会整页重渲染，靠它把上下文带过去。
function viewClaim(root) {
  const ticket = hashQuery('t') || sessionStorage.getItem('sp_claim_ticket') || '';
  if (!ticket) {
    root.innerHTML = header('扫码进入') + `
      <div class="card"><div class="empty">
        请扫现场张贴的二维码进入。<br>
        <span class="muted">二维码里带着车场信息，扫了才知道要给哪个车场设置账户。</span>
      </div></div>`;
    return;
  }
  sessionStorage.setItem('sp_claim_ticket', ticket);

  // 未登录：先把票据留住，引导去注册（新用户）——登录后会自动回到这里。
  if (!state.token) {
    root.innerHTML = header('设置账户') + `
      <div class="card">
        <h3>扫码成功</h3>
        <p class="muted">需要先设置账户才能绑定车牌、预约车位与缴费。</p>
        <div class="row2" style="margin-top:12px">
          <button class="btn" id="c-reg">注册新账户</button>
          <button class="btn ghost" id="c-login">我有账户</button>
        </div>
      </div>`;
    root.querySelector('#c-reg').onclick = () => nav('#/register');
    root.querySelector('#c-login').onclick = () => nav('#/login');
    return;
  }

  root.innerHTML = header('设置账户') + `
    <div class="card" id="c-loading"><div class="empty">正在确认点位…</div></div>`;
  (async () => {
    let site;
    try {
      site = await api('GET', `/site/resolve?t=${encodeURIComponent(ticket)}`);
    } catch (e) {
      sessionStorage.removeItem('sp_claim_ticket');
      const fail = root.querySelector('#c-loading');
      if (fail) fail.remove();
      const box = document.createElement('div');
      box.innerHTML = `
        <div class="card">
          <div class="banner bad" style="margin:0">${esc(e.message || '二维码无效')}</div>
          <p class="muted" style="margin-top:10px">请扫现场张贴的最新二维码。</p>
        </div>`;
      root.appendChild(box);
      return;
    }

    let bound = state.plates;
    if (!bound.length) bound = await loadPlates();
    const loading = root.querySelector('#c-loading');
    if (loading) loading.remove();
    const card = document.createElement('div');
    card.innerHTML = `
      <div class="card">
        <h3>${esc(site.siteName || '停车场')}</h3>
        ${bound.length ? `
          <p class="muted">该账户已绑定车牌，可直接使用。</p>
          <div class="item"><div><div class="t">${esc(bound[0].plate)}</div>
            <div class="d">已绑定 · ${fmtTime(bound[0].createdAtMs)}</div></div>
            <span class="tag ok">已绑定</span></div>
          <button class="btn" id="c-go">进入用户端</button>
          <button class="btn ghost" id="c-add">再绑一辆车</button>
        ` : `
          <p class="muted">绑定车牌后，预约、缴费、寻车都可直接用，无需每次输入。</p>
          <div class="field"><label>车牌号</label>
            <input id="c-plate" placeholder="如 京A12345" value="${esc(state.lastPlate)}"></div>
          <button class="btn" id="c-bind">绑定并进入</button>
        `}
      </div>`;
    root.appendChild(card);

    const bind = async (plate) => {
      const p = (plate || '').trim();
      if (!p) { toast('请输入车牌号'); return; }
      try {
        await api('POST', '/me/plates', { plate: p, ticket });
        state.lastPlate = p;
        localStorage.setItem('sp_plate', p);
        sessionStorage.removeItem('sp_claim_ticket');
        await loadPlates();
        toast(`${p} 已绑定到本账户`);
        nav('#/home');
      } catch (e) {
        toast(e.message || '绑定失败');
      }
    };

    const go = card.querySelector('#c-go');
    if (go) go.onclick = () => {
      sessionStorage.removeItem('sp_claim_ticket');
      nav('#/home');
    };
    const add = card.querySelector('#c-add');
    if (add) add.onclick = () => {
      const p = window.prompt('再绑定一辆车牌', '');
      if (p) bind(p);
    };
    const bindBtn = card.querySelector('#c-bind');
    if (bindBtn) bindBtn.onclick = () => bind(card.querySelector('#c-plate').value);
  })();
}

/* ---------- 停车记录 ---------- */

function viewRecords(root) {
  root.innerHTML = header('停车记录') + `
    <div class="card">
      <div class="seg" style="margin-bottom:0">
        <a href="#/records" id="tab-active" class="on">在场中</a>
        <a href="#/records" id="tab-closed">历史</a>
      </div>
      <div id="rc-list" style="margin-top:6px"><div class="empty">加载中…</div></div>
    </div>`;
  let mode = 'active';
  const listEl = root.querySelector('#rc-list');
  const tabA = root.querySelector('#tab-active');
  const tabC = root.querySelector('#tab-closed');
  const paint = () => {
    tabA.classList.toggle('on', mode === 'active');
    tabC.classList.toggle('on', mode === 'closed');
    load();
  };
  tabA.onclick = () => { mode = 'active'; paint(); };
  tabC.onclick = () => { mode = 'closed'; paint(); };
  paint();

  async function load() {
    listEl.innerHTML = '<div class="empty">加载中…</div>';
    try {
      const data = await api('GET', `/records?state=${mode}&pageSize=50`);
      if (!data.records.length) {
        listEl.innerHTML = `<div class="empty">${mode === 'active' ? '当前没有在场车辆' : '暂无历史记录'}</div>`;
        return;
      }
      listEl.innerHTML = data.records.map((r) => `
        <div class="item">
          <div>
            <div class="t">${esc(r.plate)} <span class="tag ${r.status === 'active' ? 'info' : 'ok'}">${r.status === 'active' ? '在场' : '已离场'}</span></div>
            <div class="d">车位 ${esc(r.spotId)} · ${fmtTime(r.entryTimeMs)} 入场${r.exitTimeMs ? ' · ' + fmtTime(r.exitTimeMs) + ' 离场' : ''}</div>
          </div>
          <div style="text-align:right">
            <b>${r.status === 'closed' ? money(r.fee) : '进行中'}</b>
            <div class="d">${r.status === 'closed' ? Math.round(r.durationMin) + ' 分钟' : ''}</div>
          </div>
        </div>`).join('');
    } catch (e) {
      listEl.innerHTML = `<div class="empty">${esc(e.message || '加载失败')}</div>`;
    }
  }
}

/* ---------- 更多：反向寻车 / 服务信息 ---------- */

let layoutCache = null;

function viewMore(root) {
  root.innerHTML = header('更多功能') + `
    <div class="card">
      <h3>反向寻车 / 到场指引</h3>
      <div class="row2">
        <div class="field" style="margin:0"><input id="g-plate" placeholder="车牌号" value="${esc(plateHint())}"></div>
        <button class="btn" id="g-go" style="margin-top:0">查询路线</button>
      </div>
      <div id="g-result" style="margin-top:12px"></div>
    </div>
    <div class="card">
      <h3>拍照识牌 <span class="muted" id="lpr-mode"></span></h3>
      <p class="muted" style="margin-bottom:8px">拍摄或选择车牌照片，服务端识别后可直接用于入场/预约。</p>
      <input type="file" id="lpr-file" accept="image/*" capture="environment"
             style="width:100%;font-size:13px">
      <div id="lpr-result" style="margin-top:10px"></div>
    </div>
    <div class="card">
      <h3>无感支付（先离场后付）</h3>
      <p class="muted" style="margin-bottom:8px">开通后车辆离场自动扣费（演示网关，不产生真实扣款），大屏与手机同步收到扣费事件。</p>
      <div class="row2">
        <div class="field" style="margin:0"><input id="fr-plate" placeholder="车牌号" value="${esc(plateHint())}"></div>
        <button class="btn" id="fr-on" style="margin-top:0">开通</button>
      </div>
      <div id="fr-list" style="margin-top:10px"><div class="empty">加载中…</div></div>
    </div>
    <div class="card">
      <h3>接入二维码</h3>
      <div style="text-align:center">
        <img alt="接入二维码" width="168" height="168"
             src="/api/v1/qr?text=${encodeURIComponent(location.origin + '/')}">
        <p class="muted">其他设备扫码即可打开 SmartPark 用户端（同局域网）。</p>
      </div>
    </div>
    <div class="card">
      <h3>服务信息</h3>
      <div class="kv"><span>支付模式</span><b>${state.meta ? (state.meta.paymentMode === 'mock' ? '模拟网关（演示）' : esc(state.meta.paymentMode)) : '—'}</b></div>
      <div class="kv"><span>实时推送</span><b>${state.meta && state.meta.wsUrl ? '已启用' : '未启用'}</b></div>
      <div class="kv"><span>当前账号</span><b>${state.user ? esc(state.user.username) + '（' + esc(state.user.role) + '）' : '—'}</b></div>
      <button class="btn danger" id="m-logout">退出登录</button>
    </div>`;
  root.querySelector('#g-go').onclick = queryGuide;
  root.querySelector('#m-logout').onclick = () => doLogout();
  root.querySelector('#lpr-file').onchange = onLprFile;
  root.querySelector('#fr-on').onclick = () => toggleFrictionless(true);
  root.querySelector('#lpr-mode').textContent = '';
  loadFrictionless();

  async function onLprFile(event) {
    const file = event.target.files && event.target.files[0];
    if (!file) return;
    const box = root.querySelector('#lpr-result');
    box.innerHTML = '<div class="empty">识别中…</div>';
    try {
      const image = await fileToJpegBase64(file, 900);
      const r = await api('POST', '/lpr/recognize', { image });
      box.innerHTML = `
        <div class="banner ok">📷 识别结果：<b style="font-size:17px">${esc(r.plate)}</b>
          <span class="muted">（${{ mock: '演示模式', script: '识别脚本' }[r.backend] || r.backend}${r.confidence ? ' · 置信度 ' + Math.round(r.confidence * 100) + '%' : ''}）</span></div>
        <div class="row2" style="margin-top:10px">
          <button class="btn ghost" id="lpr-enter">🚗 用作入场登记</button>
          <button class="btn ghost" id="lpr-copy">📋 用作预约车牌</button>
        </div>`;
      box.querySelector('#lpr-enter').onclick = async () => {
        try {
          const enter = await api('POST', '/parking/enter',
            { plate: r.plate, vehicleType: 'car' });
          toast(`入场成功！车位 ${enter.spotId}`);
          nav('#/home');
        } catch (e) { toast(e.message || '入场失败'); }
      };
      box.querySelector('#lpr-copy').onclick = () => {
        state.lastPlate = r.plate;
        localStorage.setItem('sp_plate', r.plate);
        toast(`已记住车牌 ${r.plate}，预约页可直接使用`);
        nav('#/reserve');
      };
    } catch (e) {
      box.innerHTML = `<div class="banner bad">识别失败：${esc(e.message || '')}</div>`;
    } finally {
      event.target.value = '';
    }
  }

  async function toggleFrictionless(enabled) {
    const plate = root.querySelector('#fr-plate').value.trim();
    if (!plate) { toast('请输入车牌号'); return; }
    try {
      await api('POST', '/me/frictionless', { plate, enabled });
      toast(enabled ? `${plate} 已开通无感支付` : `${plate} 已关闭无感支付`);
      if (enabled) {
        state.lastPlate = plate;
        localStorage.setItem('sp_plate', plate);
      }
      loadFrictionless();
    } catch (e) {
      toast(e.message || '操作失败');
    }
  }

  async function loadFrictionless() {
    const box = root.querySelector('#fr-list');
    try {
      const data = await api('GET', '/me/frictionless');
      if (!data.plates.length) {
        box.innerHTML = '<div class="empty">暂未开通任何车牌</div>';
        return;
      }
      box.innerHTML = data.plates.map((p) => `
        <div class="item">
          <div><div class="t">${esc(p.plate)}</div>
            <div class="d">${fmtTime(p.createdAtMs)} 开通</div></div>
          <button class="btn danger sm" data-plate="${esc(p.plate)}">关闭</button>
        </div>`).join('');
      box.querySelectorAll('button[data-plate]').forEach((b) => {
        b.onclick = async () => {
          try {
            await api('POST', '/me/frictionless',
              { plate: b.dataset.plate, enabled: false });
            toast('已关闭');
            loadFrictionless();
          } catch (e) { toast(e.message || '操作失败'); }
        };
      });
    } catch (e) {
      box.innerHTML = `<div class="empty">${esc(e.message || '加载失败')}</div>`;
    }
  }

  async function queryGuide() {
    const plate = root.querySelector('#g-plate').value.trim();
    if (!plate) { toast('请输入车牌号'); return; }
    state.lastPlate = plate;
    localStorage.setItem('sp_plate', plate);
    const box = root.querySelector('#g-result');
    box.innerHTML = '<div class="empty">规划路线中…</div>';
    try {
      const [guide] = await Promise.all([
        api('GET', `/guide/${encodeURIComponent(plate)}`),
        loadLayout(),
      ]);
      box.innerHTML = `
        <canvas class="map" id="g-canvas" width="460" height="360"></canvas>
        ${LEGEND_HTML}
        <p class="muted" style="margin-top:8px">
          ${guide.source === 'active'
            ? `反向寻车：前往 <b>${esc(guide.spotId)}</b>（${esc(guide.zone)} 区），从入口步行约 <b>${esc(Math.round(guide.route.distanceM))} 米</b>、${esc(guide.route.turns)} 个转弯。`
            : `到场指引：预约车位 <b>${esc(guide.spotId)}</b>，入场步行约 <b>${esc(Math.round(guide.entryRoute.distanceM))} 米</b>。`}
        </p>`;
      drawMap(root.querySelector('#g-canvas'), guide);
    } catch (e) {
      box.innerHTML = `<div class="banner bad">${esc(e.message || '该车牌无在场记录或有效预约')}</div>`;
    }
  }
}

async function loadLayout() {
  if (!layoutCache) layoutCache = await api('GET', '/layout');
  return layoutCache;
}

const SPOT_COLORS = { 0: '#41d693', 1: '#f97066', 2: '#84caff', 3: '#4a5568' };

const LEGEND_HTML = `
  <div class="legend">
    <span><i style="background:#41d693"></i>空闲</span>
    <span><i style="background:#f97066"></i>占用</span>
    <span><i style="background:#84caff"></i>预约</span>
    <span><i style="background:#fdda54"></i>指引路线</span>
    <span><i style="background:#8b93a7"></i>停用/障碍</span>
  </div>`;

// 手机屏幕是 2x/3x：canvas 若按 CSS 尺寸做后备存储，浏览器放大后必然发糊。
// 这里按 devicePixelRatio 放大后备存储，再用 setTransform 把绘制坐标
// 还原成 CSS 像素，线宽与字号因此保持视觉一致。
function prepareCanvas(canvas) {
  const dpr = Math.min(3, Math.max(1, window.devicePixelRatio || 1));
  const cssWidth = canvas.clientWidth || 460;
  // 模板里的 width/height 属性定义宽高比；再次调用时两者同样被 dpr 放大，比例不变。
  const attrW = Number(canvas.getAttribute('width')) || 460;
  const attrH = Number(canvas.getAttribute('height')) || 340;
  const cssHeight = Math.round(cssWidth * (attrH / attrW));
  canvas.width = Math.round(cssWidth * dpr);
  canvas.height = Math.round(cssHeight * dpr);
  canvas.style.height = cssHeight + 'px';
  return { dpr, width: cssWidth, height: cssHeight };
}

// 通用车库平面图绘制：opts.highlightSpotId 高亮目标车位，
// opts.routePoints 传路线拐点时播放折线生长动画。
function drawLayout(canvas, opts = {}) {
  const { layout, spots } = layoutCache;
  const { dpr, width: W, height: H } = prepareCanvas(canvas);
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);   // 之后一律按 CSS 像素作图
  const scale = Math.min(W / (layout.siteWidth + 4), H / (layout.siteHeight + 4));
  const ox = (W - layout.siteWidth * scale) / 2;
  const oy = (H - layout.siteHeight * scale) / 2;
  const X = (x) => ox + x * scale;
  const Y = (y) => oy + y * scale;

  ctx.clearRect(0, 0, W, H);
  // 场地底板
  ctx.fillStyle = '#17233a';
  ctx.fillRect(X(0), Y(0), layout.siteWidth * scale, layout.siteHeight * scale);
  // 障碍物
  ctx.fillStyle = '#0b1120';
  (layout.obstacles || []).forEach((o) =>
    ctx.fillRect(X(o.x), Y(o.y), o.w * scale, o.h * scale));
  // 车位
  spots.forEach((s) => {
    const highlighted = opts.highlightSpotId === s.spotId;
    ctx.fillStyle = SPOT_COLORS[s.status] || '#4a5568';
    ctx.globalAlpha = highlighted ? 1 : 0.85;
    ctx.fillRect(X(s.x) + 0.5, Y(s.y) + 0.5,
                 Math.max(1, s.w * scale - 1), Math.max(1, s.h * scale - 1));
    ctx.globalAlpha = 1;
    if (highlighted) {
      ctx.strokeStyle = '#fdda54';
      ctx.lineWidth = 2;
      ctx.strokeRect(X(s.x) - 1, Y(s.y) - 1, s.w * scale + 2, s.h * scale + 2);
    }
  });
  // 出入口
  ctx.font = `${Math.max(9, 10 * scale)}px sans-serif`;
  // 出入口就在场地边缘（x=0 / x=siteWidth），标签按 -8 偏移会跑到画布外被裁掉。
  // 这里把文字夹回画布内，圆点仍在真实坐标上。
  const drawGate = (p, text, color) => {
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.arc(X(p.x), Y(p.y), Math.max(4, 3 * scale), 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = '#fff';
    const half = ctx.measureText(text).width / 2;
    const tx = Math.min(Math.max(X(p.x) - half, 2), Math.max(2, W - half * 2 - 2));
    const ty = Math.min(Math.max(Y(p.y) - 8, 12), H - 4);
    ctx.fillText(text, tx, ty);
  };
  (layout.entrances || []).forEach((p, i) => drawGate(p, '入' + (i + 1), '#fdda54'));
  (layout.exits || []).forEach((p, i) => drawGate(p, '出' + (i + 1), '#f97066'));

  const points = opts.routePoints;
  if (!points || !points.length) return;

  const target = opts.highlightSpotId
    ? spots.find((s) => s.spotId === opts.highlightSpotId) : null;
  const path = points.map((p) => [X(p.x), Y(p.y)]);
  if (target) path.push([X(target.x + target.w / 2), Y(target.y + target.h / 2)]);

  // 折线生长动画
  const total = path.reduce((sum, p, i) =>
    i ? sum + Math.hypot(p[0] - path[i - 1][0], p[1] - path[i - 1][1]) : 0, 0);
  let progress = 0;
  function drawRoute() {
    progress = Math.min(1, progress + 0.02);
    let remain = total * progress;
    ctx.strokeStyle = '#fdda54';
    ctx.lineWidth = 3;
    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';
    ctx.shadowColor = 'rgba(253,218,84,.8)';
    ctx.shadowBlur = 8;
    ctx.beginPath();
    ctx.moveTo(path[0][0], path[0][1]);
    for (let i = 1; i < path.length && remain > 0; i++) {
      const seg = Math.hypot(path[i][0] - path[i - 1][0], path[i][1] - path[i - 1][1]);
      if (seg <= remain) {
        ctx.lineTo(path[i][0], path[i][1]);
        remain -= seg;
      } else {
        const t = remain / seg;
        ctx.lineTo(path[i - 1][0] + (path[i][0] - path[i - 1][0]) * t,
                   path[i - 1][1] + (path[i][1] - path[i - 1][1]) * t);
        remain = 0;
      }
    }
    ctx.stroke();
    ctx.shadowBlur = 0;
    if (progress < 1) requestAnimationFrame(drawRoute);
  }
  drawRoute();
}

function drawMap(canvas, guide) {
  drawLayout(canvas, {
    highlightSpotId: guide.spotId,
    routePoints: guide.source === 'active'
        ? guide.route.points
        : (guide.entryRoute && guide.entryRoute.points),
  });
}

/* ---------- 启动 ---------- */

async function boot() {
  try {
    state.meta = await api('GET', '/meta');
  } catch (e) {
    $app.innerHTML = `<div class="banner bad" style="margin-top:40px">无法连接 SmartPark 服务端（${esc(e.message || '')}）<br>
      <span class="muted">请确认手机与运行服务端的 Mac 在同一网络</span></div>`;
    return;
  }
  window.addEventListener('hashchange', render);
  if (state.token) loadPlates().catch(() => {});
  render();
  connectWs();
}

boot();

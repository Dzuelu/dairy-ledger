/* ═══════════════════════════════════════════════════════════════════
   DairyLedger Dashboard — Main JavaScript
   Auto-refreshing node cards with temperature status
   ═══════════════════════════════════════════════════════════════════ */

const REFRESH_MS = 10000;   // Refresh every 10 seconds
const STALE_SEC  = 1800;    // 30 min = considered stale

let nodes = [];
let gatewayStatus = {};

// ─── Utilities ──────────────────────────────────────────────────────

function timeAgo(epoch) {
    if (!epoch || epoch < 1000000) return 'never';
    const sec = Math.floor(Date.now() / 1000) - epoch;
    if (sec < 60)   return `${sec}s ago`;
    if (sec < 3600) return `${Math.floor(sec / 60)}m ago`;
    if (sec < 86400) return `${Math.floor(sec / 3600)}h ago`;
    return `${Math.floor(sec / 86400)}d ago`;
}

function tempClass(value, thresholds) {
    if (value === null || value === undefined || value <= -126) return 'err';
    const t = thresholds || {};
    if (value >= (t.crit_high || 5.0) || value <= (t.crit_low || -3.9)) return 'crit';
    if (value >= (t.warn_high || 3.3) || value <= (t.warn_low || -2.2)) return 'warn';
    return 'ok';
}

function formatTemp(value) {
    if (value === null || value === undefined || value <= -126) return '—';
    return value.toFixed(1);
}

function sdStatusText(code) {
    const map = { 0: 'Unknown', 1: 'OK', 2: 'Degraded', 3: 'Failed', 4: 'No Card' };
    return map[code] || '?';
}

function toast(message, type = 'info') {
    const container = document.getElementById('toasts');
    const el = document.createElement('div');
    el.className = `toast ${type}`;
    el.textContent = message;
    container.appendChild(el);
    setTimeout(() => el.remove(), 4000);
}

// ─── Fetch Data ─────────────────────────────────────────────────────

async function fetchNodes() {
    try {
        const resp = await fetch('/api/nodes');
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        nodes = await resp.json();
        renderNodes();
    } catch (e) {
        console.error('Failed to fetch nodes:', e);
    }
}

async function fetchStatus() {
    try {
        const resp = await fetch('/api/status');
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        gatewayStatus = await resp.json();
        renderGatewayStatus();
    } catch (e) {
        console.error('Failed to fetch status:', e);
        document.getElementById('gateway-status').textContent = 'Offline';
        document.getElementById('gateway-dot').className = 'status-dot red';
    }
}

// ─── Render ─────────────────────────────────────────────────────────

function renderGatewayStatus() {
    const s = gatewayStatus;
    const timeEl  = document.getElementById('gateway-time');
    const dotEl   = document.getElementById('gateway-dot');
    const statEl  = document.getElementById('gateway-status');

    if (s.time_iso) {
        const t = s.time_iso.split('T')[1].replace('Z', '');
        timeEl.textContent = t.substring(0, 5);
    }

    const qualityMap = { 0: ['No Clock', 'red'], 1: ['RTC Only', 'yellow'], 2: ['NTP Synced', 'green'] };
    const [label, color] = qualityMap[s.time_quality] || ['Unknown', 'yellow'];
    dotEl.className = `status-dot ${color}`;
    statEl.textContent = `${label} · ${s.node_count || 0} nodes`;
}

function renderNodes() {
    const grid  = document.getElementById('node-grid');
    const empty = document.getElementById('empty-state');

    if (nodes.length === 0) {
        grid.style.display = 'none';
        empty.style.display = 'block';
        return;
    }

    grid.style.display = 'grid';
    empty.style.display = 'none';

    grid.innerHTML = nodes.map(n => {
        // Determine worst alert level
        let alertClass = '';
        if (!n.online) {
            alertClass = 'offline';
        } else if (n.temperatures) {
            for (const t of n.temperatures) {
                const cls = tempClass(parseFloat(t), n.thresholds);
                if (cls === 'crit') { alertClass = 'alert-crit'; break; }
                if (cls === 'warn') alertClass = 'alert-warn';
            }
        }

        // Temperature badges
        let tempsHtml = '';
        if (n.temperatures && n.temperatures.length > 0) {
            tempsHtml = n.temperatures.map((t, i) => {
                const val = parseFloat(t);
                const cls = tempClass(val, n.thresholds);
                const label = (n.probe_labels && n.probe_labels[i]) || `Probe ${i + 1}`;
                return `
                    <div class="temp-badge">
                        <span class="temp-label">${escapeHtml(label)}</span>
                        <span class="temp-value ${cls}">${formatTemp(val)}</span>
                        <span class="temp-unit">°C</span>
                    </div>`;
            }).join('');
        } else {
            tempsHtml = '<div class="temp-badge"><span class="temp-label">No data</span><span class="temp-value err">—</span></div>';
        }

        return `
            <div class="node-card ${alertClass}" onclick="location.href='/node.html?id=${n.id}'">
                <div class="node-header">
                    <div>
                        <div class="node-name">${escapeHtml(n.label)}</div>
                        <div class="node-id">${n.id}</div>
                    </div>
                    <span class="node-status ${n.online ? 'online' : 'offline'}">
                        ${n.online ? 'Online' : 'Offline'}
                    </span>
                </div>
                <div class="temps">${tempsHtml}</div>
                <div class="node-meta">
                    <span>📡 ${timeAgo(n.last_seen)}</span>
                    <span>💾 ${sdStatusText(n.sd_status)}</span>
                    ${n.pending_sync > 0 ? `<span>🔄 ${n.pending_sync} unsynced</span>` : ''}
                </div>
            </div>`;
    }).join('');
}

function escapeHtml(str) {
    const div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
}

// ─── Auto-refresh ───────────────────────────────────────────────────

async function refresh() {
    await Promise.all([fetchNodes(), fetchStatus()]);
}

refresh();
setInterval(refresh, REFRESH_MS);

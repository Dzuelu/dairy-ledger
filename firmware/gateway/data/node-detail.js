/* ═══════════════════════════════════════════════════════════════════
   DairyLedger — Node Detail Page Logic
   Shows single node info, current temps, and 24h chart
   ═══════════════════════════════════════════════════════════════════ */

(function() {
    const params = new URLSearchParams(location.search);
    const nodeId = params.get('id');

    if (!nodeId) {
        location.href = '/';
        return;
    }

    const SD_STATUS = { 0: 'Unknown', 1: 'OK', 2: 'Degraded', 3: 'Failed', 4: 'No Card' };
    let currentNode = null;  // Store for probe labels save

    async function loadNode() {
        try {
            const resp = await fetch(`/api/node/${nodeId}`);
            if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
            const node = await resp.json();
            currentNode = node;
            render(node);
            loadChart(node);
        } catch (e) {
            console.error('Failed to load node:', e);
            document.getElementById('node-label').textContent = 'Node not found';
        }
    }

    function render(n) {
        document.getElementById('node-label').textContent = n.label;
        document.getElementById('node-id').textContent = n.id;

        const statusEl = document.getElementById('node-status');
        statusEl.textContent = n.online ? 'Online' : 'Offline';
        statusEl.className = `node-status ${n.online ? 'online' : 'offline'}`;

        // Current temps
        const tempsEl = document.getElementById('node-temps');
        if (n.temperatures && n.temperatures.length > 0) {
            tempsEl.innerHTML = n.temperatures.map((t, i) => {
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
            tempsEl.innerHTML = '<p style="color:var(--text-dim)">No readings yet</p>';
        }

        // Probe labels form
        const formEl = document.getElementById('probe-labels-form');
        if (formEl && n.probe_count > 0) {
            formEl.innerHTML = '';
            for (let i = 0; i < n.probe_count; i++) {
                const label = (n.probe_labels && n.probe_labels[i]) || `Probe ${i + 1}`;
                formEl.innerHTML += `
                    <div class="form-group" style="margin-bottom:10px">
                        <label for="plabel-${i}">Probe ${i + 1}</label>
                        <input type="text" id="plabel-${i}" value="${escapeHtml(label)}"
                               maxlength="16" placeholder="Probe ${i + 1}"
                               style="max-width:300px">
                    </div>`;
            }
        }

        // Node name edit input
        const nameInput = document.getElementById('edit-node-label');
        if (nameInput && !nameInput.matches(':focus')) {
            nameInput.value = n.label;
        }

        // Info table
        document.getElementById('info-id').textContent = n.id;
        document.getElementById('info-label').textContent = n.label;
        document.getElementById('info-mac').textContent = n.mac || '—';
        document.getElementById('info-probes').textContent = n.probe_count;
        document.getElementById('info-sd').textContent = SD_STATUS[n.sd_status] || '?';
        document.getElementById('info-pending').textContent = n.pending_sync;
        document.getElementById('info-seen').textContent = n.last_seen
            ? `${timeAgo(n.last_seen)} (${new Date(n.last_seen * 1000).toLocaleString()})`
            : 'Never';

        const th = n.thresholds || {};
        document.getElementById('info-thresholds').textContent =
            `Warn: ${th.warn_low}°C – ${th.warn_high}°C · Crit: ${th.crit_low}°C – ${th.crit_high}°C`;

        // Config inputs (don't overwrite if user is typing)
        const setIfNotFocused = (id, val) => {
            const el = document.getElementById(id);
            if (el && !el.matches(':focus')) el.value = val;
        };
        setIfNotFocused('cfg-warn-high', th.warn_high);
        setIfNotFocused('cfg-crit-high', th.crit_high);
        setIfNotFocused('cfg-warn-low', th.warn_low);
        setIfNotFocused('cfg-crit-low', th.crit_low);
        setIfNotFocused('cfg-interval', Math.round((n.reading_interval_sec || 900) / 60));
    }

    // ─── Simple Canvas Chart ──────────────────────────────────────────

    async function loadChart(node) {
        // Get today's date in YYYY-MM-DD
        const today = new Date().toISOString().split('T')[0];

        try {
            const resp = await fetch(`/api/node/${nodeId}/data?date=${today}`);
            if (!resp.ok) throw new Error('No data');
            const csv = await resp.text();
            drawChart(csv, node);
        } catch (e) {
            document.getElementById('temp-chart').style.display = 'none';
            document.getElementById('chart-empty').style.display = 'block';
        }
    }

    function drawChart(csv, node) {
        const canvas = document.getElementById('temp-chart');
        const ctx = canvas.getContext('2d');
        const W = canvas.width;
        const H = canvas.height;

        // Parse CSV rows: timestamp,t0,t1,...
        const probeCount = node.probe_count || 1;
        const rows = csv.trim().split('\n').map(line => {
            const parts = line.split(',');
            return {
                time: parseInt(parts[0]),
                temps: parts.slice(1, 1 + probeCount).map(v => v === 'ERR' ? null : parseFloat(v))
            };
        }).filter(r => r.time > 0);

        if (rows.length === 0) {
            canvas.style.display = 'none';
            document.getElementById('chart-empty').style.display = 'block';
            return;
        }

        // Find data bounds
        let minT = Infinity, maxT = -Infinity;
        let minTime = rows[0].time, maxTime = rows[rows.length - 1].time;

        rows.forEach(r => {
            r.temps.forEach(t => {
                if (t !== null) {
                    minT = Math.min(minT, t);
                    maxT = Math.max(maxT, t);
                }
            });
        });

        // Include thresholds in range so all lines are always visible
        const th = node.thresholds || {};
        if (th.crit_high !== undefined) maxT = Math.max(maxT, th.crit_high);
        if (th.warn_high !== undefined) maxT = Math.max(maxT, th.warn_high);
        if (th.crit_low  !== undefined) minT = Math.min(minT, th.crit_low);
        if (th.warn_low  !== undefined) minT = Math.min(minT, th.warn_low);

        // Add some padding to temp range
        const tempPad = Math.max((maxT - minT) * 0.1, 0.5);
        minT -= tempPad;
        maxT += tempPad;

        const MARGIN = { top: 20, right: 20, bottom: 40, left: 50 };
        const plotW = W - MARGIN.left - MARGIN.right;
        const plotH = H - MARGIN.top - MARGIN.bottom;

        // Clear
        ctx.fillStyle = '#252740';
        ctx.fillRect(0, 0, W, H);

        // Draw threshold bands and lines
        function yPos(temp) {
            return MARGIN.top + plotH - ((temp - minT) / (maxT - minT)) * plotH;
        }

        // Warning high band (warn_high → crit_high)
        if (th.warn_high !== undefined && th.crit_high !== undefined) {
            ctx.fillStyle = 'rgba(250,204,21,0.08)';
            const y1 = yPos(th.warn_high);
            const y2 = yPos(th.crit_high);
            ctx.fillRect(MARGIN.left, Math.min(y1, y2), plotW, Math.abs(y2 - y1));
        }

        // Warning low band (crit_low → warn_low)
        if (th.warn_low !== undefined && th.crit_low !== undefined) {
            ctx.fillStyle = 'rgba(250,204,21,0.08)';
            const y1 = yPos(th.warn_low);
            const y2 = yPos(th.crit_low);
            ctx.fillRect(MARGIN.left, Math.min(y1, y2), plotW, Math.abs(y2 - y1));
        }

        // Warn high line (solid yellow)
        if (th.warn_high !== undefined) {
            ctx.strokeStyle = 'rgba(250,204,21,0.4)';
            ctx.lineWidth = 1;
            ctx.setLineDash([4, 4]);
            ctx.beginPath();
            ctx.moveTo(MARGIN.left, yPos(th.warn_high));
            ctx.lineTo(MARGIN.left + plotW, yPos(th.warn_high));
            ctx.stroke();
            ctx.setLineDash([]);
            ctx.fillStyle = 'rgba(250,204,21,0.5)';
            ctx.font = '10px system-ui';
            ctx.textAlign = 'right';
            ctx.fillText('Warn ' + th.warn_high + '°', MARGIN.left + plotW - 4, yPos(th.warn_high) - 4);
        }

        // Critical high line (dashed red)
        if (th.crit_high !== undefined) {
            ctx.strokeStyle = 'rgba(248,113,113,0.5)';
            ctx.lineWidth = 1;
            ctx.setLineDash([5, 5]);
            ctx.beginPath();
            ctx.moveTo(MARGIN.left, yPos(th.crit_high));
            ctx.lineTo(MARGIN.left + plotW, yPos(th.crit_high));
            ctx.stroke();
            ctx.setLineDash([]);
            ctx.fillStyle = 'rgba(248,113,113,0.6)';
            ctx.font = '10px system-ui';
            ctx.textAlign = 'right';
            ctx.fillText('Crit ' + th.crit_high + '°', MARGIN.left + plotW - 4, yPos(th.crit_high) - 4);
        }

        // Warn low line (solid yellow)
        if (th.warn_low !== undefined) {
            ctx.strokeStyle = 'rgba(250,204,21,0.4)';
            ctx.lineWidth = 1;
            ctx.setLineDash([4, 4]);
            ctx.beginPath();
            ctx.moveTo(MARGIN.left, yPos(th.warn_low));
            ctx.lineTo(MARGIN.left + plotW, yPos(th.warn_low));
            ctx.stroke();
            ctx.setLineDash([]);
            ctx.fillStyle = 'rgba(250,204,21,0.5)';
            ctx.font = '10px system-ui';
            ctx.textAlign = 'right';
            ctx.fillText('Warn ' + th.warn_low + '°', MARGIN.left + plotW - 4, yPos(th.warn_low) + 12);
        }

        // Critical low line (dashed red)
        if (th.crit_low !== undefined) {
            ctx.strokeStyle = 'rgba(248,113,113,0.5)';
            ctx.lineWidth = 1;
            ctx.setLineDash([5, 5]);
            ctx.beginPath();
            ctx.moveTo(MARGIN.left, yPos(th.crit_low));
            ctx.lineTo(MARGIN.left + plotW, yPos(th.crit_low));
            ctx.stroke();
            ctx.setLineDash([]);
            ctx.fillStyle = 'rgba(248,113,113,0.6)';
            ctx.font = '10px system-ui';
            ctx.textAlign = 'right';
            ctx.fillText('Crit ' + th.crit_low + '°', MARGIN.left + plotW - 4, yPos(th.crit_low) + 12);
        }

        // Grid lines
        ctx.strokeStyle = '#3d3f5c';
        ctx.lineWidth = 0.5;
        for (let i = 0; i <= 4; i++) {
            const y = MARGIN.top + (plotH / 4) * i;
            ctx.beginPath();
            ctx.moveTo(MARGIN.left, y);
            ctx.lineTo(MARGIN.left + plotW, y);
            ctx.stroke();

            // Y-axis labels
            const temp = maxT - ((maxT - minT) / 4) * i;
            ctx.fillStyle = '#8b8da8';
            ctx.font = '11px system-ui';
            ctx.textAlign = 'right';
            ctx.fillText(temp.toFixed(1) + '°', MARGIN.left - 8, y + 4);
        }

        // X-axis time labels
        ctx.fillStyle = '#8b8da8';
        ctx.textAlign = 'center';
        for (let i = 0; i <= 4; i++) {
            const t = minTime + ((maxTime - minTime) / 4) * i;
            const d = new Date(t * 1000);
            const x = MARGIN.left + (plotW / 4) * i;
            ctx.fillText(d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }),
                         x, H - 10);
        }

        // Draw probe lines
        const colors = ['#4ade80', '#60a5fa', '#facc15', '#f87171', '#a78bfa', '#22d3ee'];

        for (let p = 0; p < probeCount; p++) {
            ctx.strokeStyle = colors[p % colors.length];
            ctx.lineWidth = 2;
            ctx.beginPath();

            let started = false;
            rows.forEach(r => {
                if (r.temps[p] === null) return;
                const x = MARGIN.left + ((r.time - minTime) / (maxTime - minTime)) * plotW;
                const y = yPos(r.temps[p]);

                if (!started) {
                    ctx.moveTo(x, y);
                    started = true;
                } else {
                    ctx.lineTo(x, y);
                }
            });

            ctx.stroke();
        }

        // Legend
        for (let p = 0; p < probeCount; p++) {
            const label = (node.probe_labels && node.probe_labels[p]) || `Probe ${p + 1}`;
            const x = MARGIN.left + p * 110;
            ctx.fillStyle = colors[p % colors.length];
            ctx.fillRect(x, H - 30, 12, 3);
            ctx.fillStyle = '#8b8da8';
            ctx.font = '11px system-ui';
            ctx.textAlign = 'left';
            ctx.fillText(label, x + 16, H - 26);
        }
    }

    // ─── Init ─────────────────────────────────────────────────────────

    // Expose save functions to global scope for onclick
    window.saveNodeLabel = async function() {
        const input = document.getElementById('edit-node-label');
        const label = input ? input.value.trim() : '';
        if (!label) { toast('Node name cannot be empty', 'error'); return; }

        try {
            const body = new URLSearchParams({ label });
            const resp = await fetch(`/api/node/${nodeId}/label`, {
                method: 'POST', body
            });
            if (resp.ok) {
                toast('Node name saved!', 'success');
                loadNode();
            } else {
                toast('Failed to save node name', 'error');
            }
        } catch (e) {
            toast('Network error', 'error');
        }
    };

    window.saveProbeLabels = async function() {
        if (!currentNode) return;

        const body = new URLSearchParams();
        for (let i = 0; i < currentNode.probe_count; i++) {
            const input = document.getElementById(`plabel-${i}`);
            if (input) body.append(`probe${i}`, input.value.trim());
        }

        try {
            const resp = await fetch(`/api/node/${nodeId}/probe-labels`, {
                method: 'POST', body
            });
            if (resp.ok) {
                toast('Probe labels saved!', 'success');
                loadNode();
            } else {
                toast('Failed to save probe labels', 'error');
            }
        } catch (e) {
            toast('Network error', 'error');
        }
    };

    window.saveNodeConfig = async function() {
        const wh = document.getElementById('cfg-warn-high').value;
        const ch = document.getElementById('cfg-crit-high').value;
        const wl = document.getElementById('cfg-warn-low').value;
        const cl = document.getElementById('cfg-crit-low').value;
        const intervalMin = document.getElementById('cfg-interval').value;

        if (!wh || !ch || !wl || !cl || !intervalMin) {
            toast('Please fill in all config fields', 'error');
            return;
        }

        const intervalSec = Math.round(parseFloat(intervalMin) * 60);
        if (intervalSec < 60 || intervalSec > 86400) {
            toast('Interval must be between 1 and 1440 minutes', 'error');
            return;
        }

        try {
            const body = new URLSearchParams({
                warn_high: wh, crit_high: ch,
                warn_low: wl, crit_low: cl,
                interval: intervalSec
            });
            const resp = await fetch(`/api/node/${nodeId}/config`, {
                method: 'POST', body
            });
            if (resp.ok) {
                toast('Config saved — will push to node on next wake', 'success');
                loadNode();
            } else {
                toast('Failed to save config', 'error');
            }
        } catch (e) {
            toast('Network error', 'error');
        }
    };

    loadNode();
    setInterval(loadNode, 15000);
})();

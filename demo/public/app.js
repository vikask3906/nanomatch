// NanoMatch Live Order Book — WebSocket Client
// Connects to the Node.js server and renders the streaming JSON snapshots.

const WS_URL = `ws://${location.host}`;
const MAX_TAPE_ROWS = 80;
const MAX_DEPTH_LEVELS = 10;

// ── Elements ────────────────────────────────────────────────────────────────
const el = (id) => document.getElementById(id);

const mOrders  = el('m-orders');
const mTrades  = el('m-trades');
const mTp      = el('m-tp');
const mSpread  = el('m-spread');
const bestBid  = el('best-bid');
const bestAsk  = el('best-ask');
const spreadV  = el('spread-val');
const asksC    = el('asks-container');
const bidsC    = el('bids-container');
const tapeC    = el('tape-container');
const wsDot    = el('ws-status');
const statusLbl = el('engine-status');
const tradeLbl = el('trade-count-lbl');

// ── Connecting Overlay ───────────────────────────────────────────────────────
const overlay = document.createElement('div');
overlay.id = 'overlay';
overlay.innerHTML = `<div class="spinner"></div><p>Connecting to NanoMatch engine...</p>`;
document.body.appendChild(overlay);

// ── State ────────────────────────────────────────────────────────────────────
let prevBids = [];
let prevAsks = [];
let totalTrades = 0;
let maxBidQty = 1;
let maxAskQty = 1;

// ── Formatters ───────────────────────────────────────────────────────────────
const fmt  = (n, d=2) => Number(n).toFixed(d);
const fmtK = (n) => n >= 1e6 ? (n/1e6).toFixed(2) + 'M' : n >= 1e3 ? (n/1e3).toFixed(1) + 'K' : String(n);
const fmtTp = (n) => n >= 1e6 ? (n/1e6).toFixed(2) + 'M' : n >= 1e3 ? (n/1e3).toFixed(1) + 'K' : fmt(n, 0);

// ── Build Level Row DOM ──────────────────────────────────────────────────────
function buildLevelRow(lvl, side, maxQty, prevQty) {
    const row = document.createElement('div');
    row.className = `level-row ${side}-row`;
    row.dataset.price = lvl.price;

    const pct = Math.min(100, (lvl.qty / maxQty) * 100);
    row.style.setProperty('--bar-w', pct + '%');
    row.style.cssText += `--bar-w:${pct}%`;
    row.querySelector && null; // just creating

    const p = document.createElement('span');
    p.textContent = '$' + fmt(lvl.price);

    const q = document.createElement('span');
    q.textContent = fmtK(lvl.qty);

    const c = document.createElement('span');
    c.textContent = lvl.count;

    row.append(p, q, c);
    row.style.setProperty('--bar-w', pct + '%');

    // Inline the depth bar width via pseudo element trick using custom property
    row.style.cssText = ``;
    // We use a bg linear-gradient approach for depth:
    const bg = side === 'bid'
        ? `linear-gradient(to right, rgba(0,208,132,0.15) ${pct}%, transparent ${pct}%)`
        : `linear-gradient(to right, rgba(255,77,109,0.15) ${pct}%, transparent ${pct}%)`;
    row.style.backgroundImage = bg;

    // Flash if quantity changed
    if (prevQty !== undefined && prevQty !== lvl.qty) {
        row.classList.add(side === 'bid' ? 'flash-green' : 'flash-red');
        row.addEventListener('animationend', () => row.classList.remove('flash-green','flash-red'), {once:true});
    }

    return row;
}

// ── Render Order Book ────────────────────────────────────────────────────────
function renderBook(snapshot) {
    const { bids, asks } = snapshot;

    // Compute max qty for depth bar scaling
    maxBidQty = Math.max(1, ...bids.map(l => l.qty));
    maxAskQty = Math.max(1, ...asks.map(l => l.qty));

    // Build prev maps for flash detection
    const prevBidMap = Object.fromEntries(prevBids.map(l => [l.price, l.qty]));
    const prevAskMap = Object.fromEntries(prevAsks.map(l => [l.price, l.qty]));

    // ── Asks (displayed in reverse: lowest ask closest to spread)
    asksC.innerHTML = '';
    const asksDisplay = [...asks].reverse(); // highest ask first visually (furthest from spread)
    for (const lvl of asksDisplay) {
        asksC.appendChild(buildLevelRow(lvl, 'ask', maxAskQty, prevAskMap[lvl.price]));
    }

    // ── Bids
    bidsC.innerHTML = '';
    for (const lvl of bids) {
        bidsC.appendChild(buildLevelRow(lvl, 'bid', maxBidQty, prevBidMap[lvl.price]));
    }

    prevBids = bids;
    prevAsks = asks;
}

// ── Render Spread ────────────────────────────────────────────────────────────
function renderSpread(stats) {
    const bid = stats.best_bid > 0   ? '$' + fmt(stats.best_bid) : '—';
    const ask = stats.best_ask > 0   ? '$' + fmt(stats.best_ask) : '—';
    const sp  = stats.spread   > 0   ? fmt(stats.spread, 3)      : '—';

    bestBid.textContent = bid;
    bestAsk.textContent = ask;
    spreadV.textContent = sp;
    mSpread.textContent = sp;
}

// ── Render Metrics ───────────────────────────────────────────────────────────
function renderMetrics(stats) {
    mOrders.textContent  = fmtK(stats.processed);
    mTrades.textContent  = fmtK(stats.trades);
    mTp.textContent      = fmtTp(stats.throughput);
}

// ── Add Trade to Tape ────────────────────────────────────────────────────────
function addTrade(trade) {
    if (!trade) return;
    totalTrades++;
    tradeLbl.textContent = `${fmtK(totalTrades)} executions`;

    const row = document.createElement('div');
    const isBuy = trade.side === 'BUY';
    row.className = `tape-row ${isBuy ? 'tape-buy' : 'tape-sell'}`;

    const p = document.createElement('span');
    p.textContent = '$' + fmt(trade.price);

    const q = document.createElement('span');
    q.textContent = fmtK(trade.qty);

    const s = document.createElement('span');
    s.className = 'tape-side-badge';
    s.textContent = trade.side;

    row.append(p, q, s);
    tapeC.prepend(row); // newest at top

    // Trim old rows
    while (tapeC.children.length > MAX_TAPE_ROWS) {
        tapeC.removeChild(tapeC.lastChild);
    }
}

// ── Process snapshot ─────────────────────────────────────────────────────────
function processSnapshot(msg) {
    if (msg.type === 'snapshot') {
        renderBook(msg);
        renderSpread(msg.stats);
        renderMetrics(msg.stats);
        addTrade(msg.last_trade);
    } else if (msg.type === 'done') {
        statusLbl.textContent = `⚡ Replay complete (${fmtK(msg.stats.processed)} orders · ${fmtK(msg.stats.trades)} trades). Restarting...`;
        statusLbl.style.color = '#f6c90e';
        setTimeout(() => { statusLbl.style.color = ''; }, 3000);
    }
}

// ── WebSocket ─────────────────────────────────────────────────────────────────
function connect() {
    const ws = new WebSocket(WS_URL);

    ws.onopen = () => {
        wsDot.className = 'status-dot connected';
        overlay.classList.add('hidden');
        statusLbl.textContent = '⚡ Engine streaming live data';
    };

    ws.onmessage = (event) => {
        try {
            processSnapshot(JSON.parse(event.data));
        } catch (e) {
            console.error('Parse error:', e, event.data);
        }
    };

    ws.onclose = () => {
        wsDot.className = 'status-dot disconnected';
        statusLbl.textContent = '⚠ Connection lost. Reconnecting...';
        overlay.classList.remove('hidden');
        setTimeout(connect, 2000);
    };

    ws.onerror = () => {
        wsDot.className = 'status-dot disconnected';
    };
}

connect();

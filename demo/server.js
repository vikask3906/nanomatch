// NanoMatch Demo Server
// Spawns the C++ nanomatch_demo binary, reads JSON snapshots from its stdout,
// and broadcasts them to all connected WebSocket clients.

const express = require('express');
const { WebSocketServer } = require('ws');
const { spawn } = require('child_process');
const path = require('path');
const http = require('http');

const PORT     = 3000;
const EXE_PATH = path.join(__dirname, '..', 'nanomatch_demo.exe');
const DATA_PATH = path.join(__dirname, '..', 'data', 'orders_500k.csv');
const SPEED    = process.argv[2] || '60';   // orders/sec (visual demo speed)
const SNAP     = process.argv[3] || '3';    // emit snapshot every N orders

const app    = express();
const server = http.createServer(app);
const wss    = new WebSocketServer({ server });

// Serve static frontend
app.use(express.static(path.join(__dirname, 'public')));

// Track connected clients
const clients = new Set();

wss.on('connection', (ws) => {
    clients.add(ws);
    console.log(`[ws] Client connected (total: ${clients.size})`);

    ws.on('close', () => {
        clients.delete(ws);
        console.log(`[ws] Client disconnected (total: ${clients.size})`);
    });

    // Send current state immediately when a client joins
    if (lastSnapshot) {
        ws.send(lastSnapshot);
    }
});

function broadcast(data) {
    for (const ws of clients) {
        if (ws.readyState === 1) {  // OPEN
            ws.send(data);
        }
    }
}

let lastSnapshot = null;
let engineProcess = null;

function startEngine() {
    console.log(`[engine] Starting: ${EXE_PATH} ${DATA_PATH} ${SPEED} ${SNAP}`);

    engineProcess = spawn(EXE_PATH, [DATA_PATH, SPEED, SNAP]);

    let buffer = '';

    engineProcess.stdout.on('data', (chunk) => {
        buffer += chunk.toString();
        const lines = buffer.split('\n');
        buffer = lines.pop(); // keep incomplete line in buffer

        for (const line of lines) {
            if (!line.trim()) continue;
            try {
                const msg = JSON.parse(line);
                lastSnapshot = line;
                broadcast(line);

                if (msg.type === 'done') {
                    console.log('[engine] Replay complete. Restarting in 3s...');
                    setTimeout(startEngine, 3000);
                }
            } catch (e) {
                console.error('[engine] JSON parse error:', e.message, '| Line:', line.substring(0, 80));
            }
        }
    });

    engineProcess.stderr.on('data', (d) => {
        process.stderr.write('[engine] ' + d.toString());
    });

    engineProcess.on('error', (err) => {
        console.error('[engine] Failed to spawn:', err.message);
        console.error('Make sure you built nanomatch_demo.exe in the project root.');
    });

    engineProcess.on('exit', (code) => {
        if (code !== 0 && code !== null) {
            console.warn(`[engine] Exited with code ${code}`);
        }
    });
}

server.listen(PORT, () => {
    console.log(`\n🚀 NanoMatch Live Visualizer running at http://localhost:${PORT}`);
    console.log(`   Engine: ${EXE_PATH}`);
    console.log(`   Speed : ${SPEED} orders/sec`);
    console.log(`   Snap  : every ${SNAP} orders\n`);
    startEngine();
});

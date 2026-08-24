#!/usr/bin/env node
import http from 'node:http';
import { createRequire } from 'node:module';
import { readFileSync } from 'node:fs';

const require = createRequire(import.meta.url);
const { WebSocketServer } = require('../../server/node_modules/ws');

const port = Number(process.env.IEC104_GATEWAY_PORT ?? 24104);
const listenAddress = process.env.IEC104_GATEWAY_LISTEN ?? '127.0.0.1';
const authToken = process.env.IEC104_GATEWAY_TOKEN ?? '';
const gatewayVersion = readGatewayVersion();

const devices = new Map();
const clients = new Set();

function readGatewayVersion() {
  try {
    const rootPackage = JSON.parse(readFileSync(new URL('../../package.json', import.meta.url), 'utf8'));
    return rootPackage.version ?? '0.0.0';
  } catch {
    return '0.0.0';
  }
}

function sendJson(res, status, body) {
  const payload = Buffer.from(JSON.stringify(body));
  res.writeHead(status, {
    'content-type': 'application/json',
    'content-length': payload.length,
  });
  res.end(payload);
}

function normalizePath(pathname) {
  return pathname.replace(/\/+$/, '') || '/';
}

function authorized(req) {
  if (!authToken) return true;
  return req.headers.authorization === `Bearer ${authToken}`;
}

async function readJson(req) {
  const chunks = [];
  for await (const chunk of req) chunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk));
  const text = Buffer.concat(chunks).toString('utf8').trim();
  return text ? JSON.parse(text) : {};
}

function broadcast(event) {
  const payload = JSON.stringify(event);
  for (const ws of clients) {
    if (ws.readyState === ws.OPEN) ws.send(payload);
  }
}

function getDevice(deviceId) {
  let state = devices.get(deviceId);
  if (!state) {
    state = {
      config: null,
      running: false,
      interval: null,
      values: new Map(),
    };
    devices.set(deviceId, state);
  }
  return state;
}

function startDevice(deviceId) {
  const state = getDevice(deviceId);
  state.running = true;
  if (state.interval) clearInterval(state.interval);

  broadcast({ type: 'status', deviceId, gatewayConnected: true, iec104Connected: true, state: 'running', lastError: '' });
  state.interval = setInterval(() => emitValues(deviceId), 1000);
  emitValues(deviceId);
}

function stopDevice(deviceId) {
  const state = getDevice(deviceId);
  state.running = false;
  if (state.interval) clearInterval(state.interval);
  state.interval = null;
  broadcast({ type: 'status', deviceId, gatewayConnected: true, iec104Connected: false, state: 'off', lastError: '' });
}

function emitValues(deviceId, cot = 3) {
  const state = getDevice(deviceId);
  if (!state.running || !state.config) return;

  const now = Date.now();
  for (const tag of state.config.tags ?? []) {
    if (String(tag.deviceDataType ?? '').startsWith('C_')) continue;
    const current = state.values.has(tag.tagId) ? state.values.get(tag.tagId) : defaultValue(tag, now);
    const value = typeof current === 'number' ? Number((current + 0.1).toFixed(3)) : current;
    state.values.set(tag.tagId, value);
    broadcast({
      type: 'value',
      deviceId,
      tagId: tag.tagId,
      ioa: tag.ioa,
      asduType: tag.deviceDataType,
      value,
      quality: { invalid: false, notTopical: false, substituted: false, blocked: false, overflow: false },
      cot,
      timestamp: now,
    });
  }
}

function defaultValue(tag, now) {
  const type = String(tag.visionType ?? tag.type ?? '').toLowerCase();
  if (type === 'boolean' || type === 'bool') return false;
  if (type === 'doublepoint') return 1;
  return Number(((now / 1000) % 100).toFixed(3));
}

const server = http.createServer(async (req, res) => {
  try {
    const url = new URL(req.url ?? '/', `http://${req.headers.host ?? '127.0.0.1'}`);
    const path = normalizePath(url.pathname);
    const method = String(req.method ?? 'GET').toUpperCase();

    if (path === '/api/v1/health' && method === 'GET') {
      sendJson(res, 200, { ok: true, service: 'vision-one-iec104-mock-gateway', version: gatewayVersion });
      return;
    }

    if (path === '/api/v1/version' && method === 'GET') {
      sendJson(res, 200, { ok: true, version: gatewayVersion, mock: true });
      return;
    }

    if (!authorized(req)) {
      sendJson(res, 401, { ok: false, error: 'unauthorized' });
      return;
    }

    const match = path.match(/^\/api\/v1\/devices\/([^/]+)(?:\/(config|start|stop|status|write|interrogate))$/);
    if (!match) {
      sendJson(res, 404, { ok: false, error: 'not-found' });
      return;
    }

    const deviceId = decodeURIComponent(match[1]);
    const action = match[2];
    const state = getDevice(deviceId);

    if (action === 'config' && method === 'POST') {
      state.config = await readJson(req);
      sendJson(res, 200, { ok: true, deviceId, tags: state.config.tags?.length ?? 0 });
      return;
    }

    if (action === 'start' && method === 'POST') {
      startDevice(deviceId);
      sendJson(res, 200, { ok: true, deviceId });
      return;
    }

    if (action === 'stop' && method === 'POST') {
      stopDevice(deviceId);
      sendJson(res, 200, { ok: true, deviceId });
      return;
    }

    if (action === 'status' && method === 'GET') {
      sendJson(res, 200, { ok: true, deviceId, gatewayConnected: true, iec104Connected: state.running, state: state.running ? 'running' : 'off' });
      return;
    }

    if (action === 'interrogate' && method === 'POST') {
      if (!state.running) {
        sendJson(res, 409, { ok: false, error: 'not-connected' });
        return;
      }
      sendJson(res, 200, { ok: true });
      setTimeout(() => emitValues(deviceId, 20), 0);
      return;
    }

    if (action === 'write' && method === 'POST') {
      const body = await readJson(req);
      const requestId = typeof body.requestId === 'string' ? body.requestId.trim() : '';
      if (!requestId) {
        sendJson(res, 400, { ok: false, error: 'missing-request-id' });
        return;
      }
      if (!state.running) {
        sendJson(res, 409, { ok: false, requestId, error: 'not-connected' });
        return;
      }

      sendJson(res, 200, { ok: true, requestId, cause: 'accepted' });
      setTimeout(() => {
        broadcast({ type: 'write-result', deviceId, requestId, ioa: body.ioa, deviceDataType: body.deviceDataType, ok: true, cause: 'activation-confirmed' });
        const tags = state.config?.tags ?? [];
        const tag = tags.find((entry) => body.tagId && entry.tagId === body.tagId) ?? tags.find((entry) => entry.ioa === body.ioa);
        if (tag) {
          state.values.set(tag.tagId, body.value);
          broadcast({ type: 'value', deviceId, tagId: tag.tagId, ioa: tag.ioa, asduType: tag.deviceDataType, value: body.value, quality: { invalid: false }, cot: 3, timestamp: Date.now() });
        }
      }, 50);
      return;
    }

    sendJson(res, 405, { ok: false, error: 'method-not-allowed' });
  } catch (error) {
    sendJson(res, 500, { ok: false, error: error?.message ?? String(error) });
  }
});

const wss = new WebSocketServer({ noServer: true });

server.on('upgrade', (req, socket, head) => {
  const url = new URL(req.url ?? '/', `http://${req.headers.host ?? '127.0.0.1'}`);
  if (normalizePath(url.pathname) !== '/api/v1/events' || !authorized(req)) {
    socket.write('HTTP/1.1 401 Unauthorized\r\n\r\n');
    socket.destroy();
    return;
  }

  wss.handleUpgrade(req, socket, head, (ws) => {
    clients.add(ws);
    ws.on('close', () => clients.delete(ws));
    ws.send(JSON.stringify({ type: 'gateway', ok: true, version: gatewayVersion }));
  });
});

server.listen(port, listenAddress, () => {
  console.log(`IEC104 mock gateway listening on http://${listenAddress}:${port}`);
});

process.on('SIGINT', () => {
  for (const state of devices.values()) {
    if (state.interval) clearInterval(state.interval);
  }
  server.close(() => process.exit(0));
});

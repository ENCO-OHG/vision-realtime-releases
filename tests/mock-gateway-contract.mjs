import assert from 'node:assert/strict';
import net from 'node:net';
import { createRequire } from 'node:module';
import { once } from 'node:events';
import { spawn } from 'node:child_process';

const require = createRequire(new URL('../../vision-one/server/package.json', import.meta.url));
const { WebSocket } = require('ws');

const port = await availablePort();
const baseUrl = `http://127.0.0.1:${port}`;
const child = spawn(process.execPath, ['mock-gateway.mjs'], {
  cwd: new URL('..', import.meta.url),
  env: { ...process.env, IEC104_GATEWAY_LISTEN: '127.0.0.1', IEC104_GATEWAY_PORT: String(port), IEC104_GATEWAY_TEST_EVENTS: '1' },
  stdio: ['ignore', 'pipe', 'pipe'],
});
const childExited = once(child, 'exit');
let eventSocket;

try {
  await waitForStartup(child);
  const { events, socket } = await connectEvents();
  eventSocket = socket;
  await request('/api/v1/devices/device-1/config', 'POST', {
    tags: [{ tagId: 'tag-1', ioa: 1001, deviceDataType: 'M_ME_NC_1' }],
  });
  await request('/api/v1/devices/device-1/start', 'POST');

  const initial = await waitForEvent(events, (event) => event.type === 'value');
  assert.deepEqual(initial.quality, { raw: 0, invalid: false, notTopical: false, substituted: false, blocked: false, overflow: false });
  assert.equal(initial.sourceTimestamp, 0);
  assert.equal(initial.timestamp, initial.receivedTimestamp);
  assert.equal(initial.timestampSource, 'none');
  assert.equal(initial.timestampValid, false);
  assert.equal(initial.timestampInvalid, false);

  await emitMockValue(events, {
    value: 10,
    quality: 0x80,
    cot: 3,
    receivedTimestamp: 1_000,
  }, (event) => event.value === 10 && event.quality.raw === 0x80);
  const invalidQuality = events.at(-1);
  assert.equal(invalidQuality.quality.invalid, true);

  await emitMockValue(events, {
    value: 11,
    quality: 0x71,
    cot: 3,
    receivedTimestamp: 1_001,
  }, (event) => event.value === 11 && event.quality.raw === 0x71);
  const uncertainQuality = events.at(-1);
  assert.equal(uncertainQuality.quality.invalid, false);
  assert.equal(uncertainQuality.quality.notTopical, true);
  assert.equal(uncertainQuality.quality.substituted, true);
  assert.equal(uncertainQuality.quality.blocked, true);
  assert.equal(uncertainQuality.quality.overflow, true);

  await emitMockValue(events, {
    value: 12,
    cot: 3,
    receivedTimestamp: 2_000,
    sourceTimestamp: 1_500,
    timestampPresent: true,
    timestampValid: true,
  }, (event) => event.value === 12);
  const validCp56 = events.at(-1);
  assert.equal(validCp56.timestamp, 1_500);
  assert.equal(validCp56.sourceTimestamp, 1_500);
  assert.equal(validCp56.timestampSource, 'rtu');
  assert.equal(validCp56.timestampValid, true);
  assert.equal(validCp56.timestampInvalid, false);

  await emitMockValue(events, {
    value: 13,
    cot: 3,
    receivedTimestamp: 3_000,
    timestampPresent: true,
    timestampValid: false,
    timestampSubstituted: true,
    timestampSummerTime: true,
  }, (event) => event.value === 13);
  const invalidCp56 = events.at(-1);
  assert.equal(invalidCp56.timestamp, 3_000);
  assert.equal(invalidCp56.sourceTimestamp, 0);
  assert.equal(invalidCp56.timestampSource, 'rtu');
  assert.equal(invalidCp56.timestampValid, false);
  assert.equal(invalidCp56.timestampInvalid, true);
  assert.equal(invalidCp56.timestampSubstituted, true);
  assert.equal(invalidCp56.timestampSummerTime, true);

  for (const qualifier of [20, 21, 36]) {
    const index = events.length;
    const response = await request('/api/v1/devices/device-1/interrogate', 'POST', { qualifier });
    assert.equal(response.status, 200);
    const event = await waitForEvent(events, (candidate) => candidate.type === 'value' && candidate.cot === qualifier, index);
    assert.equal(event.cot, qualifier);
  }

  const invalidQualifier = await request('/api/v1/devices/device-1/interrogate', 'POST', { qualifier: 19 });
  assert.equal(invalidQualifier.status, 400);
  assert.equal(invalidQualifier.body.error, 'invalid-qualifier');
  console.log('Mock gateway contract tests passed.');
} finally {
  eventSocket?.terminate();
  if (child.exitCode === null) child.kill();
  await childExited;
}

async function availablePort() {
  const server = net.createServer();
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const { port } = server.address();
  await new Promise((resolve) => server.close(resolve));
  return port;
}

async function waitForStartup(process) {
  const output = [];
  process.stdout.on('data', (chunk) => output.push(String(chunk)));
  process.stderr.on('data', (chunk) => output.push(String(chunk)));
  await waitFor(() => output.join('').includes('IEC104 mock gateway listening'));
}

async function connectEvents() {
  const events = [];
  const ws = new WebSocket(baseUrl.replace('http', 'ws') + '/api/v1/events');
  ws.on('message', (message) => events.push(JSON.parse(String(message))));
  await once(ws, 'open');
  return { events, socket: ws };
}

async function emitMockValue(events, payload, predicate) {
  const index = events.length;
  const response = await request('/api/v1/devices/device-1/mock-value', 'POST', { tagId: 'tag-1', ...payload });
  assert.equal(response.status, 200);
  await waitForEvent(events, predicate, index);
}

async function request(path, method, body = undefined) {
  const response = await fetch(baseUrl + path, {
    method,
    headers: body === undefined ? undefined : { 'content-type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  return { status: response.status, body: await response.json() };
}

async function waitForEvent(events, predicate, start = 0) {
  await waitFor(() => events.slice(start).some(predicate));
  return events.slice(start).find(predicate);
}

async function waitFor(predicate) {
  const deadline = Date.now() + 5_000;
  while (!predicate()) {
    if (Date.now() >= deadline) throw new Error('timeout');
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
}

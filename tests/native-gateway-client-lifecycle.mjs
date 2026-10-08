import assert from 'node:assert/strict';
import { once } from 'node:events';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import net from 'node:net';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { spawn } from 'node:child_process';

const maxClientConnections = 64;
const port = await availablePort();
const workDir = await mkdtemp(join(tmpdir(), 'vision-realtime-client-lifecycle-'));
const configPath = join(workDir, 'gateway.json');
const executable = process.env.VISION_REALTIME_EXE ?? resolve('build/vision-realtime-mock/Release/vision-realtime.exe');
const output = [];
let child;
let childExited;
let clients = [];

try {
  await writeFile(configPath, JSON.stringify({
    listenAddress: '127.0.0.1',
    port,
    credentials: {
      controller: {
        id: 'controller',
        token: 'controller-token',
        gatewayTargetId: 'gateway',
        controllerId: 'vision-one',
        controllerGeneration: 1,
      },
      operators: [],
    },
    logLevel: 'error',
    logDir: workDir,
    stateFile: join(workDir, 'state.json'),
  }));

  child = spawn(executable, ['run', '--config', configPath], { stdio: ['ignore', 'pipe', 'pipe'] });
  childExited = once(child, 'exit');
  child.stdout.on('data', (chunk) => output.push(String(chunk)));
  child.stderr.on('data', (chunk) => output.push(String(chunk)));
  await waitFor(() => output.join('').includes('IEC104 gateway listening'));

  clients = await Promise.all(Array.from({ length: maxClientConnections }, () => openClient(port)));
  await expectClose(openClient(port), 500);

  await delay(1_500);
  const response = await request(port);
  assert.match(response, /^HTTP\/1\.1 200 OK\r\n/);
  console.log('Native gateway client lifecycle test passed.');
} finally {
  for (const client of clients) client.destroy();
  if (child?.exitCode === null) child.kill();
  if (childExited) await childExited;
  await rm(workDir, { recursive: true, force: true });
}

async function availablePort() {
  const server = net.createServer();
  await once(server.listen(0, '127.0.0.1'), 'listening');
  const { port } = server.address();
  await new Promise((resolveClose) => server.close(resolveClose));
  return port;
}

function openClient(port) {
  return new Promise((resolveOpen, reject) => {
    const client = net.createConnection({ host: '127.0.0.1', port });
    client.once('connect', () => resolveOpen(client));
    client.once('error', reject);
  });
}

async function expectClose(clientPromise, timeout) {
  const client = await clientPromise;
  await Promise.race([
    once(client, 'close'),
    delay(timeout).then(() => {
      client.destroy();
      throw new Error('gateway did not reject a client above the connection limit');
    }),
  ]);
}

function request(port) {
  return new Promise((resolveRequest, reject) => {
    const client = net.createConnection({ host: '127.0.0.1', port });
    const response = [];
    client.once('connect', () => client.end('GET /api/v1/health HTTP/1.1\r\nHost: localhost\r\n\r\n'));
    client.on('data', (chunk) => response.push(String(chunk)));
    client.once('error', reject);
    client.once('close', () => resolveRequest(response.join('')));
  });
}

async function waitFor(predicate) {
  const deadline = Date.now() + 5_000;
  while (!predicate()) {
    if (Date.now() >= deadline) throw new Error(output.join('') || 'gateway startup timed out');
    await delay(10);
  }
}

function delay(milliseconds) {
  return new Promise((resolveDelay) => setTimeout(resolveDelay, milliseconds));
}

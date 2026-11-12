import test from 'node:test';
import assert from 'node:assert/strict';
import { LineBuffer, encode, pairing, validateWiFi, hello, networks, agents, nearby, Session } from '../src/protocol.js';
import { DemoTransport, BluetoothTransport } from '../src/transports.js';
import { validateApplication, validatePartitions, validateManifest, getImage, matchesRelease, firmwareIdentity, firmwareUpdate, sha256 } from '../src/firmware.js';

test('UTF-8 lines survive single-byte BLE fragments and bound memory', () => {
  const buffer = new LineBuffer();
  const lines = [];
  for (const value of new TextEncoder().encode('NET -45 家里的网络\nOK saved\r\n')) lines.push(...buffer.append([value]));
  assert.deepEqual(lines, ['NET -45 家里的网络', 'OK saved']);
  assert.throws(() => buffer.append(new Uint8Array(8193).fill(65)), /responseInvalid/);
  assert.equal(encode('我 !~'), '%E6%88%91%20%21%7E');
});

test('pairing validates embedded address, credentials, byte limits and outer JSON', () => {
  const code = Buffer.from(JSON.stringify({ url: 'wss://gateway.example', bootstrapToken: 'example-bootstrap' })).toString('base64url');
  assert.equal(pairing(code).url, 'wss://gateway.example');
  assert.equal(pairing(JSON.stringify({ url: 'wss://gateway.example/', code })).code, code);
  assert.throws(() => pairing(code, 'wss://other.example'), /addressMismatch/);
  assert.throws(() => pairing(JSON.stringify({ url: 'wss://other.example', code })), /addressMismatch/);
  assert.throws(() => pairing(Buffer.from(JSON.stringify({ url: 'wss://user:pass@gateway.example', bootstrapToken: 'test' })).toString('base64url')), /pairing/);
  assert.throws(() => validateWiFi('Office', '', false), /passwordInvalid/);
  assert.doesNotThrow(() => validateWiFi('Office', '', false, [{ ssid: 'Office' }]));
  assert.throws(() => validateWiFi('字'.repeat(11), '12345678', false), /ssidInvalid/);
});

test('snapshot validates counts, generation and per-agent routes', () => {
  assert.equal(hello('HELLO ai-passport-openclaw 2 1 1 zh 0.4.2 scan-live').generation, 2);
  assert.throws(() => hello('HELLO another-product'), /firmwareUnsupported/);
  assert.throws(() => networks(['NETS 2 1 0', 'NETS END'], 2), /responseInvalid/);
  assert.throws(() => networks(['NETS 1 0 -1', 'NETS END'], 2), /responseInvalid/);
  const result = agents(['AGENTS 2 1 0', 'AGENT 0 My%20Agent wss%3A%2F%2Fgateway.example ready', 'ROUTE 0 ss proxy.example 443 1 aes-256-cfb', 'AGENTS END'], 2);
  assert.equal(result[0].route.savedSecret, true);
  const list = [];
  nearby('NET -40 My iPhone', list); nearby('NET_KIND hotspot My%20iPhone', list); nearby('NET -70 My iPhone', list);
  assert.deepEqual(list, [{ ssid: 'My iPhone', rssi: -40, hotspot: true }]);
});

test('same protocol powers a complete demo snapshot, scan and generation changes', async () => {
  const session = new Session(new DemoTransport());
  const snapshot = await session.snapshot();
  assert.equal(snapshot.networks.length, 2);
  assert.equal(snapshot.agents[0].route.mode, 'direct');
  const rows = [];
  await session.request('SCAN LIVE', 'SCAN END', 5000, line => nearby(line, rows));
  assert.equal(rows.length, 3);
  assert.ok(rows[1].hotspot);
  const updated = await session.mutate(`LANG ${snapshot.generation} en`, 'OK lang');
  assert.equal(updated.generation, snapshot.generation + 1);
  assert.equal(updated.language, 'en');
  await session.close();
});

test('timeouts close the session; reject parallel commands and disconnect immediately', async () => {
  let closed = false;
  const session = new Session({ send() {}, close() { closed = true; } });
  const pending = session.request('HELLO', 'never', 20);
  await assert.rejects(session.request('NETS', 'NETS END'), /busy/);
  await assert.rejects(pending, /timeout/);
  assert.ok(closed);
  await assert.rejects(session.request('HELLO', 'never'), /disconnected/);
});

test('diagnostic DEVICE events cannot corrupt the next snapshot response', async () => {
  const transport = new DemoTransport();
  const send = transport.send.bind(transport);
  transport.send = async command => {
    transport.onData(new TextEncoder().encode('DEVICE volume=40 battery=85\nI (42) passport: idle\n'));
    return send(command);
  };
  const session = new Session(transport);
  assert.equal((await session.snapshot()).networks.length, 2);
  await session.close();
});

test('Bluetooth sends acknowledged 20-byte UTF-8 chunks in order', async () => {
  const transport = new BluetoothTransport();
  const chunks = [];
  let pending = false;
  transport.rx = { async writeValueWithResponse(chunk) {
    assert.equal(pending, false);
    pending = true;
    assert.ok(chunk.length <= 20);
    await Promise.resolve();
    chunks.push(chunk);
    pending = false;
  } };
  const command = `SAVE 1 ${encode('我的网络')} ${encode('example-password')}`;
  await transport.send(command);
  assert.equal(Buffer.concat(chunks).toString(), command + '\n');
});

test('Bluetooth takeover rejects pending work and never reconnects automatically', async () => {
  for (const pending of [false, true]) {
    let closes = 0;
    let disconnects = 0;
    const transport = { send() {}, async close() { closes++; this.onDisconnect(); } };
    const session = new Session(transport, () => { disconnects++; });
    const request = pending ? session.request('NETS', 'NETS END') : null;
    transport.onData(new TextEncoder().encode('SESSION TAKEN\n'));
    if (request) await assert.rejects(request, /takenOver/);
    assert.equal(session.closed, true);
    assert.equal(session.disconnectReason, 'takenOver');
    assert.equal(closes, 1);
    assert.equal(disconnects, 1);
    await assert.rejects(session.request('HELLO', 'HELLO'), /disconnected/);
  }
});

test('unsafe chip images and partitions are refused', () => {
  const image = new Uint8Array(288); image[0] = 0xe9; image[12] = 5; image.set([0x32, 0x54, 0xcd, 0xab], 32);
  assert.doesNotThrow(() => validateApplication(image));
  image[12] = 9; assert.throws(() => validateApplication(image), /wrongChip/);
  assert.throws(() => validatePartitions(new Uint8Array(4096)), /firmwareUnsupported/);
  const table = new Uint8Array(4096);
  const view = new DataView(table.buffer);
  for (const [offset, start, size, type] of [[0, 0x10000, 0x340000, 0], [32, 0x350000, 0x10000, 1]]) {
    view.setUint16(offset, 0x50aa, true); table[offset + 2] = type;
    view.setUint32(offset + 4, start, true); view.setUint32(offset + 8, size, true);
  }
  assert.doesNotThrow(() => validatePartitions(table));
  view.setUint32(8, 0x700000, true); assert.throws(() => validatePartitions(table), /firmwareUnsupported/);
});

test('manifest paths cannot redirect firmware to third-party or traverse directories', () => {
  const entry = { file: 'app.bin', offset: 0x10000, size: 288, sha256: 'a'.repeat(64) };
  const manifest = { schema: 1, version: 'v0.4.2', commit: 'b'.repeat(40), application: entry, full: { ...entry, file: 'full.bin', offset: 0 } };
  assert.doesNotThrow(() => validateManifest(manifest));
  assert.throws(() => validateManifest({ ...manifest, application: { ...entry, file: '../app.bin' } }), /integrity/);
  assert.throws(() => validateManifest({ ...manifest, application: { ...entry, file: 'https://files.example/app.bin' } }), /integrity/);
  assert.doesNotThrow(() => validateManifest({ ...manifest, buildId: '1234567890abcdef' }));
  assert.throws(() => validateManifest({ ...manifest, buildId: 'v0.4.2' }), /integrity/);
  assert.doesNotThrow(() => validateManifest({ ...manifest, knownBuilds: { abcdef1234567890: { version: 'v0.4.1', channel: 'release' } } }));
  for (const knownBuilds of [null, [], 'invalid', { invalid: { version: 'v0.4.1', channel: 'release' } }, { abcdef1234567890: { version: 'private descriptor', channel: 'development' } }, { abcdef1234567890: { version: '', channel: 'release' } }, { abcdef1234567890: { version: 'v0.4.1', channel: 'invalid' } }]) {
    assert.throws(() => validateManifest({ ...manifest, knownBuilds }), /integrity/);
  }
});

test('firmware actions distinguish updates, development builds, reinstall and downgrade', () => {
  const build = 'abcdef1234567890';
  const manifest = { version: 'v1.10.0', buildId: '1234567890abcdef', knownBuilds: {} };
  assert.equal(firmwareUpdate(manifest.buildId, manifest).status, 'current');
  assert.equal(firmwareUpdate(build, manifest).status, 'available');
  assert.equal(firmwareUpdate(build, null).status, 'unavailable');
  manifest.knownBuilds[build] = { version: 'v1.9.0', channel: 'release' };
  assert.deepEqual(firmwareIdentity(build, manifest), { kind: 'version', value: 'v1.9.0' });
  assert.deepEqual(firmwareUpdate(build, manifest), { status: 'update', action: 'application', confirm: 'confirmFlash' });
  manifest.knownBuilds[build].version = 'v2.0.0';
  assert.deepEqual(firmwareUpdate(build, manifest), { status: 'newer', action: 'installVersion', confirm: 'confirmOlderFlash' });
  manifest.knownBuilds[build].version = 'v1.10.0';
  assert.equal(firmwareUpdate(build, manifest).status, 'available');
  manifest.knownBuilds[build].channel = 'development';
  assert.deepEqual(firmwareIdentity(build, manifest), { kind: 'development', value: 'v1.10.0' });
  assert.deepEqual(firmwareUpdate(build, manifest), { status: 'development', action: 'installRelease', confirm: 'confirmReleaseFlash' });
  manifest.knownBuilds[build].channel = 'release';
  assert.equal(firmwareUpdate(build, { ...manifest, version: 'v1.11.0-rc.1' }).status, 'available');
});

test('release versions are identified only by matching ELF build identifiers', () => {
  const manifest = { version: 'v0.4.2', buildId: '1234567890abcdef' };
  assert.equal(matchesRelease('1234567890abcdef', manifest), true);
  for (const build of ['v0.4.2', '0.4.2', 'another-build', 'abcdef1234567890', undefined]) assert.equal(matchesRelease(build, manifest), false);
  assert.equal(matchesRelease('1234567890abcdef', { version: 'v0.4.2' }), false);
  assert.equal(matchesRelease(undefined, null), false);
});

test('firmware identity preserves unmatched builds without claiming a release version', () => {
  const manifest = { version: 'v0.4.2', buildId: '1234567890abcdef' };
  assert.deepEqual(firmwareIdentity('1234567890abcdef', manifest), { kind: 'release', value: 'v0.4.2' });
  assert.deepEqual(firmwareIdentity('abcdef1234567890', manifest), { kind: 'build', value: 'abcdef1234567890' });
  assert.deepEqual(firmwareIdentity('abcdef1234567890', null), { kind: 'build', value: 'abcdef1234567890' });
  assert.deepEqual(firmwareIdentity('demo', manifest), { kind: 'demo', value: '' });
  for (const build of [undefined, '', 'v0.4.2', 'invalid-build']) {
    assert.deepEqual(firmwareIdentity(build, manifest), { kind: 'unknown', value: '' });
  }
});

test('downloaded firmware must also match the declared ELF build identifier', async () => {
  const original = globalThis.fetch;
  const image = new Uint8Array(288); image[0] = 0xe9; image[12] = 5; image.set([0x32, 0x54, 0xcd, 0xab], 32);
  image.set([0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef], 176);
  const manifest = { buildId: '1234567890abcdef', application: { file: 'app.bin', size: image.length, sha256: await sha256(image) } };
  globalThis.fetch = async () => new Response(image);
  try {
    assert.deepEqual(await getImage(manifest, 'application'), image);
    await assert.rejects(getImage({ ...manifest, buildId: 'abcdef1234567890' }, 'application'), /integrity/);
  } finally { globalThis.fetch = original; }
});

test('downloaded firmware must match its hash before flashing', async () => {
  const original = globalThis.fetch;
  globalThis.fetch = async () => new Response(new Uint8Array(288));
  try { await assert.rejects(getImage({ application: { file: 'app.bin', size: 288, sha256: '0'.repeat(64) } }, 'application'), /integrity/); }
  finally { globalThis.fetch = original; }
});

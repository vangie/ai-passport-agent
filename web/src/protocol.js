export const SERVICE = 'd1b48e2a-7051-339c-7e4a-104d2e8a1c6b';
export const RX = 'd2b48e2a-7051-339c-7e4a-104d2e8a1c6b';
export const TX = 'd3b48e2a-7051-339c-7e4a-104d2e8a1c6b';
export const bytes = value => new TextEncoder().encode(value).length;
export const encode = value => encodeURIComponent(value).replace(/[!'()*~]/g, character => `%${character.charCodeAt(0).toString(16).toUpperCase()}`);
export const decode = value => decodeURIComponent(value);

export function pairing(payload, address = '') {
  let raw = payload.trim();
  let outerURL = '';
  if (raw.startsWith('{')) {
    const outer = JSON.parse(raw);
    outerURL = outer.url || outer.gatewayUrl || '';
    raw = outer.code || outer.setupCode || '';
  }
  raw = raw.replace(/\s/g, '').replace(/^oc-pair:\/\//, '');
  if (!raw || bytes(raw) >= 1400 || !/^[\w+/=-]+$/.test(raw)) throw new Error('pairing');
  const base64 = raw.replace(/-/g, '+').replace(/_/g, '/');
  const binary = atob(base64 + '='.repeat((4 - base64.length % 4) % 4));
  const inner = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(Uint8Array.from(binary, character => character.charCodeAt(0))));
  const url = new URL(inner.url);
  if (!['ws:', 'wss:'].includes(url.protocol) || !url.hostname || url.username || url.password || url.hash || /\s/.test(inner.url) || bytes(inner.url) >= 160 || typeof inner.bootstrapToken !== 'string' || !inner.bootstrapToken || bytes(inner.bootstrapToken) >= 128) throw new Error('pairing');
  for (const expected of [outerURL, address.trim()]) {
    if (expected && new URL(expected).href !== url.href) throw new Error('addressMismatch');
  }
  return { url: inner.url, code: raw };
}

export function validateWiFi(ssid, password, open, saved = []) {
  if (!ssid || bytes(ssid) > 32 || /[\r\n\0]/.test(ssid)) throw new Error('ssidInvalid');
  if (!password && (open || saved.some(network => network.ssid === ssid))) return;
  if (!((bytes(password) >= 8 && bytes(password) <= 63) || /^[a-f\d]{64}$/i.test(password)) || /[\r\n\0]/.test(password)) throw new Error('passwordInvalid');
}

export class LineBuffer {
  constructor() { this.buffer = []; }
  append(fragment) {
    const lines = [];
    for (const byte of fragment) {
      if (byte === 10) {
        lines.push(new TextDecoder('utf-8', { fatal: true }).decode(Uint8Array.from(this.buffer)).replace(/\r$/, ''));
        this.buffer = [];
      } else {
        if (this.buffer.length >= 8192) { this.buffer = []; throw new Error('responseInvalid'); }
        this.buffer.push(byte);
      }
    }
    return lines;
  }
}

export function hello(line) {
  const parts = line.split(' ');
  if (parts.length < 7 || parts[0] !== 'HELLO' || parts[1] !== 'ai-passport-openclaw' || !/^\d+$/.test(parts[2]) || !['zh', 'en'].includes(parts[5])) throw new Error('firmwareUnsupported');
  return { generation: Number(parts[2]), language: parts[5], firmware: parts[6], capabilities: parts.slice(7) };
}

export function networks(lines, generation) {
  const head = lines[0]?.split(' ');
  if (head?.[0] !== 'NETS' || Number(head[1]) !== generation || !/^\d+$/.test(head[2]) || Number(head[2]) > 8 || lines.at(-1) !== 'NETS END' || lines.length !== Number(head[2]) + 2) throw new Error('responseInvalid');
  return lines.slice(1, -1).map((line, index) => {
    const parts = line.split(' ');
    if (parts.length !== 4 || parts[0] !== 'SAVED' || Number(parts[1]) !== index) throw new Error('responseInvalid');
    return { id: index, name: decode(parts[2]), ssid: decode(parts[3]), current: index === Number(head[3]) };
  });
}

export function agents(lines, generation) {
  const head = lines[0]?.split(' ');
  if (head?.[0] !== 'AGENTS' || Number(head[1]) !== generation || !/^\d+$/.test(head[2]) || Number(head[2]) > 4 || lines.at(-1) !== 'AGENTS END') throw new Error('responseInvalid');
  const result = [];
  for (const line of lines.slice(1, -1)) {
    const parts = line.split(' ');
    if (parts[0] === 'AGENT') {
      if (parts.length !== 5 || Number(parts[1]) !== result.length) throw new Error('responseInvalid');
      result.push({ id: Number(parts[1]), name: decode(parts[2]), url: decode(parts[3]), state: parts[4], current: Number(parts[1]) === Number(head[3]) });
    } else if (parts[0] === 'ROUTE') {
      const agent = result[Number(parts[1])];
      if (!agent || parts.length !== 7) throw new Error('responseInvalid');
      agent.route = { mode: parts[2], host: parts[3] === '-' ? '' : parts[3], port: Number(parts[4]), savedSecret: parts[5] === '1', method: parts[6] };
    }
  }
  if (result.length !== Number(head[2])) throw new Error('responseInvalid');
  return result;
}

export function nearby(line, list) {
  const match = /^NET (-?\d+) (.+)$/.exec(line);
  if (match) {
    let entry = list.find(network => network.ssid === match[2]);
    if (!entry) { entry = { ssid: match[2], rssi: -127, hotspot: false }; list.push(entry); }
    entry.rssi = Math.max(entry.rssi, Number(match[1]));
  }
  if (line.startsWith('NET_KIND hotspot ')) {
    const entry = list.find(network => network.ssid === decode(line.slice(17)));
    if (entry) entry.hotspot = true;
  }
}

export class Session {
  constructor(transport, onDisconnect = () => {}) {
    this.transport = transport;
    this.buffer = new LineBuffer();
    this.pending = null;
    this.closed = false;
    transport.onData = fragment => {
      try { for (const line of this.buffer.append(fragment)) this.receive(line); }
      catch (error) { this.abort(error); }
    };
    transport.onDisconnect = () => { this.closed = true; this.abort(new Error('disconnected')); onDisconnect(); };
  }
  abort(error) {
    if (!this.pending) return;
    clearTimeout(this.pending.timer);
    this.pending.reject(error);
    this.pending = null;
  }
  receive(line) {
    const request = this.pending;
    if (!request) return;
    if (line.startsWith('ERR ') || line.startsWith('TRY fail')) { this.abort(new Error(line)); return; }
    if (!request.prefixes.some(prefix => line.startsWith(prefix))) return;
    request.lines.push(line);
    request.onRow(line);
    if (request.matches(line)) {
      clearTimeout(request.timer);
      this.pending = null;
      request.resolve(request.lines);
    }
  }
  request(command, ending, timeout = 8000, onRow = () => {}) {
    if (this.closed) return Promise.reject(new Error('disconnected'));
    if (this.pending) return Promise.reject(new Error('busy'));
    return new Promise((resolve, reject) => {
      const matches = typeof ending === 'function' ? ending : line => line === ending;
      const prefixes = { HELLO: ['HELLO '], NETS: ['NETS ', 'SAVED '], AGENTS: ['AGENTS ', 'AGENT ', 'ROUTE '], DEVICE: ['DEVICE '], SCAN: ['SCAN ', 'NET ', 'NET_KIND '], TRY: ['TRY '] }[command.split(' ')[0]] || ['OK '];
      this.pending = { resolve, reject, matches, onRow, prefixes, lines: [], timer: setTimeout(() => {
        this.closed = true;
        this.abort(new Error('timeout'));
        void this.transport.close();
      }, timeout) };
      Promise.resolve().then(() => this.transport.send(command)).catch(error => this.abort(error));
    });
  }
  async snapshot() {
    const state = hello((await this.request('HELLO', line => line.startsWith('HELLO '), 30000))[0]);
    state.networks = networks(await this.request('NETS', 'NETS END'), state.generation);
    state.agents = agents(await this.request('AGENTS', 'AGENTS END'), state.generation);
    state.device = {};
    if (state.capabilities.includes('device-control')) {
      const line = (await this.request('DEVICE', line => line.startsWith('DEVICE ')))[0];
      for (const item of line.split(' ').slice(1)) { const [key, value] = item.split('='); state.device[key] = Number(value); }
    }
    return state;
  }
  async mutate(command, ending) {
    await this.request(command, ending, 60000);
    return this.snapshot();
  }
  async close() { this.closed = true; this.abort(new Error('disconnected')); await this.transport.close(); }
}

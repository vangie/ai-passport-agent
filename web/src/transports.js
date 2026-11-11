import { SERVICE, RX, TX, encode, decode } from './protocol.js';

export class SerialTransport {
  async connect() {
    this.port = await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x303a }] });
    try {
      await this.port.open({ baudRate: 115200 });
      this.writer = this.port.writable.getWriter();
      this.reader = this.port.readable.getReader();
      this.reading = this.readLoop();
    } catch (error) { await this.close(); throw error; }
  }
  async readLoop() {
    try {
      while (true) {
        const { value, done } = await this.reader.read();
        if (done) break;
        this.onData?.(value);
      }
    } catch { }
    finally { this.onDisconnect?.(); }
  }
  async send(command) { await this.writer.write(new TextEncoder().encode(command + '\n')); }
  async close() {
    await this.reader?.cancel().catch(() => {});
    await this.reading;
    this.reader?.releaseLock();
    this.writer?.releaseLock();
    await this.port?.close().catch(() => {});
    this.reader = null;
    this.writer = null;
  }
}

export class BluetoothTransport {
  async connect() {
    this.device = await navigator.bluetooth.requestDevice({ filters: [{ services: [SERVICE] }] });
    this.disconnected = () => this.onDisconnect?.();
    this.device.addEventListener('gattserverdisconnected', this.disconnected);
    try {
      const server = await this.device.gatt.connect();
      const service = await server.getPrimaryService(SERVICE);
      this.rx = await service.getCharacteristic(RX);
      this.tx = await service.getCharacteristic(TX);
      this.changed = event => this.onData?.(new Uint8Array(event.target.value.buffer, event.target.value.byteOffset, event.target.value.byteLength));
      this.tx.addEventListener('characteristicvaluechanged', this.changed);
      await this.tx.startNotifications();
    } catch (error) { await this.close(); throw error; }
  }
  async send(command) {
    const data = new TextEncoder().encode(command + '\n');
    for (let offset = 0; offset < data.length; offset += 20) {
      const chunk = data.slice(offset, offset + 20);
      if (this.rx.writeValueWithResponse) await this.rx.writeValueWithResponse(chunk);
      else await this.rx.writeValue(chunk);
    }
  }
  async close() {
    this.tx?.removeEventListener('characteristicvaluechanged', this.changed);
    this.device?.removeEventListener('gattserverdisconnected', this.disconnected);
    this.device?.gatt.disconnect();
    this.onDisconnect?.();
  }
}

export class DemoTransport {
  constructor() {
    this.generation = 1;
    this.language = 'zh';
    this.volume = 65;
    this.networks = ['Home Wi-Fi', 'My iPhone'];
    this.agents = [{ name: 'OpenClaw', url: 'wss://gateway.example', current: true }];
  }
  async connect() { }
  async send(command) {
    const parts = command.split(' ');
    const emit = line => this.onData?.(new TextEncoder().encode(line + '\n'));
    await new Promise(resolve => setTimeout(resolve, 70));
    if (parts[0] === 'HELLO') emit(`HELLO ai-passport-openclaw ${this.generation} ${this.networks.length} ${this.agents.length} ${this.language} demo device-control scan-live gateway-route`);
    else if (command === 'NETS') {
      emit(`NETS ${this.generation} ${this.networks.length} 0`);
      this.networks.forEach((ssid, index) => emit(`SAVED ${index} ${encode(ssid)} ${encode(ssid)}`));
      emit('NETS END');
    } else if (command === 'AGENTS') {
      emit(`AGENTS ${this.generation} ${this.agents.length} ${this.agents.findIndex(agent => agent.current)}`);
      this.agents.forEach((agent, index) => { emit(`AGENT ${index} ${encode(agent.name)} ${encode(agent.url)} ready`); emit(`ROUTE ${index} direct - 0 0 aes-256-cfb`); });
      emit('AGENTS END');
    } else if (command === 'DEVICE') emit(`DEVICE volume=${this.volume} battery=86 wifi=1 busy=0 sleeping=0 usb_host=1 idle_s=60`);
    else if (parts[0] === 'SCAN') {
      emit('SCAN LIVE');
      for (const line of ['NET -38 Home Wi-Fi', 'NET -52 My iPhone', 'NET_KIND hotspot My%20iPhone', 'NET -72 Studio']) {
        await new Promise(resolve => setTimeout(resolve, 220)); emit(line);
      }
      emit('SCAN END');
    } else if (parts[0] === 'TRY') emit('TRY ok');
    else {
      const ending = { SAVE: 'saved', LANG: 'lang', VOLUME: 'volume', AGENT: 'agent', USE: 'use', DROP: 'drop', FORGET: 'forget', ROUTE: 'route' }[parts[0]];
      if (!ending) { emit('ERR command'); return; }
      if (parts[0] === 'SAVE') { const ssid = decode(parts[2]); if (!this.networks.includes(ssid)) this.networks.push(ssid); }
      if (parts[0] === 'FORGET') this.networks.splice(Number(parts[2]), 1);
      if (parts[0] === 'DROP') this.agents.splice(Number(parts[2]), 1);
      if (parts[0] === 'USE') this.agents.forEach((agent, index) => { agent.current = index === Number(parts[2]); });
      if (parts[0] === 'LANG') this.language = parts[2];
      if (parts[0] === 'VOLUME') this.volume = Number(parts[2]);
      if (parts[0] === 'AGENT') { this.agents.forEach(agent => { agent.current = false; }); this.agents.push({ name: decode(parts[2]), url: 'wss://gateway.example', current: true }); }
      if (parts[0] !== 'VOLUME') this.generation++;
      emit(`OK ${ending}`);
    }
  }
  async close() { this.onDisconnect?.(); }
}

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { BluetoothTransport } from '../src/transports.js';

for (const [name, message] of [
  ['NotAllowedError', 'bluetoothPermission'],
  ['SecurityError', 'bluetoothPermission'],
  ['NotFoundError', 'bluetoothNotSelected'],
]) {
  test(`Bluetooth chooser ${name} gives actionable feedback`, async () => {
    const originalNavigator = Object.getOwnPropertyDescriptor(globalThis, 'navigator');
    const cause = new DOMException('Device selection failed', name);
    Object.defineProperty(globalThis, 'navigator', {
      configurable: true,
      value: { bluetooth: { async requestDevice() { throw cause; } } },
    });
    try {
      await assert.rejects(new BluetoothTransport().connect(), error => error.message === message && error.cause === cause);
    } finally {
      if (originalNavigator) Object.defineProperty(globalThis, 'navigator', originalNavigator);
      else delete globalThis.navigator;
    }
  });
}

for (const broadDiscovery of [false, true]) test(`Bluetooth missing service disconnects cleanly (broadDiscovery=${broadDiscovery})`, async () => {
  const originalNavigator = Object.getOwnPropertyDescriptor(globalThis, 'navigator');
  let disconnected = false;
  const cause = new DOMException('Service not found', 'NotFoundError');
  const events = new Map();
  const device = {
    addEventListener(name, callback) { events.set(name, callback); },
    removeEventListener(name) { events.delete(name); },
    gatt: {
      async connect() { return { async getPrimaryService() { throw cause; } }; },
      disconnect() { disconnected = true; },
    },
  };
  Object.defineProperty(globalThis, 'navigator', {
    configurable: true,
    value: { bluetooth: { async requestDevice() { return device; } } },
  });
  try {
    await assert.rejects(new BluetoothTransport({ broadDiscovery }).connect(), error => error.message === 'bluetoothServiceMissing' && error.cause === cause);
    assert.equal(disconnected, true);
    assert.equal(events.size, 0);
  } finally {
    if (originalNavigator) Object.defineProperty(globalThis, 'navigator', originalNavigator);
    else delete globalThis.navigator;
  }
});
import { SERVICE, RX, TX } from '../src/protocol.js';

for (const broadDiscovery of [false, true]) test(`Bluetooth discovery authorizes and checks the configuration service (broadDiscovery=${broadDiscovery})`, async () => {
  const originalNavigator = Object.getOwnPropertyDescriptor(globalThis, 'navigator');
  let requested;
  const events = new Map();
  const characteristic = {
    addEventListener() {}, removeEventListener() {}, async startNotifications() {},
  };
  const service = {
    async getCharacteristic(uuid) {
      assert.ok([RX, TX].includes(uuid));
      return characteristic;
    },
  };
  const device = {
    addEventListener(name, callback) { events.set(name, callback); },
    removeEventListener(name) { events.delete(name); },
    gatt: {
      async connect() {
        return {
          async getPrimaryService(uuid) { assert.equal(uuid, SERVICE); return service; },
        };
      },
      disconnect() {},
    },
  };
  Object.defineProperty(globalThis, 'navigator', {
    configurable: true,
    value: { bluetooth: { async requestDevice(options) { requested = options; return device; } } },
  });
  const transport = new BluetoothTransport({ broadDiscovery });
  try {
    await transport.connect();
    assert.deepEqual(requested, broadDiscovery ? {
      acceptAllDevices: true,
      optionalServices: [SERVICE],
    } : {
      filters: [{ services: [SERVICE] }, { namePrefix: 'AI Passport ' }],
      optionalServices: [SERVICE],
    });
    assert.equal(transport.rx, characteristic);
    assert.equal(transport.tx, characteristic);
    assert.ok(events.has('gattserverdisconnected'));
    await transport.close();
    assert.equal(events.size, 0);
  } finally {
    if (originalNavigator) Object.defineProperty(globalThis, 'navigator', originalNavigator);
    else delete globalThis.navigator;
  }
});

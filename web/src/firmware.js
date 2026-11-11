import { md5 } from '@noble/hashes/legacy.js';

export async function sha256(data) {
  return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', data)), value => value.toString(16).padStart(2, '0')).join('');
}

export function validateApplication(data) {
  if (data.length < 288 || data[0] !== 0xe9 || data[12] !== 5 || data[13] !== 0 || data[32] !== 0x32 || data[33] !== 0x54 || data[34] !== 0xcd || data[35] !== 0xab) throw new Error('wrongChip');
  if (data.length > 0x340000) throw new Error('flashTooLarge');
}

export function validatePartitions(data) {
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  let application = false;
  let card = false;
  for (let offset = 0; offset + 32 <= data.length; offset += 32) {
    if (view.getUint16(offset, true) !== 0x50aa) break;
    const start = view.getUint32(offset + 4, true);
    const size = view.getUint32(offset + 8, true);
    if (data[offset + 2] === 0 && data[offset + 3] === 0 && start === 0x10000 && size === 0x340000) application = true;
    if (start === 0x350000 && size === 0x10000) card = true;
  }
  if (!application || !card) throw new Error('firmwareUnsupported');
}

export function validateManifest(manifest) {
  if (manifest.schema !== 1 || !/^v\d+\.\d+\.\d+(?:[-.\w]*)?$/.test(manifest.version) || !/^[a-f\d]{40}$/.test(manifest.commit)) throw new Error('integrity');
  for (const kind of ['application', 'full']) {
    const image = manifest[kind];
    if (!image || !/^[\w.-]+\.bin$/.test(image.file) || !/^[a-f\d]{64}$/.test(image.sha256) || !Number.isSafeInteger(image.size) || image.size < 288 || image.size > 0x350000 || image.offset !== (kind === 'full' ? 0 : 0x10000)) throw new Error('integrity');
  }
  return manifest;
}

export async function getManifest() {
  const response = await fetch('./firmware/manifest.json', { cache: 'no-cache', redirect: 'error' });
  if (!response.ok) throw new Error('firmwareMissing');
  return validateManifest(await response.json());
}

export async function getImage(manifest, kind) {
  const entry = manifest[kind];
  const response = await fetch(`./firmware/${entry.file}`, { redirect: 'error' });
  if (!response.ok) throw new Error('firmwareMissing');
  const data = new Uint8Array(await response.arrayBuffer());
  if (data.length !== entry.size || await sha256(data) !== entry.sha256) throw new Error('integrity');
  if (kind === 'application') validateApplication(data);
  else {
    validatePartitions(data.subarray(0x8000, 0x9000));
    validateApplication(data.subarray(0x10000));
  }
  return data;
}

export async function flashFirmware(port, data, kind, reportProgress) {
  const { ESPLoader, Transport } = await import('esptool-js');
  const transport = new Transport(port, true);
  try {
    const loader = new ESPLoader({ transport, baudrate: 460800, terminal: { clean() {}, writeLine() {}, write() {} }, debugLogging: false });
    await loader.main();
    if (loader.chip.CHIP_NAME !== 'ESP32-C3') throw new Error('wrongChip');
    if (kind === 'application') {
      validateApplication(data);
      const partitionBytes = await loader.readFlash(0x8000, 0x1000);
      validatePartitions(typeof partitionBytes === 'string' ? Uint8Array.from(partitionBytes, character => character.charCodeAt(0)) : new Uint8Array(partitionBytes));
    }
    await loader.writeFlash({
      fileArray: [{ data, address: kind === 'full' ? 0 : 0x10000 }],
      flashMode: 'dio', flashFreq: '80m', flashSize: '8MB', eraseAll: false, compress: true,
      calculateMD5Hash: image => Array.from(md5(image), value => value.toString(16).padStart(2, '0')).join(''),
      reportProgress: (_index, written, total) => reportProgress(Math.round(written / total * 100)),
    });
    await loader.after('hard_reset');
  } finally { await transport.disconnect().catch(() => {}); }
}


import './style.css';
import { language, text, setLanguage } from './i18n.js';
import { Session, encode, bytes, pairing, validateWiFi, nearby } from './protocol.js';
import { SerialTransport, BluetoothTransport, DemoTransport } from './transports.js';
import { getManifest, getImage, validateApplication, flashFirmware, sha256 } from './firmware.js';

const icons = {
  wifi: '<path d="M2 8a16 16 0 0 1 20 0M5 12a11 11 0 0 1 14 0M8.5 16a5 5 0 0 1 7 0"/><circle cx="12" cy="20" r=".7"/>',
  usb: '<path d="M12 21V3m-3 3 3-3 3 3M12 15l-6-4V8m6 4 6-4V5"/><circle cx="6" cy="7" r="1"/><path d="M17 3h2v2h-2z"/>',
  bluetooth: '<path d="m7 7 10 10-5 4V3l5 4L7 17"/>',
  agent: '<rect x="3" y="5" width="18" height="14" rx="4"/><path d="M12 5V2M7 19v3m10-3v3M8 10v3m8-3v3M10 16h4"/>',
  settings: '<path d="M4 7h16M4 17h16"/><circle cx="9" cy="7" r="3" fill="white"/><circle cx="15" cy="17" r="3" fill="white"/>',
  shield: '<path d="m12 2 8 4v6c0 5-8 10-8 10S4 17 4 12V6zM8 12l3 3 5-6"/>',
  download: '<path d="M12 3v12m-5-5 5 5 5-5M4 16v5h16v-5"/>',
  hotspot: '<path d="M5 5a10 10 0 0 0 0 14M19 5a10 10 0 0 1 0 14M8 8a6 6 0 0 0 0 8M16 8a6 6 0 0 1 0 8"/><circle cx="12" cy="12" r="1.5"/>',
};
const icon = name => `<svg class="icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${icons[name] || icons.agent}</svg>`;
const escape = value => String(value ?? '').replace(/[&<>"']/g, character => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[character]);
const root = document.querySelector('#app');
let session = null;
let transportKind = '';
let snapshot = null;
let busy = false;
let scanning = false;
let notice = null;
let manifest = null;
let firmwareReady = false;
let selectedFirmware = null;
let progress = null;
let nearbyNetworks = [];
const dirty = new Set();
let draft = blankDraft();

function blankDraft() { return { ssid: '', password: '', open: false, name: '', address: '', code: '', selectedAgent: '', routeMode: 'direct', proxyHost: '', proxyPort: '', proxySecret: '', proxyMethod: 'aes-256-cfb', language: 'zh', volume: 65, flashKind: 'application' }; }
function connected() { return session && !session.closed; }
function supportedSerial() { return window.isSecureContext && 'serial' in navigator; }
function supportedBLE() { return window.isSecureContext && 'bluetooth' in navigator; }
function say(key, error = false) { notice = { key, error }; }
function report(error) {
  const message = error.message;
  const key = error.name === 'NotFoundError' || error.name === 'AbortError' ? 'cancelled'
    : message === 'ERR stale' ? 'stale' : message === 'ERR busy' ? 'deviceBusy'
      : message === 'ERR join pass' || message === 'TRY fail pass' ? 'joinPass'
        : message === 'ERR join notfound' || message === 'TRY fail notfound' ? 'joinNotFound'
          : message === 'ERR join timeout' || message === 'TRY fail timeout' ? 'joinTimeout'
            : text(message) !== message ? message : 'deviceError';
  say(key, true);
}
function routeDraft(agent) {
  const route = agent?.route;
  Object.assign(draft, { selectedAgent: agent ? String(agent.id) : '', routeMode: route?.mode || 'direct', proxyHost: route?.host || '', proxyPort: route?.port || '', proxySecret: '', proxyMethod: route?.method || 'aes-256-cfb' });
}
function adopt(value, replace = false) {
  snapshot = value;
  if (replace) {
    draft = blankDraft();
    draft.language = value.language;
    draft.volume = value.device.volume ?? 65;
    routeDraft(value.agents.find(agent => agent.current) || value.agents[0]);
    dirty.clear();
  } else {
    if (!dirty.has('language')) draft.language = value.language;
    if (!dirty.has('volume')) draft.volume = value.device.volume ?? 65;
    if (!dirty.has('route')) routeDraft(value.agents.find(agent => String(agent.id) === draft.selectedAgent) || value.agents.find(agent => agent.current) || value.agents[0]);
  }
}
function field(key, label, type = 'text', placeholder = '') {
  return `<label class="field">${text(label)}<input id="${key}" data-field="${key}" type="${type}" value="${escape(draft[key])}" placeholder="${escape(placeholder)}" autocomplete="off" spellcheck="false"></label>`;
}
function sectionHeader(name, title, detail, action = '') { return `<div class="card-head"><div><div class="card-title">${icon(name)}<h2>${text(title)}</h2></div>${detail ? `<p class="subtle">${text(detail)}</p>` : ''}</div>${action}</div>`; }

function render() {
  const linked = connected();
  const demo = linked && transportKind === 'demo';
  const currentAgent = snapshot?.agents.find(agent => agent.current);
  const selectedAgent = snapshot?.agents.find(agent => String(agent.id) === draft.selectedAgent);
  const connectionLabel = linked ? demo ? text('preview') : `AI Passport · ${transportKind === 'usb' ? 'USB' : 'Bluetooth'}` : text('offline');
  root.innerHTML = `<div class="shell">
    <header><a class="brand" href="./"><img src="./lobster.svg" alt="">AI Passport<span>OpenClaw</span></a><nav><a href="https://github.com/vangie/ai-passport-openclaw" target="_blank" rel="noreferrer">${text('github')} ↗</a><button class="quiet small" id="language">${text('language')}</button></nav></header>
    <section class="hero"><div><div class="eyebrow">${text('eyebrow')}</div><h1>${text('headline')}</h1><p class="intro">${text('intro')}</p></div><div class="passport-art" aria-hidden="true"><div class="screen">OpenClaw · ◒<img src="./lobster.svg" alt=""><div class="screen-lines"></div><div class="screen-lines"></div></div></div></section>
    <section class="connection"><div><div class="connection-title"><span class="dot ${linked ? demo ? 'demo' : 'on' : ''}"></span><h2>${linked ? escape(connectionLabel) : text('connectTitle')}</h2></div><p class="subtle">${linked ? `${text('firmware')}: ${escape(snapshot?.firmware || '—')} · ${text('battery')}: ${snapshot?.device.battery >= 0 ? snapshot.device.battery + '%' : '—'}${currentAgent ? ` · ${escape(currentAgent.name)}` : ''}` : text('connectDetail')}</p></div><div class="actions connection-actions">${linked ? `<button id="refresh" ${busy ? 'disabled' : ''}>${text('refresh')}</button><button id="disconnect" ${busy ? 'disabled' : ''}>${text('disconnect')}</button>` : `<button id="connect-usb" class="primary" ${busy || !supportedSerial() ? 'disabled' : ''}>${icon('usb')}${busy ? text('connecting') : text('usb')}</button><button id="connect-ble" ${busy || !supportedBLE() ? 'disabled' : ''}>${icon('bluetooth')}${text('bluetooth')}</button><button id="demo" class="quiet" ${busy ? 'disabled' : ''}>${text('demo')}</button>`}</div></section>
    <div class="environment"><p class="${window.isSecureContext ? '' : 'warning'}">${text(window.isSecureContext ? 'browser' : 'insecure')}</p></div>
    <div id="notice" role="status" aria-live="polite" class="notice ${notice?.error ? 'error' : ''}" ${notice ? '' : 'hidden'}>${notice ? text(notice.key) : ''}</div>
    <main class="grid"><div class="column"><section class="card">${sectionHeader('wifi', 'wifiTitle', 'wifiDetail', `<button class="small" id="scan" ${!linked || busy ? 'disabled' : ''}>${text(scanning ? 'scanning' : 'scan')}</button>`)}<div class="card-content">
      <span class="section-label">${text('saved')}</span><div class="list">${snapshot?.networks.length ? snapshot.networks.map(network => `<div class="list-row ${network.ssid === draft.ssid ? 'selected' : ''}">${icon('wifi')}<button class="choose" data-network="${escape(network.ssid)}">${escape(network.ssid)}</button>${network.current ? `<span class="tag">${text('current')}</span>` : ''}<button class="quiet small" data-forget="${network.id}" ${busy || !linked ? 'disabled' : ''}>${text('forget')}</button></div>`).join('') : `<p class="empty">${text('noNetworks')}</p>`}</div>
      <span class="section-label">${text('nearby')}</span><div id="nearby-list" class="list"></div>
      ${field('ssid', 'ssid', 'text', 'Home Wi-Fi')}${field('password', 'password', 'password', text('passwordHint'))}<label class="check"><input type="checkbox" data-field="open" ${draft.open ? 'checked' : ''}>${text('open')}</label>
      <button id="test-wifi" class="small" ${!linked || busy ? 'disabled' : ''}>${text('test')}</button><p class="hint">${text('hotspotHint')}</p>
    </div></section>
    <section class="card">${sectionHeader('settings', 'settingsTitle')}<div class="card-content"><div class="preferences"><label for="device-language">${text('screenLanguage')}</label><select id="device-language" data-field="language"><option value="zh" ${draft.language === 'zh' ? 'selected' : ''}>${text('chinese')}</option><option value="en" ${draft.language === 'en' ? 'selected' : ''}>${text('english')}</option></select></div><div class="preferences"><label for="volume">${text('volume')}</label><div class="volume"><input id="volume" data-field="volume" type="range" min="0" max="100" value="${draft.volume}" ${snapshot && !snapshot.capabilities.includes('device-control') ? 'disabled' : ''}><output id="volume-value">${draft.volume}%</output></div></div></div></section>
    </div><div class="column"><section class="card">${sectionHeader('agent', 'agentTitle', 'agentDetail')}<div class="card-content"><div class="list">${snapshot?.agents.length ? snapshot.agents.map(agent => `<div class="list-row agent-row ${String(agent.id) === draft.selectedAgent ? 'selected' : ''}"><div class="agent-main"><button class="choose" data-agent="${agent.id}">${escape(agent.name)} ${agent.current ? `<span class="tag">${text('current')}</span>` : ''}</button><div class="agent-sub">${escape(agent.url)}</div></div><div>${!agent.current ? `<button class="quiet small" data-use="${agent.id}" ${busy || !linked ? 'disabled' : ''}>${text('use')}</button>` : ''}<button class="quiet small" data-drop="${agent.id}" ${busy || !linked ? 'disabled' : ''}>${text('remove')}</button></div></div>`).join('') : `<p class="empty">${text('noAgents')}</p>`}</div>
      <details id="pairing-details" ${!snapshot?.agents.length || draft.code ? 'open' : ''}><summary>${text('newAgent')}</summary><div class="actions"><button id="copy-prompt" class="quiet small">${text('prompt')} ↗</button></div>${field('name', 'agentName', 'text', text('namePlaceholder'))}${field('address', 'address', 'url', 'wss://gateway.example')}<label class="field">${text('code')}<textarea id="code" data-field="code" autocomplete="off" spellcheck="false">${escape(draft.code)}</textarea></label><p class="hint">${text('codeHint')}</p></details>
      <details id="route-details" ${draft.routeMode === 'ss' ? 'open' : ''}><summary>${text('route')}${selectedAgent ? ' · ' + escape(selectedAgent.name) : ''}</summary><label class="field"><select id="routeMode" data-field="routeMode"><option value="direct" ${draft.routeMode === 'direct' ? 'selected' : ''}>${text('direct')}</option><option value="ss" ${draft.routeMode === 'ss' ? 'selected' : ''}>Shadowsocks</option></select></label><div id="proxy-fields" ${draft.routeMode === 'ss' ? '' : 'hidden'}><div class="inline">${field('proxyHost', 'proxyHost', 'text', 'proxy.example')}${field('proxyPort', 'proxyPort', 'number', '443')}</div>${field('proxySecret', 'proxySecret', 'password', selectedAgent?.route?.savedSecret ? text('passwordHint') : '')}<label class="field">${text('proxyMethod')}<select data-field="proxyMethod" id="proxyMethod">${['aes-256-cfb'].map(method => `<option ${draft.proxyMethod === method ? 'selected' : ''}>${method}</option>`).join('')}</select></label></div><p class="hint">${text('proxyHint')}</p></details>
    </div></section>
    <section class="card">${sectionHeader('download', 'firmwareTitle', 'firmwareDetail')}<div class="card-content"><div class="version-row"><span>${text('firmwareCurrent')}</span><strong>${escape(snapshot?.firmware || '—')}</strong></div><div class="version-row"><span>${text('firmwareAvailable')}</span><strong>${escape(manifest?.version || '—')}</strong></div>${!manifest ? `<p class="hint">${text(firmwareReady ? 'firmwareMissing' : 'firmwareLoading')}</p>` : ''}<div class="firmware-actions"><select id="flash-kind" ${busy || selectedFirmware ? 'disabled' : ''}><option value="application" ${draft.flashKind === 'application' ? 'selected' : ''}>${text('application')}</option><option value="full" ${draft.flashKind === 'full' ? 'selected' : ''}>${text('full')}</option></select><button id="flash" ${busy || !supportedSerial() || demo || (linked && transportKind !== 'usb') || (!manifest && !selectedFirmware) ? 'disabled' : ''}>${icon('download')}${text('flash')}${progress !== null ? ` ${progress}%` : ''}</button><label class="file-label">${text('localFirmware')}<input id="firmware-file" type="file" accept=".bin" ${busy ? 'disabled' : ''}></label>${selectedFirmware ? `<p class="file-name">${escape(selectedFirmware.name)} · SHA-256 ${selectedFirmware.hash}</p>` : ''}</div>${progress !== null ? `<progress max="100" value="${progress}" aria-label="${text('flash')}"></progress>` : ''}<p class="hint">${text('flashNote')}</p><p class="hint">${text('usbOnly')}</p></div></section>
    <div class="privacy">${icon('shield')}<p><strong>${text('secure')}</strong> · ${text('private')}</p></div></div></main>
    <div class="save-bar"><div id="save-state" class="save-state"></div><div class="actions"><button id="discard" class="quiet">${text('discard')}</button><button id="save" class="primary">${text('save')}</button></div></div>
    <footer><span>AI Passport OpenClaw · ${text('title')}</span><span>${text('footer')}</span></footer>
    <dialog id="confirm-dialog"><p></p><div class="actions"><button id="dialog-cancel">${text('dialogCancel')}</button><button id="dialog-confirm" class="primary">${text('dialogConfirm')}</button></div></dialog>
  </div>`;
  renderNearby();
  updateSave();
  bind();
}

function updateSave() {
  const hasChanges = dirty.size > 0;
  document.querySelector('#save-state').className = `save-state ${hasChanges ? 'dirty' : ''}`;
  document.querySelector('#save-state').textContent = busy ? text('busy') : hasChanges ? text('unsaved') : connected() ? text('clean') : text('reconnect');
  document.querySelector('#save').disabled = !connected() || busy || !hasChanges;
  document.querySelector('#discard').disabled = busy || !hasChanges;
  root.querySelectorAll('[data-forget], [data-drop], [data-use]').forEach(button => { button.disabled = busy || !connected() || hasChanges; });
  const output = document.querySelector('#volume-value');
  if (output) output.textContent = `${draft.volume}%`;
}
function renderNearby() {
  const list = document.querySelector('#nearby-list');
  if (!list) return;
  list.innerHTML = nearbyNetworks.length ? [...nearbyNetworks].sort((first, second) => second.rssi - first.rssi).map(network => `<div class="list-row">${icon(network.hotspot ? 'hotspot' : 'wifi')}<button class="choose" data-network="${escape(network.ssid)}">${escape(network.ssid)}</button><span class="meta">${network.hotspot ? text('hotspot') + ' · ' : ''}${network.rssi} dBm</span></div>`).join('') : `<p class="empty">${text(scanning ? 'scanning' : 'noNearby')}</p>`;
  bindNetworks(list);
}
function bindNetworks(container) {
  container.querySelectorAll('[data-network]').forEach(button => button.onclick = () => {
    if (busy) return;
    if (draft.ssid !== button.dataset.network) { draft.password = ''; draft.open = false; }
    draft.ssid = button.dataset.network;
    dirty.add('wifi');
    render();
  });
}
function confirmAction(key) {
  const dialog = document.querySelector('#confirm-dialog');
  dialog.querySelector('p').textContent = text(key);
  return new Promise(resolve => {
    const finish = value => { dialog.close(); resolve(value); };
    document.querySelector('#dialog-cancel').onclick = () => finish(false);
    document.querySelector('#dialog-confirm').onclick = () => finish(true);
    dialog.oncancel = event => { event.preventDefault(); finish(false); };
    dialog.showModal();
  });
}
async function run(action) {
  if (busy) return;
  busy = true; notice = null;
  let promise;
  try { promise = action(); render(); await promise; }
  catch (error) { report(error); }
  finally { busy = false; scanning = false; render(); }
}
async function connect(kind) {
  if (kind !== 'demo' && !window.isSecureContext) throw new Error('insecure');
  if (kind === 'usb' && !supportedSerial()) throw new Error('unsupportedUSB');
  if (kind === 'ble' && !supportedBLE()) throw new Error('unsupportedBLE');
  const transport = kind === 'usb' ? new SerialTransport() : kind === 'ble' ? new BluetoothTransport() : new DemoTransport();
  try {
    await transport.connect();
    transportKind = kind;
    const linkedSession = new Session(transport, () => { if (session === linkedSession) { say('disconnected', true); if (!busy) render(); } });
    session = linkedSession;
    adopt(await session.snapshot(), !dirty.size);
    if (kind === 'demo') say('preview');
  } catch (error) { await transport.close(); throw error; }
}
function validateDraft() {
  if (dirty.has('wifi')) validateWiFi(draft.ssid, draft.password, draft.open, snapshot.networks);
  if (dirty.has('agent') && (draft.name || draft.address || draft.code)) {
    if (!draft.name || bytes(draft.name) >= 24 || /[\r\n\0]/.test(draft.name)) throw new Error('nameInvalid');
    pairing(draft.code, draft.address);
  }
  if (dirty.has('route') && draft.routeMode === 'ss') {
    const agent = snapshot.agents.find(value => String(value.id) === draft.selectedAgent);
    if (!draft.proxyHost || bytes(draft.proxyHost) >= 64 || /[\s\0]/.test(draft.proxyHost) || !/^\d+$/.test(String(draft.proxyPort)) || Number(draft.proxyPort) < 1 || Number(draft.proxyPort) > 65535 || bytes(draft.proxySecret) >= 64 || (!draft.proxySecret && !agent?.route?.savedSecret)) throw new Error('proxyInvalid');
  }
}
async function save() {
  validateDraft();
  let written = false;
  const mutate = async (command, ending, group) => { adopt(await session.mutate(command, ending)); dirty.delete(group); written = true; };
  try {
    if (dirty.has('wifi')) await mutate(`SAVE ${snapshot.generation} ${encode(draft.ssid)} ${encode(draft.password)}`, 'OK saved', 'wifi');
    if (dirty.has('agent')) {
      const pair = pairing(draft.code, draft.address);
      await mutate(`AGENT ${snapshot.generation} ${encode(draft.name)} ${encode(pair.code)}`, 'OK agent', 'agent');
      const saved = snapshot.agents.find(agent => agent.name === draft.name);
      if (saved) draft.selectedAgent = String(saved.id);
    }
    if (dirty.has('route')) {
      if (draft.selectedAgent === '') throw new Error('pairing');
      const command = draft.routeMode === 'direct' ? `ROUTE ${snapshot.generation} ${draft.selectedAgent} direct` : `ROUTE ${snapshot.generation} ${draft.selectedAgent} ss ${encode(draft.proxyHost)} ${draft.proxyPort} ${draft.proxySecret ? encode(draft.proxySecret) : '-'} ${draft.proxyMethod}`;
      await mutate(command, 'OK route', 'route');
    }
    if (dirty.has('language')) await mutate(`LANG ${snapshot.generation} ${draft.language}`, 'OK lang', 'language');
    if (dirty.has('volume')) await mutate(`VOLUME ${snapshot.device.volume} ${draft.volume}`, 'OK volume', 'volume');
    say('savedOK');
  } catch (error) { if (written) say('partial', true); else report(error); }
}

function bind() {
  const on = (id, callback) => { const element = document.getElementById(id); if (element) element.onclick = callback; };
  on('language', () => { setLanguage(language === 'zh' ? 'en' : 'zh'); render(); });
  on('connect-usb', () => run(() => connect('usb')));
  on('connect-ble', () => run(() => connect('ble')));
  on('demo', () => run(() => connect('demo')));
  on('disconnect', () => run(async () => { await session.close(); session = null; snapshot = null; transportKind = ''; }));
  on('refresh', async () => {
    if (dirty.size && !await confirmAction('confirmRefresh')) return;
    await run(async () => { adopt(await session.snapshot(), true); });
  });
  on('discard', async () => { if (await confirmAction('confirmDiscard')) { if (snapshot) adopt(snapshot, true); else { draft = blankDraft(); dirty.clear(); } render(); } });
  on('save', () => run(save));
  on('scan', () => run(async () => {
    scanning = true; nearbyNetworks = [];
    await session.request(snapshot.capabilities.includes('scan-live') ? 'SCAN LIVE' : 'SCAN', 'SCAN END', 60000, line => { nearby(line, nearbyNetworks); renderNearby(); });
    say('scanDone');
  }));
  on('test-wifi', () => run(async () => {
    validateWiFi(draft.ssid, draft.password, draft.open, snapshot.networks);
    await session.request(`TRY ${encode(draft.ssid)} ${encode(draft.password)}`, 'TRY ok', 60000);
    say('testOK');
  }));
  on('copy-prompt', () => run(async () => {
    const response = await fetch(`./setup-${language}.txt`, { redirect: 'error' });
    if (!response.ok) throw new Error('deviceError');
    await navigator.clipboard.writeText(await response.text()); say('copied');
  }));
  bindNetworks(root);
  for (const [attribute, command, ending, confirmation] of [['forget', 'FORGET', 'OK forget', 'confirmForget'], ['drop', 'DROP', 'OK drop', 'confirmRemove'], ['use', 'USE', 'OK use', null]]) {
    root.querySelectorAll(`[data-${attribute}]`).forEach(button => button.onclick = async () => {
      if (confirmation && !await confirmAction(confirmation)) return;
      await run(async () => { adopt(await session.mutate(`${command} ${snapshot.generation} ${button.dataset[attribute]}`, ending)); });
    });
  }
  root.querySelectorAll('[data-agent]').forEach(button => button.onclick = async () => {
    if (busy || (dirty.has('route') && !await confirmAction('confirmDiscard'))) return;
    routeDraft(snapshot.agents[Number(button.dataset.agent)]); dirty.delete('route'); render();
  });
  root.querySelectorAll('[data-field]').forEach(input => {
    input.disabled ||= busy;
    input.oninput = () => {
      const key = input.dataset.field;
      draft[key] = input.type === 'checkbox' ? input.checked : input.type === 'range' ? Number(input.value) : input.value;
      const group = ['ssid', 'password', 'open'].includes(key) ? 'wifi' : ['name', 'address', 'code'].includes(key) ? 'agent' : key.startsWith('proxy') || key === 'routeMode' ? 'route' : key;
      dirty.add(group);
      if (key === 'routeMode') document.querySelector('#proxy-fields').hidden = draft.routeMode !== 'ss';
      updateSave();
    };
  });
  const flashKind = document.querySelector('#flash-kind');
  flashKind.onchange = () => { draft.flashKind = flashKind.value; };
  document.querySelector('#firmware-file').onchange = event => run(async () => {
    const file = event.target.files[0];
    if (!file) return;
    if (file.size > 0x340000) throw new Error('flashTooLarge');
    const data = new Uint8Array(await file.arrayBuffer());
    validateApplication(data);
    selectedFirmware = { data, name: file.name, hash: await sha256(data) };
    draft.flashKind = 'application';
  });
  on('flash', async () => {
    if (draft.flashKind === 'application' && (!connected() || transportKind !== 'usb')) { say('updateNeedsDevice', true); render(); return; }
    if (!await confirmAction('confirmFlash')) return;
    await run(async () => {
      const port = connected() ? session.transport.port : await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x303a }] });
      const data = selectedFirmware ? selectedFirmware.data : await getImage(manifest, draft.flashKind);
      if (connected()) { await session.close(); session = null; }
      snapshot = null; transportKind = ''; progress = 0; render();
      try {
        await flashFirmware(port, data, draft.flashKind, value => {
          progress = value;
          const bar = document.querySelector('progress'); if (bar) bar.value = value;
          const button = document.querySelector('#flash'); if (button) button.textContent = `${text('flash')} ${value}%`;
        });
        say('flashDone');
      } catch (error) { report(error); if (notice.key === 'deviceError') say('flashFailed', true); }
      finally { progress = null; }
    });
  });
}

window.addEventListener('beforeunload', event => { if (dirty.size || busy) { event.preventDefault(); event.returnValue = ''; } });
setLanguage(language);
render();
getManifest().then(value => { manifest = value; }).catch(() => {}).finally(() => {
  firmwareReady = true;
  if (!busy && !document.activeElement?.matches('input, textarea, select')) render();
});

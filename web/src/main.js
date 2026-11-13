import './style.css';
import brandLogo from '../../assets/logo/logo.png';
import { language, text, setLanguage } from './i18n.js';
import { Session, encode, bytes, pairing, validateWiFi, nearby } from './protocol.js';
import { SerialTransport, BluetoothTransport } from './transports.js';
import { getManifest, getImage, firmwareIdentity, firmwareUpdate, flashFirmware } from './firmware.js';

const favicon = document.createElement('link');
favicon.rel = 'icon';
favicon.type = 'image/png';
favicon.href = brandLogo;
document.head.append(favicon);

const icons = {
  wifi: '<path d="M2 8a16 16 0 0 1 20 0M5 12a11 11 0 0 1 14 0M8.5 16a5 5 0 0 1 7 0"/><circle cx="12" cy="20" r=".7"/>',
  usb: '<path d="M12 21V3m-3 3 3-3 3 3M12 15l-6-4V8m6 4 6-4V5"/><circle cx="6" cy="7" r="1"/><path d="M17 3h2v2h-2z"/>',
  bluetooth: '<path d="m7 7 10 10-5 4V3l5 4L7 17"/>',
  agent: '<rect x="3" y="5" width="18" height="14" rx="4"/><path d="M12 5V2M7 19v3m10-3v3M8 10v3m8-3v3M10 16h4"/>',
  settings: '<path d="M4 7h16M4 17h16"/><circle cx="9" cy="7" r="3" fill="white"/><circle cx="15" cy="17" r="3" fill="white"/>',
  shield: '<path d="m12 2 8 4v6c0 5-8 10-8 10S4 17 4 12V6zM8 12l3 3 5-6"/>',
  download: '<path d="M12 3v12m-5-5 5 5 5-5M4 16v5h16v-5"/>',
  hotspot: '<path d="M5 5a10 10 0 0 0 0 14M19 5a10 10 0 0 1 0 14M8 8a6 6 0 0 0 0 8M16 8a6 6 0 0 1 0 8"/><circle cx="12" cy="12" r="1.5"/>',
  language: '<path d="m5 8 6 6m-7 0 6-6 2-3M2 5h12M7 2h1m14 20-5-10-5 10m2-4h6"/>',
  external: '<path d="M15 3h6v6m0-6L10 14M8 3H5a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-3"/>',
  copy: '<rect x="8" y="8" width="12" height="13" rx="2"/><path d="M16 8V5a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v9a2 2 0 0 0 2 2h3"/>',
  check: '<path d="m5 12 4 4L19 6"/>',
  chevron: '<path d="m6 9 6 6 6-6"/>',
};
const icon = name => `<svg class="icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${icons[name] || icons.agent}</svg>`;
const escape = value => String(value ?? '').replace(/[&<>"']/g, character => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[character]);
const root = document.querySelector('#app');
const desktopLayout = window.matchMedia('(min-width: 801px)');
const bluefyStoreURL = 'https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055';
let session = null;
let transportKind = '';
let snapshot = null;
let busy = false;
let scanning = false;
let wifiScanAttempted = false;
let bluetoothAttempted = false;
let bluetoothHelpOpen = false;
let notice = null;
let actionScope = 'save';
let promptStatus = 'idle';
let promptResetTimer;
let promptBackend = 'openclaw';
let manifest = null;
let firmwareReady = false;
let keepFirmwareSettings = true;
let privacyOpen = false;
let progress = null;
let nearbyNetworks = [];
let wifiDetailsOpen = null;
let hotspotHelpOpen = false;
let routeEditorOpen = false;
const dirty = new Set();
let draft = blankDraft();

function blankDraft() { return { ssid: '', password: '', open: false, name: '', address: '', code: '', selectedAgent: '', routeMode: 'direct', proxyHost: '', proxyPort: '', proxySecret: '', proxyMethod: 'aes-256-cfb', language: 'zh', volume: 65 }; }
function connected() { return session && !session.closed; }
function supportedSerial() { return window.isSecureContext && typeof navigator.serial?.requestPort === 'function'; }
function supportedBLE() { return window.isSecureContext && typeof navigator.bluetooth?.requestDevice === 'function'; }
function say(key, error = false, scope = actionScope) { notice = { key, error, scope }; }
function feedback(scope) {
  const active = notice?.scope === scope;
  const tone = active && notice.error ? 'error' : active && !['cancelled', 'bluetoothNotSelected'].includes(notice.key) ? 'success' : '';
  return `<p id="${scope}-feedback" class="feedback ${tone}" role="status" aria-live="polite" aria-atomic="true" ${active ? '' : 'hidden'}>${active ? text(notice.key) : ''}</p>`;
}
function updatePromptFeedback() {
  const button = document.getElementById('copy-prompt');
  const status = document.getElementById('copy-prompt-status');
  if (!button || !status) return;
  button.innerHTML = `${icon(promptStatus === 'copied' ? 'check' : 'copy')}${text(promptStatus === 'copying' ? 'copying' : promptStatus === 'copied' ? 'copied' : 'prompt')}`;
  button.disabled = busy || promptStatus === 'copying';
  button.classList.toggle('copied', promptStatus === 'copied');
  button.setAttribute('aria-busy', String(promptStatus === 'copying'));
  status.hidden = promptStatus === 'idle' || promptStatus === 'copying';
  status.classList.toggle('error', ['promptUnavailable', 'clipboardFailed'].includes(promptStatus));
  status.textContent = status.hidden ? '' : promptStatus === 'copied' ? text('promptNext').replace('{backend}', promptBackend === 'hermes' ? 'Hermes' : 'OpenClaw') : text(promptStatus);
  root.querySelectorAll('[name="prompt-backend"], [name="interface-language"]').forEach(input => { input.disabled = promptStatus === 'copying'; });
}
function setPromptStatus(status) {
  clearTimeout(promptResetTimer);
  promptStatus = status;
  updatePromptFeedback();
  if (status === 'copied') promptResetTimer = setTimeout(() => setPromptStatus('idle'), 4000);
}
async function copyPrompt() {
  if (busy || promptStatus === 'copying') return;
  const source = promptBackend === 'hermes' ? 'setup-hermes' : 'setup';
  const promptLanguage = language;
  setPromptStatus('copying');
  let prompt;
  try {
    const response = await fetch(`./${source}-${promptLanguage}.txt`, { redirect: 'error' });
    if (!response.ok) throw new Error('promptUnavailable');
    prompt = await response.text();
  } catch {
    setPromptStatus('promptUnavailable');
    return;
  }
  try {
    await navigator.clipboard.writeText(prompt);
    setPromptStatus('copied');
  } catch {
    setPromptStatus('clipboardFailed');
  }
}
function report(error) {
  const message = error.message;
  const key = error.name === 'NotFoundError' || error.name === 'AbortError' ? 'cancelled'
    : message === 'ERR stale' ? 'stale' : message === 'ERR busy' ? 'deviceBusy'
      : message === 'ERR join pass' || message === 'TRY fail pass' ? 'joinPass'
        : message === 'ERR join notfound' || message === 'TRY fail notfound' ? 'joinNotFound'
          : message === 'ERR join timeout' || message === 'TRY fail timeout' ? 'joinTimeout'
            : text(message) !== message ? message : 'deviceError';
  say(key, !['cancelled', 'bluetoothNotSelected'].includes(key));
}
function routeDraft(agent, preserveSecret = false) {
  const route = agent?.route;
  const keepSecret = preserveSecret && agent && String(agent.id) === draft.selectedAgent && route?.savedSecret && route.mode === draft.routeMode && route.host === draft.proxyHost && String(route.port) === String(draft.proxyPort) && route.method === draft.proxyMethod;
  Object.assign(draft, { selectedAgent: agent ? String(agent.id) : '', routeMode: route?.mode || 'direct', proxyHost: route?.host || '', proxyPort: route?.port || '', proxySecret: keepSecret ? draft.proxySecret : '', proxyMethod: route?.method || 'aes-256-cfb' });
}
function adopt(value, replace = false) {
  snapshot = value;
  if (replace) {
    draft = blankDraft();
    wifiDetailsOpen = null;
    routeEditorOpen = false;
    draft.language = value.language;
    draft.volume = value.device.volume ?? 65;
    routeDraft(value.agents.find(agent => agent.current) || value.agents[0]);
    dirty.clear();
  } else {
    if (!dirty.has('language')) draft.language = value.language;
    if (!dirty.has('volume')) draft.volume = value.device.volume ?? 65;
    if (!dirty.has('route')) routeDraft(value.agents.find(agent => String(agent.id) === draft.selectedAgent) || value.agents.find(agent => agent.current) || value.agents[0], true);
  }
}
function field(key, label, type = 'text', placeholder = '') {
  return `<label class="field">${text(label)}<input id="${key}" data-field="${key}" type="${type}" value="${escape(draft[key])}" placeholder="${escape(placeholder)}" autocomplete="off" spellcheck="false"></label>`;
}
function sectionHeader(name, title, detail, action = '') { return `<div class="card-head"><div><div class="card-title">${icon(name)}<h2>${text(title)}</h2></div>${detail ? `<p class="subtle">${text(detail)}</p>` : ''}</div>${action}</div>`; }
function wifiExpanded() { return wifiDetailsOpen ?? Boolean(draft.ssid || !snapshot?.networks.length); }
function routeEditor(agent) {
  return `<div id="route-editor-${agent.id}" class="agent-route-editor" role="region" aria-labelledby="agent-name-${agent.id}"><label class="field" for="routeMode">${text('route')}<select id="routeMode" data-field="routeMode"><option value="direct" ${draft.routeMode === 'direct' ? 'selected' : ''}>${text('direct')}</option><option value="ss" ${draft.routeMode === 'ss' ? 'selected' : ''}>Shadowsocks</option></select></label><div id="proxy-fields" ${draft.routeMode === 'ss' ? '' : 'hidden'}><div class="inline">${field('proxyHost', 'proxyHost', 'text', 'proxy.example')}${field('proxyPort', 'proxyPort', 'number', '443')}</div>${field('proxySecret', 'proxySecret', 'password', agent.route?.savedSecret ? text('proxyPasswordHint') : '')}<dl class="connection-fact"><dt>${text('proxyMethod')}</dt><dd id="proxyMethod"><code>${escape(draft.proxyMethod)}</code></dd></dl><p class="hint">${text('proxyHint')}</p></div><p class="hint route-save-hint">${text('routeSaveHint')}</p></div>`;
}
function agentRow(agent, linked) {
  const editing = routeEditorOpen && String(agent.id) === draft.selectedAgent;
  return `<article class="agent-item ${editing ? 'editing' : ''}" data-agent-id="${agent.id}"><div class="agent-row"><div class="agent-main"><div class="agent-heading"><h3 id="agent-name-${agent.id}">${escape(agent.name)}</h3>${agent.current ? `<span class="tag">${text('current')}</span>` : ''}</div><div class="agent-sub">${escape(agent.url)}</div></div><div class="agent-actions">${!agent.current ? `<button class="quiet small" data-use="${agent.id}" ${busy || !linked ? 'disabled' : ''}>${text('use')}</button>` : ''}<button class="quiet small" data-drop="${agent.id}" ${busy || !linked ? 'disabled' : ''}>${text('remove')}</button></div></div><div class="agent-route-line"><div class="agent-route-summary"><span class="route-badge" data-route-agent="${agent.id}"></span><span class="route-unsaved" data-route-dirty="${agent.id}" hidden>${text('unsaved')}</span></div><button class="quiet small route-toggle" data-agent="${agent.id}" aria-label="${escape(agent.name)} · ${text('connectionSettings')}" aria-expanded="${editing}" ${editing ? `aria-controls="route-editor-${agent.id}"` : ''} ${busy ? 'disabled' : ''}>${text('connectionSettings')}${icon('chevron')}</button></div>${editing ? routeEditor(agent) : ''}</article>`;
}
function connectionButton(kind, supported) {
  const usb = kind === 'usb';
  if (usb && !supported && supportedBLE()) return '';
  const hint = !window.isSecureContext ? 'browser-security' : !supported ? `${kind}-support` : '';
  return `<button id="connect-${kind}" ${usb ? 'class="primary"' : ''} ${hint ? `aria-describedby="${hint}"` : ''} ${busy || !supported ? 'disabled' : ''}>${icon(usb ? 'usb' : 'bluetooth')}${text(usb ? 'usb' : 'bluetooth')}</button>`;
}

function layoutCards() {
  const grid = root.querySelector('.grid');
  const focused = document.activeElement;
  const cards = ['wifi', 'agent', 'preferences', 'firmware'].map(name => root.querySelector(`#${name}-card`));
  if (desktopLayout.matches) {
    const columns = [[cards[0], cards[2]], [cards[1], cards[3]]].map(items => {
      const column = document.createElement('div');
      column.className = 'card-column';
      column.append(...items);
      return column;
    });
    grid.replaceChildren(...columns);
  } else grid.replaceChildren(...cards);
  if (grid.contains(focused)) focused.focus({ preventScroll: true });
}

function render() {
  const linked = connected();
  const preferencesDisabled = busy || !linked || !snapshot;
  const usbReady = linked && snapshot && transportKind === 'usb';
  const firmware = firmwareIdentity(snapshot?.firmware, manifest);
  const update = firmwareUpdate(snapshot?.firmware, manifest);
  const deviceVersion = !snapshot ? '—' : ['release', 'version'].includes(firmware.kind) ? firmware.value
    : firmware.kind === 'development' ? `${firmware.value ? `${firmware.value} · ` : ''}${text('firmwareDevelopment')}`
      : text('firmwareUnknown');
  const flashDisabled = busy || !supportedSerial() || (linked && transportKind !== 'usb') || !manifest || (keepFirmwareSettings && !usbReady);
  const currentAgent = snapshot?.agents.find(agent => agent.current);
  const connectionLabel = linked ? `AI Passport · ${transportKind === 'usb' ? 'USB' : 'Bluetooth'}` : text('offline');
  const showBluetoothHelp = !linked && bluetoothAttempted && supportedBLE();
  const firmwareConnectionHint = !window.isSecureContext ? '' : !supportedSerial() ? 'unsupportedUSB' : linked && transportKind !== 'usb' ? 'usbOnly' : '';
  const firmwareNote = keepFirmwareSettings ? !usbReady && !firmwareConnectionHint ? 'updateNeedsDevice' : 'flashNote' : 'fullNote';
  const flashLabel = text(keepFirmwareSettings ? 'application' : 'full');
  const connectionSupport = !linked && window.isSecureContext ? `${!supportedSerial() && !supportedBLE() ? `<p id="usb-support">${text('unsupportedUSB')}</p>` : ''}${!supportedBLE() ? `<div id="ble-support"><p>${text('unsupportedBLE')}</p><a class="bluefy-install" href="${bluefyStoreURL}" target="_blank" rel="noopener noreferrer">${text('installBluefy')}${icon('external')}</a><p>${text('bluefyNext')}</p></div>` : ''}` : '';
  root.innerHTML = `<div class="shell">
    <header><div class="header-identity"><a class="brand" href="./" aria-label="AI Passport Agent"><img src="${brandLogo}" alt="" width="44" height="44"><span class="brand-name">AI Passport Agent</span></a><span class="header-divider" aria-hidden="true"></span><h1>${text('title')}</h1></div><nav><a class="source-link" href="https://github.com/vangie/ai-passport-agent" target="_blank" rel="noreferrer" aria-label="${text('github')}"><span>${text('github')} ↗</span>${icon('external')}</a><fieldset class="language-switch" aria-label="${text('interfaceLanguage')}">${icon('language')}<div class="language-options" data-selected="${language}"><span class="language-thumb" aria-hidden="true"></span><label for="language-zh"><input id="language-zh" type="radio" name="interface-language" value="zh" aria-label="${text('chinese')}" ${language === 'zh' ? 'checked' : ''}><span lang="zh-CN">中</span></label><label for="language-en"><input id="language-en" type="radio" name="interface-language" value="en" aria-label="English" ${language === 'en' ? 'checked' : ''}><span lang="en">EN</span></label></div></fieldset></nav></header>
    <section class="connection ${linked ? 'is-connected' : ''}"><div class="connection-info"><div class="connection-title"><span class="dot ${linked ? 'on' : ''}"></span><h2>${linked ? escape(connectionLabel) : text('connectTitle')}</h2></div>${linked ? `<p class="subtle">${text('firmware')}: ${escape(deviceVersion)} · ${text('battery')}: ${snapshot?.device.battery >= 0 ? snapshot.device.battery + '%' : '—'}${currentAgent ? ` · ${escape(currentAgent.name)}` : ''}</p>` : ''}${feedback('connection')}</div><div class="actions connection-actions">${linked ? `<button id="refresh" ${busy ? 'disabled' : ''}>${text('refresh')}</button><button id="disconnect" ${busy ? 'disabled' : ''}>${text('disconnect')}</button>` : connectionButton('usb', supportedSerial()) + connectionButton('ble', supportedBLE())}</div>${connectionSupport ? `<div class="connection-support">${connectionSupport}</div>` : ''}</section>
    ${!window.isSecureContext || showBluetoothHelp ? `<div class="environment">${!window.isSecureContext ? `<p id="browser-security" class="warning">${text('insecure')}</p>` : ''}${showBluetoothHelp ? `<div id="bluetooth-panel"><p id="bluetooth-hint">${text('bluetoothHint')}</p><details id="bluetooth-help" ${bluetoothHelpOpen ? 'open' : ''}><summary>${text('bluetoothHelp')}</summary><p>${text('bluetoothBrowser')}</p><p>${text('bluetoothHelpDetail')}</p><p id="bluetooth-restart">${text('bluetoothRestart')}</p><button id="connect-ble-broad" class="small" ${busy ? 'disabled' : ''}>${text('bluetoothBroad')}</button><p>${text('bluetoothBroadHint')}</p></details></div>` : ''}</div>` : ''}
    <main class="grid"><section id="wifi-card" class="card wifi-card">${sectionHeader('wifi', 'wifiTitle', 'wifiDetail')}<div class="card-content">
      ${linked && snapshot ? `<h3 class="section-label">${text('saved')}</h3><div id="saved-networks" class="list">${snapshot.networks.length ? snapshot.networks.map(network => `<div class="list-row ${network.ssid === draft.ssid ? 'selected' : ''}">${icon('wifi')}<button class="choose" data-network="${escape(network.ssid)}">${escape(network.ssid)}</button>${network.current ? `<span class="tag">${text('current')}</span>` : ''}<button class="quiet small" data-forget="${network.id}" ${busy ? 'disabled' : ''}>${text('forget')}</button></div>`).join('') : `<p class="empty">${text('emptyNetworks')}</p>`}</div>` : ''}
      <section id="nearby-section" aria-labelledby="nearby-heading"><div class="section-heading"><h3 id="nearby-heading" class="section-label">${text('nearby')}</h3><button class="small" id="scan" aria-controls="nearby-list" ${!linked || busy ? 'disabled' : ''}>${text(scanning ? 'scanning' : 'scan')}</button></div><div id="nearby-list" class="list" role="group" aria-labelledby="nearby-heading"></div>${linked && wifiScanAttempted ? `<details id="hotspot-help" ${hotspotHelpOpen ? 'open' : ''}><summary>${text('hotspotHelp')}</summary><p class="hint">${text('hotspotHint')}</p></details>` : ''}</section>
      <details id="wifi-details" ${wifiExpanded() ? 'open' : ''}><summary><span id="wifi-form-title">${text('addNetwork')}</span><span id="wifi-draft" class="wifi-draft"></span></summary>
      ${field('ssid', 'ssid', 'text', 'Home Wi-Fi')}<div class="wifi-password"><div class="wifi-password-heading"><label for="password">${text('password')}</label><label class="check"><input type="checkbox" data-field="open" ${draft.open ? 'checked' : ''}><span>${text('open')}</span></label></div><input id="password" data-field="password" type="password" value="${escape(draft.password)}" placeholder="${escape(text('passwordHint'))}" autocomplete="off" spellcheck="false"></div>
      <div class="wifi-form-actions"><button id="test-wifi" class="small" ${!linked || busy ? 'disabled' : ''}>${text('test')}</button></div>
      </details>
      ${feedback('wifi')}
    </div></section>
    <section id="agent-card" class="card">${sectionHeader('agent', 'agentTitle', 'agentDetail')}<div class="card-content"><div id="agent-list" class="list">${snapshot?.agents.length ? snapshot.agents.map(agent => agentRow(agent, linked)).join('') : `<p class="empty">${text('noAgents')}</p>`}</div>
      <details id="pairing-details" ${!snapshot?.agents.length || draft.code ? 'open' : ''}><summary>${text('newAgent')}</summary>
        <div class="agent-setup"><fieldset class="backend-switch"><legend>${text('agentBackend')}</legend><div class="backend-options">${['openclaw', 'hermes'].map(backend => `<label><input type="radio" name="prompt-backend" value="${backend}" ${promptBackend === backend ? 'checked' : ''}><span>${backend === 'hermes' ? 'Hermes' : 'OpenClaw'}</span></label>`).join('')}</div></fieldset><button id="copy-prompt" class="quiet small" aria-describedby="copy-prompt-status">${text('prompt')}</button></div>
        <div class="prompt-actions"><span id="copy-prompt-status" class="prompt-status" role="status" aria-live="polite" hidden></span></div>${field('name', 'agentName', 'text', text('namePlaceholder'))}${field('address', 'address', 'url', 'wss://gateway.example')}<label class="field">${text('code')}<textarea id="code" data-field="code" autocomplete="off" spellcheck="false">${escape(draft.code)}</textarea></label><p class="hint">${text('codeHint')}</p></details>
      ${feedback('agent')}
    </div></section>
    <section id="preferences-card" class="card">${sectionHeader('settings', 'settingsTitle')}<fieldset class="card-content preference-fields" ${preferencesDisabled ? 'disabled' : ''}><div class="preferences"><label for="device-language">${text('screenLanguage')}</label><select id="device-language" data-field="language"><option value="zh" ${draft.language === 'zh' ? 'selected' : ''}>${text('chinese')}</option><option value="en" ${draft.language === 'en' ? 'selected' : ''}>${text('english')}</option></select></div><div class="preferences ${snapshot && !snapshot.capabilities.includes('device-control') ? 'is-unavailable' : ''}"><label for="volume">${text('volume')}</label><div class="volume"><input id="volume" data-field="volume" type="range" min="0" max="100" value="${draft.volume}" ${snapshot && !snapshot.capabilities.includes('device-control') ? 'disabled' : ''}><output id="volume-value">${draft.volume}%</output></div></div></fieldset></section>
    ${supportedSerial() ? `<section id="firmware-card" class="card firmware-card">${sectionHeader('download', 'firmwareTitle', null, '<span class="tag">USB</span>')}<div class="card-content">
      <dl class="firmware-versions"><div><dt>${text('firmwareCurrent')}</dt><dd id="firmware-current">${escape(deviceVersion)}</dd></div><div><dt>${text('firmwareAvailable')}</dt><dd>${escape(manifest?.version || '—')}</dd></div></dl>
      ${snapshot && manifest && ['current', 'update', 'newer'].includes(update.status) ? `<p id="firmware-status" class="firmware-status" data-status="${update.status}">${text(`firmwareStatus_${update.status}`)}</p>` : ''}${!manifest ? `<p class="hint">${text(firmwareReady ? 'firmwareMissing' : 'firmwareLoading')}</p>` : ''}
      <div class="firmware-actions"><label class="firmware-keep"><input id="keep-firmware-settings" type="checkbox" aria-describedby="firmware-mode-note" ${keepFirmwareSettings ? 'checked' : ''} ${busy || (linked && !usbReady) ? 'disabled' : ''}><span>${text('keepSettings')}</span></label><button id="flash" aria-describedby="firmware-mode-note${firmwareConnectionHint ? ' firmware-connection-hint' : ''}" ${flashDisabled ? 'disabled' : ''}>${icon('download')}${escape(flashLabel)}${progress !== null ? ` ${progress}%` : ''}</button></div>
      <p id="firmware-mode-note" class="hint ${firmwareNote === 'fullNote' ? 'warning' : ''}">${text(firmwareNote)}</p>
      ${progress !== null ? `<progress max="100" value="${progress}" aria-label="${text('flash')}"></progress>` : ''}${firmwareConnectionHint ? `<p id="firmware-connection-hint" class="hint">${text(firmwareConnectionHint)}</p>` : ''}
      ${snapshot?.firmware ? `<details id="firmware-details"><summary>${text('firmwareDetails')}</summary><dl class="firmware-identifiers"><div><dt>${text('firmwareDeviceBuild')}</dt><dd>${escape(snapshot.firmware)}</dd></div>${manifest?.buildId ? `<div><dt>${text('firmwareTargetBuild')}</dt><dd>${escape(manifest.buildId)}</dd></div>` : ''}</dl></details>` : ''}
      ${feedback('firmware')}
    </div></section>` : `<section id="firmware-card" class="firmware-notice">${icon('download')}<p>${text('firmwareDesktop')}</p></section>`}
    </main>
    <div class="save-bar"><div class="save-summary"><div id="save-state" class="save-state"></div>${feedback('save')}</div><div class="actions"><button id="discard" class="quiet">${text('discard')}</button><button id="save" class="primary">${text('save')}</button></div></div>
    <footer><span>AI Passport Agent · ${text('title')}</span><button id="privacy-info" class="footer-privacy" aria-haspopup="dialog" aria-controls="privacy-dialog">${icon('shield')}<span>${text('privacyTitle')}</span></button></footer>
    <dialog id="privacy-dialog" aria-labelledby="privacy-title" aria-describedby="privacy-intro"><h2 id="privacy-title">${text('privacyTitle')}</h2><p id="privacy-intro">${text('privacyIntro')}</p><dl class="privacy-facts"><div><dt>${text('privacyTransferTitle')}</dt><dd>${text('privacyTransfer')}</dd></div><div><dt>${text('privacyStorageTitle')}</dt><dd>${text('privacyStorage')}</dd></div><div><dt>${text('privacyServicesTitle')}</dt><dd>${text('privacyServices')}</dd></div></dl><p class="hint privacy-hosting">${text('privacyHosting')}</p><div class="actions"><button id="privacy-close" autofocus>${text('privacyClose')}</button></div></dialog>
    <dialog id="confirm-dialog"><p></p><div class="actions"><button id="dialog-cancel">${text('dialogCancel')}</button><button id="dialog-confirm" class="primary">${text('dialogConfirm')}</button></div></dialog>
  </div>`;
  layoutCards();
  renderNearby();
  updateSave();
  updatePromptFeedback();
  bind();
  if (privacyOpen) document.getElementById('privacy-dialog').showModal();
}

function updateSave() {
  const hasChanges = dirty.size > 0;
  const showFeedback = notice?.scope === 'save' && !busy && (notice.error || !hasChanges);
  document.querySelector('#save-state').hidden = Boolean(showFeedback);
  document.querySelector('#save-feedback').hidden = !showFeedback;
  document.querySelector('#save-state').className = `save-state ${hasChanges ? 'dirty' : ''}`;
  document.querySelector('#save-state').textContent = busy ? text('busy') : hasChanges ? text('unsaved') : connected() ? text('clean') : text('reconnect');
  document.querySelector('#save').disabled = !connected() || busy || !hasChanges;
  document.querySelector('#discard').disabled = busy || !hasChanges;
  root.querySelectorAll('[data-forget], [data-drop], [data-use]').forEach(button => { button.disabled = busy || !connected() || hasChanges; });
  const output = document.querySelector('#volume-value');
  if (output) output.textContent = `${draft.volume}%`;
  document.querySelector('#wifi-form-title').textContent = text(snapshot?.networks.some(network => network.ssid === draft.ssid) ? 'editNetwork' : 'addNetwork');
  document.querySelector('#wifi-draft').textContent = draft.ssid;
  for (const agent of snapshot?.agents || []) {
    const routeDirty = dirty.has('route') && String(agent.id) === draft.selectedAgent;
    document.querySelector(`[data-route-agent="${agent.id}"]`).textContent = text((routeDirty ? draft.routeMode : agent.route?.mode) === 'ss' ? 'shadowsocks' : 'direct');
    document.querySelector(`[data-route-dirty="${agent.id}"]`).hidden = !routeDirty;
  }
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
    wifiDetailsOpen = true;
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
async function run(action, scope = 'save') {
  if (busy) return;
  busy = true; notice = null; actionScope = scope;
  let promise;
  try { promise = action(); render(); await promise; }
  catch (error) { report(error); }
  finally { busy = false; scanning = false; render(); }
}
async function connect(kind, options) {
  if (!window.isSecureContext) throw new Error('insecure');
  if (kind === 'usb' && !supportedSerial()) throw new Error('unsupportedUSB');
  if (kind === 'ble' && !supportedBLE()) throw new Error('unsupportedBLE');
  bluetoothAttempted = kind === 'ble';
  if (!bluetoothAttempted) bluetoothHelpOpen = false;
  const transport = kind === 'usb' ? new SerialTransport() : new BluetoothTransport(options);
  try {
    await transport.connect();
    transportKind = kind;
    const linkedSession = new Session(transport, () => { if (session === linkedSession) { keepFirmwareSettings = true; say(linkedSession.disconnectReason || 'disconnected', true, 'connection'); if (!busy) render(); } });
    session = linkedSession;
    adopt(await session.snapshot(), !dirty.size);
    keepFirmwareSettings = true;
    wifiScanAttempted = false;
    nearbyNetworks = [];
    hotspotHelpOpen = false;
    bluetoothAttempted = false;
    bluetoothHelpOpen = false;
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
      if (saved && !dirty.has('route')) routeDraft(saved);
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
  const privacyDialog = document.getElementById('privacy-dialog');
  on('privacy-info', () => { privacyOpen = true; privacyDialog.showModal(); });
  on('privacy-close', () => privacyDialog.close());
  privacyDialog.onclose = () => { if (privacyDialog.isConnected) privacyOpen = false; };
  const bluetoothHelp = document.getElementById('bluetooth-help');
  if (bluetoothHelp) bluetoothHelp.ontoggle = () => { bluetoothHelpOpen = bluetoothHelp.open; };
  const wifiDetails = document.getElementById('wifi-details');
  wifiDetails.ontoggle = () => {
    if (wifiDetails.isConnected && wifiDetails.open !== wifiExpanded()) wifiDetailsOpen = wifiDetails.open;
  };
  const hotspotHelp = document.getElementById('hotspot-help');
  if (hotspotHelp) hotspotHelp.ontoggle = () => { if (hotspotHelp.isConnected) hotspotHelpOpen = hotspotHelp.open; };
  const keepSettings = document.getElementById('keep-firmware-settings');
  if (keepSettings) keepSettings.onchange = event => {
    keepFirmwareSettings = event.target.checked;
    render();
    document.getElementById('keep-firmware-settings').focus({ preventScroll: true });
  };
  root.querySelectorAll('[name="interface-language"]').forEach(input => {
    input.onchange = () => {
      const previous = language;
      setLanguage(input.value);
      render();
      document.querySelector(`[name="interface-language"][value="${language}"]`).focus({ preventScroll: true });
      if (!window.matchMedia('(prefers-reduced-motion: reduce)').matches) {
        document.querySelector('.language-thumb').animate([
          { transform: `translateX(${previous === 'en' ? '100%' : '0'})` },
          { transform: `translateX(${language === 'en' ? '100%' : '0'})` },
        ], { duration: 180, easing: 'cubic-bezier(.2,.8,.2,1)' });
      }
    };
  });
  on('connect-usb', () => run(() => connect('usb'), 'connection'));
  on('connect-ble', () => run(() => connect('ble'), 'connection'));
  on('connect-ble-broad', () => run(() => connect('ble', { broadDiscovery: true }), 'connection'));
  on('disconnect', () => run(async () => { await session.close(); session = null; snapshot = null; transportKind = ''; keepFirmwareSettings = true; }, 'connection'));
  on('refresh', async () => {
    if (dirty.size && !await confirmAction('confirmRefresh')) return;
    await run(async () => { adopt(await session.snapshot(), true); }, 'connection');
  });
  on('discard', async () => { if (await confirmAction('confirmDiscard')) { if (snapshot) adopt(snapshot, true); else { draft = blankDraft(); dirty.clear(); } if (notice?.scope === 'save') notice = null; render(); } });
  on('save', () => run(save));
  on('scan', () => run(async () => {
    wifiScanAttempted = true; scanning = true; nearbyNetworks = [];
    await session.request(snapshot.capabilities.includes('scan-live') ? 'SCAN LIVE' : 'SCAN', 'SCAN END', 60000, line => { nearby(line, nearbyNetworks); renderNearby(); });
    say('scanDone');
  }, 'wifi'));
  on('test-wifi', () => run(async () => {
    validateWiFi(draft.ssid, draft.password, draft.open, snapshot.networks);
    await session.request(`TRY ${encode(draft.ssid)} ${encode(draft.password)}`, 'TRY ok', 60000);
    say('testOK');
  }, 'wifi'));
  on('copy-prompt', copyPrompt);
  root.querySelectorAll('[name="prompt-backend"]').forEach(input => {
    input.onchange = () => {
      promptBackend = input.value;
      setPromptStatus('idle');
    };
  });
  bindNetworks(root);
  for (const [attribute, command, ending, confirmation] of [['forget', 'FORGET', 'OK forget', 'confirmForget'], ['drop', 'DROP', 'OK drop', 'confirmRemove'], ['use', 'USE', 'OK use', null]]) {
    root.querySelectorAll(`[data-${attribute}]`).forEach(button => button.onclick = async () => {
      if (confirmation && !await confirmAction(confirmation)) return;
      await run(async () => { adopt(await session.mutate(`${command} ${snapshot.generation} ${button.dataset[attribute]}`, ending)); }, attribute === 'forget' ? 'wifi' : 'agent');
    });
  }
  root.querySelectorAll('[data-agent]').forEach(button => button.onclick = async () => {
    if (busy) return;
    if (draft.selectedAgent === button.dataset.agent) routeEditorOpen = !routeEditorOpen;
    else {
      if (dirty.has('route') && !await confirmAction('confirmRouteSwitch')) return;
      routeDraft(snapshot.agents.find(agent => String(agent.id) === button.dataset.agent));
      dirty.delete('route');
      routeEditorOpen = true;
    }
    render();
    document.querySelector(`[data-agent="${button.dataset.agent}"]`).focus({ preventScroll: true });
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
  const flash = async kind => {
    if (connected() && transportKind !== 'usb') { say('usbOnly', true, 'firmware'); render(); return; }
    if (kind === 'application' && (!connected() || transportKind !== 'usb')) { say('updateNeedsDevice', true, 'firmware'); render(); return; }
    if (!await confirmAction(kind === 'full' ? 'confirmFullFlash' : firmwareUpdate(snapshot?.firmware, manifest).confirm)) return;
    await run(async () => {
      const port = connected() ? session.transport.port : await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x303a }] });
      const data = await getImage(manifest, kind);
      if (connected()) { await session.close(); session = null; }
      snapshot = null; transportKind = ''; progress = 0; render();
      try {
        await flashFirmware(port, data, kind, value => {
          progress = value;
          const bar = document.querySelector('progress'); if (bar) bar.value = value;
          const button = document.querySelector('#flash'); if (button) button.textContent = `${text('flash')} ${value}%`;
        });
        keepFirmwareSettings = true;
        say('flashDone');
      } catch (error) { report(error); if (notice.key === 'deviceError') say('flashFailed', true); }
      finally { progress = null; }
    }, 'firmware');
  };
  on('flash', () => flash(keepFirmwareSettings ? 'application' : 'full'));
}

window.addEventListener('beforeunload', event => { if (dirty.size || busy) { event.preventDefault(); event.returnValue = ''; } });
desktopLayout.addEventListener('change', layoutCards);
setLanguage(language);
render();
getManifest().then(value => { manifest = value; }).catch(() => {}).finally(() => {
  firmwareReady = true;
  if (!busy && !document.activeElement?.matches('input, textarea, select')) render();
});

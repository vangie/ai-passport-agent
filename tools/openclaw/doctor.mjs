import {readFile} from 'node:fs/promises';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {createPassportProvider, PASSPORT_PATH, PASSPORT_PROTOCOL} from './passport-plugin.mjs';
import {gatewayTarget} from './passport-proxy.mjs';

export async function checkCompatibility({config, getProvider, WebSocket, api, version}) {
    for (const method of ['registerHttpRoute', 'registerGatewayMethod', 'registerService', 'registerRealtimeVoiceProvider', 'on'])
        if (typeof api[method] !== 'function') throw new Error(`Unsupported OpenClaw plugin API: ${method}`);
    const upstreamId = config?.plugins?.entries?.['ai-passport-openclaw']?.config?.upstreamProvider ?? 'openai';
    const upstream = getProvider(upstreamId, config);
    if (!upstream) throw new Error('Configured upstream realtime provider is unavailable');
    const provider = createPassportProvider(getProvider, upstreamId);
    const providerConfig = provider.resolveConfig({cfg: config, rawConfig: {}, surface: 'gateway-relay'});
    const bridge = provider.createBridge({cfg: config, providerConfig, language: 'zh',
        onAudio() {}, onClearAudio() {}, onError() {}, onClose() {}});
    await bridge.close();
    const target = gatewayTarget(config);
    await new Promise((resolve, reject) => {
        const peer = new WebSocket(target, {headers: {'X-Forwarded-For': '192.0.2.1'}});
        const timeout = setTimeout(() => { peer.terminate(); reject(new Error('Native gateway challenge timed out')); }, 10000);
        const finish = error => { clearTimeout(timeout); peer.terminate(); error ? reject(error) : resolve(); };
        peer.once('error', () => finish(new Error('Native gateway endpoint unavailable')));
        peer.once('message', data => {
            try {
                const frame = JSON.parse(data.toString());
                if (frame.event !== 'connect.challenge' || !frame.payload?.nonce) throw new Error('Native gateway challenge contract changed');
                finish();
            } catch { finish(new Error('Native gateway challenge contract changed')); }
        });
    });
    return {status: 'compatible', openclawVersion: version, pluginVersion: '0.7.0',
        protocol: PASSPORT_PROTOCOL, path: PASSPORT_PATH, upstreamProvider: upstreamId,
        checks: ['public-plugin-sdk', 'native-provider-contract', 'native-device-challenge'],
        credentials: 'checked by the running gateway, not exported to the doctor',
        microphoneStarted: false, providerConnected: false};
}

async function main() {
    const {WebSocket} = await import('ws');
    const sdk = await import('openclaw/plugin-sdk/realtime-voice');
    const configFile = process.env.OPENCLAW_CONFIG_PATH ?? `${process.env.HOME}/.openclaw/openclaw.json`;
    const config = JSON.parse(await readFile(configFile, 'utf8'));
    const entry = await import('./index.mjs');
    const registrations = {};
    const api = Object.fromEntries(['registerHttpRoute', 'registerGatewayMethod', 'registerService', 'registerRealtimeVoiceProvider', 'on']
        .map(method => [method, (...args) => { registrations[method] = args; }]));
    entry.default.register({...api, config, pluginConfig: config.plugins?.entries?.['ai-passport-openclaw']?.config,
        logger: {info() {}, warn() {}, error() {}}});
    const version = execFileSync('openclaw', ['--version'], {encoding: 'utf8', timeout: 10000}).trim();
    const report = await checkCompatibility({config, getProvider: sdk.getRealtimeVoiceProvider,
        WebSocket, api, version});
    console.log(JSON.stringify(report));
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1])
    main().catch(() => { console.error(JSON.stringify({status: 'incompatible', action: 'Keep the current version or restore the previous plugin; check native SDK/provider availability. No core files were modified.'})); process.exitCode = 1; });

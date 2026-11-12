import {getRealtimeVoiceProvider} from 'openclaw/plugin-sdk/realtime-voice';
import WebSocket, {WebSocketServer} from 'ws';
import {createPassportProvider, PASSPORT_PATH, PASSPORT_PROTOCOL} from './passport-plugin.mjs';
import {createPassportTransport} from './passport-proxy.mjs';
import {registerPassportTranscripts} from './passport-transcript.mjs';

export default {
    id: 'ai-passport-openclaw',
    name: 'AI Passport OpenClaw',
    register(api) {
        const upstreamId = api.pluginConfig?.upstreamProvider ?? 'openai';
        const maxConnections = api.pluginConfig?.maxConnections ?? 4;
        if (typeof api.registerRealtimeVoiceProvider !== 'function' || typeof api.registerHttpRoute !== 'function' ||
            typeof api.registerService !== 'function' || typeof api.registerGatewayMethod !== 'function')
            throw new Error('OpenClaw plugin SDK is incompatible; run the Passport doctor');
        api.registerRealtimeVoiceProvider(createPassportProvider(getRealtimeVoiceProvider, upstreamId));
        registerPassportTranscripts(api);
        let transport;
        api.registerHttpRoute({path: PASSPORT_PATH, auth: 'plugin', match: 'exact',
            handler(_req, res) {
                res.writeHead(426, {'Content-Type': 'application/json', 'Cache-Control': 'no-store'});
                res.end(JSON.stringify({error: 'Native signed device WebSocket required', protocol: PASSPORT_PROTOCOL}));
            },
            handleUpgrade(req, socket, head) {
                if (transport) return transport.upgrade(req, socket, head);
                socket.end('HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n');
                return true;
            }});
        api.registerGatewayMethod('passport.status', ({respond, context, params}) => {
            const config = context.getRuntimeConfig();
            const upstream = getRealtimeVoiceProvider(upstreamId, config);
            const provider = createPassportProvider(getRealtimeVoiceProvider, upstreamId);
            let upstreamConfigured = false;
            try {
                const agentId = typeof params?.agentId === 'string' ? params.agentId : undefined;
                const providerConfig = provider.resolveConfig({cfg: config, rawConfig: {}, agentId, surface: 'gateway-relay'});
                upstreamConfigured = provider.isConfigured({cfg: config, providerConfig, agentId});
            } catch {}
            respond(true, {pluginVersion: '0.5.1', protocol: PASSPORT_PROTOCOL, path: PASSPORT_PATH,
                upstreamProvider: upstreamId, upstreamAvailable: Boolean(upstream), upstreamConfigured,
                providerRegistered: Boolean(getRealtimeVoiceProvider('passport-realtime', config)),
                ...(transport?.diagnostics() ?? {connections: 0})});
        }, {scope: 'operator.read'});
        api.registerService({id: 'ai-passport-transport',
            start(context) {
                transport = createPassportTransport({WebSocket, WebSocketServer, config: context.config, maxConnections});
                context.logger.info('AI Passport native-auth transport ready (protocol 1)');
            },
            stop() { transport?.stop(); transport = undefined; }
        });
    }
};

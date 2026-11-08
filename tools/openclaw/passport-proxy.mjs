import {isIP} from 'node:net';
import {createPassportRelayOutput} from './passport-relay.mjs';
import {PASSPORT_PATH, PASSPORT_PROTOCOL, PASSPORT_PROVIDER} from './passport-plugin.mjs';

const MAX_FRAME_BYTES = 256 * 1024;
const MAX_PENDING_BYTES = 512 * 1024;
const MAX_PENDING_REQUESTS = 64;

export function gatewayTarget(config) {
    const port = config?.gateway?.port ?? 18789;
    if (!Number.isInteger(port) || port < 1 || port > 65535 || config?.gateway?.tls?.enabled)
        throw new Error('Passport requires a supported local gateway endpoint; run the Passport doctor');
    return `ws://127.0.0.1:${port}/`;
}

export function forwardedAddress(req) {
    const address = req.socket.remoteAddress?.replace(/^::ffff:/, '');
    return address && isIP(address) && address !== '::1' && !address.startsWith('127.') ? address : '192.0.2.1';
}

export function createPassportConnection({sendUpstream, sendClient, close, now, schedule, cancel}) {
    let phase = 'challenge', connectId, createId, sessionId, ima = false, closed = false;
    let downstreamSequence = 0, upstreamSequence;
    const requests = new Map();
    function emit(frame) {
        if (frame.type === 'event' && frame.event !== 'connect.challenge') frame = {...frame, seq: ++downstreamSequence};
        sendClient(frame);
    }
    const makeOutput = () => createPassportRelayOutput({enabled: true, ima: true,
        send: payload => emit({type: 'event', event: 'talk.event', payload}),
        fail: () => stop('Passport playback queue exceeded its bound'), now, schedule, cancel});
    let output = makeOutput();
    function resetSession() {
        output.close(); output = makeOutput(); sessionId = undefined;
    }
    function stop(reason) {
        if (closed) return;
        closed = true; output.close(); requests.clear(); close(reason);
    }
    function reject(id, message) {
        emit({type: 'res', id, ok: false, error: {code: 'INVALID_REQUEST', message}});
    }
    function track(frame) {
        const pending = [...requests.values()].reduce((total, request) => total + request.count, 0);
        if (!frame.id || pending >= MAX_PENDING_REQUESTS || requests.has(frame.id) && requests.get(frame.id).method !== frame.method)
            throw new Error('Passport request window exceeded');
        const request = requests.get(frame.id) ?? {method: frame.method, count: 0};
        request.count++; requests.set(frame.id, request);
    }
    function client(frame) {
        if (closed) return;
        try {
            if (frame.type !== 'req' || typeof frame.id !== 'string' || typeof frame.method !== 'string')
                throw new Error('Invalid Passport request');
            if (phase === 'connect' && frame.method === 'connect') {
                const params = frame.params;
                if (params?.client?.deviceFamily !== 'ai-passport' || params.role !== 'operator' ||
                    !params.device?.signature || !params.device?.publicKey ||
                    !params.auth?.deviceToken && !params.auth?.bootstrapToken && !params.auth?.token)
                    throw new Error('Passport requires native signed device authentication');
                connectId = frame.id;
                ima = params.caps?.includes('passport-ima-v1') === true;
                phase = 'authenticating';
                sendUpstream(frame);
                return;
            }
            if (phase !== 'ready') throw new Error('Passport device is not authenticated');
            if (frame.method === 'talk.session.create') {
                if (createId || sessionId) return reject(frame.id, 'Close the active Passport call before creating another');
                if (!ima) return reject(frame.id, 'Passport firmware requires passport-ima-v1');
                if (frame.params?.mode !== 'realtime' || frame.params.transport !== 'gateway-relay' || frame.params.brain !== 'agent-consult')
                    return reject(frame.id, 'Unsupported Passport call mode');
                createId = frame.id;
                frame = {...frame, params: {...frame.params, provider: PASSPORT_PROVIDER}};
            } else if (frame.method.startsWith('talk.session.') || frame.method === 'talk.client.toolCall') {
                const owner = frame.params?.sessionId ?? frame.params?.relaySessionId;
                if (!sessionId || owner !== sessionId) return reject(frame.id, 'Passport session ownership mismatch');
                if (frame.method === 'talk.session.appendAudio') {
                    const decoded = output.decodeAudio(frame.params.audioBase64);
                    if (!decoded) return reject(frame.id, 'Passport audio format was not negotiated');
                    frame = {...frame, params: {...frame.params, audioBase64: decoded.toString('base64')}};
                }
            }
            track(frame); sendUpstream(frame);
        } catch { stop('Invalid Passport protocol or unsupported audio'); }
    }
    function upstream(frame) {
        if (closed) return;
        try {
            if (frame.type === 'event' && frame.seq !== undefined) {
                if (!Number.isSafeInteger(frame.seq) || frame.seq < 0 || upstreamSequence !== undefined && frame.seq > upstreamSequence + 1)
                    throw new Error('Native gateway event sequence gap');
                upstreamSequence = frame.seq;
            }
            if (frame.event === 'connect.challenge' && phase === 'challenge') phase = 'connect';
            if (frame.type === 'res' && frame.id === connectId) {
                connectId = undefined;
                if (!frame.ok) { emit(frame); return stop('Native gateway device authentication failed'); }
                phase = 'ready';
                frame = {...frame, payload: {...frame.payload, passportProtocol: PASSPORT_PROTOCOL}};
            }
            const request = frame.type === 'res' ? requests.get(frame.id) : undefined;
            const method = request?.method;
            if (request && !--request.count) requests.delete(frame.id);
            if (frame.type === 'res' && frame.id === createId) {
                createId = undefined;
                if (frame.ok) {
                    if (!frame.payload?.sessionId || frame.payload.provider !== PASSPORT_PROVIDER ||
                        frame.payload.audio?.inputEncoding !== 'pcm16' || frame.payload.audio?.inputSampleRateHz !== 24000 ||
                        frame.payload.audio?.outputEncoding !== 'pcm16' || frame.payload.audio?.outputSampleRateHz !== 24000)
                        throw new Error('Native Talk session contract changed');
                    sessionId = frame.payload.sessionId;
                    frame = {...frame, payload: {...frame.payload, passportIma: true, passportSilence: true, passportProtocol: PASSPORT_PROTOCOL}};
                }
            }
            if (method === 'talk.session.close' && frame.ok) resetSession();
            if (frame.event === 'talk.event') {
                const payload = frame.payload;
                if (!payload || !sessionId || payload.relaySessionId !== sessionId) return;
                if (payload.type === 'close') {
                    resetSession(); emit(frame); return;
                }
                output.send(payload);
            } else emit(frame);
        } catch { stop('Native Talk protocol is incompatible; run the Passport doctor'); }
    }
    return {client, upstream, stop, phase: () => phase, diagnostics: () => output.diagnostics()};
}

export function createPassportTransport({WebSocket, WebSocketServer, config, maxConnections = 4}) {
    const target = gatewayTarget(config);
    const server = new WebSocketServer({noServer: true, maxPayload: MAX_FRAME_BYTES, perMessageDeflate: false});
    const connections = new Set();
    function upgrade(req, socket, head) {
        if (new URL(req.url, 'http://localhost').pathname !== PASSPORT_PATH) return false;
        if (connections.size >= maxConnections) {
            socket.end('HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n');
            return true;
        }
        server.handleUpgrade(req, socket, head, downstream => {
            const upstream = new WebSocket(target, {maxPayload: MAX_FRAME_BYTES, perMessageDeflate: false,
                headers: {'X-Forwarded-For': forwardedAddress(req), 'X-Forwarded-Proto': 'https'}});
            let stopped = false, deadline;
            function send(peer, frame) {
                if (stopped) return;
                if (peer.readyState !== WebSocket.OPEN || peer.bufferedAmount > MAX_PENDING_BYTES)
                    return stop('Passport transport unavailable');
                peer.send(JSON.stringify(frame), error => { if (error) stop('Passport transport write failed'); });
            }
            function stop(reason) {
                if (stopped) return;
                stopped = true;
                clearTimeout(deadline);
                connection.stop(reason);
                connections.delete(stop);
                for (const peer of [upstream, downstream]) {
                    if (peer.readyState === WebSocket.OPEN) peer.close(1011, reason.slice(0, 100));
                    else peer.terminate();
                }
            }
            const connection = createPassportConnection({
                sendUpstream: frame => send(upstream, frame),
                sendClient: frame => send(downstream, frame),
                close: stop
            });
            connections.add(stop);
            deadline = setTimeout(() => stop('Passport native device authentication timed out'), 30000);
            deadline.unref?.();
            function receive(handler, data, binary) {
                if (stopped) return;
                try {
                    if (binary) throw new Error('Unexpected binary frame');
                    handler(JSON.parse(data.toString()));
                    if (connection.phase() === 'ready') { clearTimeout(deadline); deadline = undefined; }
                } catch { stop('Invalid Passport WebSocket frame'); }
            }
            upstream.on('message', (data, binary) => receive(connection.upstream, data, binary));
            downstream.on('message', (data, binary) => receive(connection.client, data, binary));
            upstream.on('ping', data => { if (downstream.readyState === WebSocket.OPEN) downstream.ping(data); });
            downstream.on('ping', data => { if (upstream.readyState === WebSocket.OPEN) upstream.ping(data); });
            for (const peer of [upstream, downstream]) {
                peer.on('error', () => stop('Passport upstream transport failed'));
                peer.on('close', () => stop('Passport transport closed'));
            }
        });
        return true;
    }
    return {upgrade, stop() { for (const close of [...connections]) close('Passport plugin stopped'); server.close(); },
        diagnostics: () => ({connections: connections.size, maxConnections, protocol: PASSPORT_PROTOCOL})};
}

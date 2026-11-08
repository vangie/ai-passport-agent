import assert from 'node:assert/strict';
import {createPassportProvider, PASSPORT_PROVIDER} from '../tools/openclaw/passport-plugin.mjs';
import {createPassportConnection, forwardedAddress, gatewayTarget} from '../tools/openclaw/passport-proxy.mjs';

const config = {talk: {realtime: {providers: {openai: {model: 'fixture-model', voice: 'coral', apiKey: 'fixture-secret'}}}}};
let resolved, launched, spoken, connected = 0, closed = 0;
const nativeBridge = {
    connect() { connected++; }, sendAudio() {}, submitToolResult() {}, acknowledgeMark() {},
    close() { closed++; }, isConnected() { return false; },
    sendUserMessage(text) { assert.equal(this, nativeBridge); spoken = text; }
};
const nativeProvider = {
    resolveConfig(context) { resolved = context; return context.rawConfig; },
    isConfigured: () => true,
    createBridge(request) { launched = request; return nativeBridge; }
};
const provider = createPassportProvider(name => name === 'openai' ? nativeProvider : undefined);
assert.equal(provider.id, PASSPORT_PROVIDER);
const providerConfig = provider.resolveConfig({cfg: config, rawConfig: {silenceDurationMs: 600}});
assert.equal(providerConfig.model, 'fixture-model');
assert.equal(providerConfig.voice, 'coral');
assert.equal(resolved.autoRespondToAudio, true);
const onToolCall = () => {};
const bridge = provider.createBridge({cfg: config, providerConfig, language: 'zh', instructions: 'Keep agent identity.', onToolCall});
assert(launched.instructions.startsWith('Keep agent identity.'));
assert(launched.instructions.includes('普通话'));
assert.equal(launched.onToolCall, onToolCall);
assert.equal(launched.autoRespondToAudio, true);
bridge.sendUserMessage('Read the tool result.');
assert(spoken.includes('普通话'));
bridge.close();
assert.equal(closed, 1);
assert.equal(connected, 0);
assert.throws(() => createPassportProvider(() => undefined).createBridge({}), /unavailable/);
assert.throws(() => createPassportProvider(() => nativeProvider, PASSPORT_PROVIDER), /Invalid/);
assert.throws(() => provider.resolveConfig({cfg: {talk: {realtime: {consultRouting: 'force-agent-consult'}}}}), /provider-direct/);
assert.equal(gatewayTarget({gateway: {port: 18789}}), 'ws://127.0.0.1:18789/');
assert.throws(() => gatewayTarget({gateway: {tls: {enabled: true}}}), /supported/);
assert.equal(forwardedAddress({socket: {remoteAddress: '127.0.0.1'}, headers: {'x-forwarded-for': '127.0.0.1'}}), '192.0.2.1');
assert.equal(forwardedAddress({socket: {remoteAddress: '::ffff:10.0.0.4'}}), '10.0.0.4');

function fixture() {
    const upstream = [], client = [], failures = [];
    const connection = createPassportConnection({sendUpstream: frame => upstream.push(structuredClone(frame)),
        sendClient: frame => client.push(structuredClone(frame)), close: reason => failures.push(reason)});
    return {connection, upstream, client, failures};
}
const signedConnect = {type: 'req', id: 'connect', method: 'connect', params: {
    client: {deviceFamily: 'ai-passport'}, role: 'operator', scopes: ['operator.read', 'operator.write', 'operator.talk'],
    device: {signature: 'fixture-signature', publicKey: 'fixture-public-key'}, auth: {deviceToken: 'fixture-device-token'}, caps: ['passport-ima-v1']
}};
function authenticate(current, success = true) {
    current.connection.upstream({type: 'event', event: 'connect.challenge', payload: {nonce: 'fixture-nonce'}});
    current.connection.client(structuredClone(signedConnect));
    assert.deepEqual(current.upstream[0], signedConnect);
    current.connection.upstream({type: 'res', id: 'connect', ok: success, payload: {auth: {scopes: ['operator.talk']}}});
}
const current = fixture();
authenticate(current);
assert.equal(current.client.at(-1).payload.passportProtocol, 1);
const create = {type: 'req', id: 'create', method: 'talk.session.create', params: {
    sessionKey: 'agent:badge:fixture', mode: 'realtime', transport: 'gateway-relay', brain: 'agent-consult', language: 'zh'
}};
current.connection.client(create);
assert.equal(current.upstream.at(-1).params.provider, PASSPORT_PROVIDER);
current.connection.upstream({type: 'res', id: 'create', ok: true, payload: {provider: PASSPORT_PROVIDER, sessionId: 'call',
    audio: {inputEncoding: 'pcm16', inputSampleRateHz: 24000, outputEncoding: 'pcm16', outputSampleRateHz: 24000}}});
assert.equal(current.client.at(-1).payload.passportIma, true);
current.connection.upstream({type: 'event', event: 'talk.event', payload: {
    relaySessionId: 'call', type: 'toolResult', callId: 'news', talkEvent: {type: 'tool.progress'}
}});
assert.equal(current.client.at(-1).payload.talkEvent.type, 'tool.progress');
current.connection.upstream({type: 'event', event: 'chat', payload: {runId: 'news-run', state: 'final', message: {content: 'fixture result'}}});
assert.equal(current.client.at(-1).event, 'chat');
current.connection.client({type: 'req', id: 'append', method: 'talk.session.appendAudio', params: {sessionId: 'call', audioBase64: 'passport-silence:3840'}});
assert.equal(Buffer.from(current.upstream.at(-1).params.audioBase64, 'base64').length, 3840);
for (let index = 0; index < 4; index++) {
    current.connection.client({type: 'req', id: 'mark', method: 'talk.session.acknowledgeMark', params: {sessionId: 'call', markName: `audio-${index}`}});
    current.connection.upstream({type: 'res', id: 'mark', ok: true});
}
current.connection.client({type: 'req', id: 'cross-owner', method: 'talk.session.close', params: {sessionId: 'other'}});
assert.equal(current.client.at(-1).ok, false);
assert.equal(current.failures.length, 0);
current.connection.upstream({type: 'event', event: 'talk.event', payload: {relaySessionId: 'call', type: 'toolResult', callId: 'news', talkEvent: {type: 'tool.result', final: true}}});
assert.equal(current.client.at(-1).payload.talkEvent.final, true);
current.connection.upstream({type: 'event', event: 'talk.event', payload: {relaySessionId: 'call', type: 'close'}});
const afterClose = current.client.length;
current.connection.upstream({type: 'event', event: 'talk.event', payload: {relaySessionId: 'call', type: 'audioDone'}});
assert.equal(current.client.length, afterClose);
current.connection.client({...create, id: 'create-again'});
assert.equal(current.upstream.at(-1).params.provider, PASSPORT_PROVIDER);
current.connection.upstream({type: 'res', id: 'create-again', ok: true, payload: {provider: PASSPORT_PROVIDER, sessionId: 'next-call',
    audio: {inputEncoding: 'pcm16', inputSampleRateHz: 24000, outputEncoding: 'pcm16', outputSampleRateHz: 24000}}});
current.connection.upstream({type: 'event', event: 'talk.event', payload: {relaySessionId: 'next-call', type: 'ready'}});
assert.equal(current.client.at(-1).payload.relaySessionId, 'next-call');
current.connection.upstream({type: 'event', event: 'talk.event', seq: 40, payload: {relaySessionId: 'next-call', type: 'inputAudio'}});
current.connection.upstream({type: 'event', event: 'tick', seq: 41, payload: {}});
const deliveredEvents = current.client.filter(frame => frame.type === 'event' && frame.event !== 'connect.challenge');
assert.deepEqual(deliveredEvents.map(frame => frame.seq), deliveredEvents.map((frame, index) => index + 1));
current.connection.stop('finished');
assert.equal(current.failures.length, 1);
const denied = fixture(); authenticate(denied, false);
assert.equal(denied.failures.length, 1);
const unauthenticated = fixture(); unauthenticated.connection.client(create);
assert.equal(unauthenticated.upstream.length, 0);
assert.equal(unauthenticated.failures.length, 1);
const incompatible = fixture(); authenticate(incompatible); incompatible.connection.client(create);
incompatible.connection.upstream({type: 'res', id: 'create', ok: true, payload: {sessionId: 'unsupported'}});
assert.equal(incompatible.failures.length, 1);
const gap = fixture(); authenticate(gap);
gap.connection.upstream({type: 'event', event: 'tick', seq: 1});
gap.connection.upstream({type: 'event', event: 'tick', seq: 3});
assert.equal(gap.failures.length, 1);
console.log('Passport plugin: native provider, retained device proof/scopes, ownership, compact lifecycle, bounded requests and fail-closed compatibility PASS');

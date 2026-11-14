import assert from 'node:assert/strict';
import {createPassportProvider, PASSPORT_PROVIDER, PASSPORT_END_CALL} from '../tools/openclaw/passport-plugin.mjs';
import {createPassportConnection, createPassportTransport, forwardedAddress, gatewayTarget} from '../tools/openclaw/passport-proxy.mjs';
import {createPassportTranscripts, registerPassportTranscripts} from '../tools/openclaw/passport-transcript.mjs';

let transcriptClock = 0;
const transcripts = createPassportTranscripts({now: () => transcriptClock});
const transcriptSession = 'agent:passport:passport-fixture';
const transcriptContext = {sessionKey: transcriptSession, runId: 'run-1'};
const transcriptEvent = {currentUserMessage: '', currentUserMessageId: 'clip-1:user', prompt: '[Audio]\nTranscript:\n请介绍水浒传里的林冲。'};
const transcriptParams = {sessionKey: transcriptSession, runId: 'run-1', requestId: 'clip-1'};
transcripts.capture(transcriptEvent, transcriptContext);
assert.equal(transcripts.lookup(transcriptParams), '请介绍水浒传里的林冲。');
assert.equal(transcripts.lookup({...transcriptParams, sessionKey: 'agent:other:passport-fixture'}), undefined);
assert.equal(transcripts.lookup({...transcriptParams, runId: 'old', requestId: 'old'}), undefined);
assert.equal(transcripts.lookup({...transcriptParams, runId: 'old'}), undefined);
assert.equal(transcripts.lookup({...transcriptParams, requestId: 'old'}), undefined);
assert.equal(transcripts.lookup({...transcriptParams, runId: 'invalid\n'}), undefined);
transcripts.capture({...transcriptEvent, prompt: '[Audio]\nTranscript:\nreplacement'}, transcriptContext);
assert.equal(transcripts.lookup(transcriptParams), '请介绍水浒传里的林冲。');
for (const candidate of [
    {...transcriptEvent, currentUserMessage: 'typed text'},
    {...transcriptEvent, prompt: 'no transcript'},
    {...transcriptEvent, prompt: '[Audio]\nTranscript:\n[Voice note could not be transcribed because the audio attachment was too small]'}
]) {
    transcripts.capture(candidate, {...transcriptContext, runId: 'run-2'});
    assert.equal(transcripts.lookup({...transcriptParams, runId: 'run-2', requestId: 'clip-2'}), undefined);
}
transcriptClock = 10 * 60 * 1000;
assert.equal(transcripts.lookup(transcriptParams), undefined);
transcripts.capture({...transcriptEvent, prompt: '[Audio]\nTranscript:\n' + '春'.repeat(300)}, transcriptContext);
assert.equal(transcripts.lookup(transcriptParams), '春'.repeat(159));
transcripts.clear();
for (let index = 0; index < 33; index++) transcripts.capture({...transcriptEvent, currentUserMessageId: `clip-${index}:user`},
    {...transcriptContext, runId: `run-${index}`});
assert.equal(transcripts.lookup({...transcriptParams, runId: 'run-0', requestId: 'clip-0'}), undefined);
const transcriptHooks = new Map(), transcriptMethods = new Map();
const registeredTranscripts = registerPassportTranscripts({
    on(name, handler) { transcriptHooks.set(name, handler); },
    registerGatewayMethod(name, handler, options) { transcriptMethods.set(name, {handler, options}); }
});
assert.deepEqual(transcriptMethods.get('passport.transcript').options,
    {scope: 'operator.write', profileAccess: 'required', sessionAccess: {mode: 'write', allowOwnSessionScope: true}});
transcriptHooks.get('llm_input')({prompt: transcriptEvent.prompt, runId: 'run-1', historyMessages: [
    {role: 'user', idempotencyKey: 'clip-1:user', __openclaw: {media: [{kind: 'audio', fileName: 'passport.wav'}]}}
]}, {sessionKey: transcriptSession});
assert.equal(registeredTranscripts.lookup(transcriptParams), undefined);
transcriptHooks.get('before_prompt_build')(transcriptEvent, transcriptContext);
assert.equal(registeredTranscripts.lookup(transcriptParams), '请介绍水浒传里的林冲。');
let transcriptResponse;
transcriptMethods.get('passport.transcript').handler({params: transcriptParams, respond(ok, payload) {
    assert(ok); transcriptResponse = payload;
}});
assert.equal(transcriptResponse.text, '请介绍水浒传里的林冲。');
transcriptHooks.get('gateway_stop')();
assert.equal(registeredTranscripts.lookup(transcriptParams), undefined);
const nativePrompt = '[Audio]\nUser text:\n[media attached: media://inbound/passport---fixture-recording.wav]\nTranscript:\n请介绍水浒传里的林冲。';
transcriptHooks.get('before_prompt_build')({prompt: nativePrompt, messages: []}, transcriptContext);
assert.equal(registeredTranscripts.lookup(transcriptParams), '请介绍水浒传里的林冲。');
transcriptHooks.get('gateway_stop')();
transcriptHooks.get('llm_input')({prompt: '[Sun 2026-11-08 23:12 GMT+8] ' + nativePrompt,
    runId: 'run-1', historyMessages: [{role: 'user', content: 'audio conversation metadata'}]},
{sessionKey: transcriptSession});
assert.equal(registeredTranscripts.lookup(transcriptParams), '请介绍水浒传里的林冲。');
transcriptHooks.get('gateway_stop')();
transcriptHooks.get('before_prompt_build')({prompt: nativePrompt.replace('passport---', 'other---')}, transcriptContext);
assert.equal(registeredTranscripts.lookup(transcriptParams), undefined);

const config = {talk: {realtime: {providers: {openai: {model: 'fixture-model', voice: 'coral', apiKey: 'fixture-secret'}}}}};
let resolved, launched, spoken, interrupted, connected = 0, closed = 0;
const nativeBridge = {
    connect() { connected++; }, sendAudio() {}, submitToolResult() {}, acknowledgeMark() {},
    handleBargeIn(options) { interrupted = options; },
    close() { closed++; }, isConnected() { return false; },
    sendUserMessage(text) { assert.equal(this, nativeBridge); spoken = text; }
};
const nativeProvider = {
    resolveConfig(context) { resolved = context; return context.rawConfig; },
    isConfigured: () => true,
    createBridge(request) { launched = request; return nativeBridge; }
};
const diagnostics = [];
const provider = createPassportProvider(name => name === 'openai' ? nativeProvider : undefined, 'openai',
    event => diagnostics.push(event));
assert.equal(provider.id, PASSPORT_PROVIDER);
const providerConfig = provider.resolveConfig({cfg: config, rawConfig: {silenceDurationMs: 600}});
assert.equal(providerConfig.model, 'fixture-model');
assert.equal(providerConfig.voice, 'coral');
assert.equal(resolved.autoRespondToAudio, true);
const onToolCall = () => {};
const callbackEvents = [];
const bridge = provider.createBridge({cfg: config,
    providerConfig: {...providerConfig, interruptResponseOnInputAudio: true},
    interruptResponseOnInputAudio: true, language: 'zh', instructions: 'Keep agent identity.', onToolCall,
    onResponseDone: outcome => callbackEvents.push(outcome.status),
    onEvent: event => callbackEvents.push(event.type),
    onError: error => callbackEvents.push(error.type)});
assert(launched.instructions.startsWith('Keep agent identity.'));
assert(launched.instructions.includes('普通话'));
assert(launched.instructions.includes('passport_end_call') && launched.instructions.includes('我不想聊了'));
assert.equal(launched.tools.at(-1).name, PASSPORT_END_CALL);
assert.equal(launched.autoRespondToAudio, true);
assert.equal(launched.interruptResponseOnInputAudio, false);
assert.equal(launched.providerConfig.interruptResponseOnInputAudio, false);
assert.equal(providerConfig.interruptResponseOnInputAudio, undefined);
launched.onResponseDone({status: 'incomplete', reason: 'max_output_tokens',
    error: {code: 'server_error', message: 'private conversation text'}, responseId: 'private-id'});
launched.onEvent({type: 'input_audio_buffer.speech_started', detail: 'private transcript'});
launched.onError({code: 'private-token contains spaces', type: 'server_error', message: 'private endpoint'});
assert.deepEqual(diagnostics, [
    {event: 'response.done', status: 'incomplete', reason: 'max_output_tokens', code: 'server_error'},
    {event: 'input_audio_buffer.speech_started'},
    {event: 'provider.error', type: 'server_error'}
]);
assert.deepEqual(callbackEvents, ['incomplete', 'input_audio_buffer.speech_started', 'server_error']);
bridge.sendUserMessage('Read the tool result.');
assert(spoken.includes('普通话'));
bridge.handleBargeIn({audioPlaybackActive: true});
assert.deepEqual(interrupted, {audioPlaybackActive: true, force: true});
await bridge.close();
assert.equal(closed, 1);
assert.equal(connected, 0);
launched.onResponseDone({status: 'failed'});
launched.onEvent({type: 'error'});
launched.onError({type: 'server_error'});
assert.equal(diagnostics.length, 3);
assert.equal(callbackEvents.length, 3);
function callFixture({rejectClose = false, providerCloses = true, closeReason = 'completed'} = {}) {
    const events = [], delegated = [], transcripts = [];
    let request, closeCount = 0, resolveClose;
    const completion = new Promise(resolve => { resolveClose = resolve; });
    const upstreamBridge = {
        connect() {}, sendAudio() { events.push('input'); }, submitToolResult() {}, acknowledgeMark() {}, handleBargeIn() {},
        isConnected: () => true,
        close() {
            closeCount++;
            request.onAudio(Buffer.alloc(2));
            request.onToolCall({name: 'openclaw_agent_consult'});
            request.onTranscript('user', 'final fixture', true);
            request.onTranscript('assistant', 'late partial', false);
            if (providerCloses) request.onClose(closeReason);
            return rejectClose ? Promise.reject(new Error('fixture close failure')) : completion;
        }
    };
    const currentProvider = createPassportProvider(() => ({isConfigured: () => true,
        createBridge(params) { request = params; return upstreamBridge; }}));
    const control = {type: 'function', name: 'openclaw_agent_control'};
    const currentBridge = currentProvider.createBridge({tools: [control],
        onToolCall: event => delegated.push(event), onAudio: () => events.push('output'),
        onTranscript: (...args) => transcripts.push(args),
        onClose: reason => events.push(reason), onError: error => events.push(error.message)});
    return {bridge: currentBridge, request, events, delegated, transcripts, resolveClose, get closeCount() { return closeCount; }};
}
for (const providerCloses of [true, false]) {
    const call = callFixture({providerCloses});
    assert.deepEqual(call.request.tools.map(tool => tool.name), ['openclaw_agent_control', PASSPORT_END_CALL]);
    const consult = {name: 'openclaw_agent_consult', callId: 'lookup', args: {question: 'fixture'}};
    call.request.onToolCall(consult);
    assert.deepEqual(call.delegated, [consult]);
    call.request.onToolCall({name: PASSPORT_END_CALL, callId: 'hang-up', args: {}});
    call.request.onToolCall({name: PASSPORT_END_CALL, callId: 'duplicate', args: {}});
    assert.equal(call.bridge.isConnected(), false);
    call.bridge.sendAudio(Buffer.alloc(2));
    await Promise.resolve();
    assert.equal(call.closeCount, 1);
    assert.deepEqual(call.events, []);
    call.resolveClose();
    await call.bridge.close();
    assert.deepEqual(call.events, ['completed']);
    assert.deepEqual(call.transcripts, [['user', 'final fixture', true]]);
    assert.deepEqual(call.delegated, [consult]);
    call.request.onAudio(Buffer.alloc(2));
    call.request.onClose('error');
    assert.deepEqual(call.events, ['completed']);
}
const failedHangup = callFixture({rejectClose: true});
failedHangup.request.onToolCall({name: PASSPORT_END_CALL, callId: 'failed'});
await assert.rejects(failedHangup.bridge.close(), /fixture close failure/);
assert.deepEqual(failedHangup.events, ['fixture close failure', 'error']);
const terminalError = callFixture({closeReason: 'error'});
terminalError.request.onToolCall({name: PASSPORT_END_CALL});
terminalError.resolveClose();
await terminalError.bridge.close();
assert.deepEqual(terminalError.events, ['error']);
const disconnected = callFixture();
disconnected.request.onClose('error');
disconnected.request.onClose('completed');
assert.deepEqual(disconnected.events, ['error']);
assert.equal(disconnected.bridge.isConnected(), false);
console.log('Passport voice hangup: dedicated call tool, delegated task isolation, idempotent cleanup and terminal errors PASS');
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
    sessionKey: 'agent:passport:fixture', mode: 'realtime', transport: 'gateway-relay', brain: 'agent-consult', language: 'zh'
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
current.connection.client({type: 'req', id: 'interrupt', method: 'talk.session.cancelOutput',
    params: {sessionId: 'call', reason: 'barge-in'}});
assert.equal(current.upstream.at(-1).method, 'talk.session.cancelOutput');
const pendingInterruptRequests = current.upstream.length;
current.connection.client({type: 'req', id: 'interrupt-again', method: 'talk.session.cancelOutput',
    params: {sessionId: 'call', reason: 'barge-in'}});
assert.equal(current.upstream.length, pendingInterruptRequests);
assert.equal(current.client.at(-1).ok, false);
const beforeInterrupt = current.client.length;
for (const type of ['audio', 'mark', 'audioDone', 'responseStarted', 'transcript', 'partial'])
    current.connection.upstream({type: 'event', event: 'talk.event', payload: {relaySessionId: 'call', type, role: 'assistant', audioBase64: 'AAAA'}});
assert.equal(current.client.length, beforeInterrupt);
current.connection.upstream({type: 'res', id: 'interrupt', ok: true, payload: {status: 'applied'}});
assert.equal(current.client.at(-2).payload.type, 'clear');
assert.equal(current.client.at(-1).id, 'interrupt');
current.connection.upstream({type: 'event', event: 'talk.event', payload: {relaySessionId: 'call', type: 'responseStarted'}});
assert.equal(current.client.at(-1).payload.type, 'responseStarted');
current.connection.client({type: 'req', id: 'interrupt-denied', method: 'talk.session.cancelOutput',
    params: {sessionId: 'call', reason: 'barge-in'}});
current.connection.upstream({type: 'res', id: 'interrupt-denied', ok: false});
assert.equal(current.client.at(-1).ok, false);
current.connection.upstream({type: 'event', event: 'talk.event', payload: {relaySessionId: 'call', type: 'responseStarted'}});
assert.equal(current.client.at(-1).payload.type, 'responseStarted');
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

let serverOptions;
class TransportServer {
    constructor(options) { serverOptions = options; }
    close() {}
}
const transport = createPassportTransport({WebSocketServer: TransportServer, config: {}});
const maximumAudio = Buffer.alloc(480058).toString('base64');
const question = {type: 'req', id: 'long-question', method: 'chat.send',
    params: {sessionKey: transcriptSession, attachments: [{type: 'audio', content: maximumAudio}]}};
assert.ok(Buffer.byteLength(JSON.stringify(question)) < serverOptions.maxPayload);
assert.equal(serverOptions.maxPayload, 660000);
transport.stop();
for (const limit of [undefined, 100000, 2000000]) {
    const current = fixture();
    current.connection.upstream({type: 'event', event: 'connect.challenge', payload: {}});
    current.connection.client(structuredClone(signedConnect));
    current.connection.upstream({type: 'res', id: signedConnect.id, ok: true, payload: {
        policy: {maxPayload: limit, attachments: {maxBytes: 480058}}, auth: {scopes: ['operator.read', 'operator.write']}}});
    const granted = current.client.at(-1).payload;
    assert.equal(granted.passportAsk, true);
    assert.equal(granted.policy.maxPayload, limit === 100000 ? 100000 : 660000);
    assert.equal(granted.policy.attachments.maxBytes, 480058);
    if (limit !== 100000) {
        current.connection.client(question);
        assert.equal(current.upstream.at(-1).params.attachments[0].content, maximumAudio);
    }
    assert.equal(current.failures.length, 0);
    current.connection.stop('finished');
}
console.log('Passport shared transport: bounded full-length Ask uploads and upstream policy limits PASS');

import {withPassportVoiceInstructions, withPassportVoiceLanguage} from './passport-relay.mjs';

export const PASSPORT_PROVIDER = 'passport-realtime';
export const PASSPORT_PATH = '/passport/v1';
export const PASSPORT_PROTOCOL = 1;
export const PASSPORT_END_CALL = 'passport_end_call';

const endCallTool = {
    type: 'function',
    name: PASSPORT_END_CALL,
    description: 'End this AI Passport voice call immediately when the user clearly asks to hang up or stop talking. ' +
        'This closes only the current call, not the gateway or other sessions. ' +
        'Do not use for cancelling a task, pausing, muting, quoted examples, or requests not to hang up.',
    parameters: {type: 'object', properties: {}, required: []}
};

const errorKinds = [
    ['truncate', /audio content of \d+ms is already shorter than|conversation\.item\.truncate|audio_end_ms/i],
    ['cancel', /cancellation failed|response\.cancel/i],
    ['active-response', /already has an active response/i],
    ['buffer', /input audio buffer|buffer too small|buffer is empty/i],
    ['item', /item with id|item_id|does not exist|not found/i],
    ['session-limit', /maximum duration|session.*expired/i],
    ['rate-limit', /rate limit|quota|429/i],
    ['auth', /api key|unauthorized|401|403/i],
    ['network', /timeout|timed out|econn|enotfound|socket|network|closed/i]
];

export function passportErrorKind(error) {
    const message = typeof error === 'string' ? error : typeof error?.message === 'string' ? error.message : '';
    return errorKinds.find(([, pattern]) => pattern.test(message))?.[0] ?? 'other';
}

export function createPassportProvider(getProvider, upstreamId = 'openai', diagnostic = () => {}) {
    if (!upstreamId || upstreamId === PASSPORT_PROVIDER) throw new Error('Invalid Passport upstream provider');
    function upstream(cfg) {
        const provider = getProvider(upstreamId, cfg);
        if (!provider || typeof provider.createBridge !== 'function' || typeof provider.isConfigured !== 'function')
            throw new Error('Passport upstream realtime provider is unavailable; run the Passport doctor');
        return provider;
    }
    return {
        id: PASSPORT_PROVIDER,
        label: 'AI Passport Realtime',
        capabilities: {
            transports: ['gateway-relay'],
            inputAudioFormats: [{encoding: 'pcm16', sampleRateHz: 24000, channels: 1}],
            outputAudioFormats: [{encoding: 'pcm16', sampleRateHz: 24000, channels: 1}],
            supportsToolCalls: true,
            supportsBargeIn: true,
            handlesInputAudioBargeIn: true
        },
        resolveConfig(context) {
            if (context.cfg?.talk?.realtime?.consultRouting === 'force-agent-consult')
                throw new Error('Passport requires native provider-direct consult routing');
            const provider = upstream(context.cfg);
            const native = context.cfg?.talk?.realtime?.providers?.[upstreamId] ?? {};
            const rawConfig = {...native, ...context.rawConfig};
            return provider.resolveConfig?.({...context, rawConfig, autoRespondToAudio: true}) ?? rawConfig;
        },
        isConfigured(context) { return upstream(context.cfg).isConfigured(context); },
        createBridge(request) {
            const provider = upstream(request.cfg);
            let ending = false, ended = false, closeCompletion, closeReason = 'completed';
            let bridge;
            function notifyClosed(reason) {
                if (ended) return;
                ended = true;
                request.onClose?.(reason);
            }
            function closeBridge(options) {
                if (ending) return closeCompletion;
                ending = true;
                closeCompletion = Promise.resolve().then(() => bridge.close(options)).then(() => notifyClosed(closeReason), error => {
                    try { request.onError?.(error); }
                    finally { notifyClosed('error'); }
                    throw error;
                });
                return closeCompletion;
            }
            const callbacks = Object.fromEntries(['onAudio', 'onClearAudio', 'onMark',
                'onReady'].filter(name => typeof request[name] === 'function')
                .map(name => [name, (...args) => { if (!ending && !ended) return request[name](...args); }]));
            function report(type, detail = {}, extra = {}) {
                const fields = Object.fromEntries(['status', 'reason', 'code', 'type'].map(key =>
                    [key, detail[key] ?? detail.error?.[key]]).filter(([, value]) =>
                    typeof value === 'string' && /^[a-zA-Z0-9_.-]{1,80}$/.test(value)));
                try { diagnostic({event: type, ...fields, ...extra}); } catch {}
            }
            // Whether the upstream still owes a terminal event for a reply, as told by its own
            // response lifecycle events. Other upstreams qualify once they report a reply this way.
            let replyPending = false, lifecycleKnown = upstreamId === 'openai';
            function trackReply(event) {
                if (event?.direction === 'server' && event.type === 'response.created') replyPending = lifecycleKnown = true;
                else if (event?.direction === 'client' && event.type === 'response.create') replyPending = true;
                else if (event?.direction === 'server' && ['response.done', 'response.cancelled'].includes(event.type))
                    replyPending = false;
            }
            bridge = provider.createBridge({
                ...request,
                ...callbacks,
                autoRespondToAudio: true,
                interruptResponseOnInputAudio: false,
                providerConfig: {...request.providerConfig, interruptResponseOnInputAudio: false},
                instructions: withPassportVoiceInstructions(request.instructions, request.language, true, true),
                tools: [...(request.tools ?? []).filter(tool => tool.name !== PASSPORT_END_CALL), endCallTool],
                onResponseDone(outcome) {
                    replyPending = false;
                    if (ending || ended) return;
                    report('response.done', outcome);
                    return request.onResponseDone?.(outcome);
                },
                onEvent(event) {
                    trackReply(event);
                    if (ending || ended) return;
                    if (['error', 'input_audio_buffer.speech_started', 'session.reconnect.scheduled',
                        'session.reconnect.ready'].includes(event.type)) report(event.type);
                    return request.onEvent?.(event);
                },
                onError(error) {
                    if (ending || ended) return;
                    report('provider.error', error, {kind: passportErrorKind(error)});
                    return request.onError?.(error);
                },
                onToolCall(event) {
                    if (ending || ended) return;
                    if (event.name !== PASSPORT_END_CALL) return request.onToolCall?.(event);
                    closeBridge().catch(() => {});
                },
                onTranscript(...args) { if (!ended && (!ending || args[2])) return request.onTranscript?.(...args); },
                onClose(reason) {
                    if (reason === 'error') closeReason = reason;
                    if (!ending) notifyClosed(reason);
                }
            });
            for (const method of ['connect', 'sendAudio', 'submitToolResult', 'acknowledgeMark', 'handleBargeIn', 'close', 'isConnected'])
                if (typeof bridge[method] !== 'function') throw new Error(`Passport bridge contract changed: ${method}`);
            return new Proxy(bridge, {
                get(target, property) {
                    const value = Reflect.get(target, property, target);
                    if (property === 'close') return closeBridge;
                    if (property === 'isConnected') return () => !ending && !ended && value.call(target);
                    if (property === 'sendAudio') return (...args) => { if (!ending && !ended) return value.apply(target, args); };
                    if (property === 'handleBargeIn') return options => {
                        if (ending || ended) return;
                        const owed = replyPending;
                        const result = value.call(target, {...options, force: true});
                        if (lifecycleKnown && !owed && !replyPending) {
                            // No reply is being generated: the last one already finished and only its
                            // audio is still playing, or none has started. The upstream has nothing to
                            // cancel and never sends the terminal event the gateway waits for, so the
                            // gateway would close the call 30 seconds later. Settle it here instead.
                            report('cancel.settled');
                            request.onEvent?.({direction: 'server', type: 'response.cancelled'});
                        }
                        return result;
                    };
                    if (property === 'sendUserMessage' && typeof value === 'function')
                        return (text, options) => {
                            if (!ending && !ended) return value.call(target, withPassportVoiceLanguage(text, request.language, true), options);
                        };
                    return typeof value === 'function' ? value.bind(target) : value;
                }
            });
        }
    };
}

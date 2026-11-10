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

export function createPassportProvider(getProvider, upstreamId = 'openai') {
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
                'onResponseDone', 'onReady', 'onEvent', 'onError'].filter(name => typeof request[name] === 'function')
                .map(name => [name, (...args) => { if (!ending && !ended) return request[name](...args); }]));
            bridge = provider.createBridge({
                ...request,
                ...callbacks,
                autoRespondToAudio: true,
                instructions: withPassportVoiceInstructions(request.instructions, request.language, true, true),
                tools: [...(request.tools ?? []).filter(tool => tool.name !== PASSPORT_END_CALL), endCallTool],
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
                        if (!ending && !ended) return value.call(target, {...options, force: true});
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

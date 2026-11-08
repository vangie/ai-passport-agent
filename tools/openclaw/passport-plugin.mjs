import {withPassportVoiceInstructions, withPassportVoiceLanguage} from './passport-relay.mjs';

export const PASSPORT_PROVIDER = 'passport-realtime';
export const PASSPORT_PATH = '/passport/v1';
export const PASSPORT_PROTOCOL = 1;

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
            const bridge = provider.createBridge({
                ...request,
                autoRespondToAudio: true,
                instructions: withPassportVoiceInstructions(request.instructions, request.language, true)
            });
            for (const method of ['connect', 'sendAudio', 'submitToolResult', 'acknowledgeMark', 'close', 'isConnected'])
                if (typeof bridge[method] !== 'function') throw new Error(`Passport bridge contract changed: ${method}`);
            return new Proxy(bridge, {
                get(target, property) {
                    const value = Reflect.get(target, property, target);
                    if (property === 'sendUserMessage' && typeof value === 'function')
                        return (text, options) => value.call(target, withPassportVoiceLanguage(text, request.language, true), options);
                    return typeof value === 'function' ? value.bind(target) : value;
                }
            });
        }
    };
}

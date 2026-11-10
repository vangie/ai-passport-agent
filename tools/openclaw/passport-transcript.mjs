const MAX_ENTRIES = 32;
const MAX_AGE_MS = 10 * 60 * 1000;
const MAX_TEXT_BYTES = 479;
const validId = value => typeof value === 'string' && /^[A-Za-z0-9_-]{1,79}$/.test(value);
const validSession = value => typeof value === 'string' && /^agent:[^:]+:passport-[A-Za-z0-9_-]+$/.test(value);

export function createPassportTranscripts({now = Date.now} = {}) {
    const entries = new Map();
    function prune() {
        for (const [key, entry] of entries) if (now() - entry.created >= MAX_AGE_MS) entries.delete(key);
    }
    function capture(event, context) {
        if (!validSession(context?.sessionKey) || !event ||
            (event.currentUserMessage !== undefined && event.currentUserMessage !== '')) return;
        const candidate = typeof event.currentUserMessageId === 'string'
            ? event.currentUserMessageId.replace(/:user$/, '') : undefined;
        const requestId = validId(candidate) ? candidate : undefined;
        const runId = validId(context.runId) ? context.runId : undefined;
        if (!validId(requestId) && !validId(runId)) return;
        if (typeof event.prompt !== 'string') return;
        const match = event.prompt.match(/^(?:\[[^\n]+\] )?\[Audio\]\n(User text:\n\[media attached: media:\/\/inbound\/passport---[A-Za-z0-9_-]+\.wav\]\n)?Transcript:\n([^]*?)(?=\n\n\[|$)/);
        if (!match) return;
        if (event.currentUserMessage === undefined && (!match[1] || !runId)) return;
        const source = match[2].trim();
        if (!source || source.startsWith('[Voice note could not be transcribed')) return;
        let text = '';
        for (const character of source) {
            if (Buffer.byteLength(text) + Buffer.byteLength(character) > MAX_TEXT_BYTES) break;
            text += character;
        }
        prune();
        const key = `${context.sessionKey}\n${requestId ?? runId}`;
        if (entries.has(key)) return;
        if (entries.size >= MAX_ENTRIES) entries.delete(entries.keys().next().value);
        entries.set(key, {sessionKey: context.sessionKey, requestId, runId, text, created: now()});
    }
    function lookup(params) {
        prune();
        if (!validSession(params?.sessionKey) || !validId(params.runId) || !validId(params.requestId)) return;
        for (const entry of entries.values()) {
            if (entry.sessionKey === params.sessionKey &&
                (!entry.requestId || entry.requestId === params.requestId) &&
                (!entry.runId || entry.runId === params.runId)) return entry.text;
        }
    }
    return {capture, lookup, clear() { entries.clear(); }};
}

export function registerPassportTranscripts(api) {
    if (typeof api.on !== 'function') throw new Error('OpenClaw prompt hooks are unavailable; run the Passport doctor');
    const transcripts = createPassportTranscripts();
    api.on('before_prompt_build', transcripts.capture);
    api.on('llm_input', (event, context) => {
        transcripts.capture(event, {...context, runId: context.runId ?? event.runId});
    });
    api.registerGatewayMethod('passport.transcript', ({params, respond}) => {
        const text = transcripts.lookup(params);
        respond(true, {runId: params?.runId, requestId: params?.requestId, ...(text ? {text} : {})});
    }, {scope: 'operator.write', profileAccess: 'required', sessionAccess: {mode: 'write', allowOwnSessionScope: true}});
    api.on('gateway_stop', () => transcripts.clear());
    return transcripts;
}

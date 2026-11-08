import {encodeIma, decodeIma} from './passport-ima.mjs';

export function resolvePassportConsultRouting(consultRouting, passportClient = false) {
    const forceAgentConsultOnFinalTranscript = !passportClient && consultRouting === 'force-agent-consult';
    return {autoRespondToAudio: !forceAgentConsultOnFinalTranscript, forceAgentConsultOnFinalTranscript};
}

export function withPassportVoiceInstructions(instructions, language, enabled = false) {
    if (!enabled) return instructions;
    const routing = '# AI Passport realtime conversation\n' +
        'Use the configured OpenClaw agent identity and persona above. ' +
        'For this call, answer greetings, ordinary conversation and general knowledge directly from live audio. ' +
        'Do not wait for a final input transcript or consult OpenClaw on every turn. ' +
        'Call openclaw_agent_consult for current or external information such as weather, tools or actions, ' +
        'files or workspace state, private memory, other sessions, or work that needs deeper delegated reasoning. ' +
        'Include the concrete request and relevant context from this call in the consult arguments, ' +
        'because the agent may not have the preceding direct conversation. ' +
        'Never invent live facts, private memories or action results; report tool failures honestly. ' +
        'Retain existing tool permissions and confirmation requirements. ' +
        'Use openclaw_agent_control for progress, cancellation or redirection of active work. ' +
        'While waiting, give at most one brief acknowledgement, then use the authoritative tool result. ' +
        'If speech is ambiguous, ask a short clarification rather than silently changing the question. ' +
        'Keep spoken answers concise and natural. This call-specific routing replaces any instruction ' +
        'to consult before every substantive reply, not the agent identity or security rules.';
    return withPassportVoiceLanguage(instructions ? `${instructions}\n\n${routing}` : routing, language, true);
}

export function withPassportVoiceLanguage(instructions, language, enabled = false) {
    if (!enabled || typeof language !== 'string') return instructions;
    const locale = language.trim().toLowerCase();
    let policy;
    if (/^zh(?:-|$)/.test(locale)) {
        policy = '# AI Passport 通话语言\n默认用自然、简洁的普通话与用户交谈，字幕使用简体中文。' +
            '确认、等待提示、错误解释及最终回复都使用中文。' +
            '即使工具结果、引用的历史记录或内部提示是英文，也要用中文说明结果，不要因此切换语言。' +
            '专有名词和代码可保留原文；只有用户明确要求换语言或翻译时才按要求切换。';
    } else if (/^en(?:-|$)/.test(locale)) {
        policy = '# AI Passport call language\nSpeak natural, concise English and use English captions by default. ' +
            'Use English for acknowledgements, waiting notices, errors and final replies, including summaries of tools or history in other languages. ' +
            'Preserve proper names and code; switch languages only when the user explicitly asks to switch or translate.';
    } else return instructions;
    return instructions ? `${instructions}\n\n${policy}` : policy;
}

export function createPassportRelayOutput({send, enabled = false, ima = false, fail, now = Date.now,
    schedule = setTimeout, cancel = clearTimeout, leadMs = ima ? 800 : 160, maxBytes = 6 * 1024 * 1024}) {
    let queue = [], head = 0, timer, batchTimer, due = 0, bytes = 0, closed = false, sequence = 0;
    let batch = [], batchBytes = 0, batchEvent;
    const codec = {index: 0};
    const stats = {frames: 0, peakBytes: 0, overflows: 0};
    function clear() {
        if (timer !== undefined) cancel(timer);
        if (batchTimer !== undefined) cancel(batchTimer);
        timer = undefined;
        batchTimer = undefined;
        batch = []; batchBytes = 0; batchEvent = undefined;
        queue = []; head = 0; bytes = 0; due = 0;
        codec.index = 0;
    }
    function project(event) {
        if (!enabled) return event;
        if (event.type === 'inputAudio') return null;
        const {talkEvent, ...compact} = event;
        if (event.type === 'toolResult' && talkEvent) {
            compact.talkEvent = {type: talkEvent.type, final: talkEvent.final};
        }
        return compact;
    }
    function drain() {
        timer = undefined;
        while (!closed && head < queue.length) {
            const item = queue[head];
            const wait = due - now() - leadMs;
            if (wait > 0) { timer = schedule(drain, Math.ceil(wait)); return; }
            head++;
            bytes -= item.bytes;
            send(item.event);
            if (item.bytes) {
                due = Math.max(due, now()) + item.bytes / 48;
                stats.frames++;
            }
        }
        if (head === queue.length) { queue = []; head = 0; }
    }
    function enqueue(event, size) {
        if (bytes + size > maxBytes || queue.length - head >= 20000) {
            stats.overflows++; clear(); closed = true;
            fail(new Error('Passport reply playback queue exceeded its bound'));
            return;
        }
        if (head > 512) { queue = queue.slice(head); head = 0; }
        queue.push({event: {...event, playbackSequence: ++sequence}, bytes: size}); bytes += size;
        stats.peakBytes = Math.max(stats.peakBytes, bytes);
        if (timer === undefined) drain();
    }
    function flush() {
        if (batchTimer !== undefined) cancel(batchTimer);
        batchTimer = undefined;
        if (!batchBytes || closed) return;
        const pcm = Buffer.concat(batch, batchBytes);
        const event = {...batchEvent, audioBase64: ima ? 'passport-ima:' + encodeIma(pcm, codec).toString('base64') : pcm.toString('base64')};
        const size = batchBytes;
        batch = []; batchBytes = 0; batchEvent = undefined;
        enqueue(event, size);
    }
    return {
        project,
        decodeAudio(encoded) {
            if (enabled && ima && encoded.startsWith('passport-ima:')) {
                const payload = encoded.slice(13);
                if (!payload.length || payload.length > 1312 || payload.length % 4 || !/^[A-Za-z0-9+/]+={0,2}$/.test(payload))
                    throw new Error('Invalid Passport IMA base64');
                return decodeIma(Buffer.from(payload, 'base64'));
            }
            if (!enabled || !encoded.startsWith('passport-silence:')) return undefined;
            const match = /^passport-silence:([0-9]+)$/.exec(encoded);
            const count = Number(match?.[1]);
            if (!Number.isInteger(count) || count < 2 || count > 3840 || count % 2)
                throw new Error('Invalid Passport silent PCM length');
            return Buffer.alloc(count);
        },
        send(event) {
            if (closed) return;
            if (!enabled) { send(event); return; }
            event = project(event);
            if (!event) return;
            if (event.type === 'clear') clear();
            const ordered = ['responseStarted', 'audio', 'mark', 'audioDone'].includes(event.type) ||
                event.type === 'transcript' && event.role === 'assistant';
            if (!ordered) { send(event); return; }
            if (event.type === 'audio') {
                const pcm = Buffer.from(event.audioBase64, 'base64');
                if (batchEvent && ['relaySessionId', 'itemId', 'responseId'].some(key => batchEvent[key] !== event[key])) flush();
                for (let offset = 0; !closed && offset < pcm.length;) {
                    const count = Math.min(3840 - batchBytes, pcm.length - offset);
                    if (bytes + batchBytes + count > maxBytes) {
                        stats.overflows++; clear(); closed = true;
                        fail(new Error('Passport reply playback queue exceeded its bound'));
                        break;
                    }
                    batchEvent ??= event;
                    batch.push(pcm.subarray(offset, offset + count)); batchBytes += count; offset += count;
                    if (batchBytes === 3840) flush();
                }
                if (!closed && batchBytes && batchTimer === undefined) batchTimer = schedule(flush, 20);
            } else {
                flush();
                if (!closed) enqueue(event, 0);
            }
        },
        close() { closed = true; clear(); },
        diagnostics() { return {...stats, queuedBytes: bytes + batchBytes, queuedEvents: queue.length - head + Number(batchBytes > 0)}; }
    };
}

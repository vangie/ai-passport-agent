import assert from 'node:assert/strict';
import {createPassportRelayOutput, resolvePassportConsultRouting, withPassportVoiceInstructions, withPassportVoiceLanguage} from '../tools/openclaw/passport-relay.mjs';
import {decodeIma, encodeIma} from '../tools/openclaw/passport-ima.mjs';

const baseInstructions = 'Keep existing agent instructions and permissions.';
for (const consultRouting of [undefined, 'auto', 'force-agent-consult']) {
    assert.deepEqual(resolvePassportConsultRouting(consultRouting, true),
        {autoRespondToAudio: true, forceAgentConsultOnFinalTranscript: false});
    const forced = consultRouting === 'force-agent-consult';
    assert.deepEqual(resolvePassportConsultRouting(consultRouting),
        {autoRespondToAudio: !forced, forceAgentConsultOnFinalTranscript: forced});
    assert.deepEqual(resolvePassportConsultRouting(consultRouting, false), resolvePassportConsultRouting(consultRouting));
}
for (const language of ['zh', 'en', undefined, 'fr']) {
    assert.equal(withPassportVoiceInstructions(baseInstructions, language), baseInstructions);
    const instructions = withPassportVoiceInstructions(baseInstructions, language, true);
    assert(instructions.startsWith(baseInstructions + '\n\n'));
    assert(instructions.includes('general knowledge directly from live audio'));
    assert(instructions.includes('openclaw_agent_consult') && instructions.includes('weather'));
    assert(instructions.includes('private memory') && instructions.includes('relevant context from this call'));
    assert(instructions.includes('openclaw_agent_control') && instructions.includes('confirmation requirements'));
    assert(!instructions.includes('passport_end_call'));
    const callInstructions = withPassportVoiceInstructions(baseInstructions, language, true, true);
    assert(callInstructions.includes('passport_end_call') && callInstructions.includes('我不想聊了'));
    assert(callInstructions.includes('requests not to hang up do not end the call'));
    assert(instructions.includes('Never invent live facts') && instructions.includes('clarification'));
}
assert(withPassportVoiceInstructions(undefined, 'zh', true).includes('普通话'));
assert(withPassportVoiceInstructions(baseInstructions, 'en', true).includes('Speak natural, concise English'));
console.log('Passport direct audio: native replies, optional consults, retained identity/authority and non-Passport routing PASS');
for (const language of ['zh', 'zh-CN', 'ZH-tw', 'en', 'en-US', 'fr', undefined]) {
    assert.equal(withPassportVoiceLanguage(baseInstructions, language), baseInstructions);
    assert.equal(withPassportVoiceLanguage(undefined, language, false), undefined);
}
for (const language of ['zh', 'zh-CN', ' ZH-tw ']) {
    const instructions = withPassportVoiceLanguage(baseInstructions, language, true);
    assert(instructions.startsWith(baseInstructions + '\n\n'));
    assert(instructions.includes('普通话') && instructions.includes('简体中文'));
    assert(instructions.includes('工具结果') && instructions.includes('错误解释'));
    assert(instructions.includes('用户明确要求'));
}
for (const prompt of ['Briefly tell the person that you are checking with OpenClaw.', 'Speak this result naturally.', 'Reply in a concise spoken tone.']) {
    assert(withPassportVoiceLanguage(prompt, 'zh', true).endsWith('才按要求切换。'));
}
assert(withPassportVoiceLanguage(undefined, 'zh', true).startsWith('# AI Passport'));
assert(withPassportVoiceLanguage(baseInstructions, 'en-US', true).includes('Speak natural, concise English'));
for (const language of [undefined, null, 1, 'fr', 'zh<script>', 'english'])
    assert.equal(withPassportVoiceLanguage(baseInstructions, language, true), baseInstructions);
console.log('Passport voice language: initial policy, tool summaries, waiting notices, locale mapping and non-Passport isolation PASS');

function fixture(options = {}) {
    let clock = 0, next = 0;
    const timers = new Map(), events = [], failures = [];
    const output = createPassportRelayOutput({
        enabled: true, now: () => clock,
        schedule: (callback, delay) => { const handle = ++next; timers.set(handle, {callback, at: clock + delay}); return handle; },
        cancel: handle => timers.delete(handle),
        send: event => events.push({at: clock, event}), fail: error => failures.push(error), ...options
    });
    function advance(target) {
        for (;;) {
            const [handle, timer] = [...timers].sort((left, right) => left[1].at - right[1].at)[0] ?? [];
            if (!timer || timer.at > target) break;
            clock = timer.at; timers.delete(handle); timer.callback();
        }
        clock = target;
    }
    return {output, events, failures, timers, advance};
}

const audio = {type: 'audio', audioBase64: Buffer.alloc(960).toString('base64')};
const toolLifecycle = fixture();
const toolProgress = {
    relaySessionId: 'news-call', type: 'toolResult', callId: 'news-consult',
    talkEvent: {type: 'tool.progress', seq: 31, payload: {status: 'working'}}
};
assert.deepEqual(toolLifecycle.output.project(toolProgress), {
    relaySessionId: 'news-call', type: 'toolResult', callId: 'news-consult',
    talkEvent: {type: 'tool.progress', final: undefined}
});
toolLifecycle.output.send(toolProgress);
toolLifecycle.advance(33000);
toolLifecycle.output.send({...toolProgress, talkEvent: {type: 'tool.result', final: false, payload: {result: 'partial'}}});
toolLifecycle.output.send({...toolProgress, talkEvent: {type: 'tool.result', final: true, payload: {result: 'complete'}}});
const wireResults = toolLifecycle.events.map(item => JSON.parse(JSON.stringify(item.event)));
const terminalResults = wireResults.filter(event => event.talkEvent?.final !== false && event.talkEvent?.type !== 'tool.progress');
assert.equal(terminalResults.length, 1);
assert.equal(terminalResults[0].talkEvent.final, true);
assert.equal(wireResults[0].talkEvent.type, 'tool.progress');
assert.equal(wireResults[1].talkEvent.final, false);
assert(wireResults.every(event => !('payload' in event.talkEvent) && !('seq' in event.talkEvent)));
const nativeToolLifecycle = fixture({enabled: false});
assert.deepEqual(nativeToolLifecycle.output.project(toolProgress), toolProgress);
console.log('Passport tool lifecycle: working and partial results remain pending, final completes, metadata stays compact PASS');
const test = fixture();
const expected = [];
for (let frame = 0; frame < 1000; frame++) {
    const pcm = Buffer.alloc(960, frame % 256);
    expected.push(pcm);
    test.output.send({...audio, audioBase64: pcm.toString('base64'), talkEvent: {seq: frame}});
    if (!(frame % 10)) test.output.send({type: 'mark', frame});
}
test.output.send({type: 'audioDone'});
test.output.send({type: 'inputAudio'});
test.output.send({type: 'append-ack'});
assert(test.events.filter(item => item.event.type === 'audio').length <= 4);
assert.equal(test.events.at(-1).event.type, 'append-ack');
assert(!test.events.some(item => item.event.type === 'audioDone'));
test.advance(21000);
const received = test.events.filter(item => item.event.type === 'audio');
assert(received.length <= 302);
assert.deepEqual(Buffer.concat(received.map(item => Buffer.from(item.event.audioBase64, 'base64'))), Buffer.concat(expected));
assert.equal(test.events.filter(item => item.event.type === 'mark').length, 100);
assert.equal(test.events.at(-1).event.type, 'audioDone');
const sequenced = test.events.filter(item => item.event.playbackSequence);
assert(sequenced.every((item, index) => item.event.playbackSequence === index + 1));
let sentBytes = 0;
for (const item of received) {
    assert(item.at >= sentBytes / 48 - 160);
    assert(item.at <= sentBytes / 48);
    assert(!item.event.talkEvent);
    const count = Buffer.byteLength(item.event.audioBase64, 'base64');
    assert(count <= 3840);
    sentBytes += count;
}
assert(!test.events.some(item => item.event.type === 'inputAudio'));
assert.equal(test.output.diagnostics().queuedBytes, 0);
assert.equal(test.timers.size, 0);
assert.equal(test.failures.length, 0);

for (const ending of ['clear', 'close', 'discard']) {
    const current = fixture();
    for (let frame = 0; frame < 50; frame++) current.output.send(audio);
    current.output.send({type: 'mark'});
    current.output.send({type: 'audioDone'});
    if (ending === 'clear') current.output.send({type: 'clear'});
    else if (ending === 'discard') current.output.discardOutput();
    else current.output.close();
    const count = current.events.length;
    current.advance(10000);
    assert.equal(current.events.length, count);
    assert.equal(current.timers.size, 0);
    assert.equal(current.output.diagnostics().queuedBytes, 0);
    current.output.send(audio);
    current.advance(10020);
    assert.equal(current.events.length, count + (ending !== 'close' ? 1 : 0));
}

const bounded = fixture({maxBytes: 1920});
for (let frame = 0; frame < 20; frame++) bounded.output.send(audio);
assert.equal(bounded.failures.length, 1);
assert.equal(bounded.timers.size, 0);
assert.equal(bounded.output.diagnostics().queuedBytes, 0);
const unchanged = fixture({enabled: false});
for (let frame = 0; frame < 50; frame++) unchanged.output.send(audio);
assert.equal(unchanged.events.length, 50);
assert.equal(unchanged.timers.size, 0);
assert.deepEqual(unchanged.output.project({type: 'inputAudio', talkEvent: {seq: 1}}), {type: 'inputAudio', talkEvent: {seq: 1}});
const tail = fixture();
tail.output.send(audio);
assert.equal(tail.events.length, 0);
tail.advance(20);
assert.equal(tail.events.length, 1);
assert.equal(Buffer.byteLength(tail.events[0].event.audioBase64, 'base64'), 960);
for (const ending of ['clear', 'close']) {
    const partial = fixture(); partial.output.send(audio);
    if (ending === 'clear') partial.output.send({type: 'clear'});
    else partial.output.close();
    partial.advance(1000);
    assert(!partial.events.some(item => item.event.type === 'audio'));
    assert.equal(partial.timers.size, 0);
}
const boundary = fixture();
boundary.output.send({...audio, itemId: 'first'});
boundary.output.send({...audio, itemId: 'second'});
boundary.output.send({type: 'mark'});
assert.deepEqual(boundary.events.filter(item => item.event.type === 'audio').map(item => item.event.itemId), ['first', 'second']);
console.log('Passport relay: lossless 80ms batching, 20ms tail, compact wire, paced burst, ACK bypass, boundaries, cancellation and bounds PASS');
assert.deepEqual(boundary.output.decodeAudio('passport-silence:3840'), Buffer.alloc(3840));
assert.equal(boundary.output.decodeAudio('AQIDBA=='), undefined);
assert.equal(unchanged.output.decodeAudio('passport-silence:3840'), undefined);
for (const invalid of ['0', '1', '3842', 'Infinity', '-2', '2junk', ''])
    assert.throws(() => boundary.output.decodeAudio('passport-silence:' + invalid));

const compressed = fixture({ima: true});
const source = Buffer.alloc(3840);
for (let sample = 0; sample < 1920; sample++) source.writeInt16LE(Math.round(8000 * Math.sin(sample / 9)), sample * 2);
for (let frame = 0; frame < 20; frame++) compressed.output.send({type: 'audio', audioBase64: source.toString('base64')});
compressed.output.send({type: 'audioDone'});
compressed.output.send({type: 'append-ack'});
assert.equal(compressed.events.at(-1).event.type, 'append-ack');
assert(compressed.events.filter(item => item.event.type === 'audio').length <= 11);
compressed.advance(1800);
let compressedBytes = 0;
for (const item of compressed.events.filter(item => item.event.type === 'audio')) {
    assert(item.event.audioBase64.startsWith('passport-ima:'));
    const encoded = Buffer.from(item.event.audioBase64.slice(13), 'base64');
    assert.equal(encoded.length, 984);
    assert.equal(decodeIma(encoded).length, 3840);
    assert(item.at >= compressedBytes / 48 - 800);
    compressedBytes += 3840;
}
assert.equal(compressedBytes, 20 * 3840);
assert.equal(compressed.events.at(-1).event.type, 'audioDone');
const encoded = encodeIma(source).toString('base64');
assert.deepEqual(compressed.output.decodeAudio('passport-ima:' + encoded), decodeIma(Buffer.from(encoded, 'base64')));
for (const invalid of ['', '?', 'AAAA', 'a'.repeat(1316)])
    assert.throws(() => compressed.output.decodeAudio('passport-ima:' + invalid));
assert.equal(boundary.output.decodeAudio('passport-ima:' + encoded), undefined);
compressed.output.close();
assert.equal(compressed.timers.size, 0);
assert.equal(compressed.failures.length, 0);
const compressedCancel = fixture({ima: true});
for (let frame = 0; frame < 40; frame++) compressedCancel.output.send({...audio, audioBase64: source.toString('base64')});
assert(compressedCancel.output.diagnostics().queuedBytes > 0);
compressedCancel.output.send({type: 'clear'});
const countAfterClear = compressedCancel.events.length;
compressedCancel.advance(10000);
assert.equal(compressedCancel.events.length, countAfterClear);
assert.equal(compressedCancel.output.diagnostics().queuedBytes, 0);
assert.equal(compressedCancel.timers.size, 0);
console.log('Passport compressed relay: negotiation, 4x wire reduction, bounded pacing, PCM restoration and ACK bypass PASS');

const steps = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,
    88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,
    598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,
    2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,
    11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767];
const indices = [-1,-1,-1,-1,2,4,6,8];

function update(code, state) {
    const step = steps[state.index];
    const change = (step >> 3) + (code & 1 ? step >> 2 : 0) + (code & 2 ? step >> 1 : 0) + (code & 4 ? step : 0);
    state.predictor = Math.max(-32768, Math.min(32767, state.predictor + (code & 8 ? -change : change)));
    state.index = Math.max(0, Math.min(88, state.index + indices[code & 7]));
    return state.predictor;
}

export function encodeIma(pcm, state = {index: 0}) {
    if (!pcm.length || pcm.length > 3840 || pcm.length % 2 || !Number.isInteger(state.index) || state.index < 0 || state.index > 88)
        throw new Error('Invalid Passport IMA PCM');
    const blocks = [];
    for (let offset = 0; offset < pcm.length; offset += 960) {
        const count = Math.min(480, (pcm.length - offset) / 2);
        const block = Buffer.alloc(6 + Math.floor(count / 2));
        state.predictor = pcm.readInt16LE(offset);
        block.writeInt16LE(state.predictor); block[2] = state.index; block.writeUInt16LE(count, 4);
        for (let sample = 1; sample < count; sample++) {
            let delta = pcm.readInt16LE(offset + sample * 2) - state.predictor;
            let code = delta < 0 ? 8 : 0;
            delta = Math.abs(delta);
            const step = steps[state.index];
            if (delta >= step) { code |= 4; delta -= step; }
            if (delta >= (step >> 1)) { code |= 2; delta -= step >> 1; }
            if (delta >= (step >> 2)) code |= 1;
            update(code, state);
            block[6 + ((sample - 1) >> 1)] |= sample & 1 ? code : code << 4;
        }
        blocks.push(block);
    }
    return Buffer.concat(blocks);
}

export function decodeIma(encoded) {
    if (!encoded.length || encoded.length > 992) throw new Error('Invalid Passport IMA frame');
    const blocks = [];
    let total = 0;
    for (let offset = 0; offset < encoded.length;) {
        if (encoded.length - offset < 6) throw new Error('Truncated Passport IMA header');
        const count = encoded.readUInt16LE(offset + 4);
        const bytes = 6 + Math.floor(count / 2);
        if (!count || count > 480 || encoded[offset + 2] > 88 || encoded[offset + 3] || bytes > encoded.length - offset ||
            !(count & 1) && encoded[offset + bytes - 1] & 0xf0 || total + count > 1920)
            throw new Error('Invalid Passport IMA block');
        const pcm = Buffer.alloc(count * 2);
        const state = {predictor: encoded.readInt16LE(offset), index: encoded[offset + 2]};
        pcm.writeInt16LE(state.predictor);
        for (let sample = 1; sample < count; sample++) {
            const packed = encoded[offset + 6 + ((sample - 1) >> 1)];
            pcm.writeInt16LE(update(sample & 1 ? packed & 15 : packed >> 4, state), sample * 2);
        }
        blocks.push(pcm); total += count; offset += bytes;
    }
    return Buffer.concat(blocks);
}

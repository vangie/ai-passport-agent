import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {encodeIma, decodeIma} from '../tools/openclaw/passport-ima.mjs';

for (const samples of [1, 2, 256, 479, 480, 481, 960, 1920]) {
    const pcm = Buffer.alloc(samples * 2);
    for (let sample = 0; sample < samples; sample++) pcm.writeInt16LE(Math.round(9000 * Math.sin(sample * 2 * Math.PI * 440 / 24000)), sample * 2);
    const encoded = encodeIma(pcm);
    const decoded = decodeIma(encoded);
    assert.equal(decoded.length, pcm.length);
    const binary = process.argv[2];
    if (binary) {
        assert.deepEqual(execFileSync(binary, ['encode'], {input: pcm}), encoded);
        assert.deepEqual(execFileSync(binary, ['decode'], {input: encoded}), decoded);
    }
    if (samples === 1920) {
        assert.equal(encoded.length, 984);
        let error = 0, power = 0;
        for (let sample = 120; sample < samples; sample++) {
            const actual = pcm.readInt16LE(sample * 2), received = decoded.readInt16LE(sample * 2);
            error += (actual - received) ** 2; power += actual ** 2;
        }
        assert(10 * Math.log10(power / error) > 25);
    }
    assert.throws(() => decodeIma(encoded.subarray(0, 5)));
    const malformed = Buffer.from(encoded); malformed[2] = 89;
    assert.throws(() => decodeIma(malformed));
}
assert.throws(() => encodeIma(Buffer.alloc(3)));
assert.throws(() => decodeIma(Buffer.alloc(993)));
console.log('Passport IMA: C/JS interoperability, block boundaries, bounds, malformed input and tone quality PASS');

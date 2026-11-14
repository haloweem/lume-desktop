import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createPrivateKey, createPublicKey, createHmac, createDecipheriv, diffieHellman } from 'node:crypto';

// Independent Node crypto check against the RFC 7748 Bob key. No real accounts
// or media URLs are involved; this exercises the bytes emitted by Windows CNG.
const binary = process.argv[2];
assert.ok(binary, 'pass the native music diagnostic executable');
const fixture = JSON.parse(execFileSync(binary, ['--xeapi-fixture'], { encoding: 'utf8' }));
const privateKey = createPrivateKey({ format: 'der', type: 'pkcs8', key: Buffer.from(
  '302e020100300506032b656e042204205dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb', 'hex') });
const peer = bytes => createPublicKey({ format: 'der', type: 'spki', key: Buffer.concat([
  Buffer.from('302a300506032b656e032100', 'hex'), bytes]) });
const shared = bytes => diffieHellman({ privateKey, publicKey: peer(bytes) });
assert.deepEqual(shared(Buffer.from(fixture.public, 'base64')), Buffer.from(fixture.shared, 'base64'));
const hmac = (key, bytes) => createHmac('sha256', key).update(bytes).digest();
const staticKey = Buffer.from('ab1d5a430f6bb04a3f01e81ddd72bd916d5ce591248ac128714806d7f8fb1b84', 'hex');
const ecb = (key, bytes) => {
  const cipher = createDecipheriv(`aes-${key.length * 8}-ecb`, key, null);
  return Buffer.concat([cipher.update(bytes), cipher.final()]);
};
const s = Buffer.from(fixture.envelope.S, 'base64');
const ephemeral = s.subarray(0, 32);
const aesKey = hmac(hmac(Buffer.alloc(32), shared(ephemeral)), Buffer.concat([ephemeral, Buffer.from([1])])).subarray(0, 16);
const cipher = createDecipheriv('aes-128-gcm', aesKey, s.subarray(32, 44));
cipher.setAuthTag(s.subarray(-16));
const session = Buffer.concat([cipher.update(s.subarray(44, -16)), cipher.final()]).toString().split('|');
assert.deepEqual(session.slice(1), ['android', 'fixture']);
const mid = ecb(Buffer.from(session[0], 'base64'), Buffer.from(fixture.envelope.C, 'base64url'));
const random = mid.subarray(0, 16), rotated = mid.subarray(16);
const rotation = (random[0] & 15) % rotated.length;
const unrotated = Buffer.concat([rotated.subarray(rotated.length - rotation), rotated.subarray(0, rotated.length - rotation)]);
for (let i = 0; i < unrotated.length; i++) unrotated[i] ^= random[i & 15];
const plain = JSON.parse(ecb(staticKey, unrotated).toString());
assert.equal(plain.queryString, 'e_r=true');
assert.deepEqual(Object.fromEntries(new URLSearchParams(plain.content)), {encodeType:'flac',ids:'[42]',level:'standard'});
assert.equal(ecb(staticKey, Buffer.from(fixture.envelope.R, 'base64')).toString(), 'fixture|');
console.log('Windows CNG / Node X25519, HKDF, AES-GCM and xeapi envelope interoperability passed');

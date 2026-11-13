import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtemp, readFile, readdir, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { runInNewContext } from 'node:vm';

const root = fileURLToPath(new URL('../', import.meta.url));
const html = await readFile(new URL('../tools/legacy-site/index.html', import.meta.url), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/u)[1];

test('legacy links keep paths, encoded parameters and fragments at the fixed HTTPS origin', () => {
  const locations = [
    ['/', '', ''],
    ['/index.html', '?lang=zh', '#networks'],
    ['/setup/', '?url=wss%3A%2F%2Fgateway.example&code=example%2Bcode', '#pair=example%20fragment'],
    ['/firmware/manifest.json', '', ''],
    ['//other.example/path', '?next=https://other.example/', '#https://other.example/'],
    ['/a%20b/%E8%AE%BE%E7%BD%AE', '?repeat=one&repeat=two', '#%E4%B8%AD%E6%96%87'],
  ];
  for (const [pathname, search, hash] of locations) {
    const link = {};
    let replacement;
    const location = { pathname, search, hash, replace(value) { replacement = value; } };
    const document = { getElementById(id) { assert.equal(id, 'destination'); return link; } };
    runInNewContext(script, { URL, window: { location }, document });
    const actual = new URL(replacement);
    assert.equal(actual.origin, 'https://ai-passport-agent.codelife.me');
    assert.equal(actual.pathname, pathname);
    assert.equal(actual.search, search);
    assert.equal(actual.hash, hash);
    assert.equal(link.href, replacement);
  }
});

test('the fallback is bilingual and sends no referrer or third-party asset requests', () => {
  assert.match(html, /<meta name="referrer" content="no-referrer">/u);
  assert.match(html, /<meta name="robots" content="noindex, follow">/u);
  assert.match(html, /id="destination" href="https:\/\/ai-passport-agent\.codelife\.me\/"/u);
  assert.match(html, /lang="zh-CN"/u);
  assert.match(html, /Your saved device settings are unchanged/u);
  assert.doesNotMatch(html, /<script[^>]+src=|<link|<img|fetch\(|localStorage|sessionStorage|document\.cookie/u);
});

test('the isolated deployment includes an identical deep-link fallback and only redirect files', async () => {
  const output = await mkdtemp(join(tmpdir(), 'passport-legacy-site-'));
  try {
    execFileSync('python3', ['tools/legacy_site.py', '--output', output], { cwd: root });
    assert.deepEqual((await readdir(output)).sort(), ['.nojekyll', '404.html', 'CNAME', 'index.html']);
    assert.equal(await readFile(join(output, 'index.html'), 'utf8'), html);
    assert.equal(await readFile(join(output, '404.html'), 'utf8'), html);
    assert.equal(await readFile(join(output, 'CNAME'), 'utf8'), 'ai-passport-openclaw.codelife.me\n');
    assert.equal(await readFile(join(output, '.nojekyll'), 'utf8'), '');
  } finally {
    await rm(output, { recursive: true, force: true });
  }
});

import { chromium } from '../web/node_modules/playwright/index.mjs';
import { spawn } from 'node:child_process';
import { readFile, writeFile, mkdir, copyFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { extractCommunityCopy, renderCommunityCopy } from './community_copy.mjs';

const root = resolve(import.meta.dirname, '..');
const version = JSON.parse(await readFile(resolve(root, 'tools/openclaw/package.json'), 'utf8')).version;
if (!/^\d+\.\d+\.\d+$/u.test(version)) throw new Error('Invalid materials version');
const output = resolve(root, `build/community-market/v${version}/materials`);
const shots = resolve(root, 'assets/images/screenshots');
const titles = {
  zh: extractCommunityCopy(await readFile(resolve(root, 'docs/release/community.zh_CN.md'), 'utf8'), '标题'),
  en: extractCommunityCopy(await readFile(resolve(root, 'docs/release/community.md'), 'utf8'), 'Title'),
};
const server = spawn(process.execPath, [resolve(root, 'web/node_modules/vite/bin/vite.js'), '--host', '127.0.0.1', '--port', '4189', '--strictPort'], { cwd: resolve(root, 'web'), stdio: ['ignore', 'pipe', 'pipe'] });
await new Promise((accept, reject) => {
  const timeout = setTimeout(() => reject(new Error('Preview server did not start')), 15000);
  server.stdout.on('data', data => { if (data.toString().includes('http://127.0.0.1:4189')) { clearTimeout(timeout); accept(); } });
  server.once('exit', code => { clearTimeout(timeout); reject(new Error(`Preview server exited: ${code}`)); });
});
let browser;
try {
  await mkdir(output, { recursive: true });
  await mkdir(shots, { recursive: true });
  browser = await chromium.launch({ channel: 'chrome', args: ['--disable-gpu'] });
  const page = await browser.newPage({ viewport: { width: 1360, height: 1040 }, locale: 'zh-CN', deviceScaleFactor: 1 });
  await page.route('**/firmware/manifest.json', route => route.fulfill({ json: { schema: 1, version: `v${version}`, commit: 'a'.repeat(40), buildId: 'abcdef1234567890', application: { file: 'agent-talk.bin', size: 3400000, sha256: 'a'.repeat(64), offset: 65536 }, full: { file: 'agent-talk-full.bin', size: 3465536, sha256: 'b'.repeat(64), offset: 0 }, knownBuilds: { abcdef1234567890: { version: `v${version}`, channel: 'release' } } } }));
  await page.goto('http://127.0.0.1:4189/');
  await page.evaluate(async () => {
    const { DemoTransport } = await import('/src/transports.js');
    const fixture = new DemoTransport();
    fixture.networks = ['Passport Home', 'Demo iPhone'];
    fixture.agents = [{ name: '小夏', url: 'wss://gateway.example', current: true }];
    const port = {
      async open() {
        port.readable = new ReadableStream({ start(controller) { fixture.onData = data => controller.enqueue(new TextEncoder().encode(new TextDecoder().decode(data).replace(' zh demo ', ' zh abcdef1234567890 '))); } });
        port.writable = new WritableStream({ write(data) { return fixture.send(new TextDecoder().decode(data).trimEnd()); } });
      },
      async close() {},
    };
    Object.defineProperty(navigator, 'serial', { configurable: true, value: { async requestPort() { return port; } } });
  });
  await page.locator('#connect-usb').click();
  await page.locator('[data-agent]').first().waitFor();
  await page.locator('#agent-card').scrollIntoViewIfNeeded();
  await page.evaluate(() => scrollTo(0, 0));
  await page.screenshot({ path: resolve(shots, 'web-desktop.png'), fullPage: true });
  await page.setViewportSize({ width: 390, height: 844 });
  await page.addInitScript(() => {
    Object.defineProperty(navigator, 'userAgent', { configurable: true, value: 'Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) Bluefy/3.3' });
  });
  await page.reload();
  await page.evaluate(async () => {
    const { DemoTransport } = await import('/src/transports.js');
    const { RX } = await import('/src/protocol.js');
    const fixture = new DemoTransport();
    fixture.networks = ['Passport Home', 'Demo iPhone'];
    fixture.agents = [{ name: '小夏', url: 'wss://gateway.example', current: true }];
    const tx = new EventTarget();
    tx.startNotifications = async () => tx;
    const rx = { async writeValueWithResponse(data) { this.buffer = (this.buffer || '') + new TextDecoder().decode(data); if (this.buffer.endsWith('\n')) { const command = this.buffer.trimEnd(); this.buffer = ''; await fixture.send(command); } } };
    fixture.onData = data => { const payload = new TextEncoder().encode(new TextDecoder().decode(data).replace(' zh demo ', ' zh abcdef1234567890 ')); const event = new Event('characteristicvaluechanged'); tx.value = new DataView(payload.buffer, payload.byteOffset, payload.byteLength); tx.dispatchEvent(event); };
    const service = { async getCharacteristic(uuid) { return uuid === RX ? rx : tx; } };
    const device = new EventTarget();
    device.gatt = { async connect() { return { async getPrimaryService() { return service; } }; }, disconnect() {} };
    Object.defineProperty(navigator, 'serial', { configurable: true, value: undefined });
    Object.defineProperty(navigator, 'bluetooth', { configurable: true, value: { async requestDevice() { return device; } } });
  });
  await page.locator('#connect-ble').click();
  await page.locator('[data-agent]').first().waitFor();
  await page.evaluate(() => scrollTo(0, 0));
  await page.screenshot({ path: resolve(shots, 'web-mobile.png'), fullPage: true });

  const image = async (name, width, height, className = '') => `<img class="${className}" width="${width}" height="${height}" src="data:image/png;base64,${(await readFile(resolve(shots, name))).toString('base64')}">`;
  const logo = `data:image/png;base64,${(await readFile(resolve(root, 'assets/logo/logo.png'))).toString('base64')}`;
  const style = `*{box-sizing:border-box}body{margin:0;background:#f8f7f3;color:#152b30;font-family:-apple-system,BlinkMacSystemFont,'PingFang SC',sans-serif}.poster{width:1152px;height:1536px;padding:72px;overflow:hidden;position:relative;background:radial-gradient(ellipse at 100% 0,#d9ebe2,transparent 52%),#f8f7f3}.brand{display:flex;align-items:center;gap:18px;font-size:26px;font-weight:600;letter-spacing:.3px}.brand img{width:58px;height:58px;border-radius:16px}.eyebrow{color:#cc4b3e;font-size:24px;letter-spacing:3px;margin:52px 0 20px}h1{font-size:86px;line-height:1.18;letter-spacing:-4px;margin:0 0 26px;font-weight:750}h2{font-size:62px;line-height:1.2;margin:0 0 20px;letter-spacing:-2px}.lead{color:#53676b;font-size:29px;line-height:1.6;margin:0}.screens{display:flex;gap:30px;justify-content:center;align-items:center;margin:60px 0 42px}.device{width:292px;height:auto;border-radius:10px;box-shadow:0 20px 46px #193c3524;image-rendering:pixelated}.screens .device:nth-child(2){transform:translateY(30px)}.strip{border-top:1px solid #ccd7d1;border-bottom:1px solid #ccd7d1;padding:28px 0;display:flex;justify-content:space-between;gap:20px;margin-top:38px}.strip strong{font-size:28px}.strip p{font-size:23px;color:#627579;margin:10px 0 0}.foot{position:absolute;bottom:54px;left:72px;right:72px;font-size:20px;color:#66797a;display:flex;justify-content:space-between}.mode-grid{display:grid;grid-template-columns:1fr 1fr;gap:62px;margin-top:48px}.mode-grid img{width:440px;height:auto;border-radius:8px;image-rendering:pixelated}.mode-grid h3{font-size:32px;margin:30px 0 14px}.mode-grid p{font-size:25px;line-height:1.65;color:#53676b}.webshot{width:1008px;height:auto;border:1px solid #dfe3dd;border-radius:18px;margin-top:44px;box-shadow:0 22px 60px #193c3515}.mobile-row{display:flex;gap:64px;align-items:flex-start;margin-top:55px}.mobile-shot{width:460px;height:904px;object-fit:cover;object-position:top;border:1px solid #dfe3dd;border-radius:24px;box-shadow:0 22px 60px #193c3515}.steps{padding-top:36px;width:460px}.steps h3{font-size:34px;margin:0 0 18px}.steps p{font-size:27px;line-height:1.7;color:#53676b;margin:0 0 54px}.note{background:#e5eee8;border-radius:20px;padding:26px;font-size:24px;line-height:1.7;color:#315a50;margin-top:36px}`;
  const brand = `<div class="brand"><img src="${logo}">${titles.zh} · ${titles.en}</div>`;
  const foot = label => `<div class="foot"><span>${label}</span><span>开源 · 自己的助手</span></div>`;
  const posters = [
    ['00-cover', `${brand}<div class="eyebrow">PERSONAL AGENT · 随身语音终端</div><h1>把助手带在身边</h1><p class="lead">语音问答，实时通话<br>连接 OpenClaw 或 Hermes，使用自己的 Agent</p><div class="screens">${await image('firmware-hermes.png', 240, 320, 'device')}${await image('firmware-ask.png', 240, 320, 'device')}${await image('firmware-call.png', 240, 320, 'device')}</div><div class="strip"><div><strong>电脑 USB 刷机</strong><p>网页设置网络与助手</p></div><div><strong>手机蓝牙配置</strong><p>连热点，方便换网</p></div><div><strong>设备直连网关</strong><p>不经过手机中转</p></div></div><div class="note"><strong>OpenClaw + Hermes</strong><br>连接个人 Agent 的工具与上下文<br>网页配置 · 已有 OpenClaw 配对继续用</div>${foot('实际固件界面渲染 · 演示内容，非实机照片')}`],
    ['01-ask-and-call', `${brand}<div class="eyebrow">两种对话方式</div><h2>语音问答，实时通话</h2><p class="lead">提问看文字回复，通话听语音应答</p><div class="mode-grid"><div>${await image('firmware-ask.png', 240, 320)}<h3>提问</h3><p>按一下录音，再按发送<br>说完停顿也可自动提交<br>读完回复，接着追问</p></div><div>${await image('firmware-call.png', 240, 320)}<h3>通话</h3><p>连上后直接说话<br>语音回复，滚动字幕<br>按提示静音、打断或结束</p></div></div><div class="note">需自备 OpenClaw 或 Hermes；通话另需实时语音服务。</div>${foot('实际固件界面渲染 · 演示内容，非实机照片')}`],
    ['02-web-studio', `${brand}<div class="eyebrow">电脑 USB 连接</div><h2>在网页里配置与刷机</h2><p class="lead">设置 Wi-Fi 和助手，需要时更新固件</p>${await image('web-desktop.png', 1360, 1040, 'webshot')}<div class="strip"><div><strong>配置网络与助手</strong><p>改好后统一保存</p></div><div><strong>需要时更新固件</strong><p>改配置，不刷机</p></div></div><div class="note">无应用后台 · 配置直接写入设备<br>电脑推荐 Chrome／Edge，固件从本站下载</div>${foot('网页实际界面 · 虚构网络、助手与示例地址')}`],
    ['03-mobile-setup', `${brand}<div class="eyebrow">手机蓝牙配置 · 多网络保存</div><h2>连接手机热点，灵活切换网络</h2><p class="lead">保存常用 Wi-Fi 和热点，在设备上按需切换</p><div class="mobile-row">${await image('web-mobile.png', 390, 844, 'mobile-shot')}<div class="steps"><h3>连接手机热点</h3><p>iPhone 用 Bluefy 打开配置网页<br>把个人热点保存到 Passport</p><h3>保存多套网络</h3><p>家中、办公室或手机热点<br>都保存在设备上</p><h3>在设备上切换</h3><p>「设置 → Wi-Fi」选择已存网络<br>使用手机热点时保持热点开启</p></div></div><div class="note">iPhone 的 Safari／Chrome 不支持网页蓝牙，请用 Bluefy；更新固件请用电脑 USB。</div>${foot('网页移动布局演示 · 非手机实拍，无真实凭据')}`],
  ];
  const render = await browser.newPage({ viewport: { width: 1152, height: 1536 }, deviceScaleFactor: 1 });
  for (const [name, content] of posters) {
    const overrides = 'h1{white-space:nowrap}.screens{margin:84px 0 62px}.strip{margin-top:46px}.webshot{height:690px;object-fit:cover;object-position:top}.webshot+.strip{margin-top:28px}.note{margin-top:28px}.foot{bottom:42px}.mobile-shot{height:810px}.mobile-row{margin-top:40px}.steps{padding-top:20px}.steps p{margin-bottom:38px}';
    const html = `<!doctype html><meta charset="utf-8"><style>${style}${overrides}</style><main class="poster">${content}</main>`;
    await writeFile(resolve(output, `${name}.html`), html);
    await render.setContent(html, { waitUntil: 'load' });
    await render.evaluate(() => document.fonts.ready);
    for (const heading of await render.locator('.poster > h1, .poster > h2').all()) {
      const fits = await heading.evaluate(element => element.scrollWidth <= element.clientWidth && element.clientHeight <= Math.ceil(parseFloat(getComputedStyle(element).lineHeight)));
      if (!fits) throw new Error(`${name}: title wraps or overflows`);
    }
    const footer = await render.locator('.foot').boundingBox();
    for (const block of await render.locator('.poster > :not(.foot)').all()) {
      const bounds = await block.boundingBox();
      if (bounds.y + bounds.height > footer.y - 16) throw new Error(`${name}: content overlaps the footer`);
    }
    await render.screenshot({ path: resolve(output, `${name}.png`) });
  }
  for (const filename of ['web-desktop.png', 'web-mobile.png']) await copyFile(resolve(shots, filename), resolve(output, filename));
  const sections = [];
  const metadata = { version, title_zh: titles.zh, title_en: titles.en, source_url: 'https://github.com/vangie/ai-passport-agent', release_url: null, candidate_tag: `v${version}`, cover: '00-cover.png', images: ['01-ask-and-call.png', '02-web-studio.png', '03-mobile-setup.png'], status: 'prepared-not-submitted' };
  for (const [language, suffix, headings] of [['zh', '.zh_CN', ['简介', '使用方法', '更新日志']], ['en', '', ['Description', 'Quick-start instructions', 'Update notes']]]) {
    const document = await readFile(resolve(root, `docs/release/community${suffix}.md`), 'utf8');
    for (const [index, field] of ['description', 'instructions', 'changelog'].entries()) {
      const value = extractCommunityCopy(document, headings[index]);
      metadata[`${field}_${language}`] = value;
      await writeFile(resolve(output, `${field}.${language === 'zh' ? 'zh_CN' : 'en'}.txt`), value + '\n');
      sections.push(`<section class="copy" lang="${language === 'zh' ? 'zh-CN' : 'en'}"><h2>${language === 'zh' ? '中文' : 'English'} · ${headings[index]}</h2><div class="copy-body">${renderCommunityCopy(value)}</div></section>`);
    }
  }
  await writeFile(resolve(output, 'metadata.json'), JSON.stringify(metadata, null, 2) + '\n');
  await writeFile(resolve(output, 'preview.html'), `<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>${titles.zh} · ${titles.en} · 社区素材预览</title><style>*{box-sizing:border-box}body{margin:40px;background:#eceee9;font:18px -apple-system,sans-serif;color:#18332f}main{max-width:1100px;margin:auto}.gallery{display:grid;grid-template-columns:1fr 1fr;gap:24px}img{width:100%;border-radius:12px}p,li{line-height:1.8}.copy-body{background:white;padding:24px;border-radius:12px;font:17px/1.8 -apple-system,sans-serif;overflow-wrap:anywhere}.copy-body p{margin:0 0 1.25em}.copy-body p:last-child{margin-bottom:0}.copy-body ol,.copy-body ul{margin:0;padding-left:1.5em}.copy-body li+li{margin-top:.8em}a{color:#ac4337}@media(max-width:600px){body{margin:20px}.gallery{grid-template-columns:1fr}.copy-body{padding:20px}}</style><main><h1>${titles.zh} · ${titles.en} · ${metadata.version}</h1><p>社区发布前预览，尚未提交审核。仅使用虚构数据；界面渲染不代表实机照片。</p><div class="gallery">${posters.map(([name]) => `<img src="${name}.png" alt="${name}">`).join('')}</div>${sections.join('')}</main></html>`);
  console.log('Community posters and sanitized browser screenshots rendered successfully.');
} finally {
  await browser?.close();
  server.kill('SIGTERM');
}

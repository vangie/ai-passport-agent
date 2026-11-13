import assert from 'node:assert/strict';
import { chromium } from 'playwright';
import { spawn } from 'node:child_process';
import { mkdir, readFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';

async function installBluetoothFixture(page, build = 'abcdef1234567890', networks = ['Home Wi-Fi', 'My iPhone'], agents) {
  await page.evaluate(async ({ initialBuild, initialNetworks, initialAgents }) => {
    const { DemoTransport } = await import('/src/transports.js');
    const fixture = new DemoTransport();
    fixture.networks = initialNetworks;
    if (initialAgents) fixture.agents = initialAgents;
    window.fixtureCommands = [];
    const send = fixture.send.bind(fixture);
    fixture.send = async command => {
      window.fixtureCommands.push(command);
      if (command === 'HELLO' && window.fixtureHoldHello) await new Promise(resolve => { window.fixtureReleaseHello = resolve; });
      const parts = command.split(' ');
      if (parts[0] === 'TRY' && window.fixtureWiFiError) {
        fixture.onData(new TextEncoder().encode(window.fixtureWiFiError + '\n'));
        window.fixtureWiFiError = '';
        return;
      }
      if (parts[0] === 'ROUTE') {
        if (window.fixtureRouteError) {
          fixture.onData(new TextEncoder().encode(window.fixtureRouteError + '\n'));
          window.fixtureRouteError = '';
          return;
        }
        const agent = fixture.agents[Number(parts[2])];
        agent.route = parts[3] === 'direct' ? { mode: 'direct' } : { mode: 'ss', host: decodeURIComponent(parts[4]), port: Number(parts[5]), savedSecret: parts[6] !== '-' || agent.route?.savedSecret, method: parts[7] };
      }
      await send(command);
    };
    let command = '';
    let changed;
    window.fixtureBuild = initialBuild;
    fixture.onData = data => {
      let line = new TextDecoder().decode(data).replace(' zh demo ', ` zh ${window.fixtureBuild} `);
      if (window.fixtureNoVolume) line = line.replace(' device-control', '');
      const match = /^ROUTE (\d+) /u.exec(line);
      if (match) {
        const route = fixture.agents[Number(match[1])].route;
        if (route?.mode === 'ss') line = `ROUTE ${match[1]} ss ${route.host} ${route.port} ${route.savedSecret ? 1 : 0} ${route.method}\n`;
      }
      changed?.({ target: { value: new DataView(new TextEncoder().encode(line).buffer) } });
    };
    const tx = {
      addEventListener(_name, listener) { changed = listener; },
      removeEventListener() { changed = undefined; },
      async startNotifications() {},
    };
    const rx = { async writeValueWithResponse(data) {
      command += new TextDecoder().decode(data);
      if (command.endsWith('\n')) {
        const complete = command.trimEnd();
        command = '';
        await fixture.send(complete);
      }
    } };
    const service = { async getCharacteristic(uuid) { return uuid.startsWith('d2') ? rx : tx; } };
    const device = {
      addEventListener(name, listener) { if (name === 'gattserverdisconnected') window.fixtureDisconnect = listener; }, removeEventListener() {},
      gatt: { async connect() { return { async getPrimaryService() { return service; } }; }, disconnect() {} },
    };
    Object.defineProperty(navigator, 'bluetooth', { configurable: true, value: { async requestDevice() { return device; } } });
  }, { initialBuild: build, initialNetworks: networks, initialAgents: agents });
}

async function installSerialFixture(page) {
  await page.evaluate(async () => {
    const { DemoTransport } = await import('/src/transports.js');
    const fixture = new DemoTransport();
    window.fixtureBuild = 'abcdef1234567890';
    const port = {
      async open() {
        port.readable = new ReadableStream({ start(controller) { fixture.onData = data => controller.enqueue(new TextEncoder().encode(new TextDecoder().decode(data).replace(' zh demo ', ` zh ${window.fixtureBuild} `))); } });
        port.writable = new WritableStream({ write(data) { return fixture.send(new TextDecoder().decode(data).trimEnd()); } });
      },
      async close() {},
    };
    Object.defineProperty(navigator, 'serial', { configurable: true, value: { async requestPort() { return port; } } });
  });
}

async function checkSavedBar(page, screenshotPrefix = 'save-success') {
  const viewport = page.viewportSize();
  const localeBefore = await page.locator('input[name="interface-language"]:checked').inputValue();
  for (const locale of ['zh', 'en']) {
    await page.locator(`label[for="language-${locale}"]`).click();
    for (const width of [1360, 390, 320]) {
      await page.setViewportSize({ width, height: 844 });
      const feedback = page.locator('#save-feedback');
      assert.equal(await feedback.textContent(), locale === 'zh' ? '已保存' : 'Saved');
      assert.equal(await feedback.isVisible(), true);
      assert.equal(await page.locator('#save-state').isVisible(), false);
      const bar = await page.locator('.save-bar').boundingBox();
      const summary = await feedback.boundingBox();
      const actions = await page.locator('.save-bar > .actions').boundingBox();
      assert.ok(summary.x + summary.width <= actions.x, 'save result stays beside actions');
      assert.ok(Math.abs(summary.y + summary.height / 2 - actions.y - actions.height / 2) < 1, 'save result and actions stay in one row');
      assert.ok(bar.height <= 70, 'successful save does not expand the sticky bar');
      assert.equal(await feedback.evaluate(element => element.scrollWidth <= element.clientWidth), true, 'short save result is fully readable');
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
      await page.locator('.save-bar').screenshot({ path: `../build/web-shots/${screenshotPrefix}-${locale}-${width}.png` });
      if (screenshotPrefix === 'bluefy' && width === 390) await page.screenshot({ path: `../build/web-shots/bluefy-${locale}.png`, fullPage: true });
    }
  }
  await page.setViewportSize(viewport);
  await page.locator(`label[for="language-${localeBefore}"]`).click();
}

async function checkFirmwareAlignment(page) {
  const versions = await page.locator('.firmware-versions').boundingBox();
  const installed = await page.locator('.firmware-versions > div').first().boundingBox();
  const available = await page.locator('.firmware-versions > div').last().boundingBox();
  assert.ok(Math.abs(installed.width - available.width) < 1, 'firmware versions use equal-width columns');
  assert.ok(Math.abs(available.x - versions.x - versions.width / 2) < 1, 'firmware divider stays at the center');
  const actions = await page.locator('.firmware-actions').boundingBox();
  const button = await page.locator('#flash').boundingBox();
  const checkbox = await page.locator('.firmware-keep').count() ? await page.locator('.firmware-keep').boundingBox() : null;
  assert.ok(Math.abs(button.x + button.width - actions.x - actions.width) < 1, 'firmware button is right-aligned');
  if (checkbox) assert.ok(checkbox.y + checkbox.height <= button.y || checkbox.x + checkbox.width <= button.x, 'firmware option does not overlap button');
}

async function checkCardOrder(page) {
  const narrow = await page.evaluate(() => matchMedia('(max-width: 800px)').matches);
  await page.waitForFunction(isNarrow => document.querySelectorAll('main.grid > section').length === (isNarrow ? 4 : 0), narrow);
  const expected = narrow ? ['wifi-card', 'agent-card', 'preferences-card', 'firmware-card'] : ['wifi-card', 'preferences-card', 'agent-card', 'firmware-card'];
  assert.deepEqual(await page.locator('main.grid section[id$="-card"]').evaluateAll(cards => cards.map(card => card.id)), expected);
  const cards = await page.locator('main.grid section[id$="-card"]').evaluateAll(elements => elements.map(element => {
    const { x, y, width, height } = element.getBoundingClientRect();
    return { x, y, width, height };
  }));
  for (let index = 1; index < cards.length; index++) {
    const current = cards[index];
    const previous = cards[index - 1];
    if (narrow) {
      assert.ok(Math.abs(current.x - previous.x) < 1, 'narrow-screen cards share one column');
      assert.ok(current.y >= previous.y + previous.height + 15, 'configuration precedes preferences and firmware');
    } else if (index === 2) {
      assert.ok(Math.abs(current.y - cards[0].y) < 1, 'desktop columns start at the same height');
      assert.ok(current.x >= cards[0].x + cards[0].width + 19, 'desktop keeps two columns');
    } else {
      assert.ok(Math.abs(current.y - previous.y - previous.height - 20) < 1, 'desktop cards stack without row-induced whitespace');
    }
  }
  await page.locator('#copy-prompt').focus();
  await page.keyboard.press('Shift+Tab');
  assert.equal(await page.evaluate(() => document.activeElement.closest('section.card')?.id), 'agent-card');
  await page.locator('#code').focus();
  await page.keyboard.press('Tab');
  if (narrow && await page.locator('#device-language').isEnabled()) assert.equal(await page.evaluate(() => document.activeElement.id), 'device-language');
  else assert.equal(await page.evaluate(() => document.activeElement.closest('section')?.id), 'firmware-card');
}

async function checkConnectionChoices(page, locale) {
  const card = page.locator('.connection');
  const title = await card.locator('.connection-title').boundingBox();
  const actions = await card.locator('.connection-actions').boundingBox();
  assert.ok(title.x + title.width <= actions.x, 'connection choices remain beside the title');
  const heading = await card.locator('h2').boundingBox();
  assert.ok(heading.x + heading.width + 8 <= actions.x, 'connection heading leaves space before the buttons');
  assert.ok(Math.abs(title.y + title.height / 2 - actions.y - actions.height / 2) < 1, 'connection title and choices share one row');
  assert.equal(await card.locator('.subtle').count(), 0, 'disconnected toolbar has no redundant description');
  const buttons = card.locator('.connection-actions button');
  const first = await buttons.first().boundingBox();
  for (const button of await buttons.all()) {
    const bounds = await button.boundingBox();
    assert.equal(bounds.y, first.y, 'connection buttons never wrap onto another row');
    assert.ok(bounds.height >= 38, 'connection choices remain easy to tap');
    assert.equal(await button.evaluate(element => element.scrollWidth <= element.clientWidth && element.scrollHeight <= element.clientHeight), true, 'connection labels stay fully visible');
  }
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true, `connection choices do not overflow in ${locale}`);
}

async function checkCompactConnection(context, release) {
  const connectionPage = await context.newPage();
  try {
    await connectionPage.route('**/firmware/manifest.json', route => route.fulfill({ json: release }));
    await connectionPage.goto('http://127.0.0.1:4175');
    await installSerialFixture(connectionPage);
    await installBluetoothFixture(connectionPage);
    for (const locale of ['zh', 'en']) {
      await connectionPage.locator(`label[for="language-${locale}"]`).click();
      assert.equal(await connectionPage.locator('#saved-networks').count(), 0, 'saved networks stay hidden before connecting');
      assert.equal(await connectionPage.getByRole('heading', { name: locale === 'zh' ? '已存网络' : 'Saved networks', exact: true }).count(), 0, 'saved networks heading stays hidden before connecting');
      for (const selector of ['#device-language', '#volume']) {
        assert.equal(await connectionPage.locator(selector).isDisabled(), true, `${selector} is unavailable before connecting`);
      }
      assert.equal(await connectionPage.locator('#flash').isDisabled(), true);
      assert.equal(await connectionPage.locator('#flash').textContent(), locale === 'zh' ? '更新固件' : 'Update firmware');
      assert.equal(await connectionPage.locator('#keep-firmware-settings').isChecked(), true);
      assert.equal(await connectionPage.locator('#keep-firmware-settings').isEnabled(), true);
      assert.equal(await connectionPage.locator('#firmware-install-toggle').count(), 0, 'firmware keeps one action without a separate recovery entry');
      assert.equal(await connectionPage.locator('#preferences-card fieldset').evaluate(element => element.disabled), true);
      for (const width of [1360, 800, 768, 560, 390, 320]) {
        await connectionPage.setViewportSize({ width, height: 900 });
        await checkConnectionChoices(connectionPage, locale);
        assert.ok((await connectionPage.locator('.connection').boundingBox()).height <= 66, `disconnected toolbar stays compact at ${locale}/${width}`);
        if ([1360, 390, 320].includes(width)) await connectionPage.locator('.connection').screenshot({ path: `../build/web-shots/connection-choices-${locale}-${width}.png` });
        if ([1360, 390].includes(width)) {
          for (const section of ['preferences', 'firmware']) await connectionPage.locator(`#${section}-card`).screenshot({ path: `../build/web-shots/${section}-disabled-${locale}-${width}.png` });
        }
      }
    }
    for (const kind of ['usb', 'ble']) {
      await (kind === 'usb' ? installSerialFixture(connectionPage) : installBluetoothFixture(connectionPage));
      await connectionPage.locator(`#connect-${kind}`).click();
      await connectionPage.locator('.connection.is-connected').waitFor();
      assert.equal(await connectionPage.locator('#device-language').isEnabled(), true);
      assert.equal(await connectionPage.locator('#volume').isEnabled(), true);
      assert.equal(await connectionPage.locator('#keep-firmware-settings').isDisabled(), kind !== 'usb');
      assert.equal(await connectionPage.locator('#flash').isDisabled(), kind !== 'usb');
      for (const locale of ['zh', 'en']) {
        await connectionPage.locator(`label[for="language-${locale}"]`).click();
        await connectionPage.getByRole('heading', { name: locale === 'zh' ? '已存网络' : 'Saved networks', exact: true }).waitFor();
        assert.ok(await connectionPage.locator('#saved-networks [data-network]').count() > 0, 'saved networks appear after reading the device');
        for (const width of [1360, 800, 768, 560, 390, 320]) {
          await connectionPage.setViewportSize({ width, height: 900 });
          const card = connectionPage.locator('.connection');
          const bounds = await card.boundingBox();
          const title = await card.locator('.connection-title').boundingBox();
          const actions = await card.locator('.connection-actions').boundingBox();
          const metadata = await card.locator('.subtle').boundingBox();
          assert.ok(bounds.height <= (width > 800 ? 62 : 84), `compact ${kind} connection at ${locale}/${width}`);
          assert.ok(title.x + title.width <= actions.x, 'device title and actions do not overlap');
          assert.ok(Math.abs(title.y + title.height / 2 - actions.y - actions.height / 2) <= 14, 'actions remain beside device information');
          assert.ok(metadata.x >= bounds.x && metadata.x + metadata.width <= bounds.x + bounds.width, 'metadata stays inside the card');
          if (width <= 560) {
            assert.ok((await card.locator('h2').boundingBox()).height <= 40, 'device title uses at most two compact lines');
            assert.ok(metadata.y >= actions.y + actions.height, 'mobile metadata sits directly below the device toolbar');
            for (const action of ['refresh', 'disconnect']) {
              const button = connectionPage.locator(`#${action}`);
              const buttonBounds = await button.boundingBox();
              assert.ok(buttonBounds.width >= 40 && buttonBounds.height >= 40, 'mobile actions remain easy to tap');
              const label = locale === 'zh' ? action === 'refresh' ? '重新读取' : '断开' : action === 'refresh' ? 'Read again' : 'Disconnect';
              assert.equal(await button.textContent(), label, 'actions use visible, explicit labels');
              assert.equal(await button.locator('svg').count(), 0, 'connection actions do not rely on ambiguous icons');
              assert.equal(await button.evaluate(element => element.scrollWidth <= element.clientWidth && element.scrollHeight <= element.clientHeight), true, 'action labels are not clipped');
            }
          }
          assert.equal(await connectionPage.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
          if ([1360, 768, 390, 320].includes(width)) await card.screenshot({ path: `../build/web-shots/connection-compact-${kind}-${locale}-${width}.png` });
        }
      }
      await connectionPage.setViewportSize({ width: 390, height: 900 });
      await connectionPage.evaluate(() => { document.querySelector('.connection .subtle').textContent += ' · Example assistant with a longer display name'; });
      assert.equal(await connectionPage.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true, 'long metadata wraps without horizontal overflow');
      await connectionPage.locator('#disconnect').click();
      await connectionPage.locator('#connect-usb').waitFor();
      assert.equal(await connectionPage.locator('#saved-networks').count(), 0, 'saved networks disappear after disconnecting');
      assert.equal(await connectionPage.getByRole('heading', { name: 'Saved networks', exact: true }).count(), 0, 'saved networks heading disappears after disconnecting');
      for (const selector of ['#device-language', '#volume']) assert.equal(await connectionPage.locator(selector).isDisabled(), true, `${selector} disables after disconnecting`);
      assert.equal(await connectionPage.locator('#keep-firmware-settings').isChecked(), true);
      assert.equal(await connectionPage.locator('#keep-firmware-settings').isEnabled(), true);
      assert.equal(await connectionPage.locator('#flash').isDisabled(), true);
      assert.equal(await connectionPage.locator('#flash').textContent(), 'Update firmware');
    }
    await installBluetoothFixture(connectionPage);
    await connectionPage.evaluate(() => { window.fixtureHoldHello = true; });
    await connectionPage.locator('#connect-ble').click();
    await connectionPage.waitForFunction(() => typeof window.fixtureReleaseHello === 'function');
    assert.equal(await connectionPage.locator('#device-language').isDisabled(), true, 'preferences stay disabled during initial read');
    assert.equal(await connectionPage.locator('#volume').isDisabled(), true);
    await connectionPage.evaluate(() => { window.fixtureNoVolume = true; window.fixtureReleaseHello(); });
    await connectionPage.locator('#disconnect:enabled').waitFor();
    assert.equal(await connectionPage.locator('#device-language').isEnabled(), true);
    assert.equal(await connectionPage.locator('#volume').isDisabled(), true, 'unsupported volume control stays disabled');
    await connectionPage.evaluate(() => window.fixtureDisconnect());
    await connectionPage.locator('#connect-ble').waitFor();
    assert.equal(await connectionPage.locator('#device-language').isDisabled(), true, 'unexpected disconnect disables preferences even with a retained snapshot');
    assert.equal(await connectionPage.locator('#volume').isDisabled(), true);
    assert.equal(await connectionPage.locator('#keep-firmware-settings').isChecked(), true);
    assert.equal(await connectionPage.locator('#keep-firmware-settings').isEnabled(), true);
    assert.equal(await connectionPage.locator('#flash').isDisabled(), true);
    assert.equal(await connectionPage.locator('#flash').textContent(), 'Update firmware');
    await connectionPage.locator('label[for="language-zh"]').click();
    await connectionPage.setViewportSize({ width: 390, height: 900 });
    await connectionPage.locator('#wifi-card').screenshot({ path: '../build/web-shots/wifi-disconnected-mobile.png' });
  } finally {
    await connectionPage.close();
  }
}

async function checkConnectionFeedback(context, release) {
  const feedbackPage = await context.newPage();
  try {
    await feedbackPage.route('**/firmware/manifest.json', route => route.fulfill({ json: release }));
    await feedbackPage.goto('http://127.0.0.1:4175');
    await feedbackPage.getByText('v0.4.2', { exact: true }).waitFor();
    await feedbackPage.evaluate(() => {
      Object.defineProperty(navigator, 'serial', { configurable: true, value: {
        async requestPort() { throw new DOMException('Fixture chooser result', window.fixtureChooserError || 'NotFoundError'); },
      } });
    });
    for (const locale of ['zh', 'en']) {
      await feedbackPage.locator(`label[for="language-${locale}"]`).click();
      await feedbackPage.locator('#ssid').fill('Unsaved example Wi-Fi');
      await feedbackPage.locator('#password').fill('example-password');
      for (const width of [1360, 390, 320]) {
        await feedbackPage.setViewportSize({ width, height: 900 });
        await feedbackPage.locator('.connection').scrollIntoViewIfNeeded();
        const before = await feedbackPage.locator('main.grid').boundingBox();
        for (const error of ['NotFoundError', 'AbortError']) {
          await feedbackPage.evaluate(value => { window.fixtureChooserError = value; }, error);
          await feedbackPage.locator('#connect-usb').click();
          const feedback = feedbackPage.locator('.connection #connection-feedback');
          await feedback.getByText(locale === 'zh' ? '已取消连接，输入内容仍保留。' : 'Connection cancelled. Your inputs are kept.', { exact: true }).waitFor();
          assert.equal(await feedback.getAttribute('role'), 'status');
          assert.equal(await feedback.getAttribute('aria-atomic'), 'true');
          assert.equal(await feedbackPage.locator('#connection-feedback.error, #connection-feedback.success').count(), 0);
          assert.equal(await feedbackPage.locator('#notice, .notice, .shell > .feedback').count(), 0);
          assert.ok(Math.abs((await feedbackPage.locator('main.grid').boundingBox()).y - before.y) <= (await feedback.boundingBox()).height + 9, 'connection feedback adds at most one message row');
          assert.equal(await feedbackPage.locator('#ssid').inputValue(), 'Unsaved example Wi-Fi');
          assert.equal(await feedbackPage.locator('#password').inputValue(), 'example-password');
          assert.equal(await feedbackPage.locator('#discard').isEnabled(), true);
          assert.equal(await feedbackPage.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
        }
        await feedbackPage.locator('.connection').screenshot({ path: `../build/web-shots/connection-cancelled-${locale}-${width}.png` });
      }
    }
    await feedbackPage.evaluate(() => { window.fixtureChooserError = 'NetworkError'; });
    await feedbackPage.locator('#connect-usb').click();
    await feedbackPage.locator('#connection-feedback.error').waitFor();
    assert.equal(await feedbackPage.locator('#save-feedback').isVisible(), false);
    await installSerialFixture(feedbackPage);
    await feedbackPage.locator('#connect-usb').click();
    await feedbackPage.getByRole('heading', { name: 'AI Passport · USB', exact: true }).waitFor();
    assert.equal(await feedbackPage.locator('#connection-feedback').isVisible(), false);
    assert.equal(await feedbackPage.locator('#ssid').inputValue(), 'Unsaved example Wi-Fi');
    await feedbackPage.locator('#disconnect').click();
  } finally {
    feedbackPage.on('dialog', dialog => dialog.accept());
    await feedbackPage.close();
  }
}

async function checkPrivacyDialog(page, locale, screenshot) {
  const title = locale === 'zh' ? '隐私与数据' : 'Privacy & data';
  const close = locale === 'zh' ? '知道了' : 'Got it';
  const requests = [];
  const recordRequest = request => requests.push(request.url());
  page.on('request', recordRequest);
  await page.locator('#privacy-info').click();
  const dialog = page.getByRole('dialog', { name: title, exact: true });
  assert.equal(await dialog.isVisible(), true);
  assert.equal(await dialog.locator('.privacy-facts > div').count(), 3);
  assert.match(await page.locator('#privacy-intro').textContent(), locale === 'zh' ? /没有配置后台的静态单页应用/ : /static single-page app with no configuration backend/);
  assert.match(await dialog.locator('.privacy-facts dd').nth(1).textContent(), locale === 'zh' ? /临时保留.*不写入浏览器持久存储/ : /temporarily.*never in persistent browser storage/);
  assert.match(await dialog.locator('.privacy-hosting').textContent(), locale === 'zh' ? /常规日志/ : /ordinary access logs/);
  const bounds = await dialog.boundingBox();
  const viewport = page.viewportSize();
  assert.ok(bounds.x >= 0 && bounds.x + bounds.width <= viewport.width);
  assert.ok(bounds.y >= 0 && bounds.y + bounds.height <= viewport.height);
  if (screenshot) await dialog.screenshot({ path: `../build/web-shots/${screenshot}.png` });
  await page.keyboard.press('Escape');
  await dialog.waitFor({ state: 'hidden' });
  assert.equal(await page.locator('#privacy-info').evaluate(element => element === document.activeElement), true);
  await page.locator('#privacy-info').click();
  await dialog.getByRole('button', { name: close, exact: true }).click();
  await dialog.waitFor({ state: 'hidden' });
  assert.deepEqual(requests, []);
  assert.deepEqual(await page.evaluate(() => [localStorage.length, sessionStorage.length]), [0, 0]);
  page.off('request', recordRequest);
}

const server = spawn('node', ['node_modules/vite/bin/vite.js', '--host', '127.0.0.1', '--port', '4175', '--strictPort'], { stdio: 'pipe' });
let browser;
try {
  await new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error('Preview server did not start')), 15000);
    server.stdout.on('data', data => { if (String(data).includes('4175')) { clearTimeout(timeout); resolve(); } });
    server.on('exit', code => { clearTimeout(timeout); reject(new Error(`Preview server exited: ${code}`)); });
  });
  browser = await chromium.launch(process.env.CI ? {} : { channel: 'chrome', args: ['--disable-gpu'] });
  const context = await browser.newContext({ locale: 'zh-CN', viewport: { width: 1360, height: 1150 }, permissions: ['clipboard-read', 'clipboard-write'] });
  await context.addInitScript(() => {
    Object.defineProperty(navigator, 'serial', { configurable: true, value: {
      async requestPort() { throw new DOMException('No port selected', 'NotFoundError'); },
    } });
    Object.defineProperty(navigator, 'bluetooth', { configurable: true, value: {
      async requestDevice() { throw new DOMException('No device selected', 'NotFoundError'); },
    } });
  });
  const page = await context.newPage();
  const release = { schema: 1, version: 'v0.4.2', commit: 'a'.repeat(40), buildId: '1234567890abcdef', knownBuilds: { abcdef1234567890: { version: 'v0.4.1', channel: 'release' }, aaaaaaaaaaaaaaaa: { version: 'v0.4.2', channel: 'development' }, bbbbbbbbbbbbbbbb: { version: 'v0.4.3', channel: 'release' } }, application: { file: 'app.bin', offset: 0x10000, size: 288, sha256: 'b'.repeat(64) }, full: { file: 'full.bin', offset: 0, size: 288, sha256: 'c'.repeat(64) } };
  await checkCompactConnection(context, release);
  await checkConnectionFeedback(context, release);
  await page.route('**/firmware/manifest.json', route => route.fulfill({ json: release }));
  const errors = [];
  const external = [];
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => { if (!request.url().startsWith('http://127.0.0.1:4175') && !request.url().startsWith('ws://127.0.0.1:4175')) external.push(request.url()); });
  await page.goto('http://127.0.0.1:4175');
  for (const locale of ['zh', 'en']) {
    await page.locator(`label[for="language-${locale}"]`).click();
    for (const width of [1360, 801, 800, 390, 320]) {
      await page.setViewportSize({ width, height: 1150 });
      await checkCardOrder(page);
    }
    await page.setViewportSize({ width: 390, height: 844 });
    await page.evaluate(() => window.scrollTo(0, 0));
    await page.screenshot({ path: `../build/web-shots/card-order-${locale}-mobile.png`, fullPage: true });
  }
  await page.setViewportSize({ width: 1360, height: 1150 });
  await page.locator('label[for="language-zh"]').click();
  await page.evaluate(() => window.scrollTo(0, 0));
  await page.screenshot({ path: '../build/web-shots/card-order-desktop.png', fullPage: true });
  assert.equal(await page.evaluate(() => window.isSecureContext), true);
  assert.equal(await page.locator('.hero, .intro').count(), 0);
  assert.equal(await page.locator('header h1').textContent(), '设备配置台');
  assert.equal(await page.locator('#agent-card .card-head .subtle').textContent(), '复制提示词，让 Agent 准备网关并生成配对码');
  assert.equal(await page.locator('#wifi-details').evaluate(element => element.open), true);
  assert.equal(await page.locator('#nearby-section #hotspot-help').count(), 0);
  assert.equal(await page.locator('#wifi-details #hotspot-help, #wifi-details .hint').count(), 0);
  assert.ok((await page.locator('header').boundingBox()).height <= 80);
  assert.ok((await page.locator('.connection').boundingBox()).y <= 104);
  assert.equal(await page.locator('.passport-art').count(), 0);
  assert.equal(await page.locator('.brand').textContent(), 'AI Passport Agent');
  assert.equal(await page.title(), 'AI Passport Agent');
  assert.equal(await page.locator('.brand').getAttribute('aria-label'), 'AI Passport Agent');
  const logoBytes = await readFile('../assets/logo/logo.png');
  const logoDigest = createHash('sha256').update(logoBytes).digest('hex');
  const logoURL = await page.locator('.brand img').getAttribute('src');
  const logoResponse = await context.request.get(new URL(logoURL, page.url()).href);
  assert.equal(logoResponse.status(), 200);
  assert.equal(createHash('sha256').update(await logoResponse.body()).digest('hex'), logoDigest);
  await page.locator('.brand img').evaluate(image => image.decode());
  assert.equal(await page.locator('.brand img').evaluate(image => image.naturalWidth), 1024);
  const faviconURL = await page.locator('link[rel="icon"]').getAttribute('href');
  const faviconResponse = await context.request.get(new URL(faviconURL, page.url()).href);
  assert.equal(faviconResponse.status(), 200);
  assert.equal(createHash('sha256').update(await faviconResponse.body()).digest('hex'), logoDigest);
  const desktopBrand = await page.locator('.brand').boundingBox();
  const desktopTitle = await page.locator('header h1').boundingBox();
  assert.ok(desktopTitle.x >= desktopBrand.x + desktopBrand.width);
  assert.ok(Math.abs(desktopTitle.y + desktopTitle.height / 2 - desktopBrand.y - desktopBrand.height / 2) < 1);
  assert.equal(await page.locator('.connection-actions button').count(), 2);
  assert.equal(await page.locator('footer .footer-privacy').count(), 1);
  assert.equal(await page.locator('.connection-privacy, .grid .footer-privacy, .connection .footer-privacy').count(), 0);
  assert.equal(await page.locator('.footer-privacy').textContent(), '隐私与数据');
  assert.equal(await page.locator('.footer-privacy strong').count(), 0);
  assert.equal(await page.locator('#demo, #demo-hint, .demo-entry').count(), 0);
  assert.equal(await page.locator('footer button').count(), 1);
  assert.equal(await page.locator('#privacy-dialog').isVisible(), false);
  await checkPrivacyDialog(page, 'zh', 'privacy-zh-desktop');
  assert.equal(await page.getByRole('button', { name: '查看配置示例', exact: true }).count(), 0);
  assert.equal(await page.locator('#bluetooth-panel').count(), 0);
  assert.equal(await page.locator('.environment').count(), 0);
  assert.equal(await page.locator('#usb-support, #ble-support, #firmware-connection-hint').count(), 0);
  assert.equal(await page.locator('#connect-usb').isEnabled(), true);
  assert.equal(await page.locator('#connect-ble').isEnabled(), true);
  assert.equal(await page.locator('#firmware-card .card-head .subtle').count(), 0);
  assert.equal(await page.locator('#firmware-card .card-head .tag').textContent(), 'USB');
  assert.equal(await page.locator('.firmware-versions dt').count(), 2);
  const deviceVersionBounds = await page.locator('.firmware-versions > div').first().boundingBox();
  const releaseVersionBounds = await page.locator('.firmware-versions > div').last().boundingBox();
  assert.equal(deviceVersionBounds.y, releaseVersionBounds.y);
  assert.ok(releaseVersionBounds.x >= deviceVersionBounds.x + deviceVersionBounds.width);
  assert.ok((await page.locator('#firmware-card').boundingBox()).height <= 225);
  assert.equal(await page.locator('#flash').isDisabled(), true);
  assert.equal(await page.locator('#keep-firmware-settings').isChecked(), true);
  assert.equal(await page.locator('#keep-firmware-settings').isEnabled(), true);
  assert.equal(await page.locator('#flash').textContent(), '更新固件');
  assert.equal(await page.locator('#firmware-card button').count(), 1);
  assert.match(await page.locator('#firmware-mode-note').textContent(), /通过 USB 连接设备/);
  assert.equal(await page.getByRole('group', { name: '界面语言' }).count(), 1);
  assert.equal(await page.getByRole('radio').count(), 2);
  assert.equal(await page.locator('#language-zh').isChecked(), true);
  assert.equal(await page.locator('#language-en').isChecked(), false);
  await page.locator('label[for="language-en"]').click();
  assert.equal(await page.locator('#language-en').isChecked(), true);
  assert.equal(await page.locator('#language-zh').isChecked(), false);
  assert.equal(await page.locator('#language-en').evaluate(element => element === document.activeElement), true);
  assert.equal(await page.locator('html').getAttribute('lang'), 'en');
  assert.equal(await page.getByRole('group', { name: 'Interface language' }).count(), 1);
  assert.equal(await page.locator('#agent-card .card-head .subtle').textContent(), 'Copy the prompt to have your agent prepare the gateway and generate a pairing code');
  assert.equal(await page.locator('footer .footer-privacy').textContent(), 'Privacy & data');
  await checkPrivacyDialog(page, 'en', 'privacy-en-desktop');
  for (const width of [1360, 801, 800, 640, 561, 560, 390, 320]) {
    await page.setViewportSize({ width, height: 1150 });
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true, `English header: ${width}px overflow`);
    assert.equal(await page.locator('.brand-name').isVisible(), width > 800);
    const titleBounds = await page.locator('header h1').boundingBox();
    const navBounds = await page.locator('header nav').boundingBox();
    assert.ok(navBounds.x >= titleBounds.x + titleBounds.width, `English header: ${width}px overlap`);
    assert.ok(Math.abs(navBounds.y + navBounds.height / 2 - titleBounds.y - titleBounds.height / 2) < 1);
  }
  await page.setViewportSize({ width: 1360, height: 1150 });
  await page.locator('.language-thumb').evaluate(async element => { await Promise.all(element.getAnimations().map(animation => animation.finished)); });
  const englishThumb = await page.locator('.language-thumb').boundingBox();
  const englishOption = await page.locator('label[for="language-en"]').boundingBox();
  assert.ok(Math.abs(englishThumb.x - englishOption.x) < 1);
  assert.equal(await page.getByRole('button', { name: 'View sample settings', exact: true }).count(), 0);
  assert.equal(await page.locator('#bluetooth-panel').count(), 0);
  await page.locator('#language-en').press('ArrowLeft');
  assert.equal(await page.locator('#language-zh').isChecked(), true);
  assert.equal(await page.locator('#language-zh').evaluate(element => element === document.activeElement), true);
  await page.locator('#language-zh').press('ArrowRight');
  assert.equal(await page.locator('#language-en').isChecked(), true);
  await page.locator('label[for="language-zh"]').click();
  assert.equal(await page.locator('html').getAttribute('lang'), 'zh-CN');
  await page.locator('.language-thumb').evaluate(async element => { await Promise.all(element.getAnimations().map(animation => animation.finished)); });
  const chineseThumb = await page.locator('.language-thumb').boundingBox();
  const chineseOption = await page.locator('label[for="language-zh"]').boundingBox();
  assert.ok(Math.abs(chineseThumb.x - chineseOption.x) < 1);
  await mkdir('../build/web-shots', { recursive: true });
  await page.screenshot({ path: '../build/web-shots/connect-clean.png' });
  await page.locator('.connection').screenshot({ path: '../build/web-shots/connection-clean.png' });
  await page.locator('#firmware-card').screenshot({ path: '../build/web-shots/firmware-compact.png' });
  await page.locator('footer').screenshot({ path: '../build/web-shots/footer.png' });
  await page.evaluate(() => window.scrollTo(0, 0));
  await page.evaluate(() => {
    Object.defineProperty(navigator, 'serial', { configurable: true, value: {
      async requestPort() { throw new DOMException('No port selected', 'NotFoundError'); },
    } });
    Object.defineProperty(navigator, 'bluetooth', { configurable: true, value: {
      async requestDevice(options) {
        window.bluetoothOptions = options;
        throw new DOMException('No device selected', 'NotFoundError');
      },
    } });
  });
  await page.locator('#connect-usb').click();
  await page.getByText('已取消连接，输入内容仍保留。', { exact: true }).waitFor();
  assert.equal(await page.locator('#bluetooth-panel').count(), 0);
  await page.locator('#connect-ble').click();
  await page.locator('.connection #connection-feedback').getByText('未选择蓝牙设备。', { exact: true }).waitFor();
  assert.equal(await page.locator('#connection-feedback.error').count(), 0);
  assert.ok((await page.locator('#bluetooth-hint').textContent()).includes('无需删除配对记录'));
  assert.equal(await page.locator('#bluetooth-help').evaluate(element => element.open), false);
  await page.locator('#bluetooth-help summary').click();
  await page.getByText(/USB 权限和蓝牙权限互不相通/).waitFor();
  assert.equal(await page.locator('#bluetooth-restart').isVisible(), true);
  assert.match(await page.locator('#bluetooth-restart').textContent(), /先记下未保存的内容.*不是刷新网页.*chrome:\/\/restart/);
  const panelBounds = await page.locator('#bluetooth-panel').boundingBox();
  assert.ok(panelBounds.width >= (await page.locator('.connection').boundingBox()).width - 8);
  assert.ok((await page.locator('#bluetooth-help summary').boundingBox()).width > 200);
  await page.screenshot({ path: '../build/web-shots/bluetooth-help.png' });
  await page.setViewportSize({ width: 390, height: 844 });
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
  assert.ok((await page.locator('#bluetooth-help summary').boundingBox()).width > 200);
  assert.equal(await page.locator('.shell').count(), 1);
  await page.screenshot({ path: '../build/web-shots/bluetooth-help-mobile.png' });
  await page.setViewportSize({ width: 1360, height: 1150 });
  await page.locator('#connect-ble-broad').click();
  await page.locator('.connection #connection-feedback').getByText('未选择蓝牙设备。', { exact: true }).waitFor();
  assert.deepEqual(await page.evaluate(() => window.bluetoothOptions), {
    acceptAllDevices: true,
    optionalServices: ['d1b48e2a-7051-339c-7e4a-104d2e8a1c6b'],
  });
  assert.equal(await page.locator('#bluetooth-restart').isVisible(), true);
  await page.locator('#connect-ble').click();
  await page.locator('.connection #connection-feedback').getByText('未选择蓝牙设备。', { exact: true }).waitFor();
  assert.ok((await page.evaluate(() => window.bluetoothOptions)).filters);
  assert.equal(await page.locator('.icon').first().evaluate(element => getComputedStyle(element).width), '18px');
  assert.equal(await page.locator('input[type="file"]').count(), 0);
  assert.equal(await page.locator('#flash-kind').count(), 0);
  assert.equal(await page.locator('#firmware-advanced, #flash-full').count(), 0);
  assert.equal(await page.locator('#firmware-install-toggle').count(), 0);
  assert.equal(await page.locator('#keep-firmware-settings').isChecked(), true);
  assert.equal(await page.locator('#flash').isDisabled(), true);
  await page.locator('#keep-firmware-settings').uncheck();
  assert.equal(await page.locator('#flash').textContent(), '安装固件');
  assert.equal(await page.locator('#flash').isEnabled(), true);
  assert.match(await page.locator('#firmware-mode-note.warning').textContent(), /覆盖设备上的 Wi-Fi 和助手配置/);
  await page.locator('label[for="language-en"]').click();
  assert.equal(await page.locator('#flash').textContent(), 'Install firmware');
  await page.locator('label[for="language-zh"]').click();
  await page.locator('#firmware-card').evaluate(element => element.scrollIntoView({ block: 'start' }));
  await page.locator('#firmware-card').screenshot({ path: '../build/web-shots/firmware-install.png' });
  await page.evaluate(() => {
    window.installPortRequests = 0;
    window.installPortOpened = false;
    Object.defineProperty(navigator, 'serial', { configurable: true, value: {
      async requestPort() {
        window.installPortRequests++;
        if (window.cancelInstallChooser) throw new DOMException('Fixture cancellation', 'NotFoundError');
        return { async open() { window.installPortOpened = true; throw new Error('Unexpected flash'); } };
      },
    } });
  });
  await page.locator('#flash').click();
  await page.getByText('完整安装会覆盖设备上的 Wi-Fi 和助手配置。请保持 USB 连接，确定继续？', { exact: true }).waitFor();
  await page.locator('#dialog-cancel').click();
  assert.equal(await page.locator('#keep-firmware-settings').isChecked(), false);
  assert.equal(await page.evaluate(() => window.installPortRequests), 0, 'cancelling installation does not request USB access');
  await page.locator('#keep-firmware-settings').check();
  assert.equal(await page.locator('#flash').isDisabled(), true, 'keeping settings never falls back to a full installation');
  await page.locator('#keep-firmware-settings').uncheck();
  await page.evaluate(() => { window.cancelInstallChooser = true; });
  await page.locator('#flash').click();
  await page.locator('#dialog-confirm').click();
  await page.locator('#firmware-feedback').getByText('已取消连接，输入内容仍保留。', { exact: true }).waitFor();
  assert.equal(await page.evaluate(() => window.installPortOpened), false);
  await page.evaluate(() => { window.cancelInstallChooser = false; });
  const installImages = [];
  await page.route('**/firmware/*.bin', route => {
    installImages.push(new URL(route.request().url()).pathname.split('/').pop());
    return route.fulfill({ status: 404, body: '' });
  });
  await page.locator('#flash').click();
  await page.locator('#dialog-confirm').click();
  await page.getByText('暂时无法获取网站固件，请稍后刷新重试。', { exact: true }).waitFor();
  assert.deepEqual(installImages, ['full.bin'], 'installation without a configuration connection selects the merged image');
  assert.equal(await page.evaluate(() => window.installPortOpened), false, 'failed download never starts writing');
  assert.equal(await page.locator('#flash').isEnabled(), true, 'installation can be retried');
  assert.equal(await page.locator('#keep-firmware-settings').isChecked(), false, 'failed installation retains the selected mode');
  await page.unroute('**/firmware/*.bin');
  await installBluetoothFixture(page);
  await page.locator('#connect-ble').click();
  await page.getByRole('heading', { name: 'AI Passport · Bluetooth', exact: true }).waitFor();
  assert.equal(await page.locator('#disconnect').textContent(), '断开');
  for (const locale of ['zh', 'en']) {
    await page.locator(`label[for="language-${locale}"]`).click();
    for (const width of [1360, 801, 800, 390, 320]) {
      await page.setViewportSize({ width, height: 1150 });
      await page.locator('#pairing-details').evaluate(element => { element.open = true; });
      await checkCardOrder(page);
    }
  }
  await page.setViewportSize({ width: 1360, height: 1150 });
  await page.locator('label[for="language-zh"]').click();
  assert.equal(await page.locator('footer .footer-privacy').count(), 1);
  assert.equal(await page.locator('.connection-privacy, .connection .footer-privacy').count(), 0);
  await page.locator('.connection').screenshot({ path: '../build/web-shots/connection-clean-connected.png' });
  assert.equal(await page.locator('#demo, #demo-hint, .demo-entry').count(), 0);
  assert.equal(await page.locator('#bluetooth-hint').count(), 0);
  assert.equal(await page.locator('#bluetooth-help').count(), 0);
  await page.getByText('Home Wi-Fi', { exact: true }).first().waitFor();
  assert.equal(await page.locator('#flash').isDisabled(), true);
  assert.equal(await page.locator('#keep-firmware-settings').isChecked(), true);
  assert.equal(await page.locator('#keep-firmware-settings').isDisabled(), true);
  assert.equal(await page.locator('#firmware-install-toggle').count(), 0);
  assert.equal(await page.locator('#firmware-current').textContent(), 'v0.4.1');
  assert.equal(await page.locator('#wifi-details').evaluate(element => element.open), false);
  assert.equal(await page.locator('#ssid').isVisible(), false);
  assert.equal(await page.locator('#wifi-form-title').textContent(), '添加网络');
  assert.equal(await page.locator('#wifi-card .card-head #scan').count(), 0);
  assert.equal(await page.locator('#scan').getAttribute('aria-controls'), 'nearby-list');
  assert.equal(await page.locator('#hotspot-help').count(), 0);
  assert.equal(await page.locator('#wifi-details').evaluate(element => element.open), false);
  const nearbyHeading = await page.locator('#nearby-heading').boundingBox();
  const scanButton = await page.locator('#scan').boundingBox();
  assert.ok(Math.abs(nearbyHeading.y + nearbyHeading.height / 2 - scanButton.y - scanButton.height / 2) < 2);
  await page.locator('#wifi-details summary').click();
  await page.locator('label[for="language-en"]').click();
  assert.equal(await page.locator('#wifi-details').evaluate(element => element.open), true);
  assert.equal(await page.locator('#wifi-form-title').textContent(), 'Add a network');
  assert.equal(await page.locator('#hotspot-help').count(), 0);
  await page.locator('#wifi-details summary').click();
  await page.locator('label[for="language-zh"]').click();
  assert.equal(await page.locator('#wifi-details').evaluate(element => element.open), false);
  await page.locator('#scan').click();
  assert.equal(await page.locator('#nearby-section #hotspot-help').count(), 1);
  assert.equal(await page.locator('#hotspot-help').evaluate(element => element.open), false);
  assert.equal(await page.locator('#hotspot-help summary').textContent(), '找不到 iPhone 热点？');
  await page.locator('#hotspot-help summary').click();
  assert.equal(await page.locator('#hotspot-help p').isVisible(), true);
  assert.match(await page.locator('#hotspot-help p').textContent(), /设置 → 个人热点.*允许其他人加入.*最大兼容性.*保持该页亮屏，再扫描/);
  await page.locator('#nearby-list .list-row').first().waitFor();
  assert.equal(await page.locator('#scan').isDisabled(), true);
  assert.equal(await page.locator('#wifi-details').evaluate(element => element.open), false);
  assert.equal(await page.locator('#hotspot-help').evaluate(element => element.open), true);
  await page.getByText('扫描完成', { exact: true }).waitFor();
  assert.equal(await page.locator('#wifi-card #wifi-feedback.success').textContent(), '扫描完成');
  assert.equal(await page.locator('#connection-feedback, #save-feedback').filter({ visible: true }).count(), 0);
  assert.equal(await page.locator('#nearby-list .list-row').count(), 3);
  assert.equal(await page.locator('#wifi-details').evaluate(element => element.open), false);
  assert.equal(await page.locator('#hotspot-help').evaluate(element => element.open), true);
  await page.locator('label[for="language-en"]').click();
  assert.equal(await page.locator('#hotspot-help').evaluate(element => element.open), true);
  assert.equal(await page.locator('#hotspot-help summary').textContent(), 'Can’t find your iPhone hotspot?');
  assert.match(await page.locator('#hotspot-help p').textContent(), /Personal Hotspot.*Allow Others to Join.*Maximize Compatibility.*scan again/);
  await page.locator('label[for="language-zh"]').click();
  await page.locator('#scan').click();
  await page.getByText('扫描完成', { exact: true }).waitFor();
  assert.equal(await page.locator('#hotspot-help').evaluate(element => element.open), true);
  const nearbyBounds = await page.locator('#nearby-list').boundingBox();
  const hotspotHelpBounds = await page.locator('#hotspot-help').boundingBox();
  const wifiFormBounds = await page.locator('#wifi-details').boundingBox();
  assert.ok(hotspotHelpBounds.y >= nearbyBounds.y + nearbyBounds.height);
  assert.ok(hotspotHelpBounds.y + hotspotHelpBounds.height <= wifiFormBounds.y);
  await page.locator('#nearby-section').screenshot({ path: '../build/web-shots/wifi-scan-help.png' });
  await page.setViewportSize({ width: 390, height: 844 });
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
  await page.locator('#nearby-section').evaluate(element => element.scrollIntoView({ block: 'start' }));
  await page.locator('#nearby-section').screenshot({ path: '../build/web-shots/wifi-scan-help-mobile.png' });
  await page.setViewportSize({ width: 1360, height: 1150 });
  await page.locator('#hotspot-help summary').click();
  await page.locator('#wifi-card').screenshot({ path: '../build/web-shots/wifi-collapsed.png' });
  await page.setViewportSize({ width: 390, height: 844 });
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
  await page.locator('#wifi-card').screenshot({ path: '../build/web-shots/wifi-collapsed-mobile.png' });
  await page.setViewportSize({ width: 1360, height: 1150 });
  await page.locator('#saved-networks [data-network="Home Wi-Fi"]').click();
  assert.equal(await page.locator('#wifi-details').evaluate(element => element.open), true);
  assert.equal(await page.locator('#wifi-form-title').textContent(), '编辑网络');
  await page.locator('#wifi-details summary').click();
  await page.locator('#nearby-list [data-network="My iPhone"]').click();
  assert.equal(await page.locator('#wifi-details').evaluate(element => element.open), true);
  assert.equal(await page.locator('#ssid').inputValue(), 'My iPhone');
  assert.equal(await page.locator('#hotspot-help').evaluate(element => element.open), false);
  await page.locator('#save').click();
  await page.getByText('已保存', { exact: true }).waitFor();
  assert.equal(await page.locator('.save-bar #save-feedback.success').isVisible(), true);
  await checkSavedBar(page);
  assert.equal(await page.locator('#ssid').inputValue(), 'My iPhone');
  assert.equal(await page.locator('#wifi-details').evaluate(element => element.open), true);
  await page.locator('#ssid').fill('New Wi-Fi');
  assert.equal(await page.locator('#save-state').textContent(), '有未保存的修改');
  assert.equal(await page.locator('#save-state').isVisible(), true);
  assert.equal(await page.locator('#save-feedback').isVisible(), false);
  await page.locator('#password').fill('short');
  assert.equal(await page.locator('#wifi-form-title').textContent(), '添加网络');
  await page.locator('#wifi-details summary').click();
  await page.locator('#save').click();
  await page.locator('.save-bar #save-feedback.error').waitFor();
  for (const locale of ['zh', 'en']) {
    await page.locator(`label[for="language-${locale}"]`).click();
    for (const width of [1360, 390, 320]) {
      await page.setViewportSize({ width, height: 900 });
      const bar = await page.locator('.save-bar').boundingBox();
      const feedback = await page.locator('#save-feedback').boundingBox();
      const actions = await page.locator('.save-bar > .actions').boundingBox();
      assert.ok(feedback.x >= bar.x && feedback.x + feedback.width <= bar.x + bar.width);
      assert.ok(feedback.y + feedback.height <= actions.y || feedback.x + feedback.width <= actions.x, 'save feedback never overlaps the actions');
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
      await page.locator('.save-bar').screenshot({ path: `../build/web-shots/save-error-${locale}-${width}.png` });
    }
  }
  await page.setViewportSize({ width: 1360, height: 1150 });
  await page.locator('label[for="language-zh"]').click();
  assert.equal(await page.locator('#wifi-details').evaluate(element => element.open), false);
  assert.equal(await page.locator('#password').inputValue(), 'short');
  assert.equal(await page.locator('#wifi-draft').textContent(), 'New Wi-Fi');
  await page.locator('#wifi-details summary').click();
  await page.locator('#password').fill('example-password');
  await page.evaluate(() => { window.fixtureWiFiError = 'TRY fail pass'; });
  await page.locator('#test-wifi').click();
  await page.locator('#wifi-card #wifi-feedback.error').getByText('Wi-Fi 密码认证失败，请核对。', { exact: true }).waitFor();
  assert.equal(await page.locator('#password').inputValue(), 'example-password');
  assert.equal(await page.locator('#save-feedback').isVisible(), false);
  await page.locator('#test-wifi').click();
  await page.locator('#wifi-card #wifi-feedback.success').waitFor();
  assert.equal(await page.locator('#wifi-feedback.error').count(), 0);
  await page.locator('[data-field="open"]').check();
  assert.equal(await page.locator('#password').inputValue(), 'example-password');
  await page.locator('[data-field="open"]').uncheck();
  for (const width of [1360, 390, 320]) {
    await page.setViewportSize({ width, height: 1150 });
    for (const locale of ['en', 'zh']) {
      await page.locator(`label[for="language-${locale}"]`).click();
      const passwordBounds = await page.locator('#password').boundingBox();
      const passwordTitleBounds = await page.locator('.wifi-password-heading > label').first().boundingBox();
      const checkboxBounds = await page.locator('.wifi-password-heading .check').boundingBox();
      const testBounds = await page.locator('#test-wifi').boundingBox();
      assert.ok(checkboxBounds.x >= passwordTitleBounds.x + passwordTitleBounds.width || checkboxBounds.y >= passwordTitleBounds.y + passwordTitleBounds.height);
      assert.ok(Math.abs(checkboxBounds.x + checkboxBounds.width - passwordBounds.x - passwordBounds.width) < 1);
      assert.ok(checkboxBounds.y + checkboxBounds.height <= passwordBounds.y);
      assert.ok(Math.abs(testBounds.x + testBounds.width - passwordBounds.x - passwordBounds.width) < 1);
      assert.ok(testBounds.y >= passwordBounds.y + passwordBounds.height);
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
      assert.equal(await page.getByLabel(locale === 'zh' ? 'Wi-Fi 密码' : 'Wi-Fi password', { exact: true }).inputValue(), 'example-password');
    }
    if (width === 390) await page.locator('#wifi-card').screenshot({ path: '../build/web-shots/wifi-expanded-mobile.png' });
  }
  await page.setViewportSize({ width: 1360, height: 1150 });
  await page.locator('#wifi-card').screenshot({ path: '../build/web-shots/wifi-expanded.png' });
  await page.locator('#save').click();
  await page.getByText('已保存', { exact: true }).waitFor();
  assert.equal(await page.locator('#password').inputValue(), 'example-password');
  await page.locator('#volume').fill('40');
  await page.locator('#save').click();
  await page.getByText('已保存', { exact: true }).waitFor();
  assert.equal(await page.locator('#volume-value').textContent(), '40%');
  await page.locator('#ssid').fill('Unsaved');
  await page.locator('#discard').click();
  await page.locator('#dialog-confirm').click();
  assert.equal(await page.locator('#save').isDisabled(), true);
  assert.equal(await page.locator('#wifi-details').evaluate(element => element.open), false);
  assert.equal(await page.locator('#ssid').inputValue(), '');
  await page.locator('#pairing-details summary').click();
  await page.locator('#name').fill('My Agent');
  const code = Buffer.from(JSON.stringify({ url: 'wss://gateway.example', bootstrapToken: 'demo-bootstrap' })).toString('base64url');
  await page.locator('#address').fill('wss://different.example');
  await page.locator('#code').fill(code);
  await page.locator('#save').click();
  await page.getByText('地址与配对码不一致。请核对，网页不会静默覆盖地址。', { exact: true }).waitFor();
  assert.equal(await page.locator('#address').inputValue(), 'wss://different.example');
  await page.locator('#address').fill('wss://gateway.example');
  await page.locator('#save').click();
  await page.getByText('已保存', { exact: true }).waitFor();
  assert.equal(await page.locator('[data-agent]').count(), 2);
  assert.equal(await page.locator('#code').inputValue(), code);
  await page.clock.install();
  await page.locator('#copy-prompt').scrollIntoViewIfNeeded();
  const noticeBeforeCopy = await page.locator('#save-feedback').textContent();
  const gridBeforeCopy = await page.locator('.grid').boundingBox();
  const nameBeforeCopy = await page.locator('#name').inputValue();
  const addressBeforeCopy = await page.locator('#address').inputValue();
  const ssidBeforeCopy = await page.locator('#ssid').inputValue();
  await page.locator('#code').evaluate(element => { window.codeBeforeCopy = element; });
  await page.locator('#copy-prompt').click();
  await page.getByRole('button', { name: '已复制', exact: true }).waitFor();
  assert.equal(await page.locator('#copy-prompt-status').textContent(), '粘贴给运行网关的 OpenClaw Agent');
  assert.equal(await page.locator('#copy-prompt-status').getAttribute('role'), 'status');
  assert.equal(await page.locator('#copy-prompt').getAttribute('aria-describedby'), 'copy-prompt-status');
  assert.equal(await page.locator('#code').evaluate(element => element === window.codeBeforeCopy), true);
  assert.equal(await page.locator('#save-feedback').textContent(), noticeBeforeCopy);
  assert.equal((await page.locator('.grid').boundingBox()).y, gridBeforeCopy.y);
  assert.equal(await page.locator('#name').inputValue(), nameBeforeCopy);
  assert.equal(await page.locator('#address').inputValue(), addressBeforeCopy);
  assert.equal(await page.locator('#ssid').inputValue(), ssidBeforeCopy);
  assert.ok((await page.evaluate(() => navigator.clipboard.readText())).includes('openclaw qr'));
  assert.equal(await page.locator('#discard').isDisabled(), true);
  await mkdir('../build/web-shots', { recursive: true });
  await page.locator('.prompt-actions').screenshot({ path: '../build/web-shots/prompt-copied.png' });
  await page.clock.fastForward(4100);
  assert.equal(await page.locator('#copy-prompt').textContent(), '复制配置提示词');
  assert.equal(await page.locator('#copy-prompt-status').isVisible(), false);
  let pendingPrompt;
  await page.route('**/setup-zh.txt', route => { pendingPrompt = route; });
  const promptRequest = page.waitForRequest('**/setup-zh.txt');
  await page.locator('#copy-prompt').click();
  await promptRequest;
  assert.equal(await page.locator('#copy-prompt').textContent(), '正在复制…');
  assert.equal(await page.locator('#copy-prompt').isDisabled(), true);
  assert.equal(await page.locator('#copy-prompt').getAttribute('aria-busy'), 'true');
  assert.equal(await page.locator('#refresh').isEnabled(), true);
  await pendingPrompt.fulfill({ status: 503, body: '' });
  await page.getByText('提示词暂时无法读取，请重试。', { exact: true }).waitFor();
  assert.equal(await page.locator('#save-feedback').textContent(), noticeBeforeCopy);
  assert.equal(await page.locator('#copy-prompt').isEnabled(), true);
  await page.unroute('**/setup-zh.txt');
  await page.evaluate(() => {
    window.originalClipboardWrite = navigator.clipboard.writeText.bind(navigator.clipboard);
    navigator.clipboard.writeText = async () => { throw new DOMException('Clipboard denied', 'NotAllowedError'); };
  });
  await page.locator('#copy-prompt').click();
  await page.getByText('未能复制，请允许剪贴板权限后重试。', { exact: true }).waitFor();
  assert.equal(await page.locator('#copy-prompt-status.error').count(), 1);
  assert.equal(await page.locator('#save-feedback').textContent(), noticeBeforeCopy);
  assert.equal(await page.locator('#code').inputValue(), code);
  await page.locator('label[for="language-en"]').click();
  assert.equal(await page.locator('#copy-prompt-status').textContent(), 'Could not copy. Allow clipboard access and retry.');
  await page.evaluate(() => { navigator.clipboard.writeText = window.originalClipboardWrite; });
  await page.locator('#copy-prompt').click();
  await page.getByRole('button', { name: 'Copied', exact: true }).waitFor();
  assert.equal(await page.locator('#copy-prompt-status.error').count(), 0);
  assert.ok((await page.evaluate(() => navigator.clipboard.readText())).includes('openclaw qr'));
  await page.clock.fastForward(4100);
  assert.equal(await page.locator('#copy-prompt').textContent(), 'Copy setup prompt');
  assert.equal(await page.locator('#copy-prompt-status').isVisible(), false);
  await page.locator('label[for="language-zh"]').click();
  const pairingBeforeLanguage = await page.locator('#code').inputValue();
  await page.locator('label[for="language-en"]').click();
  await page.getByRole('heading', { name: 'Device studio', exact: true }).waitFor();
  assert.ok(await page.getByRole('option', { name: 'Simplified Chinese' }).count());
  assert.equal(await page.locator('#code').inputValue(), pairingBeforeLanguage);
  await page.locator('label[for="language-zh"]').click();
  await page.screenshot({ path: '../build/web-shots/desktop.png', fullPage: true });
  await page.setViewportSize({ width: 390, height: 844 });
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
  assert.ok((await page.locator('header').boundingBox()).height <= 72);
  assert.equal(await page.locator('.brand-name').isVisible(), false);
  assert.equal(await page.locator('header h1').isVisible(), true);
  const mobileBrand = await page.locator('.brand').boundingBox();
  const mobileTitle = await page.locator('header h1').boundingBox();
  const mobileLanguage = await page.locator('.language-switch').boundingBox();
  assert.ok(mobileTitle.x >= mobileBrand.x + mobileBrand.width);
  assert.ok(mobileLanguage.x >= mobileTitle.x + mobileTitle.width);
  assert.ok(mobileLanguage.y < mobileBrand.y + mobileBrand.height);
  await page.screenshot({ path: '../build/web-shots/mobile.png' });
  await page.locator('#disconnect').click();
  await page.locator('#connect-ble').waitFor();
  assert.equal(await page.locator('#hotspot-help').count(), 0);
  assert.equal(await page.locator('#code').inputValue(), code);
  await page.locator('#connect-ble').click();
  await page.getByRole('heading', { name: 'AI Passport · Bluetooth', exact: true }).waitFor();
  assert.equal(await page.locator('#hotspot-help').count(), 0);
  assert.equal(await page.locator('#nearby-list .list-row').count(), 0);
  await page.locator('#disconnect').click();
  assert.equal(await page.locator('#demo, #demo-hint, .demo-entry').count(), 0);
  assert.equal(await page.locator('#save').isDisabled(), true);
  assert.equal(await page.locator('#scan').isDisabled(), true);
  await page.route('**/firmware/manifest.json', route => route.fulfill({ status: 404, body: '' }));
  await page.reload();
  await page.getByText('暂时无法获取网站固件，请稍后刷新重试。', { exact: true }).waitFor();
  assert.equal(await page.locator('#flash').isDisabled(), true);
  await page.locator('#keep-firmware-settings').uncheck();
  assert.equal(await page.locator('#flash').isDisabled(), true, 'full installation also requires bundled firmware');
  assert.equal(await page.locator('#firmware-card button').count(), 1);
  assert.equal(await page.locator('input[type="file"]').count(), 0);
  await page.locator('label[for="language-en"]').click();
  await page.getByText('Website firmware is unavailable. Refresh and try again later.', { exact: true }).waitFor();
  assert.equal(await page.locator('#bluetooth-panel').count(), 0);
  await page.evaluate(() => {
    Object.defineProperty(navigator, 'bluetooth', { configurable: true, value: {
      async requestDevice() { throw new DOMException('No device selected', 'NotFoundError'); },
    } });
  });
  await page.locator('#connect-ble').click();
  await page.locator('.connection #connection-feedback').getByText('No Bluetooth device selected.', { exact: true }).waitFor();
  assert.equal(await page.locator('#connection-feedback.error').count(), 0);
  assert.ok((await page.locator('#bluetooth-hint').textContent()).includes('takeover support'));
  assert.ok((await page.locator('#bluetooth-help').textContent()).includes('Privacy & Security'));
  await page.locator('#bluetooth-help summary').click();
  assert.equal(await page.locator('#bluetooth-restart').isVisible(), true);
  assert.match(await page.locator('#bluetooth-restart').textContent(), /Note any unsaved inputs.*not just refresh.*chrome:\/\/restart/);
  assert.equal(await page.getByRole('button', { name: 'Compatibility scan: show all Bluetooth devices', exact: true }).isVisible(), true);
  await page.unroute('**/firmware/manifest.json');
  await page.route('**/firmware/manifest.json', route => route.fulfill({ json: release }));
  await page.reload();
  await page.setViewportSize({ width: 1360, height: 900 });
  await installBluetoothFixture(page);
  await page.locator('#connect-ble').click();
  await page.getByRole('heading', { name: 'AI Passport · Bluetooth', exact: true }).waitFor();
  assert.equal(await page.locator('#bluetooth-panel').count(), 0);
  assert.equal(await page.locator('#firmware-current').textContent(), 'v0.4.1');
  assert.equal(await page.locator('#firmware-connection-hint').textContent(), '刷机请在电脑上使用 USB，蓝牙仅用于配置。');
  assert.equal(await page.locator('#flash').getAttribute('aria-describedby'), 'firmware-mode-note firmware-connection-hint');
  assert.match(await page.locator('.connection .subtle').textContent(), /v0.4.1/);
  assert.equal(await page.locator('#firmware-status').textContent(), '有新版本可更新');
  assert.equal(await page.locator('#firmware-details').evaluate(element => element.open), false);
  assert.equal(await page.locator('.firmware-identifiers').isVisible(), false);
  assert.equal(await page.locator('#flash').textContent(), '更新固件');
  assert.equal(await page.getByText('尚未识别版本', { exact: true }).count(), 0);
  assert.ok((await page.locator('.grid').boundingBox()).y < 340);
  await page.screenshot({ path: '../build/web-shots/compact-connected.png', fullPage: true });
  await page.locator('label[for="language-en"]').click();
  assert.equal(await page.locator('#firmware-current').textContent(), 'v0.4.1');
  assert.equal(await page.locator('#firmware-status').textContent(), 'Update available');
  await page.evaluate(() => { window.fixtureBuild = '1234567890abcdef'; });
  await page.locator('#refresh').click();
  await page.getByText('Up to date', { exact: true }).waitFor();
  assert.equal(await page.locator('#firmware-current').textContent(), 'v0.4.2');
  assert.equal(await page.locator('#firmware-details').evaluate(element => element.open), false);
  assert.equal(await page.locator('#flash').textContent(), 'Update firmware');
  assert.equal(await page.locator('#flash').isDisabled(), true);
  assert.equal(await page.locator('#firmware-build-note').count(), 0);
  await page.evaluate(() => { window.fixtureBuild = 'unknown'; });
  await page.locator('#refresh').click();
  await page.locator('#firmware-current').filter({ hasText: 'Not identified' }).waitFor();
  assert.equal(await page.locator('#firmware-current').textContent(), 'Not identified');
  assert.equal(await page.locator('#firmware-status').count(), 0);
  assert.equal(await page.locator('#flash').textContent(), 'Update firmware');
  await page.emulateMedia({ reducedMotion: 'reduce' });
  await page.locator('label[for="language-zh"]').click();
  assert.equal(await page.locator('.language-thumb').evaluate(element => element.getAnimations().length), 0);
  await page.locator('#language-zh').press('ArrowRight');
  assert.equal(await page.locator('#language-en').isChecked(), true);
  assert.equal(await page.locator('.language-thumb').evaluate(element => element.getAnimations().length), 0);
  await page.setViewportSize({ width: 320, height: 844 });
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
  await page.reload();
  await installBluetoothFixture(page, 'abcdef1234567890', []);
  await page.locator('#connect-ble').click();
  await page.getByRole('heading', { name: 'AI Passport · Bluetooth', exact: true }).waitFor();
  assert.equal(await page.locator('#wifi-details').evaluate(element => element.open), true);
  assert.equal(await page.locator('#saved-networks').textContent(), '尚未保存网络');
  await page.locator('#ssid').fill('Office Wi-Fi');
  await page.locator('#password').fill('example-password');
  await page.locator('#save').click();
  await page.getByText('已保存', { exact: true }).waitFor();
  assert.equal(await page.locator('#wifi-details').evaluate(element => element.open), true);
  assert.equal(await page.locator('#ssid').inputValue(), 'Office Wi-Fi');
  await page.locator('#wifi-details summary').click();
  await page.locator('#disconnect').click();
  assert.equal(await page.locator('#wifi-details').evaluate(element => element.open), false);
  assert.equal(await page.locator('#password').inputValue(), 'example-password');
  await page.reload();
  await page.setViewportSize({ width: 1360, height: 1150 });
  await installBluetoothFixture(page, 'abcdef1234567890', ['Home Wi-Fi'], [
    { name: 'OpenClaw Cloudflare', url: 'wss://cloud.gateway.example', current: true, route: { mode: 'ss', host: 'proxy.example', port: 443, savedSecret: true, method: 'aes-256-cfb' } },
    { name: 'OpenClaw Local', url: 'wss://local.gateway.example', current: false, route: { mode: 'direct' } },
  ]);
  await page.locator('#connect-ble').click();
  await page.getByRole('heading', { name: 'AI Passport · Bluetooth', exact: true }).waitFor();
  assert.equal(await page.locator('[data-route-agent="0"]').textContent(), 'Shadowsocks');
  assert.equal(await page.locator('[data-route-agent="1"]').textContent(), '直连');
  assert.equal(await page.locator('.agent-route-editor, #route-details').count(), 0);
  assert.equal(await page.locator('[data-agent-id="0"] .tag').textContent(), '当前');
  await page.locator('#agent-card').screenshot({ path: '../build/web-shots/agents-routes.png' });
  await page.locator('[data-agent="0"]').click();
  assert.equal(await page.locator('[data-agent="0"]').getAttribute('aria-expanded'), 'true');
  assert.equal(await page.locator('[data-agent-id="0"] #routeMode').count(), 1);
  assert.equal(await page.locator('[data-agent-id="1"] #routeMode').count(), 0);
  assert.equal(await page.locator('#proxySecret').getAttribute('placeholder'), '留空沿用设备中已保存的代理密码');
  assert.equal(await page.locator('select#proxyMethod, [data-field="proxyMethod"]').count(), 0);
  assert.equal(await page.locator('.connection-fact dt').textContent(), '加密方式');
  assert.equal(await page.locator('#proxyMethod').textContent(), 'aes-256-cfb');
  assert.equal(await page.locator('#save').isDisabled(), true);
  assert.equal(await page.locator('#proxyHost').inputValue(), 'proxy.example');
  await page.locator('#proxyHost').fill('new-proxy.example');
  assert.equal(await page.locator('[data-route-dirty="0"]').isVisible(), true);
  await page.locator('[data-agent="0"]').click();
  assert.equal(await page.locator('.agent-route-editor').count(), 0);
  assert.equal(await page.locator('[data-route-dirty="0"]').isVisible(), true);
  await page.locator('[data-agent="0"]').click();
  assert.equal(await page.locator('#proxyHost').inputValue(), 'new-proxy.example');
  await page.locator('[data-agent="1"]').click();
  await page.getByText('放弃当前助手连接方式的未保存修改，转去编辑另一助手？其他配置不受影响。', { exact: true }).waitFor();
  await page.locator('#dialog-cancel').click();
  assert.equal(await page.locator('[data-agent-id="0"] #routeMode').count(), 1);
  assert.equal(await page.locator('#proxyHost').inputValue(), 'new-proxy.example');
  await page.locator('label[for="language-en"]').click();
  assert.equal(await page.locator('#proxyHost').inputValue(), 'new-proxy.example');
  assert.equal(await page.locator('[data-route-dirty="0"]').textContent(), 'Unsaved changes');
  assert.equal(await page.locator('[data-agent="0"]').textContent(), 'Connection settings');
  assert.equal(await page.locator('.connection-fact dt').textContent(), 'Cipher');
  await page.locator('label[for="language-zh"]').click();
  await page.evaluate(() => { window.fixtureRouteError = 'ERR busy'; });
  await page.locator('#save').click();
  await page.locator('.save-bar #save-feedback.error').waitFor();
  assert.equal(await page.locator('#proxyHost').inputValue(), 'new-proxy.example');
  assert.equal(await page.locator('[data-route-dirty="0"]').isVisible(), true);
  await page.locator('#save').click();
  await page.getByText('已保存', { exact: true }).waitFor();
  assert.equal(await page.locator('[data-route-dirty="0"]').isVisible(), false);
  assert.match((await page.evaluate(() => window.fixtureCommands.filter(command => command.startsWith('ROUTE ')))).at(-1), /^ROUTE \d+ 0 ss new-proxy\.example 443 - aes-256-cfb$/);
  assert.equal(await page.locator('#proxySecret').inputValue(), '');
  await page.locator('#agent-card').screenshot({ path: '../build/web-shots/agent-route-editor.png' });
  for (const width of [390, 320]) {
    await page.setViewportSize({ width, height: 844 });
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
    await page.locator('label[for="language-en"]').click();
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
    await page.locator('label[for="language-zh"]').click();
    if (width === 390) await page.locator('#agent-card').screenshot({ path: '../build/web-shots/agent-route-editor-mobile.png' });
  }
  await page.setViewportSize({ width: 1360, height: 1150 });
  await page.locator('#volume').fill('45');
  await page.locator('#proxyHost').fill('discarded.example');
  await page.locator('[data-agent="1"]').click();
  await page.locator('#dialog-confirm').click();
  assert.equal(await page.locator('[data-agent-id="1"] #routeMode').count(), 1);
  assert.equal(await page.locator('[data-route-dirty="0"]').isVisible(), false);
  assert.equal(await page.locator('#volume').inputValue(), '45');
  assert.equal(await page.locator('#save').isEnabled(), true);
  assert.equal(await page.locator('[data-agent-id="0"] .tag').textContent(), '当前');
  assert.equal(await page.locator('[data-agent-id="1"] .tag').count(), 0);
  await page.locator('#routeMode').selectOption('ss');
  await page.locator('#proxyHost').fill('local-proxy.example');
  await page.locator('#proxyPort').fill('8443');
  await page.locator('#proxySecret').fill('example-proxy-secret');
  assert.equal(await page.locator('[data-route-agent="1"]').textContent(), 'Shadowsocks');
  await page.locator('#save').click();
  await page.getByText('已保存', { exact: true }).waitFor();
  assert.match((await page.evaluate(() => window.fixtureCommands.filter(command => command.startsWith('ROUTE ')))).at(-1), /^ROUTE \d+ 1 ss local-proxy\.example 8443 example-proxy-secret aes-256-cfb$/);
  assert.equal(await page.locator('#proxySecret').inputValue(), 'example-proxy-secret');
  await page.locator('[data-agent="0"]').click();
  assert.equal(await page.locator('#proxyHost').inputValue(), 'new-proxy.example');
  await page.locator('#routeMode').selectOption('direct');
  assert.equal(await page.locator('#proxy-fields').isVisible(), false);
  await page.locator('#save').click();
  await page.getByText('已保存', { exact: true }).waitFor();
  assert.equal(await page.locator('[data-route-agent="0"]').textContent(), '直连');
  assert.equal(await page.locator('[data-route-agent="1"]').textContent(), 'Shadowsocks');
  await page.locator('[data-agent="1"]').click();
  assert.equal(await page.locator('#proxyHost').inputValue(), 'local-proxy.example');
  await page.locator('#proxyHost').fill('discarded.example');
  await page.locator('#discard').click();
  await page.locator('#dialog-confirm').click();
  assert.equal(await page.locator('.agent-route-editor').count(), 0);
  assert.equal(await page.locator('[data-route-dirty="1"]').isVisible(), false);
  await page.locator('[data-agent="1"]').click();
  assert.equal(await page.locator('#proxyHost').inputValue(), 'local-proxy.example');
  await page.locator('#proxyHost').fill('updated-local-proxy.example');
  await page.locator('#pairing-details summary').click();
  await page.locator('#name').fill('New Agent');
  await page.locator('#code').fill(code);
  await page.locator('#save').click();
  await page.getByText('已保存', { exact: true }).waitFor();
  assert.equal(await page.locator('[data-agent]').count(), 3);
  assert.match((await page.evaluate(() => window.fixtureCommands.filter(command => command.startsWith('ROUTE ')))).at(-1), /^ROUTE \d+ 1 ss updated-local-proxy\.example 8443 - aes-256-cfb$/);
  assert.equal(await page.locator('[data-agent-id="1"] #routeMode').count(), 1);
  assert.equal(await page.locator('[data-agent-id="2"] .tag').textContent(), '当前');
  assert.equal(await page.locator('[data-route-agent="2"]').textContent(), '直连');
  await page.locator('[data-agent="2"]').click();
  assert.equal(await page.locator('#routeMode').inputValue(), 'direct');
  assert.equal(await page.locator('#proxyHost').inputValue(), '');
  await page.reload();
  await page.locator('#firmware-card').waitFor();
  await installSerialFixture(page);
  await page.locator('#connect-usb').click();
  await page.getByRole('heading', { name: 'AI Passport · USB', exact: true }).waitFor();
  assert.equal(await page.locator('#keep-firmware-settings').isChecked(), true);
  assert.equal(await page.locator('#flash').isEnabled(), true);
  const imageRequests = [];
  for (const image of ['app.bin', 'full.bin']) await page.route(`**/firmware/${image}`, route => {
    imageRequests.push(image);
    return route.fulfill({ status: 404, body: '' });
  });
  await page.locator('#flash').click();
  await page.getByText('更新将保留 Wi-Fi、助手及设备数据。写入期间请勿拔线，确认继续？', { exact: true }).waitFor();
  await page.locator('#dialog-confirm').click();
  await page.getByText('暂时无法获取网站固件，请稍后刷新重试。', { exact: true }).waitFor();
  assert.deepEqual(imageRequests, ['app.bin']);
  assert.equal(await page.locator('#firmware-card #firmware-feedback.error').isVisible(), true);
  assert.equal(await page.locator('#connection-feedback, #save-feedback').filter({ visible: true }).count(), 0);
  assert.equal(await page.locator('#keep-firmware-settings').isChecked(), true);
  assert.equal(await page.locator('#flash').isEnabled(), true);
  await page.locator('#keep-firmware-settings').uncheck();
  await page.locator('#flash').click();
  await page.getByText('完整安装会覆盖设备上的 Wi-Fi 和助手配置。请保持 USB 连接，确定继续？', { exact: true }).waitFor();
  await page.locator('#dialog-cancel').click();
  assert.deepEqual(imageRequests, ['app.bin']);
  await page.locator('#flash').click();
  await page.locator('#dialog-confirm').click();
  await page.getByText('暂时无法获取网站固件，请稍后刷新重试。', { exact: true }).waitFor();
  assert.deepEqual(imageRequests, ['app.bin', 'full.bin']);
  assert.equal(await page.locator('#disconnect').isEnabled(), true);
  for (const locale of ['zh', 'en']) {
    await page.locator(`label[for="language-${locale}"]`).click();
    for (const width of [1360, 390, 320]) {
      await page.setViewportSize({ width, height: 1150 });
      for (const keep of [true, false]) {
        await page.locator('#keep-firmware-settings').setChecked(keep);
        await checkFirmwareAlignment(page);
        assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
        if (width !== 320) {
          await page.locator('#firmware-card').evaluate(element => element.scrollIntoView({ block: 'start' }));
          await page.locator('#firmware-card').screenshot({ path: `../build/web-shots/firmware-${keep ? 'keep' : 'replace'}-${locale}-${width}.png` });
        }
      }
    }
  }
  for (const locale of ['zh', 'en']) {
    await page.locator(`label[for="language-${locale}"]`).click();
    await page.setViewportSize({ width: 1360, height: 1150 });
    for (const state of [
      { build: '1234567890abcdef', status: 'current' },
      { build: 'aaaaaaaaaaaaaaaa', status: 'development' },
      { build: 'bbbbbbbbbbbbbbbb', status: 'newer' },
      { build: 'cccccccccccccccc', status: 'available' },
    ]) {
      await page.locator('#keep-firmware-settings').check();
      await page.evaluate(build => { window.fixtureBuild = build; }, state.build);
      await page.locator('#refresh').click();
      if (['development', 'available'].includes(state.status)) {
        await page.locator('#firmware-status').waitFor({ state: 'detached' });
        assert.equal(await page.locator('#firmware-status').count(), 0, 'no redundant installation availability message');
      } else {
        await page.locator(`#firmware-status[data-status="${state.status}"]`).waitFor();
      }
      if (state.status === 'available') {
        assert.equal(await page.locator('#firmware-current').textContent(), locale === 'zh' ? '暂未识别' : 'Not identified');
        assert.doesNotMatch(await page.locator('.connection .subtle').textContent(), /已安装|Installed/);
      }
      assert.equal(await page.locator('#flash').textContent(), locale === 'zh' ? '更新固件' : 'Update firmware');
      assert.equal(await page.locator('#flash').isEnabled(), true);
      assert.equal(await page.locator('#firmware-details').evaluate(element => element.open), false);
      assert.equal(await page.locator('#firmware-card').textContent().then(value => /不匹配|尚未对应|not mapped/.test(value)), false);
      await page.locator('#firmware-details summary').click();
      assert.match(await page.locator('.firmware-identifiers').textContent(), new RegExp(state.build));
      await page.locator('#firmware-details summary').click();
      await checkFirmwareAlignment(page);
      await page.locator('#firmware-card').evaluate(element => element.scrollIntoView({ block: 'start' }));
      await page.locator('#firmware-card').screenshot({ path: `../build/web-shots/firmware-${state.status}-${locale}.png` });
      await page.locator('#flash').click();
      const confirmation = await page.locator('#confirm-dialog p').textContent();
      if (state.status === 'development') assert.match(confirmation, locale === 'zh' ? /开发版.*可能不包含开发中的功能/ : /development build.*may omit development features/);
      if (state.status === 'newer') assert.match(confirmation, locale === 'zh' ? /更早的固件/ : /older firmware version/);
      await page.locator('#dialog-cancel').click();
      assert.deepEqual(imageRequests, ['app.bin', 'full.bin']);
      if (['development', 'available', 'newer'].includes(state.status)) {
        for (const width of [390, 320]) {
          await page.setViewportSize({ width, height: 1150 });
          await checkFirmwareAlignment(page);
          assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
          await page.locator('#firmware-card').evaluate(element => element.scrollIntoView({ block: 'start' }));
          await page.locator('#firmware-card').screenshot({ path: `../build/web-shots/firmware-${state.status}-${locale}-${width}.png` });
        }
        await page.setViewportSize({ width: 1360, height: 1150 });
      }
    }
  }
  await page.locator('#disconnect').click();
  await page.locator('#connect-usb').click();
  await page.getByRole('heading', { name: 'AI Passport · USB', exact: true }).waitFor();
  assert.equal(await page.locator('#keep-firmware-settings').isChecked(), true);
  await page.locator('#disconnect').click();
  assert.deepEqual(errors, []);
  assert.deepEqual(external, []);
  const capabilities = [
    { name: 'supported', usb: 'available', ble: 'available', secure: true },
    { name: 'no-usb', usb: 'missing', ble: 'available', secure: true },
    { name: 'empty-usb', usb: 'empty', ble: 'available', secure: true },
    { name: 'no-bluetooth', usb: 'available', ble: 'missing', secure: true },
    { name: 'unsupported', usb: 'missing', ble: 'missing', secure: true },
    { name: 'empty-api', usb: 'empty', ble: 'empty', secure: true },
    { name: 'insecure', usb: 'available', ble: 'available', secure: false },
  ];
  for (const capability of capabilities) {
    const capabilityContext = await browser.newContext({ locale: 'zh-CN', viewport: { width: 1360, height: 900 } });
    try {
      await capabilityContext.addInitScript(({ usb, ble, secure }) => {
        window.permissionRequests = { usb: 0, ble: 0 };
        const serial = usb === 'available' ? { async requestPort() { window.permissionRequests.usb++; throw new Error('Unexpected USB permission request'); } } : usb === 'empty' ? {} : undefined;
        const bluetooth = ble === 'available' ? { async requestDevice() { window.permissionRequests.ble++; throw new Error('Unexpected Bluetooth permission request'); } } : ble === 'empty' ? {} : undefined;
        Object.defineProperty(navigator, 'serial', { configurable: true, value: serial });
        Object.defineProperty(navigator, 'bluetooth', { configurable: true, value: bluetooth });
        if (!secure) Object.defineProperty(window, 'isSecureContext', { configurable: true, value: false });
      }, capability);
      const capabilityPage = await capabilityContext.newPage();
      capabilityPage.on('pageerror', error => errors.push(error.message));
      await capabilityPage.route('**/firmware/manifest.json', route => route.fulfill({ json: release }));
      await capabilityPage.goto('http://127.0.0.1:4175');
      await capabilityPage.getByRole('heading', { name: '设备配置台', exact: true }).waitFor();
      assert.equal(await capabilityPage.evaluate(() => window.isSecureContext), capability.secure);
      const hasSerial = capability.secure && capability.usb === 'available';
      const hideUSB = !hasSerial && capability.secure && capability.ble === 'available';
      for (const method of ['usb', 'ble']) {
        const supported = capability.secure && capability[method] === 'available';
        const button = capabilityPage.locator(`#connect-${method}`);
        if (method === 'usb' && hideUSB) {
          assert.equal(await button.count(), 0);
          assert.equal(await capabilityPage.locator('#usb-support').count(), 0);
          continue;
        }
        assert.equal(await button.isEnabled(), supported, `${capability.name}: ${method} button`);
        assert.equal(await capabilityPage.locator(`#${method}-support`).count(), capability.secure && !supported ? 1 : 0);
        assert.equal(await button.getAttribute('aria-describedby'), !capability.secure ? 'browser-security' : supported ? null : `${method}-support`);
      }
      assert.equal(await capabilityPage.locator('.environment').count(), capability.secure ? 0 : 1);
      const bluefyLink = capabilityPage.getByRole('link', { name: '在 App Store 获取 Bluefy', exact: true });
      assert.equal(await bluefyLink.count(), capability.secure && capability.ble !== 'available' ? 1 : 0);
      if (await bluefyLink.count()) {
        assert.equal(await bluefyLink.getAttribute('href'), 'https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055');
        assert.equal(await bluefyLink.getAttribute('target'), '_blank');
        assert.match(await capabilityPage.locator('#ble-support').textContent(), /安装后，在 Bluefy 中打开本页/);
        assert.match(await capabilityPage.locator('#ble-support').textContent(), /iPhone 的 Safari／Chrome 无法让网页连接设备/);
        assert.match(await capabilityPage.locator('#ble-support').textContent(), /配置完成即可关闭 Bluefy/);
      }
      assert.equal(await capabilityPage.locator('#browser-security').count(), capability.secure ? 0 : 1);
      assert.equal(await capabilityPage.locator('#bluetooth-panel').count(), 0);
      assert.equal(await capabilityPage.locator('#demo, #demo-hint, .demo-entry').count(), 0);
      assert.equal(await capabilityPage.locator('#firmware-connection-hint').count(), 0);
      assert.equal(await capabilityPage.locator('#firmware-card button, #firmware-card input, #firmware-card dl, #firmware-card details').count() > 0, hasSerial);
      if (!hasSerial) assert.equal(await capabilityPage.locator('#firmware-card').textContent(), '固件更新请在电脑上通过 USB 完成。');
      assert.equal(await capabilityPage.getByText('电脑刷机请用 Chrome / Edge。', { exact: true }).count(), 0);
      if (capability.name === 'unsupported') await capabilityPage.screenshot({ path: '../build/web-shots/browser-unsupported.png' });
      for (const width of [390, 320]) {
        await capabilityPage.setViewportSize({ width, height: 844 });
        await checkConnectionChoices(capabilityPage, 'zh');
        assert.equal(await capabilityPage.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true, `${capability.name}: ${width}px overflow`);
        assert.ok((await capabilityPage.locator('header').boundingBox()).height <= 72);
        assert.equal(await capabilityPage.locator('footer .footer-privacy').isVisible(), true);
        assert.equal(await capabilityPage.locator('.connection-privacy, .connection .footer-privacy').count(), 0);
        const privacyBounds = await capabilityPage.locator('.footer-privacy').boundingBox();
        const footerBounds = await capabilityPage.locator('footer').boundingBox();
        assert.ok(privacyBounds.x >= footerBounds.x && privacyBounds.x + privacyBounds.width <= footerBounds.x + footerBounds.width);
        if (width === 390 && capability.name === 'supported') await capabilityPage.locator('footer').screenshot({ path: '../build/web-shots/footer-mobile.png' });
        const titleBounds = await capabilityPage.locator('header h1').boundingBox();
        const navBounds = await capabilityPage.locator('header nav').boundingBox();
        assert.ok(navBounds.x >= titleBounds.x + titleBounds.width);
        assert.ok(Math.abs(navBounds.y + navBounds.height / 2 - titleBounds.y - titleBounds.height / 2) < 1);
        if (hasSerial) {
          const currentBounds = await capabilityPage.locator('.firmware-versions > div').first().boundingBox();
          const availableBounds = await capabilityPage.locator('.firmware-versions > div').last().boundingBox();
          assert.equal(currentBounds.y, availableBounds.y);
          assert.ok(availableBounds.x >= currentBounds.x + currentBounds.width);
          await checkFirmwareAlignment(capabilityPage);
        }
        if (capability.name === 'supported') await checkPrivacyDialog(capabilityPage, 'zh', `privacy-zh-${width}`);
        if (width === 390 && capability.name === 'supported') await capabilityPage.locator('#firmware-card').screenshot({ path: '../build/web-shots/firmware-compact-mobile.png' });
        if (width === 390 && ['supported', 'unsupported'].includes(capability.name)) await capabilityPage.screenshot({ path: `../build/web-shots/browser-${capability.name}-mobile.png` });
      }
      await capabilityPage.locator('#ssid').fill('Unsaved example network');
      await capabilityPage.locator('label[for="language-en"]').click();
      await capabilityPage.getByRole('heading', { name: 'Device studio', exact: true }).waitFor();
      if (await bluefyLink.count()) assert.fail('Chinese install label must change with the interface language');
      assert.equal(await capabilityPage.getByRole('link', { name: 'Get Bluefy on the App Store', exact: true }).count(), capability.secure && capability.ble !== 'available' ? 1 : 0);
      if (capability.name === 'supported') {
        await checkPrivacyDialog(capabilityPage, 'en', 'privacy-en-mobile');
        assert.equal(await capabilityPage.locator('#ssid').inputValue(), 'Unsaved example network');
      }
      assert.equal(await capabilityPage.locator('#demo, #demo-hint, .demo-entry').count(), 0);
      assert.equal(await capabilityPage.locator('#ssid').inputValue(), 'Unsaved example network');
      assert.equal(await capabilityPage.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
      const englishTitleBounds = await capabilityPage.locator('header h1').boundingBox();
      const englishNavBounds = await capabilityPage.locator('header nav').boundingBox();
      assert.ok(englishNavBounds.x >= englishTitleBounds.x + englishTitleBounds.width);
      assert.ok((await capabilityPage.locator('header').boundingBox()).height <= 72);
      if (!hasSerial) assert.equal(await capabilityPage.locator('#firmware-card').textContent(), 'Update firmware via USB on a computer.');
      if (capability.secure && capability.usb !== 'available' && !hideUSB) assert.match(await capabilityPage.locator('#usb-support').textContent(), /Web Serial/);
      if (capability.secure && capability.ble !== 'available') {
        assert.match(await capabilityPage.locator('#ble-support').textContent(), /Safari and Chrome on iPhone cannot connect web pages to Bluetooth devices/);
        assert.match(await capabilityPage.locator('#ble-support').textContent(), /You can close Bluefy after setup/);
      }
      if (!capability.secure) assert.match(await capabilityPage.locator('#browser-security').textContent(), /secure context/);
      assert.deepEqual(await capabilityPage.evaluate(() => window.permissionRequests), { usb: 0, ble: 0 });
      if (capability.name === 'no-usb') {
        await installBluetoothFixture(capabilityPage, release.buildId);
        await capabilityPage.locator('#connect-ble').click();
        await capabilityPage.locator('#disconnect').waitFor();
        assert.equal(await capabilityPage.locator('#firmware-card button, #firmware-card input').count(), 0);
        assert.equal(await capabilityPage.locator('#ssid').inputValue(), 'Unsaved example network');
        await capabilityPage.locator('#saved-networks [data-network="Home Wi-Fi"]').click();
        await capabilityPage.locator('#volume').fill('45');
        await capabilityPage.locator('#save').click();
        await capabilityPage.getByText('Saved', { exact: true }).waitFor();
        await checkSavedBar(capabilityPage, 'bluefy');
        await capabilityPage.locator('#disconnect').click();
        assert.equal(await capabilityPage.locator('#connect-usb').count(), 0);
      }
    } finally {
      await capabilityContext.close();
    }
  }
  assert.deepEqual(errors, []);
  console.log('Browser checks passed: real-device-only connection UI, automatic connection capability detection without permission prompts, firmware update/recovery separation, no local image picker, localhost secure context, mocked Bluetooth, progressive scan, save/error retention, per-agent connection editors and route ownership, pairing, copy prompt, bilingual UI, mobile layout, no third-party requests.');
} finally {
  await browser?.close();
  server.kill('SIGTERM');
}

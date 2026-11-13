import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import { extractCommunityCopy, renderCommunityCopy } from '../tools/community_copy.mjs';

test('soft wraps become spaces while paragraphs remain separate', () => {
  const markdown = '## Description\n\nAsk records a\nquestion; follow up without\nstarting over.\n\nA second\nparagraph.\n\n## Next\n\nOther copy.';
  const value = extractCommunityCopy(markdown, 'Description');
  assert.equal(value, 'Ask records a question; follow up without starting over.\n\nA second paragraph.');
  assert.equal(renderCommunityCopy(value), '<p>Ask records a question; follow up without starting over.</p><p>A second paragraph.</p>');
});

test('Chinese soft wraps do not insert artificial spaces', () => {
  assert.equal(extractCommunityCopy('## 简介\n\n配置网络，\n继续对话。\n\n设备通过 Wi-Fi\n独立联网。', '简介'), '配置网络，继续对话。\n\n设备通过 Wi-Fi 独立联网。');
});

test('instruction and update boundaries survive continuation folding', () => {
  const steps = extractCommunityCopy('## Steps\n\n1. Open the\n   page.\n2. Save\n   settings.', 'Steps');
  assert.equal(steps, '1. Open the page.\n2. Save settings.');
  assert.equal(renderCommunityCopy(steps), '<ol><li>Open the page.</li><li>Save settings.</li></ol>');
  const notes = extractCommunityCopy('## Notes\n\n- Improve\n  setup.\n- Keep\n  settings.', 'Notes');
  assert.equal(renderCommunityCopy(notes), '<ul><li>Improve setup.</li><li>Keep settings.</li></ul>');
});

test('copy is escaped and missing sections fail clearly', () => {
  assert.equal(renderCommunityCopy('A <script> & example'), '<p>A &lt;script&gt; &amp; example</p>');
  assert.throws(() => extractCommunityCopy('## Other\n\nText', 'Missing'), /Missing community copy section/);
  assert.equal(extractCommunityCopy('## Description\r\n\r\nFirst\r\nsecond', 'Description'), 'First second');
});

test('actual bilingual submission copy has natural paragraphs and intact lists', async () => {
  for (const [suffix, headings] of [['', ['Description', 'Quick-start instructions', 'Update notes']], ['.zh_CN', ['简介', '使用方法', '更新日志']]]) {
    const document = await readFile(new URL(`../docs/release/community${suffix}.md`, import.meta.url), 'utf8');
    const description = extractCommunityCopy(document, headings[0]);
    assert.equal(description.split('\n\n').length, 4);
    assert.equal(description.replaceAll('\n\n', '').includes('\n'), false);
    const steps = extractCommunityCopy(document, headings[1]);
    assert.equal(steps.split('\n').length, 4);
    assert.equal((renderCommunityCopy(steps).match(/<li>/g) || []).length, 4);
    assert.equal(extractCommunityCopy(document, headings[2]).split('\n').length, 4);
  }
});

test('community titles are bilingual product names, independent from repository branding', async () => {
  for (const [suffix, heading, expected] of [['', 'Title', 'Agent Talk'], ['.zh_CN', '标题', '随身搭档']]) {
    const document = await readFile(new URL(`../docs/release/community${suffix}.md`, import.meta.url), 'utf8');
    const title = extractCommunityCopy(document, heading);
    assert.equal(title, expected);
    assert.doesNotMatch(title, /AI Passport|OpenClaw/u);
    assert.match(document, /https:\/\/github\.com\/vangie\/ai-passport-agent/u);
  }
});

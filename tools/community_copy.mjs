function joinLines(value) {
  return value.split('\n').map(line => line.trim()).reduce((joined, line) => {
    if (!joined) return line;
    const cjkBoundary = (/\p{Script=Han}$/u.test(joined) && /^\p{Script=Han}/u.test(line)) || /[，。；：、！？「」《》]$/.test(joined) || /^[，。；：、！？「」《》]/.test(line);
    return joined + (cjkBoundary ? '' : ' ') + line;
  }, '');
}

export function extractCommunityCopy(document, heading) {
  const marker = `## ${heading}\n\n`;
  const normalized = document.replaceAll('\r\n', '\n');
  const start = normalized.indexOf(marker);
  if (start < 0) throw new Error(`Missing community copy section: ${heading}`);
  const value = normalized.slice(start + marker.length).split('\n## ')[0].trim();
  return value.split(/\n\s*\n/).map(block => block.split(/\n(?=[ \t]*(?:\d+\.|-)\s)/).map(joinLines).join('\n')).join('\n\n');
}

const escape = value => value.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;');

export function renderCommunityCopy(value) {
  return value.split('\n\n').map(block => {
    const lines = block.split('\n');
    const ordered = lines.every(line => /^\d+\.\s/.test(line));
    const unordered = lines.every(line => /^-\s/.test(line));
    if (ordered || unordered) {
      const tag = ordered ? 'ol' : 'ul';
      return `<${tag}>${lines.map(line => `<li>${escape(line.replace(/^(?:\d+\.|-)\s+/, ''))}</li>`).join('')}</${tag}>`;
    }
    return `<p>${escape(block)}</p>`;
  }).join('');
}

// Host tests for web/theme.js: colour parsing, validation, migration of the old
// saved values, import/export and contrast. Run: node --test croaster-webui/test/*.test.js
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const T = require('../web/theme.js');

test('normalizeHex accepts #rgb and #rrggbb in any case, rejects the rest', () => {
  assert.equal(T.normalizeHex('#ABC'), '#aabbcc');
  assert.equal(T.normalizeHex(' 4cf27c '), '#4cf27c');
  assert.equal(T.normalizeHex('#4CF27C'), '#4cf27c');
  for (const bad of ['', '#12', '#12345', '#1234567', 'red', 'rgb(0,0,0)', '#ggg000', null, 42, undefined]) {
    assert.equal(T.normalizeHex(bad), null, String(bad));
  }
});

test('every preset defines every token as #rrggbb and passes the contrast checks', () => {
  for (const name of Object.keys(T.PRESETS)) {
    const t = T.fromPreset(name);
    for (const tok of T.TOKENS) assert.match(t.colors[tok.key], /^#[0-9a-f]{6}$/, name + ' ' + tok.key);
    assert.deepEqual(Object.keys(T.PRESETS[name].colors).sort(), T.TOKENS.map((x) => x.key).sort(), name);
    assert.deepEqual(T.contrastWarnings(t), [], name);
    assert.equal(T.normalize(t).errors.length, 0, name);
  }
});

test('the default is the neon green dark scheme, and its chart series stay apart from green', () => {
  assert.equal(T.DEFAULT_PRESET, 'neon');
  const c = T.fromPreset('neon').colors;
  assert.equal(c.accent, c.bt);
  assert.ok(T.luminance(c.bg) < 0.01, 'near-black background');
  const hue = (hex) => {
    const [r, g, b] = [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16) / 255);
    const max = Math.max(r, g, b), d = max - Math.min(r, g, b);
    const h = max === r ? ((g - b) / d) % 6 : max === g ? (b - r) / d + 2 : (r - g) / d + 4;
    return (h * 60 + 360) % 360;
  };
  for (const k of ['et', 'ror', 'ror-et', 'heat', 'fan']) {
    const diff = Math.abs(hue(c[k]) - hue(c.bt));
    assert.ok(Math.min(diff, 360 - diff) > 40, k + ' hue is too close to BT');
  }
});

test('migrate: nothing saved or the old "dark" gives the default; "light" stays light', () => {
  assert.equal(T.migrate(null).theme.preset, 'neon');
  assert.equal(T.migrate(null).migrated, false);
  const dark = T.migrate('dark');
  assert.equal(dark.theme.preset, 'neon');
  assert.equal(dark.migrated, true);
  const light = T.migrate('light');
  assert.deepEqual(light.theme, T.fromPreset('light'));
  assert.equal(light.migrated, true);
});

test('migrate: a saved JSON scheme round-trips; junk falls back to the default', () => {
  const t = T.fromPreset('copper');
  t.colors.accent = '#123456';
  t.style.radius = 3;
  assert.deepEqual(T.migrate(T.serialize(t)).theme, t);
  const junk = T.migrate('{not json');
  assert.equal(junk.theme.preset, 'neon');
  assert.equal(junk.migrated, true);
  assert.equal(junk.errors.length, 1);
});

test('normalize fills missing values from the preset, drops unknown keys and reports bad values', () => {
  const r = T.normalize({ preset: 'light', colors: { text: '#000', bg: 'nope', bogus: '#ffffff' },
    style: { radius: 99, scale: '120', density: 'tight', reducedMotion: 'yes', extra: 1 } });
  assert.equal(r.theme.colors.text, '#000000');
  assert.equal(r.theme.colors.bg, T.PRESETS.light.colors.bg);
  assert.equal(r.theme.colors.bogus, undefined);
  assert.equal(r.theme.style.radius, 24, 'clamped to max');
  assert.equal(r.theme.style.scale, 120, 'numeric string accepted');
  assert.equal(r.theme.style.density, 'comfortable');
  assert.equal(r.theme.style.reducedMotion, false);
  assert.equal(r.theme.style.extra, undefined);
  assert.equal(r.errors.length, 3, r.errors.join(' | '));
  assert.equal(T.normalize({ preset: 'nosuch' }).theme.preset, 'neon');
  assert.equal(T.normalize([]).errors.length, 1);
});

test('style numbers snap to their step', () => {
  assert.equal(T.normalize({ style: { lineWidth: 2.3 } }).theme.style.lineWidth, 2.5);
  assert.equal(T.normalize({ style: { lineWidth: 0 } }).theme.style.lineWidth, 1);
  assert.equal(T.normalize({ style: { scale: 103 } }).theme.style.scale, 105);
});

test('parseImport: export round-trip, partial files, and rejects', () => {
  const t = T.fromPreset('graphite');
  t.colors.bt = '#ff00ff';
  const text = T.serialize(t, true);
  assert.equal(JSON.parse(text).format, 'croaster-theme');
  assert.deepEqual(T.parseImport(text).theme, t);
  const partial = T.parseImport('{"colors":{"accent":"#00ff00"}}');
  assert.equal(partial.theme.colors.accent, '#00ff00');
  assert.equal(partial.theme.colors.bg, T.PRESETS.neon.colors.bg);
  assert.equal(T.parseImport('nope').theme, null);
  assert.equal(T.parseImport('[1,2]').theme, null);
  assert.equal(T.parseImport('{"a":1}').theme, null);
  assert.equal(T.parseImport('{"format":"other","colors":{}}').theme, null);
  assert.equal(T.parseImport('{"v":2,"colors":{}}').theme, null);
});

test('contrast ratio matches WCAG reference values', () => {
  assert.equal(Math.round(T.contrast('#000000', '#ffffff') * 100) / 100, 21);
  assert.equal(T.contrast('#777777', '#777777'), 1);
  assert.equal(Math.round(T.contrast('#767676', '#ffffff') * 100) / 100, 4.54);
  assert.equal(T.contrast('#ffffff', '#000000'), T.contrast('#000000', '#ffffff'));
});

test('contrastWarnings flags low text contrast, including translucent cards over the background', () => {
  const t = T.fromPreset('neon');
  t.colors.text = '#333333';
  const labels = T.contrastWarnings(t).map((w) => w.label);
  assert.ok(labels.includes('Text on background'));
  assert.ok(labels.includes('Text on cards'));
  const glass = T.fromPreset('light');
  glass.colors.bg = '#000000';
  glass.colors.text = '#666666';
  glass.style.glass = 40; // a white card at 40% over black is mid-grey
  assert.ok(T.contrastWarnings(glass).some((w) => w.label === 'Text on cards'));
  glass.style.glass = 100;
  assert.ok(!T.contrastWarnings(glass).some((w) => w.label === 'Text on cards'));
});

test('cssVars turns style options into custom properties and root attributes', () => {
  const t = T.fromPreset('neon');
  Object.assign(t.style, { radius: 6, scale: 120, lineWidth: 3, numerals: 'sans', density: 'compact',
    reducedMotion: true, glass: 80, glow: 0 });
  const css = T.cssVars(t);
  assert.equal(css.vars['--radius'], '6px');
  assert.equal(css.vars['--font-scale'], '1.2');
  assert.equal(css.vars['--line-width'], '3');
  assert.equal(css.vars['--num-font'], 'var(--font)');
  assert.equal(css.vars['--panel-alpha'], '80%');
  assert.equal(css.vars['--glow'], '0');
  assert.equal(css.vars['--bt'], t.colors.bt);
  assert.deepEqual(css.attrs, { density: 'compact', motion: 'reduce' });
  assert.equal(css.scheme, 'dark');
  assert.equal(T.cssVars(T.fromPreset('light')).scheme, 'light');
});

test('apply sets properties and attributes on an element', () => {
  const props = {};
  const elem = { style: { setProperty: (k, v) => { props[k] = v; } }, dataset: {} };
  T.apply(T.fromPreset('contrast'), elem);
  assert.equal(props['--bg'], '#000000');
  assert.equal(elem.dataset.density, 'comfortable');
  assert.equal(elem.style.colorScheme, 'dark');
});

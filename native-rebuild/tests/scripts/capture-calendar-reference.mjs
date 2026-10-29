// Development-only reference capture. Reads original assets without starting Electron.
// Usage: node capture-calendar-reference.mjs <original-widgets-dir> <output-dir> <playwright-dir>
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';
import { createHash } from 'node:crypto';

const [original, destination, playwrightDirectory] = process.argv.slice(2);
if (!original || !destination || !playwrightDirectory) {
  throw new Error('Pass the original widgets directory, output directory and installed Playwright directory.');
}
const source = await fs.realpath(original);
const output = path.resolve(destination);
await fs.mkdir(output, { recursive: true });
const { chromium } = createRequire(import.meta.url)(path.resolve(playwrightDirectory));
const files = ['index.html', 'widget.css', 'calendar-theme.css', 'calendar-picker.css',
  'calendar-todos.css', 'calendar.js', 'calendar-picker.js', 'renderer.js'];
const sourceHashes = Object.fromEntries(await Promise.all(files.map(async name =>
  [name, createHash('sha256').update(await fs.readFile(path.join(source, name))).digest('hex')])));
const browser = await chromium.launch({ headless: true, channel: 'msedge' });
const results = [];
try {
  const context = await browser.newContext({ bypassCSP: true, timezoneId: 'Asia/Shanghai', offline: true });
  await context.addInitScript(() => {
    const listeners = new Set();
    window.desktopWidget = {
      onUpdate(fn) { listeners.add(fn); return () => listeners.delete(fn); },
      setGlassDiscs(value) { window.referenceDiscs = structuredClone(value); },
      setGlassPanels(value) { window.referencePanels = structuredClone(value); },
    };
  });
  const page = await context.newPage();
  page.setDefaultTimeout(5000);
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.clock.setFixedTime(new Date('2026-09-25T04:00:00Z'));
  for (const theme of ['spacious', 'split']) {
    for (const [width, height] of [[820, 500], [320, 320], [480, 390], [600, 420], [1440, 720]]) {
      await page.setViewportSize({ width, height });
      const url = pathToFileURL(path.join(source, 'index.html'));
      url.search = new URLSearchParams({ kind: 'calendar', theme }).toString();
      await page.goto(url.href);
      await page.locator('#days .day').first().waitFor({ state: 'attached' });
      assert.equal(await page.locator('#days .day').count(), 42);
      // A neutral background makes transparent glyphs legible; it is not glass evidence.
      await page.addStyleTag({ content: 'body{background:#202020}' });
      await page.evaluate(() => {
        document.querySelector('#calendar-create').disabled = true;
        document.querySelector('#calendar-agenda-toggle').disabled = true;
      });
      await page.evaluate(() => document.fonts.ready);
      const measurement = await page.evaluate(() => {
        const rect = element => {
          const r = element.getBoundingClientRect();
          const style = getComputedStyle(element);
          return { x:r.x, y:r.y, width:r.width, height:r.height,
            font:style.fontFamily, fontSize:style.fontSize, fontWeight:style.fontWeight,
            visible:style.visibility !== 'hidden' && style.display !== 'none',
            color:style.color, lineHeight:style.lineHeight };
        };
        return {
          theme:document.querySelector('#widget').dataset.calendarTheme,
          month:document.querySelector('#days').dataset.month,
          selectors:Object.fromEntries(['#widget', '#calendar', '.calendar-header', '#previous', '#month',
            '#month-label', '#next', '.calendar-actions', '.weekdays', '#days', '#calendar-summary',
            '#split-day-number', '#split-lunar', '#split-remaining'].map(selector =>
            [selector,rect(document.querySelector(selector))])),
          cells:[...document.querySelectorAll('#days .day')].map(cell => ({
            date:cell.dataset.date, selected:cell.getAttribute('aria-selected'),
            ...rect(cell), number:rect(cell.querySelector('.day-number')),
            lunar:rect(cell.querySelector('.day-lunar')),
          })),
        };
      });
      assert.equal(measurement.month, '2026-09');
      const stem = `${theme}-${width}x${height}`;
      await page.screenshot({ path:path.join(output, `${stem}.png`) });
      await page.locator('#month').click();
      const picker = await page.locator('#month-picker').boundingBox();
      assert.ok(picker && picker.x >= 0 && picker.y >= 0 &&
        picker.x + picker.width <= width + 1 && picker.y + picker.height <= height + 1);
      assert.equal(await page.locator('#picker-months button').count(), 12);
      await page.screenshot({ path:path.join(output, `${stem}-picker.png`) });
      await page.keyboard.press('Escape');
      assert.equal(await page.locator('#month').evaluate(element => element === document.activeElement), true);
      results.push({ width, height, ...measurement, picker });
    }
  }
  assert.deepEqual(errors, []);
  await fs.writeFile(path.join(output, 'reference-layout.json'), JSON.stringify({
    fixedDate:'2026-09-25', timezone:'Asia/Shanghai', source, sourceHashes,
    scope:'original CSS geometry and text; neutral background; original lunar data; no native glass',
    results,
  }, null, 2));
  console.log(`CALENDAR_REFERENCE_PASS: ${results.length} layouts, ${results.length * 2} screenshots`);
} finally {
  await browser.close();
}

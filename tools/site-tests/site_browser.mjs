// Browser tests of the project site as it is published: tools/release/stage_site.py's layout, served under
// /project-vekta/ from a local static server on 127.0.0.1 that, like GitHub Pages, answers an unknown address with
// 404.html. What tools/release/check_site.py cannot see
// because it only reads the files: what a browser does with them.
//
//   1. axe-core finds no WCAG 2.2 A/AA violation on either page, in the dark and the light theme, at a desktop, a
//      short desktop and a phone size, with each section of the main page shown in turn, and with the viewer and the
//      controls help open;
//   2. the menu stays inside its frame and never covers the footer, at sizes from 1024x576 to 1920x1080;
//   3. the screenshot viewer is a modal dialog: opened by keyboard or by a #shot-N link it holds the focus, and closed
//      by Esc, by its Close button or by the browser's Back it gives the focus back to the thumbnail;
//   4. a broken or hostile address (#%, a lone surrogate, markup, a very long fragment) leaves a section shown and
//      raises no script error;
//   5. game pads (simulated: a standard Xbox one, and a DualSense outside the standard layout): the d-pad, sticks,
//      A, B, LB, RB, RT and Start each do their job, the pad used last drives, a held direction repeats, the legend
//      follows the pad, and the d-pad and A alone reach every link, button and screenshot of every section, with B
//      leading back to the menu; the keyboard: digits, Home, End and Page Down from the menu, Enter and the right
//      arrow onto the section's title, the arrows in the screenshot grid, the Controls help, find in page;
//   6. weight and layout shift: no request leaves the site; with the script and the font held back (a slow line)
//      the layout moves by less than CLS 0.05 on a desktop and a phone; with no script at all CSS still shows one
//      section, the one the address names; the home view and the screens view stay within their byte budgets;
//   7. the published files, with the pages' own Content-Security-Policy in force (every other suite bypasses it to
//      inject axe and the simulated pads): browsing, the viewer, the help and its form method="dialog" raise no
//      policy violation and no script error; an unknown address gets the 404 page, styled; the old /site/ address
//      sends the visitor to the root; the manifest and its icons load; printed, every section shows, without the
//      menu, and an outside link prints its address.
//
//   cd tools/site-tests && npm ci --ignore-scripts && npx playwright install chromium   (once)
//   node tools/site-tests/site_browser.mjs [--only NAME]
//
// Exits 1 on any failure. Needs no game data and no GPU.
import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '..', '..');
const AXE = fs.readFileSync(path.join(HERE, 'node_modules', 'axe-core', 'axe.min.js'), 'utf8');
const SECTIONS = ['top', 'shots', 'status', 'news', 'progress', 'install', 'requirements', 'faq', 'about', 'channels', 'legal'];
const WCAG = ['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa', 'wcag22aa'];
const TYPES = { '.html': 'text/html; charset=utf-8', '.css': 'text/css', '.js': 'text/javascript', '.png': 'image/png',
  '.svg': 'image/svg+xml', '.webp': 'image/webp', '.woff2': 'font/woff2', '.json': 'application/json', '.xml': 'application/xml',
  '.webmanifest': 'application/manifest+json', '.txt': 'text/plain; charset=utf-8' };
const PREFIX = '/project-vekta/';
const only = (() => { const i = process.argv.indexOf('--only'); return i > 0 ? process.argv[i + 1] : null; })();

let fails = 0;
function check(cond, what) {
  if (!cond) { fails += 1; console.log('FAIL:', what); }
}

// The staged site over http, read-only, under PREFIX; an unknown address gets 404.html with status 404, as on Pages.
function stage() {
  const out = fs.mkdtempSync(path.join(os.tmpdir(), 'vekta-site-'));
  const dir = path.join(out, '_site');
  const r = spawnSync('python3', [path.join(ROOT, 'tools', 'release', 'stage_site.py'), dir], { encoding: 'utf8' });
  if (r.status !== 0) { console.log(r.stdout + r.stderr); process.exit(1); }
  return { out, dir };
}

function serve(dir) {
  const server = http.createServer((req, res) => {
    let rel = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    let file = null;
    if (rel.startsWith(PREFIX)) {
      rel = rel.slice(PREFIX.length);
      if (rel === '' || rel.endsWith('/')) rel += 'index.html';
      file = path.resolve(dir, rel);
    }
    const ok = file && file.startsWith(dir + path.sep) && fs.existsSync(file) && fs.statSync(file).isFile();
    if (!ok) file = path.join(dir, '404.html');
    res.writeHead(ok ? 200 : 404, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
    fs.createReadStream(file).pipe(res);
  });
  return new Promise(done => server.listen(0, '127.0.0.1', () => done(server)));
}

// The site loads nothing from outside; a request that tries is answered empty (the tests never touch the network)
// and counted, and the weight suite fails on any.
const outside = [];
async function context(browser, opts) {
  const ctx = await browser.newContext({ bypassCSP: true, ...opts });
  await ctx.route(/^https?:\/\/(?!127\.0\.0\.1)/, route => { outside.push(route.request().url()); route.fulfill({ status: 200, body: '' }); });
  return ctx;
}

async function settle(page) { await page.waitForTimeout(250); }

async function focused(page) {
  return page.evaluate(() => {
    const a = document.activeElement;
    if (!a || a === document.body) return 'body';
    const fig = a.closest('figure');
    return a.tagName.toLowerCase() + (a.id ? '#' + a.id : '') + (fig ? ' in #' + fig.id : '') + (a.closest('dialog') ? ' in dialog' : '');
  });
}

async function axe(base, browser) {
  const sizes = [['desktop', 1366, 768], ['short', 1366, 560], ['phone', 390, 844]];
  for (const scheme of ['dark', 'light']) {
    for (const [name, width, height] of sizes) {
      const ctx = await context(browser, { viewport: { width, height }, colorScheme: scheme });
      const page = await ctx.newPage();
      const targets = SECTIONS.map(s => 'index.html#' + s).concat(['plain.html']);
      for (const t of targets) {
        await page.goto(base + t, { waitUntil: 'load' });
        await settle(page);
        await page.addScriptTag({ content: AXE });
        const found = await page.evaluate(async tags => {
          const r = await window.axe.run(document, { runOnly: { type: 'tag', values: tags } });
          return r.violations.map(v => `${v.id} (${v.nodes.length}): ${v.nodes.slice(0, 3).map(n => n.target.join(' ')).join(', ')}`);
        }, WCAG);
        check(found.length === 0, `axe ${scheme} ${name} ${t}: ${found.join(' | ')}`);
      }
      if (name !== 'phone') {   // the two dialogs, open
        await page.goto(base + 'index.html#shot-3', { waitUntil: 'load' });
        await settle(page);
        for (const which of ['viewer', 'help']) {
          if (which === 'help') {
            await page.keyboard.press('Escape');
            await settle(page);
            await page.click('.help-btn');
            await settle(page);
          }
          await page.addScriptTag({ content: AXE });
          const found = await page.evaluate(async tags => {
            const r = await window.axe.run(document, { runOnly: { type: 'tag', values: tags } });
            return r.violations.map(v => `${v.id} (${v.nodes.length}): ${v.nodes.slice(0, 3).map(n => n.target.join(' ')).join(', ')}`);
          }, WCAG);
          check(found.length === 0, `axe ${scheme} ${name} ${which} open: ${found.join(' | ')}`);
        }
      }
      await ctx.close();
    }
  }
}

async function layout(base, browser) {
  for (const [width, height] of [[1920, 1080], [1920, 950], [1536, 730], [1366, 768], [1366, 650], [1280, 600],
    [1093, 614], [1024, 576], [900, 700]]) {
    const ctx = await context(browser, { viewport: { width, height } });
    const page = await ctx.newPage();
    await page.goto(base, { waitUntil: 'load' });
    await settle(page);
    const m = await page.evaluate(() => {
      const r = s => document.querySelector(s).getBoundingClientRect();
      const menu = document.getElementById('menu');
      const last = menu.querySelector('li:last-child a');
      return { menu: r('#menu').bottom, frame: r('.frontend').bottom, foot: r('#foot').top, inner: menu.scrollHeight > menu.clientHeight,
        lastReachable: (() => { last.scrollIntoView({ block: 'nearest', behavior: 'instant' }); const b = last.getBoundingClientRect(); return b.bottom <= innerHeight + 1 && b.top >= 0; })() };
    });
    check(m.menu <= m.frame + 1, `layout ${width}x${height}: the menu (bottom ${Math.round(m.menu)}) leaves its frame (${Math.round(m.frame)})`);
    check(m.menu <= m.foot + 1, `layout ${width}x${height}: the menu (bottom ${Math.round(m.menu)}) covers the footer (${Math.round(m.foot)})`);
    check(m.lastReachable, `layout ${width}x${height}: the last menu item cannot be brought into view`);
    await ctx.close();
  }
}

async function viewer(base, browser) {
  const ctx = await context(browser, { viewport: { width: 1366, height: 768 } });
  const page = await ctx.newPage();
  const errors = [];
  page.on('pageerror', e => errors.push(String(e)));
  const isOpen = () => page.evaluate(() => { const d = document.querySelector('dialog.viewer'); return !!(d && d.open); });

  await page.goto(base + '#shots', { waitUntil: 'load' });
  await settle(page);
  await page.focus('#shot-3 a.shot');
  await page.keyboard.press('Enter');
  await settle(page);
  check(await isOpen(), 'viewer: Enter on a thumbnail opens it');
  check(await page.evaluate(() => document.querySelector('.v-close kbd').textContent === 'Esc'), 'viewer: its Close button shows Esc');
  check((await focused(page)).endsWith('in dialog'), `viewer: the focus moves into it (${await focused(page)})`);
  for (let i = 0; i < 5; i++) {   // past the last button the browser may take the focus to its own toolbar (body here)
    await page.keyboard.press('Tab');
    const f = await focused(page);
    check(f.endsWith('in dialog') || f === 'body', `viewer: Tab ${i + 1} never reaches the page behind it (${f})`);
  }
  await page.keyboard.press('ArrowRight');
  check(await page.evaluate(() => location.hash) === '#shot-4', 'viewer: the right arrow shows the next screenshot');
  await page.keyboard.press('Escape');
  await settle(page);
  check(!(await isOpen()), 'viewer: Esc closes it');
  check(await focused(page) === 'a in #shot-4', `viewer: after Esc the focus is on the thumbnail shown last (${await focused(page)})`);
  check(await page.evaluate(() => location.hash) === '#shots', 'viewer: after Esc the address is back to #shots');

  await page.focus('#shot-2 a.shot');
  await page.keyboard.press('Enter');
  await settle(page);
  await page.click('dialog.viewer .v-close');
  await settle(page);
  check(!(await isOpen()) && await focused(page) === 'a in #shot-2', `viewer: Close gives the focus back (${await focused(page)})`);

  await page.focus('#shot-6 a.shot');
  await page.keyboard.press('Enter');
  await settle(page);
  await page.goBack();
  await settle(page);
  check(!(await isOpen()) && await focused(page) === 'a in #shot-6', `viewer: Back closes it and gives the focus back (${await focused(page)})`);

  await page.goto(base + '#shot-5', { waitUntil: 'load' });
  await settle(page);
  check(await isOpen(), 'viewer: a #shot-5 link in the page opens it');
  check((await focused(page)).endsWith('in dialog'), `viewer: opened by a fragment link it holds the focus (${await focused(page)})`);
  await page.keyboard.press('Escape');
  await settle(page);
  await page.goto('about:blank');
  await page.goto(base + '#shot-5', { waitUntil: 'load' });
  await settle(page);
  check(await isOpen(), 'viewer: loading a #shot-5 address opens it');
  check((await focused(page)).endsWith('in dialog'), `viewer: opened from a link it holds the focus (${await focused(page)})`);
  await page.keyboard.press('Escape');
  await settle(page);
  check(!(await isOpen()) && await focused(page) === 'a in #shot-5', `viewer: opened from a link, Esc gives the focus to its thumbnail (${await focused(page)})`);
  check(errors.length === 0, `viewer: script errors ${errors.join(' | ')}`);
  await ctx.close();
}

async function addresses(base, browser) {
  const ctx = await context(browser, { viewport: { width: 1366, height: 768 } });
  const page = await ctx.newPage();
  const errors = [];
  page.on('pageerror', e => errors.push(String(e)));
  for (const hash of ['#%', '#%E0%A4%A', '#%ED%A0%80', '#<img src=x>', '#' + 'x'.repeat(5000), '#nowhere', '#shot-999', '#shot-0']) {
    await page.goto(base + hash, { waitUntil: 'load' });
    await settle(page);
    const shown = await page.evaluate(() => [...document.querySelectorAll('.sec')].filter(s => getComputedStyle(s).display !== 'none' && !s.hidden).map(s => s.id));
    check(shown.length === 1, `address ${hash.slice(0, 20)}: ${shown.length} sections shown, want 1`);
  }
  check(errors.length === 0, `addresses: script errors ${errors.join(' | ')}`);
  await ctx.close();
}

// A simulated pad set: window.__pads, read by navigator.getGamepads(); __press(button), __hold(button, ms),
// __axis(axis, value, ms) act on pad __use (0 unless set). Each change bumps the pad's timestamp, as a real one does.
function padInit() {
  const mk = (index, id, mapping, axes) => ({ id, index, connected: true, mapping, timestamp: 1, axes: Array(axes).fill(0),
    buttons: Array.from({ length: 17 }, () => ({ pressed: false, touched: false, value: 0 })) });
  window.__pads = [mk(0, 'Xbox Wireless Controller (STANDARD GAMEPAD Vendor: 045e Product: 0b13)', 'standard', 4), null, null, null];
  window.__add = (index, id, mapping, axes) => { window.__pads[index] = mk(index, id, mapping, axes); };
  window.__use = 0;
  navigator.getGamepads = () => window.__pads.slice();
  const wait = ms => new Promise(r => setTimeout(r, ms));
  const pad = () => window.__pads[window.__use];
  window.__hold = async (i, ms) => {
    pad().buttons[i] = { pressed: true, touched: true, value: 1 }; pad().timestamp += 1;
    await wait(ms);
    pad().buttons[i] = { pressed: false, touched: false, value: 0 }; pad().timestamp += 1;
    await wait(70);
  };
  window.__press = i => window.__hold(i, 70);
  window.__axis = async (a, v, ms) => {
    pad().axes[a] = v; pad().timestamp += 1;
    await wait(ms || 70);
    pad().axes[a] = 0; pad().timestamp += 1;
    await wait(70);
  };
}
const A = 0, B = 1, LB = 4, RB = 5, RT = 7, START = 9, UP = 12, DOWN = 13, RIGHT = 15;

async function padPage(browser, base, hash) {
  const ctx = await context(browser, { viewport: { width: 1366, height: 768 } });
  const page = await ctx.newPage();
  await page.addInitScript(padInit);
  await page.goto(base + (hash || ''), { waitUntil: 'load' });
  await settle(page);
  await page.evaluate(() => window.dispatchEvent(new Event('gamepadconnected')));
  return { ctx, page, press: b => page.evaluate(b => window.__press(b), b) };
}

// every link, button and screenshot of a section the d-pad can stop on (as site.js counts them)
async function stops(page) {
  return page.evaluate(() => [...document.querySelector('.sec.on').querySelectorAll('a[href], button, summary, [tabindex="0"]')]
    .filter(el => !el.disabled && el.checkVisibility()).length);
}

async function gamepad(base, browser) {
  let { ctx, page, press } = await padPage(browser, base);
  const errors = [];
  page.on('pageerror', e => errors.push(String(e)));
  await press(DOWN);
  check(await page.evaluate(() => location.hash) === '#shots', 'game pad: down selects the next section');
  await press(A);
  await settle(page);
  check(await focused(page) === 'h2#h-shots', `game pad: A enters the section, on its title (${await focused(page)})`);
  check(await page.title() === 'Screens · Project Vekta', `game pad: the tab title names the section (${await page.title()})`);
  check(await page.evaluate(() => document.documentElement.classList.contains('pad')), 'game pad: the focus is drawn while the pad drives');
  check(await page.evaluate(() => document.querySelector('#foot .legend').getAttribute('data-device')) === 'xbox',
    'game pad: the legend shows an Xbox pad\'s button names');
  check(await page.evaluate(() => document.querySelectorAll('#foot .legend svg.pi').length >= 6 &&
    !document.querySelector('#foot .legend kbd')), 'game pad: the legend draws the pad\'s buttons as icons');
  await press(RIGHT);
  check(await focused(page) === 'a in #shot-1', `game pad: right goes to the first screenshot (${await focused(page)})`);
  const top1 = await page.evaluate(() => document.activeElement.getBoundingClientRect().top);
  await press(DOWN);
  const below = await page.evaluate(() => document.activeElement.getBoundingClientRect().top);
  check((await focused(page)).startsWith('a in #shot-') && below > top1 + 20, `game pad: down goes a row down in the grid (${await focused(page)})`);
  const thumb = await focused(page);
  await press(A);
  await settle(page);
  check(await page.evaluate(() => document.querySelector('dialog.viewer').open), 'game pad: A on a screenshot opens the viewer');
  check(await page.evaluate(() => !!document.querySelector('.v-close .v-hint svg.pi') && !document.querySelector('.v-close kbd')),
    'game pad: the viewer\'s Close button shows the pad\'s back button');
  const h1 = await page.evaluate(() => location.hash);
  await press(RB);
  check(await page.evaluate(() => location.hash) !== h1, 'game pad: RB shows the next screenshot');
  await press(LB);
  await press(B);
  await settle(page);
  check(!(await page.evaluate(() => document.querySelector('dialog.viewer').open)) && await focused(page) === thumb,
    `game pad: B closes the viewer, the focus back on its screenshot (${await focused(page)})`);
  await press(B);
  check((await focused(page)).startsWith('a') && await page.evaluate(() => !!document.activeElement.closest('#menu')),
    `game pad: B goes back to the menu (${await focused(page)})`);

  await press(START);
  await settle(page);
  check(await page.evaluate(() => document.getElementById('help').open), 'game pad: Start opens the controls help');
  check(await page.evaluate(() => { const h = document.getElementById('help');
    return h.querySelectorAll('td[data-label="Keyboard"] kbd').length >= 10 && h.querySelectorAll('.pb svg.pi').length >= 14 &&
      [...h.querySelectorAll('.pb[data-pad="ok"] .vh')].every(n => n.textContent === 'A'); }),
    'game pad: the help shows the keys as <kbd> and the pad\'s buttons as icons, named as an Xbox pad names them');
  await press(B);
  await settle(page);
  check(!(await page.evaluate(() => document.getElementById('help').open)) && await page.evaluate(() => !!document.activeElement.closest('#menu')),
    `game pad: B closes the help, the focus back where it was (${await focused(page)})`);

  await press(DOWN);   // status: a long section
  await press(A);
  await press(RB);
  await settle(page);
  check(await focused(page) === 'h2#h-news', `game pad: RB from the content goes to the next section's title (${await focused(page)})`);
  await press(LB);
  await settle(page);
  check(await focused(page) === 'h2#h-status', `game pad: LB goes back (${await focused(page)})`);
  await page.evaluate(() => window.__axis(3, 1, 400));
  const y1 = await page.evaluate(() => document.getElementById('panel').scrollTop);
  check(y1 > 100, `game pad: the right stick scrolls the text (${y1})`);
  await press(RT);
  await page.waitForTimeout(400);
  const y2 = await page.evaluate(() => document.getElementById('panel').scrollTop);
  check(y2 > y1 + 200, `game pad: RT scrolls a page (${y1} -> ${y2})`);
  await press(B);
  await page.evaluate(() => window.__hold(13, 700));   // held down: 400 ms, then every 120 ms
  const moved = await page.evaluate(() => [...document.querySelectorAll('#menu a')].indexOf(document.activeElement));
  check(moved >= 5, `game pad: a held direction repeats (from 3 to ${moved})`);
  check(errors.length === 0, `game pad: script errors ${errors.join(' | ')}`);
  await ctx.close();

  // a pad outside the standard layout (Firefox on Linux): the d-pad on axes 6 and 7; the pad used last drives
  ({ ctx, page, press } = await padPage(browser, base));
  await page.evaluate(() => { window.__add(1, '054c-0ce6-Sony Interactive Entertainment DualSense Wireless Controller', '', 8); window.__use = 1; });
  await page.evaluate(() => window.__axis(7, 1));
  check(await page.evaluate(() => location.hash) === '#shots', 'game pad: a d-pad given as axis 7 moves down');
  check(await page.evaluate(() => document.querySelector('#foot .legend').getAttribute('data-device')) === 'playstation',
    'game pad: the legend follows the pad used last (PlayStation)');
  await page.keyboard.press('Shift');
  check(await page.evaluate(() => document.querySelector('#foot .legend').getAttribute('data-device')) === 'keys',
    'game pad: a key brings the keyboard legend back');
  await ctx.close();

  // every link, button and screenshot of every section is reached with the d-pad and A alone
  ({ ctx, page, press } = await padPage(browser, base));
  for (let s = 0; s < SECTIONS.length; s++) {
    await page.evaluate(i => { location.replace('#' + i); }, SECTIONS[s]);
    await page.evaluate(() => document.querySelector('#menu a.on').focus());
    await press(A);
    const want = await stops(page), seen = new Set();
    for (let n = 0; n < want * 3 + 12 && seen.size < want; n++) {
      await page.evaluate(() => window.__press(15));
      const f = await page.evaluate(() => { const a = document.activeElement; return a && a.closest('.sec') ? [...document.querySelectorAll('*')].indexOf(a) : -1; });
      if (f >= 0) seen.add(f);
    }
    check(seen.size === want, `game pad: ${SECTIONS[s]}: the d-pad reached ${seen.size} of ${want} stops`);
    for (let n = 0; n < want + 4 && !(await page.evaluate(() => !!document.activeElement.closest('#menu'))); n++) await press(B);
    check(await page.evaluate(() => !!document.activeElement.closest('#menu')), `game pad: ${SECTIONS[s]}: B leads back to the menu`);
  }
  await ctx.close();
}

async function keys(base, browser) {
  const ctx = await context(browser, { viewport: { width: 1366, height: 768 } });
  const page = await ctx.newPage();
  await page.goto(base, { waitUntil: 'load' });
  await settle(page);
  await page.focus('#menu a[href="#top"]');
  await page.keyboard.press('3');
  check(await page.evaluate(() => location.hash) === '#status', 'keys: 3 in the menu opens the third section');
  await page.keyboard.press('End');
  check(await page.evaluate(() => location.hash) === '#legal', 'keys: End opens the last section');
  await page.keyboard.press('Home');
  check(await page.evaluate(() => location.hash) === '#top', 'keys: Home opens the first section');
  await page.keyboard.press('3');
  await page.keyboard.press('PageDown');
  await page.waitForTimeout(500);
  check(await page.evaluate(() => document.getElementById('panel').scrollTop) > 100, 'keys: Page Down from the menu scrolls the text');
  await page.keyboard.press('Enter');
  await settle(page);
  check(await focused(page) === 'h2#h-status', `keys: Enter enters the section, on its title (${await focused(page)})`);
  check(await page.title() === 'Status · Project Vekta', `keys: the tab title names the section (${await page.title()})`);
  await page.keyboard.press('4');
  check(await page.evaluate(() => location.hash) === '#status', 'keys: a digit does nothing outside the menu');
  await page.keyboard.press('Tab');
  check(await page.evaluate(() => !!document.activeElement.closest('#status')), `keys: Tab goes on from the title (${await focused(page)})`);
  await page.keyboard.press('Escape');
  check(await page.evaluate(() => document.activeElement.getAttribute('href')) === '#status', `keys: Esc goes back to the menu (${await focused(page)})`);
  await page.keyboard.press('ArrowUp');
  await page.keyboard.press('ArrowRight');
  await settle(page);
  check(await focused(page) === 'h2#h-shots', `keys: the right arrow enters the section (${await focused(page)})`);
  await page.focus('#shot-1 a.shot');
  await page.keyboard.press('ArrowRight');
  check(await focused(page) === 'a in #shot-2', `keys: the right arrow moves in the screenshot grid (${await focused(page)})`);
  await page.keyboard.press('ArrowDown');
  check(/a in #shot-[3-9]/.test(await focused(page)), `keys: the down arrow goes a row down (${await focused(page)})`);
  // the Controls button opens the help; Esc closes it and the focus goes back to the button
  await page.focus('.help-btn');
  await page.keyboard.press('Enter');
  await settle(page);
  check(await page.evaluate(() => document.getElementById('help').open), 'keys: the Controls button opens the help');
  await page.keyboard.press('Escape');
  await settle(page);
  check(await page.evaluate(() => !document.getElementById('help').open && document.activeElement.matches('.help-btn')),
    `keys: Esc closes the help, the focus back on its button (${await focused(page)})`);
  // the browser's find in page: a match in a hidden section opens it (beforematch, where the browser has it)
  if (await page.evaluate(() => 'onbeforematch' in document.body)) {
    check(await page.evaluate(() => document.getElementById('install').getAttribute('hidden')) === 'until-found',
      'keys: a hidden section stays findable (hidden="until-found")');
    await page.evaluate(() => document.getElementById('install').dispatchEvent(new Event('beforematch')));
    check(await page.evaluate(() => location.hash === '#install' && document.getElementById('install').classList.contains('on')),
      'keys: a find match opens its section');
  }
  await ctx.close();
}

const HOME_BUDGET = 250 * 1024, SCREENS_BUDGET = 600 * 1024, CLS_MAX = 0.05;

// The bytes the page has fetched so far (bodies as served: the local server does not compress, GitHub Pages does).
function fetched(page) {
  return page.evaluate(() => [...performance.getEntriesByType('navigation'), ...performance.getEntriesByType('resource')]
    .reduce((n, e) => n + (e.encodedBodySize || 0), 0));
}

async function weight(base, browser) {
  for (const [w, h, phone] of [[1280, 800, false], [390, 844, true]]) {
    for (const hash of ['', '#install', '#shot-2']) {
      const ctx = await context(browser, { viewport: { width: w, height: h }, isMobile: phone, hasTouch: phone });
      // a slow line: the script and the font arrive well after the first paint
      await ctx.route(/\/project-vekta\/(site\.js|fonts\/)/, async route => { await new Promise(r => setTimeout(r, 700)); route.continue(); });
      await ctx.addInitScript(() => {
        window.__cls = 0;
        new PerformanceObserver(list => {
          for (const e of list.getEntries()) if (!e.hadRecentInput) window.__cls += e.value;
        }).observe({ type: 'layout-shift', buffered: true });
      });
      const page = await ctx.newPage();
      await page.goto(base + hash, { waitUntil: 'load' });
      await page.waitForTimeout(1200);
      const cls = await page.evaluate(() => window.__cls);
      check(cls < CLS_MAX, `CLS ${cls.toFixed(3)} at ${w}x${h} ${hash || '(home)'} with a slow script and font`);
      await ctx.close();
    }
  }

  const ctx = await context(browser, { javaScriptEnabled: false, viewport: { width: 1280, height: 800 } });
  const page = await ctx.newPage();
  for (const [hash, want] of [['', 'top'], ['#install', 'install'], ['#changelog', 'progress'], ['#shot-3', 'shots'], ['#%', 'top']]) {
    await page.goto(base + 'index.html' + hash, { waitUntil: 'load' });
    const shown = await page.evaluate(() => [...document.querySelectorAll('.sec')].filter(s => getComputedStyle(s).display !== 'none' && !s.hidden).map(s => s.id));
    check(shown.length === 1 && shown[0] === want, `no script: ${hash || '(home)'} shows ${shown.join(', ')}, want ${want}`);
  }
  await ctx.close();

  const c2 = await context(browser, { viewport: { width: 1280, height: 800 } });
  const p2 = await c2.newPage();
  await p2.goto(base, { waitUntil: 'networkidle' });
  const home = await fetched(p2);
  check(home <= HOME_BUDGET, `the home view fetched ${Math.round(home / 1024)} KB, budget ${HOME_BUDGET / 1024} KB`);
  await p2.evaluate(() => { location.hash = '#shots'; });
  await p2.waitForTimeout(300);
  await p2.evaluate(async () => {
    const panel = document.getElementById('panel');
    for (let y = 0; y < panel.scrollHeight; y += 400) { panel.scrollTop = y; await new Promise(r => setTimeout(r, 60)); }
  });
  await p2.waitForTimeout(500);
  await p2.waitForLoadState('networkidle');
  const shots = await fetched(p2) - home;
  check(home > 50 * 1024 && shots > 50 * 1024, `the byte count works (home ${home}, screens ${shots})`);
  check(shots <= SCREENS_BUDGET, `the screens view fetched ${Math.round(shots / 1024)} KB, budget ${SCREENS_BUDGET / 1024} KB`);
  console.log(`     home ${Math.round(home / 1024)} KB, screens +${Math.round(shots / 1024)} KB`);
  await c2.close();
  check(!outside.length, `requests left the site: ${[...new Set(outside)].join(', ')}`);
}

// 7. the published files, the policy in force
async function published(base, browser) {
  const ctx = await browser.newContext({ viewport: { width: 1366, height: 768 } });
  await ctx.route(/^https?:\/\/(?!127\.0\.0\.1)/, route => { outside.push(route.request().url()); route.fulfill({ status: 200, body: '' }); });
  await ctx.addInitScript(() => {
    window.__csp = [];
    document.addEventListener('securitypolicyviolation', e => window.__csp.push(`${e.violatedDirective} ${e.blockedURI}`));
  });
  const page = await ctx.newPage();
  const errors = [];
  page.on('pageerror', e => errors.push(String(e)));
  page.on('console', m => {   // the 404 answer itself is logged as an error; anything else is one
    if (m.type() === 'error' && !(m.location().url || '').endsWith('/no/such/page')) errors.push(`${m.text()} ${m.location().url}`);
  });
  const violations = async where => {
    const v = await page.evaluate(() => window.__csp);
    check(v.length === 0, `policy: ${where}: ${v.join(' | ')}`);
  };

  await page.goto(base, { waitUntil: 'load' });
  await settle(page);
  check(await page.evaluate(() => document.documentElement.classList.contains('ready')), 'policy: site.js runs under the policy');
  for (const s of ['install', 'faq', 'shots']) {
    await page.click(`#menu a[href="#${s}"]`);
    await settle(page);
  }
  await page.focus('#shot-2 a.shot');
  await page.keyboard.press('Enter');
  await settle(page);
  check(await page.evaluate(() => document.querySelector('dialog.viewer').open), 'policy: the viewer opens');
  await page.keyboard.press('ArrowRight');
  await page.click('dialog.viewer .v-close');
  await settle(page);
  await page.click('.help-btn');
  await settle(page);
  check(await page.evaluate(() => document.getElementById('help').open), 'policy: the help opens');
  await page.click('#help form[method="dialog"] button');
  await settle(page);
  check(!(await page.evaluate(() => document.getElementById('help').open)), 'policy: the help\'s form method="dialog" closes it');
  await violations('the main page, the viewer and the help');

  await page.goto(base + 'plain.html', { waitUntil: 'load' });
  await violations('the plain page');
  const font = await page.evaluate(() => [...document.fonts].length);
  check(font === 0, 'the plain page loads no web font');

  const r = await page.goto(base + 'no/such/page', { waitUntil: 'load' });
  check(r.status() === 404, `an unknown address answers 404 (${r.status()})`);
  check(await page.evaluate(() => document.querySelector('h1').textContent === 'Not found'), 'the 404 page shows');
  check(await page.evaluate(() => document.styleSheets.length === 1 && document.styleSheets[0].cssRules.length > 10),
    'the 404 page is styled from the site root');
  await violations('the 404 page');
  await page.click('main a[href="/project-vekta/"]');
  await page.waitForLoadState('load');
  check(new URL(page.url()).pathname === PREFIX, `the 404 page leads to the root (${page.url()})`);

  await page.goto(base + 'site/', { waitUntil: 'load' });
  await page.waitForURL(u => new URL(u).pathname === PREFIX, { timeout: 5000 }).catch(() => {});
  check(new URL(page.url()).pathname === PREFIX, `the old /site/ address sends the visitor to the root (${page.url()})`);

  const m = { href: await page.evaluate(() => document.querySelector('link[rel="manifest"]').href) };
  const mr = await page.request.get(m.href);
  const manifest = mr.ok() ? await mr.json() : {};
  check(mr.ok() && manifest.icons && manifest.icons.length >= 2, 'the manifest loads');
  for (const icon of manifest.icons || []) {
    const ir = await page.request.get(new URL(icon.src, m.href).href);
    check(ir.ok() && ir.headers()['content-type'] === 'image/png', `manifest icon ${icon.src} loads`);
  }
  const sm = await page.request.get(base + 'sitemap.xml');
  check(sm.ok() && (await sm.text()).includes('<urlset'), 'the sitemap loads');

  await page.emulateMedia({ media: 'print' });
  await page.goto(base + '#install', { waitUntil: 'load' });
  await settle(page);
  const printed = await page.evaluate(() => ({
    shown: [...document.querySelectorAll('.sec')].filter(s => getComputedStyle(s).display !== 'none').length,
    all: document.querySelectorAll('.sec').length,
    menu: getComputedStyle(document.getElementById('menu')).display,
    after: (() => { const a = document.querySelector('.sec a[href^="https://"]:not(.btn)'); return a ? getComputedStyle(a, '::after').content : ''; })(),
    bg: getComputedStyle(document.body).color,
  }));
  check(printed.shown === printed.all && printed.all > 5, `print: every section shows (${printed.shown} of ${printed.all})`);
  check(printed.menu === 'none', 'print: the menu is hidden');
  check(printed.after.includes('https://'), `print: an outside link prints its address (${printed.after})`);
  await page.emulateMedia({ media: 'screen' });

  check(errors.length === 0, `published: script or console errors ${errors.join(' | ')}`);
  check(!outside.length, `published: requests left the site: ${[...new Set(outside)].join(', ')}`);
  await ctx.close();
}

const SUITES = { axe, layout, viewer, addresses, gamepad, keys, weight, published };
const staged = stage();
const server = await serve(staged.dir);
const base = `http://127.0.0.1:${server.address().port}${PREFIX}`;
const browser = await chromium.launch();
try {
  for (const [name, run] of Object.entries(SUITES)) {
    if (only && only !== name) continue;
    const before = fails;
    await run(base, browser);
    console.log(`${fails === before ? 'ok  ' : 'FAIL'} ${name}`);
  }
} finally {
  await browser.close();
  server.close();
  fs.rmSync(staged.out, { recursive: true, force: true });
}
console.log('site_browser:', fails ? `FAIL (${fails})` : 'ok');
process.exit(fails ? 1 : 0);

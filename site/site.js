/* Project Vekta site: an optional enhancement; the page is complete without this file. The behaviour follows Project
   Pyrrhus's site/site.js (undecuple, MIT), itself after Project Helghast's; this file is written for this project.
   Same origin, no network calls, no storage, no analytics, no eval.
   What it does: shows one section at a time like a game's front-end menu (the tab title follows it, and the browser's
   find in page still reaches the hidden ones), reads the keyboard and any game pad, opens the screenshots in an
   in-page viewer (a modal <dialog>: previous / next, swipe, counter, #shot-N links; without <dialog> support the
   thumbnails stay plain links) and the controls help (another modal <dialog>).
   The input model has four zones: the menu, the section's content, the viewer and the help. Tab and Enter always
   reach everything; the arrows and the pad's buttons are shortcuts, never the only way. Letter and digit keys act
   only with the focus in the menu (WCAG 2.1.4). Esc, or the pad's right button, always leads back to the menu. */
(function () {
  'use strict';
  var doc = document.documentElement;
  doc.classList.add('js');

  function start() {
    var menu = document.getElementById('menu');
    var panel = document.getElementById('panel');
    var detail = document.getElementById('detail');
    var app = document.getElementById('app');
    if (!menu || !panel || !app) return;
    var links = [].slice.call(menu.querySelectorAll('ol a'));
    var secs = [].slice.call(panel.children).filter(function (el) { return el.classList.contains('sec'); });
    var phone = window.matchMedia('(max-width: 860px)');
    var still = window.matchMedia('(prefers-reduced-motion: reduce)');
    var current = null;

    var baseTitle = document.title;
    /* hidden="until-found": a hidden section's text stays findable, and a match opens it (beforematch) */
    var findable = 'onbeforematch' in document.body;
    if (findable) doc.classList.add('findable');

    function linkId(a) { return a.getAttribute('href').slice(1); }
    function linkOf(sec) { return links.filter(function (a) { return linkId(a) === sec.id; })[0]; }
    function label(sec) {
      var a = linkOf(sec), l = a && a.querySelector('.mi-l');
      return l ? l.textContent : sec.id;
    }
    function heading(sec) { return sec.querySelector('h2'); }
    secs.forEach(function (s) {
      var h = heading(s);
      if (h) h.setAttribute('tabindex', '-1');   /* entering a section puts the focus on its title */
      s.addEventListener('beforematch', function () {
        history.replaceState(null, '', location.pathname + location.search + '#' + s.id);
        show(s, null, true);
      });
    });

    /* Which section a hash belongs to, and the element inside it to scroll to (a #changelog, say). */
    function resolve(hash) {
      var fallback = current || secs[0];
      var id = '';
      try { id = decodeURIComponent((hash || '').replace(/^#/, '')); } catch (err) { id = ''; }   /* "#%": stay put */
      var el = id ? document.getElementById(id) : null;
      if (!el) return { sec: fallback, target: null };
      var sec = el.closest('.sec');
      if (!sec) return { sec: fallback, target: null };
      return { sec: sec, target: el === sec ? null : el };
    }

    function show(sec, target, keepScroll) {
      secs.forEach(function (s) { s.classList.toggle('on', s === sec); });
      links.forEach(function (a) {
        var on = linkId(a) === sec.id;
        a.classList.toggle('on', on);
        if (on) {
          a.setAttribute('aria-current', 'true');
          if (detail) detail.textContent = a.getAttribute('data-detail') || '';
        } else {
          a.removeAttribute('aria-current');
        }
      });
      document.title = sec === secs[0] ? baseTitle : label(sec) + ' \u00b7 Project Vekta';
      if (findable) {
        secs.forEach(function (s) {
          if (s === sec) s.removeAttribute('hidden'); else s.setAttribute('hidden', 'until-found');
        });
      }
      if (current !== sec) {
        var moved = current !== null;   /* the first show keeps what CSS already painted: no fade, no scroll */
        current = sec;
        panel.scrollTop = 0;
        secs.forEach(function (s) { s.classList.toggle('fade', moved && s === sec); });
        if (moved && phone.matches && !keepScroll) panel.scrollIntoView({ block: 'start' });   /* #panel scroll-margin-top */
      }
      if (target && !/^shot-\d+$/.test(target.id)) {
        target.scrollIntoView({ block: 'start', behavior: still.matches ? 'auto' : 'smooth' });
      }
    }

    function go(i, focus) {
      var n = links.length;
      i = (i + n) % n;
      location.replace('#' + linkId(links[i]));   /* the hash drives the section; no history entry per step */
      if (focus) links[i].focus();
    }

    function index() {
      var i = links.indexOf(document.activeElement);
      if (i >= 0) return i;
      for (i = 0; i < links.length; i++) if (links[i].classList.contains('on')) return i;
      return 0;
    }

    function menuHasFocus() {
      var a = document.activeElement;
      return !a || a === document.body || menu.contains(a);
    }

    /* into the content: the section's title takes the focus (screen readers read it; Tab goes on from there) */
    function enterPanel(sec) {
      if (sec && sec !== current) show(sec, null, false);
      var h = heading(current) || panel;
      h.focus({ preventScroll: true });
      focusSoon(h);
    }
    /* A history step or a fragment link moves the browser's focus starting point after our handlers run, which drops a
       focus set inside them; set it once that is done. */
    function focusSoon(el) { window.setTimeout(function () { if (el.isConnected) el.focus(); }, 0); }
    function backToMenu() { links[index()].focus(); }

    /* ---- the screenshot viewer ---- */
    var canDialog = typeof document.createElement('dialog').showModal === 'function';
    var thumbs = canDialog ? [].slice.call(document.querySelectorAll('a.shot')) : [];
    var groups = {};
    function groupName(a) {
      var box = a.closest('[data-group]');
      return box ? box.getAttribute('data-group') : 'screens';
    }
    thumbs.forEach(function (a) { (groups[groupName(a)] = groups[groupName(a)] || []).push(a); });

    var closeHint = null;
    var box = null, img, cap, full, count, say, prev, next, close, touchX = null;
    var group = null, at = -1, pushed = false, open = false;

    function build() {
      box = document.createElement('dialog');   /* modal: the browser keeps the focus inside and the page inert */
      box.className = 'viewer';
      box.setAttribute('aria-label', 'Screenshot viewer');
      var bar = document.createElement('div');
      bar.className = 'v-bar';
      count = document.createElement('span');
      count.className = 'v-count';
      count.setAttribute('aria-hidden', 'true');   /* "2 / 19" is for the eye; say tells screen readers */
      say = document.createElement('span');
      say.className = 'vh';
      say.setAttribute('aria-live', 'polite');
      close = button('v-close', 'Close the viewer', 'Close ');
      closeHint = document.createElement('span');   /* Esc, or the pad's back button (setDevice) */
      closeHint.className = 'v-hint';
      close.appendChild(closeHint);
      hint();
      bar.appendChild(count);
      bar.appendChild(say);
      bar.appendChild(close);
      var stage = document.createElement('div');
      stage.className = 'v-stage';
      prev = button('v-prev', 'Previous screenshot', '‹');
      next = button('v-next', 'Next screenshot', '›');
      var fig = document.createElement('figure');
      img = document.createElement('img');
      img.alt = '';
      cap = document.createElement('figcaption');
      full = document.createElement('a');   /* the viewer shows a 1920 wide WebP copy; this is the screenshot itself */
      full.className = 'v-full';
      full.textContent = 'Full-size PNG';
      fig.appendChild(img);
      fig.appendChild(cap);
      fig.appendChild(full);
      stage.appendChild(prev);
      stage.appendChild(fig);
      stage.appendChild(next);
      box.appendChild(bar);
      box.appendChild(stage);
      document.body.appendChild(box);
      prev.addEventListener('click', function () { step(-1); });
      next.addEventListener('click', function () { step(1); });
      close.addEventListener('click', shut);
      box.addEventListener('cancel', function (e) { e.preventDefault(); shut(); });   /* Esc, or a phone's back gesture */
      /* Chrome drops the focus when it scrolls to the address's target (a #shot-N address, just after the page loads):
         while the viewer is open and the page still has the focus, it goes back to the Close button */
      box.addEventListener('focusout', function () {
        window.setTimeout(function () {
          if (open && document.activeElement === document.body && document.hasFocus()) close.focus();
        }, 0);
      });
      box.addEventListener('click', function (e) {
        if (e.target === box || e.target === stage) shut();
      });
      box.addEventListener('touchstart', function (e) {
        touchX = e.touches.length === 1 ? e.touches[0].clientX : null;
      }, { passive: true });
      box.addEventListener('touchend', function (e) {
        if (touchX === null || !e.changedTouches.length) return;
        var dx = e.changedTouches[0].clientX - touchX;
        touchX = null;
        if (Math.abs(dx) > 50) step(dx < 0 ? 1 : -1);
      });
    }

    function button(cls, label, text) {
      var b = document.createElement('button');
      b.type = 'button';
      b.className = cls;
      b.setAttribute('aria-label', label);
      b.textContent = text;
      return b;
    }

    function figureId(a) { var f = a.closest('figure'); return f ? f.id : ''; }

    function paint() {
      var list = groups[group], a = list[at];
      var t = a.querySelector('img');   /* the screenshot's shape, so the viewer's box is set before the image loads */
      if (t && t.width && t.height) img.style.setProperty('--ar', t.getAttribute('width') + ' / ' + t.getAttribute('height'));
      img.src = a.getAttribute('href');
      full.href = a.getAttribute('data-full') || a.getAttribute('href');
      img.alt = a.getAttribute('data-caption') || '';
      cap.textContent = '';
      var b = document.createElement('b');
      b.textContent = a.getAttribute('data-title') || '';
      cap.appendChild(b);
      var d = document.createElement('span');   /* the image's alt text says it already: read once */
      d.setAttribute('aria-hidden', 'true');
      d.textContent = ' ' + (a.getAttribute('data-caption') || '');
      cap.appendChild(d);
      count.textContent = (at + 1) + ' / ' + list.length;
      say.textContent = 'Screenshot ' + (at + 1) + ' of ' + list.length + ': ' + (a.getAttribute('data-title') || '');
      prev.disabled = next.disabled = list.length < 2;
      [-1, 1].forEach(function (d) {   /* warm the neighbours */
        var n = list[(at + d + list.length) % list.length];
        if (n && n !== a) (new Image()).src = n.getAttribute('href');
      });
    }

    function setHash(id, push) {
      var url = location.pathname + location.search + '#' + id;
      if (push) { history.pushState(null, '', url); pushed = true; }
      else history.replaceState(null, '', url);
    }

    function view(a, fromHash) {
      if (!box) build();
      group = groupName(a);
      at = groups[group].indexOf(a);
      var was = open;
      open = true;
      paint();
      if (!was) {
        box.showModal();
        document.body.classList.add('v-open');
        close.focus();
        focusSoon(close);
      }
      var id = figureId(a);
      if (id && !fromHash) setHash(id, !was);   /* opening adds one history entry (Back closes); stepping replaces it */
    }

    function hide() {
      if (!open) return;
      open = false;
      pushed = false;
      box.close();
      document.body.classList.remove('v-open');
      var a = groups[group][at];
      if (a) { a.focus(); focusSoon(a); }   /* back to the thumbnail it showed last */
    }

    function shut() {
      if (!open) return;
      if (pushed) { pushed = false; history.back(); }   /* popstate hides it */
      else { hide(); setHash('shots', false); }
    }

    function step(d) {
      if (!open) return;
      var list = groups[group];
      at = (at + d + list.length) % list.length;
      paint();
      var id = figureId(list[at]);
      if (id) setHash(id, false);
    }

    thumbs.forEach(function (a) {
      a.addEventListener('click', function (e) {
        if (e.button !== 0 || e.ctrlKey || e.metaKey || e.shiftKey || e.altKey) return;   /* new tab keeps the link */
        e.preventDefault();
        view(a, false);
      });
    });

    function viewerFromHash() {
      var m = /^#shot-(\d+)$/.exec(location.hash);
      var fig = m && document.getElementById('shot-' + m[1]);
      var a = fig && fig.querySelector('a.shot');
      if (a) { view(a, true); return true; }
      if (open) hide();
      return false;
    }

    function onNav() {
      if (viewerFromHash()) { show(resolve('#shots').sec, null, false); return; }
      var r = resolve(location.hash);
      show(r.sec, r.target, false);
    }
    window.addEventListener('hashchange', onNav);
    window.addEventListener('popstate', onNav);

    /* ---- moving inside the content ---- */
    function scroller() { return phone.matches || panel.scrollHeight <= panel.clientHeight ? document.scrollingElement : panel; }
    function scrollPage(d) {
      var el = scroller(), h = el === panel ? panel.clientHeight : window.innerHeight;
      el.scrollBy({ top: d * h * 0.85, behavior: still.matches ? 'auto' : 'smooth' });
    }
    function inPanel(el) { return !!el && el !== panel && panel.contains(el); }
    function focusables(sec) {
      return [].slice.call(sec.querySelectorAll('a[href], button, summary, [tabindex="0"]')).filter(function (el) {
        /* rendered: not in a closed <details> (whose content still has boxes) nor a hidden section */
        return !el.disabled && (el.checkVisibility ? el.checkVisibility() : el.getClientRects().length > 0);
      });
    }
    function rectOf(el) { return el.getBoundingClientRect(); }
    function visible() {   /* the visible part of the content, in window coordinates */
      var r = scroller() === panel ? rectOf(panel) : { top: 0, bottom: window.innerHeight };
      return { top: r.top, bottom: r.bottom, h: r.bottom - r.top };
    }
    /* the screenshot in the row above or below: the nearest by column, then by row */
    function gridStep(el, dir) {
      var shots = [].slice.call(current.querySelectorAll('a.shot')), r = rectOf(el), cx = (r.left + r.right) / 2;
      var best = null, score = Infinity;
      shots.forEach(function (o) {
        if (o === el) return;
        var q = rectOf(o), dy = dir > 0 ? q.top - r.bottom : r.top - q.bottom;
        if (dy < -2) return;
        var sc = dy * 4 + Math.abs((q.left + q.right) / 2 - cx);
        if (sc < score) { score = sc; best = o; }
      });
      return best;
    }
    /* d-pad / arrows in the content: the next or previous link, button or screenshot, in reading order (so every one is
       reached); up and down in the screenshot grid go by row. A target more than a screen away scrolls the text a page
       first, so nothing between is skipped unread. Back past the first one goes to the title, then to the menu. */
    function move(dir, vertical) {
      var el = document.activeElement, list = focusables(current), i = list.indexOf(el), next = null;
      if (vertical && el && el.matches && el.matches('a.shot')) next = gridStep(el, dir);
      else if (dir > 0) next = list[i + 1] || null;
      else next = i > 0 ? list[i - 1] : null;
      if (!next) {
        if (dir > 0) { scrollPage(1); return; }
        var h = heading(current);
        if (h && el !== h) { h.focus({ preventScroll: true }); scroller().scrollTo({ top: 0 }); }
        else if (!vertical) backToMenu();
        else scrollPage(-1);
        return;
      }
      var v = visible(), q = rectOf(next);
      if (dir > 0 && q.top > v.bottom + v.h * 0.5) { scrollPage(1); return; }
      if (dir < 0 && q.bottom < v.top - v.h * 0.5) { scrollPage(-1); return; }
      next.focus({ preventScroll: true });
      next.scrollIntoView({ block: 'nearest', behavior: still.matches ? 'auto' : 'smooth' });
    }
    function changeSection(d) {
      var el = document.activeElement, wasIn = inPanel(el) || el === panel, i = (index() + d + links.length) % links.length;
      go(i, !wasIn);
      if (wasIn) enterPanel(resolve('#' + linkId(links[i])).sec);
    }

    /* ---- the controls help ---- */
    var help = document.getElementById('help');
    var helpBtn = document.querySelector('.help-btn');
    var helpFrom = null;
    function openHelp() {
      if (!help || !canDialog || help.open) return;
      helpFrom = document.activeElement;
      help.showModal();
    }
    if (help && canDialog) {
      if (helpBtn) helpBtn.addEventListener('click', openHelp);
      help.addEventListener('close', function () {
        var back = helpFrom && helpFrom !== document.body && helpFrom.isConnected ? helpFrom : (helpBtn || links[index()]);
        back.focus();
        focusSoon(back);
      });
    } else if (helpBtn) {
      helpBtn.parentNode.removeChild(helpBtn);
    }

    /* ---- the legend follows the last input: the keys, or the pad's own button names (text only) ---- */
    var legend = document.querySelector('#foot .legend');
    /* Pad buttons are drawn here as small line icons of our own (no maker's logo or artwork): a face button with its
       letter or shape, a shoulder button with its name, the d-pad, the menu button. Each pad family names its
       buttons its own way; confirming is always the bottom face button. Generic pads get the position: four dots
       with the one meant filled. */
    var mark = document.querySelector('svg');   /* the header's emblem: an inline <svg>, so its namespace is SVG's */
    var NS = mark ? mark.namespaceURI : '';
    function svg(w, h, parts) {
      if (!NS) return document.createTextNode('');
      var el = document.createElementNS(NS, 'svg');
      el.setAttribute('viewBox', '0 0 ' + w + ' ' + h);
      el.setAttribute('width', w);
      el.setAttribute('height', h);
      el.setAttribute('class', 'pi');
      el.setAttribute('focusable', 'false');
      el.setAttribute('aria-hidden', 'true');
      parts.forEach(function (d) {
        var n = document.createElementNS(NS, d[0]);
        Object.keys(d[1]).forEach(function (k) { n.setAttribute(k, d[1][k]); });
        if (d[2]) n.textContent = d[2];
        el.appendChild(n);
      });
      return el;
    }
    var RING = ['circle', { cx: 12, cy: 12, r: 10.5, 'class': 'pi-ring' }];
    function face(mark) {
      var m = {
        cross: [['path', { d: 'M8 8l8 8M16 8l-8 8', 'class': 'pi-line' }]],
        circle: [['circle', { cx: 12, cy: 12, r: 4.6, 'class': 'pi-line' }]],
        square: [['rect', { x: 7.8, y: 7.8, width: 8.4, height: 8.4, 'class': 'pi-line' }]],
        triangle: [['path', { d: 'M12 7l5 8.6H7z', 'class': 'pi-line' }]]
      }[mark];
      return svg(24, 24, [RING].concat(m || [['text', { x: 12, y: 16.6, 'class': 'pi-text' }, mark]]));
    }
    function shoulder(name) {
      return svg(30, 20, [['rect', { x: 1, y: 2, width: 28, height: 16, rx: 7, 'class': 'pi-ring' }],
        ['text', { x: 15, y: 14.4, 'class': 'pi-text pi-small' }, name]]);
    }
    function dpad() {
      return svg(24, 24, [['path', { d: 'M9 2h6v7h7v6h-7v7H9v-7H2V9h7z', 'class': 'pi-ring' }],
        ['path', { d: 'M12 4.5l1.6 2.2h-3.2zM12 19.5l1.6-2.2h-3.2z', 'class': 'pi-fill' }]]);
    }
    function menuBtn(kind) {
      if (kind === 'nintendo') return svg(24, 24, [RING, ['path', { d: 'M12 7.5v9M7.5 12h9', 'class': 'pi-line' }]]);
      return svg(24, 24, [['rect', { x: 3, y: 6, width: 18, height: 12, rx: 6, 'class': 'pi-ring' }],
        ['path', { d: 'M8.5 10h7M8.5 12h7M8.5 14h7', 'class': 'pi-line pi-thin' }]]);
    }
    function trigger(name) {
      return svg(30, 22, [['path', { d: 'M3 20V9a8 7 0 0 1 8-7h8a8 7 0 0 1 8 7v11z', 'class': 'pi-ring' }],
        ['text', { x: 15, y: 16.4, 'class': 'pi-text pi-small' }, name]]);
    }
    function stickIcon(side) {
      return svg(24, 24, [RING, ['circle', { cx: 12, cy: 12, r: 6, 'class': 'pi-line pi-thin' }],
        ['text', { x: 12, y: 15.6, 'class': 'pi-text pi-small' }, side]]);
    }
    function position(which) {   /* a generic pad: the four face buttons, the one meant filled */
      var at = { bottom: [12, 19], right: [19, 12], top: [12, 5], left: [5, 12] };
      return svg(24, 24, Object.keys(at).map(function (k) {
        return ['circle', { cx: at[k][0], cy: at[k][1], r: 3.6, 'class': k === which ? 'pi-fill' : 'pi-ring pi-thin' }];
      }));
    }
    var FAMILY = {
      xbox: { ok: function () { return face('A'); }, back: function () { return face('B'); }, l: 'LB', r: 'RB',
        lt: 'LT', rt: 'RT', names: { ok: 'A', back: 'B', start: 'Menu' } },
      playstation: { ok: function () { return face('cross'); }, back: function () { return face('circle'); }, l: 'L1', r: 'R1',
        lt: 'L2', rt: 'R2', names: { ok: 'Cross', back: 'Circle', start: 'Options' } },
      nintendo: { ok: function () { return face('B'); }, back: function () { return face('A'); }, l: 'L', r: 'R',
        lt: 'ZL', rt: 'ZR', names: { ok: 'B', back: 'A', start: 'Plus' } },
      pad: { ok: function () { return position('bottom'); }, back: function () { return position('right'); }, l: 'LB', r: 'RB',
        lt: 'LT', rt: 'RT', names: { ok: 'bottom button', back: 'right button', start: 'Start' } }
    };
    var padFamily = 'xbox';   /* the pad used last; the help shows its buttons even while the keys drive */
    /* one named button of a pad family: [icon, its name there] */
    function padButton(token, kind) {
      var f = FAMILY[kind];
      switch (token) {
        case 'dpad': return [dpad(), 'D-pad'];
        case 'lstick': return [stickIcon('L'), 'left stick'];
        case 'rstick': return [stickIcon('R'), 'right stick'];
        case 'ok': return [f.ok(), f.names.ok];
        case 'back': return [f.back(), f.names.back];
        case 'l': return [shoulder(f.l), f.l];
        case 'r': return [shoulder(f.r), f.r];
        case 'lt': return [trigger(f.lt), f.lt];
        case 'rt': return [trigger(f.rt), f.rt];
        case 'start': return [menuBtn(kind), f.names.start];
      }
      return null;
    }
    /* the help's pad column: each named button becomes its icon (the name stays for screen readers) */
    function paintHelp() {
      if (!help) return;
      [].slice.call(help.querySelectorAll('.pb[data-pad]')).forEach(function (el) {
        var b = padButton(el.getAttribute('data-pad'), padFamily);
        if (!b) return;
        while (el.firstChild) el.removeChild(el.firstChild);
        el.appendChild(b[0]);
        var name = document.createElement('span');
        name.className = 'vh';
        name.textContent = b[1];
        el.appendChild(name);
        el.setAttribute('title', b[1]);
      });
    }
    /* the viewer's Close button shows Esc, or the pad's back button */
    function hint() {
      if (!closeHint) return;
      while (closeHint.firstChild) closeHint.removeChild(closeHint.firstChild);
      if (doc.classList.contains('pad')) closeHint.appendChild(FAMILY[padFamily].back());
      else {
        var k = document.createElement('kbd');
        k.textContent = 'Esc';
        closeHint.appendChild(k);
      }
    }
    function padKind(id) {
      if (/playstation|dualsense|dualshock|054c|sony/i.test(id)) return 'playstation';
      if (/nintendo|switch|joy-?con|057e|pro controller/i.test(id)) return 'nintendo';
      if (/xbox|xinput|045e|microsoft/i.test(id)) return 'xbox';
      return 'pad';
    }
    function legendItems(kind) {
      if (kind === 'keys') {
        return [[['↑', '↓'], 'select'], [['Enter'], 'open'], [['Esc'], 'back'], [['←', '→'], 'in the viewer']];
      }
      var f = FAMILY[kind];
      return [[[dpad()], 'move'], [[f.ok()], 'open'], [[f.back()], 'back'], [[shoulder(f.l), shoulder(f.r)], 'section'],
        [[menuBtn(kind)], 'controls']];
    }
    function setDevice(kind) {
      var was = legend ? legend.getAttribute('data-device') : null;
      doc.classList.toggle('pad', kind !== 'keys');
      if (was === kind) return;
      if (kind !== 'keys') { padFamily = kind; paintHelp(); }
      hint();
      if (!legend) return;
      legend.setAttribute('data-device', kind);
      while (legend.firstChild) legend.removeChild(legend.firstChild);
      legendItems(kind).forEach(function (item) {
        var span = document.createElement('span');
        item[0].forEach(function (key) {
          if (typeof key === 'string') {
            var k = document.createElement('kbd');
            k.textContent = key;
            span.appendChild(k);
          } else span.appendChild(key);
        });
        span.appendChild(document.createTextNode(' ' + item[1]));
        legend.appendChild(span);
      });
    }
    document.addEventListener('mousedown', function () { setDevice('keys'); }, true);

    /* ---- keys ---- */
    document.addEventListener('keydown', function (e) {
      setDevice('keys');
      if (e.altKey || e.ctrlKey || e.metaKey) return;
      var k = e.key, el = document.activeElement;
      if (help && help.open) return;   /* the dialog: Tab stays inside, Esc closes it */
      if (open) {
        if (k === 'Escape') { e.preventDefault(); shut(); }
        else if (k === 'ArrowLeft') { e.preventDefault(); step(-1); }
        else if (k === 'ArrowRight') { e.preventDefault(); step(1); }
        else if (k === 'Home') { e.preventDefault(); step(-at); }
        else if (k === 'End') { e.preventDefault(); step(groups[group].length - 1 - at); }
        return;   /* Tab stays inside the modal dialog on its own */
      }
      if (phone.matches && k !== 'Escape') return;   /* on a phone the page scrolls and the keys keep their job */
      if (menuHasFocus()) {
        var n = links.length;
        if (k === 'ArrowDown') { e.preventDefault(); go(index() + 1, true); }
        else if (k === 'ArrowUp') { e.preventDefault(); go(index() - 1, true); }
        else if (k === 'Home') { e.preventDefault(); go(0, true); }
        else if (k === 'End') { e.preventDefault(); go(n - 1, true); }
        else if (/^[0-9]$/.test(k) && !e.shiftKey && menu.contains(el)) {
          var d = k === '0' ? 9 : +k - 1;
          if (d < n) { e.preventDefault(); go(d, true); }
        }
        else if (k === 'PageDown' || (k === ' ' && !e.shiftKey)) { e.preventDefault(); scrollPage(1); }
        else if (k === 'PageUp' || (k === ' ' && e.shiftKey)) { e.preventDefault(); scrollPage(-1); }
        else if (k === 'ArrowRight' || (k === 'Enter' && menu.contains(el))) {
          var a = menu.contains(el) && el.matches('a[href^="#"]') ? el : links[index()];
          e.preventDefault();
          location.replace(a.getAttribute('href'));
          enterPanel(resolve(a.getAttribute('href')).sec);
        }
        return;
      }
      if (k === 'Escape' && (panel.contains(el) || el === panel)) { e.preventDefault(); backToMenu(); return; }
      if (el && el.matches && el.matches('a.shot') && /^Arrow/.test(k)) {   /* the screenshot grid */
        e.preventDefault();
        move(k === 'ArrowDown' || k === 'ArrowRight' ? 1 : -1, k === 'ArrowDown' || k === 'ArrowUp');
        return;
      }
      if (k === 'ArrowLeft' && (el === panel || el === heading(current))) { e.preventDefault(); backToMenu(); }
    });

    /* ---- game pads (optional): the standard layout, any number of pads; the one used last drives ----
       Pads outside the standard layout (Firefox on Linux) give the d-pad as axes 6 and 7. The sticks have a dead zone
       with hysteresis; a held direction repeats after 400 ms, then every 120 ms. */
    var pads = 0, held = {}, when = {}, reps = {}, stick = {}, lastPad = -1, lastStamp = {};
    var PRESS = 0.5, RELEASE = 0.35, FIRST = 400, AGAIN = 120;
    function btn(gp, i) { var b = gp.buttons[i]; return !!b && (b.pressed || b.value > 0.5); }
    function axis(gp, i) { return gp.axes[i] || 0; }
    function tilt(id, v, dir) {   /* a stick direction, on at PRESS and off below RELEASE */
      var on = stick[id] ? dir * v > RELEASE : dir * v > PRESS;
      stick[id] = on;
      return on;
    }
    function readPad(gp) {
      var std = gp.mapping === 'standard', hx = std ? 0 : axis(gp, 6), hy = std ? 0 : axis(gp, 7);
      return {
        up: btn(gp, 12) || hy < -0.5 || tilt('ly-', axis(gp, 1), -1),
        down: btn(gp, 13) || hy > 0.5 || tilt('ly+', axis(gp, 1), 1),
        left: btn(gp, 14) || hx < -0.5 || tilt('lx-', axis(gp, 0), -1),
        right: btn(gp, 15) || hx > 0.5 || tilt('lx+', axis(gp, 0), 1),
        a: btn(gp, 0), b: btn(gp, 1), lb: btn(gp, 4), rb: btn(gp, 5),
        lt: std && btn(gp, 6), rt: std && btn(gp, 7), start: btn(gp, 9),
        ry: std && Math.abs(axis(gp, 3)) > RELEASE ? axis(gp, 3) : 0
      };
    }
    function busy(gp) {
      return gp.buttons.some(function (b) { return b && (b.pressed || b.value > 0.5); }) ||
        gp.axes.some(function (v) { return Math.abs(v) > PRESS; });
    }
    function edge(name, now, repeats) {
      if (!held[name]) { held[name] = true; when[name] = now; reps[name] = 0; return true; }
      if (!repeats || now - when[name] < (reps[name] ? AGAIN : FIRST)) return false;
      when[name] = now;
      reps[name] += 1;
      return true;
    }
    var REPEATS = { up: 1, down: 1, left: 1, right: 1, lb: 1, rb: 1, lt: 1, rt: 1 };
    function act(n) {
      var el = document.activeElement;
      if (help && help.open) {
        if (n === 'b' || n === 'start') help.close();
        else if (n === 'a' && el && el.click) el.click();
        return;
      }
      if (open) {
        if (n === 'left' || n === 'lb') step(-1);
        else if (n === 'right' || n === 'rb') step(1);
        else if (n === 'b') shut();
        else if (n === 'a' && el && el.click && el !== box) el.click();
        return;
      }
      if (n === 'start') { openHelp(); return; }
      if (n === 'lb' || n === 'rb') { changeSection(n === 'lb' ? -1 : 1); return; }
      if (n === 'lt' || n === 'rt') { scrollPage(n === 'lt' ? -1 : 1); return; }
      if (menuHasFocus()) {
        if (n === 'down') go(index() + 1, true);
        else if (n === 'up') go(index() - 1, true);
        else if (n === 'right' || n === 'a') {
          var a = menu.contains(el) && el.matches('a[href^="#"]') ? el : links[index()];
          location.replace(a.getAttribute('href'));
          enterPanel(resolve(a.getAttribute('href')).sec);
        }
        return;
      }
      if (n === 'b') backToMenu();
      else if (n === 'a') { if (el && el !== panel && el !== heading(current) && el.click) el.click(); }
      else if (n === 'up' || n === 'down') move(n === 'down' ? 1 : -1, true);
      else move(n === 'right' ? 1 : -1, false);
    }
    function poll() {
      var list = navigator.getGamepads ? navigator.getGamepads() : [], now = Date.now(), i, gp = null;
      for (i = 0; i < list.length; i++) {   /* the pad that moved last, by its own timestamp */
        var p = list[i];
        if (!p) continue;
        if (busy(p) && (lastStamp[p.index] === undefined || p.timestamp !== lastStamp[p.index] || p.timestamp === 0)) {
          if (lastPad !== p.index) { held = {}; stick = {}; }
          lastPad = p.index;
        }
        lastStamp[p.index] = p.timestamp;
      }
      for (i = 0; i < list.length; i++) if (list[i] && list[i].index === lastPad) gp = list[i];
      if (gp) {
        var s = readPad(gp), any = false;
        Object.keys(s).forEach(function (n) {
          if (n === 'ry') return;
          if (!s[n]) { held[n] = false; return; }
          any = true;
          if (edge(n, now, !!REPEATS[n])) act(n);
        });
        if (s.ry && !open && !(help && help.open)) {   /* the right stick scrolls the text, faster the further it goes */
          any = true;
          scroller().scrollBy(0, Math.round(s.ry * 22));
        }
        if (any) setDevice(padKind(gp.id));
      }
      if (pads > 0) window.requestAnimationFrame(poll);
    }
    window.addEventListener('gamepadconnected', function () {
      pads += 1;
      if (pads === 1) window.requestAnimationFrame(poll);
    });
    window.addEventListener('gamepaddisconnected', function () { pads = Math.max(0, pads - 1); });

    paintHelp();

    /* printing: style.css prints every section; the closed <details> open for it and close again after */
    var printed = [];
    window.addEventListener('beforeprint', function () {
      printed = [].slice.call(panel.querySelectorAll('details:not([open])'));
      printed.forEach(function (d) { d.open = true; });
    });
    window.addEventListener('afterprint', function () {
      printed.forEach(function (d) { d.open = false; });
      printed = [];
    });

    /* first paint: the hash decides (a #shot-N link opens the viewer) */
    if (viewerFromHash()) show(resolve('#shots').sec, null, true);
    else { var r = resolve(location.hash); show(r.sec, r.target, false); }
    doc.classList.add('ready');
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start);
  else start();
})();

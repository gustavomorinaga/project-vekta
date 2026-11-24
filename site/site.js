/* Project Vekta site: an optional enhancement; the page is complete without this file. The behaviour follows Project
   Pyrrhus's site/site.js (undecuple, MIT), itself after Project Helghast's; this file is written for this project.
   Same origin, no network calls, no storage, no analytics, no eval.
   What it does: shows one section at a time like a game's front-end menu, reads the arrow keys, Enter, Esc and a game
   pad, and opens the screenshots in an in-page viewer (previous / next, swipe, counter, #shot-N links). */
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

    function linkId(a) { return a.getAttribute('href').slice(1); }

    /* Which section a hash belongs to, and the element inside it to scroll to (a #changelog, say). */
    function resolve(hash) {
      var fallback = current || secs[0];
      var id = decodeURIComponent((hash || '').replace(/^#/, ''));
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
      if (current !== sec) {
        current = sec;
        panel.scrollTop = 0;
        if (phone.matches && !keepScroll && doc.classList.contains('ready')) {
          window.scrollTo(0, Math.max(0, panel.getBoundingClientRect().top + window.pageYOffset - 8));
        }
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

    function enterPanel() { panel.focus({ preventScroll: true }); }
    function backToMenu() { links[index()].focus(); }

    /* ---- the screenshot viewer ---- */
    var thumbs = [].slice.call(document.querySelectorAll('a.shot'));
    var groups = {};
    function groupName(a) {
      var box = a.closest('[data-group]');
      return box ? box.getAttribute('data-group') : 'screens';
    }
    thumbs.forEach(function (a) { (groups[groupName(a)] = groups[groupName(a)] || []).push(a); });

    var box = null, img, cap, count, prev, next, close, touchX = null;
    var group = null, at = -1, pushed = false, open = false;

    function build() {
      box = document.createElement('div');
      box.className = 'viewer';
      box.setAttribute('role', 'dialog');
      box.setAttribute('aria-modal', 'true');
      box.setAttribute('aria-label', 'Screenshot viewer');
      box.hidden = true;
      var bar = document.createElement('div');
      bar.className = 'v-bar';
      count = document.createElement('span');
      count.className = 'v-count';
      count.setAttribute('aria-live', 'polite');
      close = button('v-close', 'Close the viewer', 'Close ');
      var k = document.createElement('kbd');
      k.textContent = 'Esc';
      close.appendChild(k);
      bar.appendChild(count);
      bar.appendChild(close);
      var stage = document.createElement('div');
      stage.className = 'v-stage';
      prev = button('v-prev', 'Previous screenshot', '‹');
      next = button('v-next', 'Next screenshot', '›');
      var fig = document.createElement('figure');
      img = document.createElement('img');
      img.alt = '';
      cap = document.createElement('figcaption');
      fig.appendChild(img);
      fig.appendChild(cap);
      stage.appendChild(prev);
      stage.appendChild(fig);
      stage.appendChild(next);
      box.appendChild(bar);
      box.appendChild(stage);
      document.body.appendChild(box);
      prev.addEventListener('click', function () { step(-1); });
      next.addEventListener('click', function () { step(1); });
      close.addEventListener('click', shut);
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
      img.src = a.getAttribute('href');
      img.alt = a.getAttribute('data-caption') || '';
      cap.textContent = '';
      var b = document.createElement('b');
      b.textContent = a.getAttribute('data-title') || '';
      cap.appendChild(b);
      cap.appendChild(document.createTextNode(' ' + (a.getAttribute('data-caption') || '')));
      count.textContent = (at + 1) + ' / ' + list.length;
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
        box.hidden = false;
        app.setAttribute('inert', '');
        app.setAttribute('aria-hidden', 'true');
        document.body.classList.add('v-open');
        close.focus();
      }
      var id = figureId(a);
      if (id && !fromHash) setHash(id, !was);   /* opening adds one history entry (Back closes); stepping replaces it */
    }

    function hide() {
      if (!open) return;
      open = false;
      pushed = false;
      box.hidden = true;
      app.removeAttribute('inert');
      app.removeAttribute('aria-hidden');
      document.body.classList.remove('v-open');
      var a = groups[group][at];
      if (a) a.focus();
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

    /* ---- keys ---- */
    document.addEventListener('keydown', function (e) {
      if (e.altKey || e.ctrlKey || e.metaKey) return;
      var k = e.key;
      if (open) {
        if (k === 'Escape') { e.preventDefault(); shut(); }
        else if (k === 'ArrowLeft') { e.preventDefault(); step(-1); }
        else if (k === 'ArrowRight') { e.preventDefault(); step(1); }
        else if (k === 'Tab') {   /* keep the focus inside the viewer */
          var f = [close, prev, next].filter(function (b) { return !b.disabled; });
          var i = f.indexOf(document.activeElement);
          e.preventDefault();
          f[(i + (e.shiftKey ? -1 : 1) + f.length) % f.length].focus();
        }
        return;
      }
      if (phone.matches && k !== 'Escape') return;   /* on a phone the page scrolls and the keys keep their job */
      if (k === 'ArrowDown' && menuHasFocus()) { e.preventDefault(); go(index() + 1, true); }
      else if (k === 'ArrowUp' && menuHasFocus()) { e.preventDefault(); go(index() - 1, true); }
      else if (k === 'Enter' && menu.contains(document.activeElement)) { window.setTimeout(enterPanel, 0); }
      else if (k === 'Escape' && panel.contains(document.activeElement)) { e.preventDefault(); backToMenu(); }
    });

    /* ---- game pad (optional): d-pad or left stick, A (button 0) = Enter, B (button 1) = Esc ---- */
    var pads = 0, held = {}, when = {};
    function down(gp, i) { return gp.buttons[i] && gp.buttons[i].pressed; }
    function edge(name, now, repeats) {
      if (held[name] && !(repeats && now - when[name] > 260)) return false;
      held[name] = true;
      when[name] = now;
      return true;
    }
    function poll() {
      var list = navigator.getGamepads ? navigator.getGamepads() : [];
      var gp = null, now = Date.now(), i;
      for (i = 0; i < list.length; i++) if (list[i]) { gp = list[i]; break; }
      if (gp) {
        var s = {
          up: down(gp, 12) || (gp.axes[1] || 0) < -0.6, down: down(gp, 13) || (gp.axes[1] || 0) > 0.6,
          left: down(gp, 14) || (gp.axes[0] || 0) < -0.6, right: down(gp, 15) || (gp.axes[0] || 0) > 0.6,
          a: down(gp, 0), b: down(gp, 1)
        };
        Object.keys(s).forEach(function (n) {
          if (!s[n]) { held[n] = false; return; }
          if (!edge(n, now, n !== 'a' && n !== 'b')) return;
          if (open) {
            if (n === 'left') step(-1); else if (n === 'right') step(1); else if (n === 'b') shut();
          } else if (n === 'down') go(index() + 1, true);
          else if (n === 'up') go(index() - 1, true);
          else if (n === 'a') {
            var el = document.activeElement;
            if (el && el !== document.body && el.click) el.click(); else enterPanel();
            if (menu.contains(el)) window.setTimeout(enterPanel, 0);
          } else if (n === 'b') backToMenu();
        });
      }
      if (pads > 0) window.requestAnimationFrame(poll);
    }
    window.addEventListener('gamepadconnected', function () {
      pads += 1;
      if (pads === 1) window.requestAnimationFrame(poll);
    });
    window.addEventListener('gamepaddisconnected', function () { pads = Math.max(0, pads - 1); });

    /* first paint: the hash decides (a #shot-N link opens the viewer) */
    if (viewerFromHash()) show(resolve('#shots').sec, null, true);
    else { var r = resolve(location.hash); show(r.sec, r.target, false); }
    doc.classList.add('ready');
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start);
  else start();
})();

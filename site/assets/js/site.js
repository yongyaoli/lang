/* ============================================================
   L (lang) 站点脚本 · 无依赖 · 渐进增强
   1. 代码高亮  2. 复制按钮  3. 标签页  4. 下载链接  5. 年份
   ============================================================ */
(function () {
  'use strict';

  /* ---------- 1. 代码高亮 ---------- */
  var KW = /^(let|const|fn|return|if|else|for|while|break|continue|import|as|try|catch|throw|true|false|nil|and|or|not|self)$/;
  var RE = /(\/\/[^\n]*)|("(?:\\.|[^"\\\n])*")|('(?:\\.|[^'\\\n])*')|(\b0x[0-9a-fA-F_]+\b|\b0b[01_]+\b|\b\d+(?:\.\d+)?(?:e[-+]?\d+)?\b)|([A-Za-z_]\w*)/g;

  function esc(s) {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  function highlight(code) {
    if (code.getAttribute('data-hl-done')) return;
    code.setAttribute('data-hl-done', '1');
    var src = code.textContent;
    var out = '';
    var last = 0;
    var m, t, cls;
    RE.lastIndex = 0;
    while ((m = RE.exec(src)) !== null) {
      if (m.index > last) out += esc(src.slice(last, m.index));
      t = m[0];
      cls = '';
      if (m[1]) cls = 'c';
      else if (m[2] || m[3]) cls = 's';
      else if (m[4]) cls = 'n';
      else if (m[5] && KW.test(m[5])) cls = 'k';
      out += cls ? '<span class="tk-' + cls + '">' + esc(t) + '</span>' : esc(t);
      last = m.index + t.length;
    }
    out += esc(src.slice(last));
    code.innerHTML = out;
  }

  var codes = document.querySelectorAll('code.hl');
  for (var i = 0; i < codes.length; i++) highlight(codes[i]);

  /* ---------- 2. 复制按钮 ---------- */
  function legacyCopy(text, done) {
    var ta = document.createElement('textarea');
    ta.value = text;
    ta.setAttribute('readonly', '');
    ta.style.cssText = 'position:fixed;top:-1000px;opacity:0';
    document.body.appendChild(ta);
    ta.select();
    try { document.execCommand('copy'); } catch (e) {}
    document.body.removeChild(ta);
    done();
  }

  var copies = document.querySelectorAll('.copy');
  for (var j = 0; j < copies.length; j++) {
    (function (btn) {
      var label = btn.textContent;
      var doneLabel = btn.getAttribute('data-copied') || label;
      btn.addEventListener('click', function () {
        var box = btn.closest('.term, .console, .code');
        var code = box ? box.querySelector('code') : null;
        var text = code ? code.textContent : '';
        var done = function () {
          btn.textContent = doneLabel;
          btn.classList.add('ok');
          setTimeout(function () {
            btn.textContent = label;
            btn.classList.remove('ok');
          }, 1600);
        };
        if (navigator.clipboard && navigator.clipboard.writeText) {
          navigator.clipboard.writeText(text).then(done, function () { legacyCopy(text, done); });
        } else {
          legacyCopy(text, done);
        }
      });
    })(copies[j]);
  }

  /* ---------- 3. 标签页 ---------- */
  var tabGroups = document.querySelectorAll('[data-tabs]');
  for (var k = 0; k < tabGroups.length; k++) {
    (function (box) {
      var btns = [].slice.call(box.querySelectorAll('.tab-bar button'));
      var panels = [].slice.call(box.querySelectorAll('.tab-panels > .panel'));
      if (!btns.length || btns.length !== panels.length) return;
      box.classList.add('ready');

      function sel(n) {
        btns.forEach(function (b, x) {
          b.classList.toggle('on', x === n);
          b.setAttribute('aria-selected', x === n ? 'true' : 'false');
          b.tabIndex = x === n ? 0 : -1;
        });
        panels.forEach(function (p, x) {
          p.classList.toggle('on', x === n);
        });
      }

      btns.forEach(function (b, n) {
        b.addEventListener('click', function () { sel(n); });
        b.addEventListener('keydown', function (e) {
          var d = e.key === 'ArrowRight' ? 1 : e.key === 'ArrowLeft' ? -1 : 0;
          if (!d) return;
          e.preventDefault();
          var next = (n + d + btns.length) % btns.length;
          btns[next].focus();
          sel(next);
        });
      });
      sel(0);
    })(tabGroups[k]);
  }

  /* ---------- 4. 下载清单 ---------- */
  var R = window.L_RELEASES;
  if (R && R.assets) {
    var versions = document.querySelectorAll('[data-version]');
    for (var v = 0; v < versions.length; v++) versions[v].textContent = R.version;

    var dates = document.querySelectorAll('[data-released]');
    for (var d = 0; d < dates.length; d++) dates[d].textContent = R.released;

    var dls = document.querySelectorAll('[data-asset]');
    for (var a = 0; a < dls.length; a++) {
      (function (el) {
        var item = R.assets[el.getAttribute('data-asset')];
        if (!item) return;
        el.href = item.published
          ? R.repo + '/releases/latest/download/' + item.file
          : R.repo + '/releases/latest';
        var f = el.querySelector('[data-file]');
        if (f && item.file) f.textContent = item.file;
        var n = el.querySelector('[data-note]');
        if (n && item.note) n.textContent = item.note;
        if (!item.published) el.classList.add('pending');
      })(dls[a]);
    }
  }

  /* ---------- 5. 年份 ---------- */
  var years = document.querySelectorAll('[data-year]');
  for (var y = 0; y < years.length; y++) years[y].textContent = new Date().getFullYear();
})();

/* dpCREATOR Workbench: Ctrl+K search on every admin page.
   Finds orders, customers, products, pages and Inbox conversations (server), and admin menu items (this page). */
(function () {
  'use strict';
  var token = new URLSearchParams(location.search).get('user_token');
  if (!token) return;
  var base = location.pathname.replace(/[^/]*$/, '') + 'index.php?user_token=' + encodeURIComponent(token) + '&route=';
  var box, input, list, items = [], sel = 0, timer = null, seq = 0;

  function esc(s) { return String(s).replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; }); }

  function menuItems() {
    var out = [];
    document.querySelectorAll('#menu a[href]').forEach(function (a) {
      var href = a.getAttribute('href');
      if (!href || href === '#' || /^javascript/i.test(href) || /^#/.test(href)) return;
      var path = [], li = a.closest('li');
      while (li) {
        var t = li.querySelector(':scope > a');
        if (t) path.unshift(t.textContent.trim());
        li = li.parentElement ? li.parentElement.closest('li') : null;
      }
      out.push({ group: 'Admin menu', title: path[path.length - 1] || a.textContent.trim(), sub: path.slice(0, -1).join(' › '), href: href });
    });
    return out;
  }
  var quick = [
    { group: 'Go to', title: 'Workbench', sub: 'Admin home', href: base + 'extension/module/dpc_desk/desk' },
    { group: 'Go to', title: 'Orders', sub: 'Sales › Orders', href: base + 'sale/order' },
    { group: 'Go to', title: 'Add a product', sub: 'Catalog › Products', href: base + 'catalog/product/add' },
    { group: 'Go to', title: 'Inbox', sub: 'Email', href: base + 'extension/module/dpc_inbox' },
    { group: 'Go to', title: 'Classic dashboard', sub: 'OpenCart', href: base + 'common/dashboard&classic=1' }
  ];

  function build() {
    box = document.createElement('div');
    box.id = 'dpc-search';
    box.hidden = true;
    box.innerHTML = '<div class="dps-back"></div><div class="dps-win" role="dialog" aria-modal="true" aria-label="Search the admin">' +
      '<div class="dps-in"><i class="fa fa-search" aria-hidden="true"></i><input type="search" placeholder="Order number, customer, product, page or menu item" aria-label="Search" autocomplete="off"><kbd>Esc</kbd></div>' +
      '<ul class="dps-list" role="listbox"></ul><div class="dps-foot">↑ ↓ to choose · Enter to open · Ctrl+Enter in a new tab</div></div>';
    document.body.appendChild(box);
    var css = document.createElement('style');
    css.textContent = '#dpc-search{position:fixed;inset:0;z-index:100000}#dpc-search[hidden]{display:none}' +
      '.dps-back{position:absolute;inset:0;background:rgba(20,28,36,.45)}' +
      '.dps-win{position:relative;margin:9vh auto 0;width:min(640px,calc(100% - 32px));background:#fff;border-radius:8px;box-shadow:0 18px 50px rgba(0,0,0,.3);overflow:hidden;font-size:14px;color:#1E2A35}' +
      '.dps-in{display:flex;align-items:center;gap:10px;padding:12px 14px;border-bottom:1px solid #E3E8ED}.dps-in .fa{color:#6B7885}' +
      '.dps-in input{flex:1;border:0;outline:0;font-size:16px;background:none;min-width:0}' +
      '.dps-in kbd{background:#EEF1F4;color:#6B7885;border:1px solid #E3E8ED;box-shadow:none;font-size:11px}' +
      '.dps-list{list-style:none;margin:0;padding:6px 0;max-height:60vh;overflow:auto}' +
      '.dps-list .g{padding:8px 14px 3px;font-size:11.5px;font-weight:700;color:#6B7885}' +
      '.dps-list .r a{display:block;padding:7px 14px;color:#1E2A35;text-decoration:none}' +
      '.dps-list .r small{display:block;color:#6B7885;font-size:12px}' +
      '.dps-list .r.on a{background:#E8F1FA}.dps-list .none{padding:12px 14px;color:#6B7885}' +
      '.dps-foot{padding:7px 14px;border-top:1px solid #E3E8ED;font-size:12px;color:#6B7885}';
    document.head.appendChild(css);
    input = box.querySelector('input');
    list = box.querySelector('.dps-list');
    box.querySelector('.dps-back').addEventListener('click', close);
    input.addEventListener('input', function () { clearTimeout(timer); timer = setTimeout(run, 160); });
    input.addEventListener('keydown', function (e) {
      if (e.key === 'ArrowDown') { e.preventDefault(); move(1); }
      else if (e.key === 'ArrowUp') { e.preventDefault(); move(-1); }
      else if (e.key === 'Enter') { e.preventDefault(); go(items[sel], e.ctrlKey || e.metaKey); }
      else if (e.key === 'Escape') { e.preventDefault(); close(); }
    });
    list.addEventListener('mousemove', function (e) { var r = e.target.closest('.r'); if (r) { sel = +r.dataset.i; mark(); } });
  }

  function render(rows, q) {
    items = rows;
    sel = 0;
    if (!rows.length) { list.innerHTML = '<li class="none">' + (q ? 'Nothing found for “' + esc(q) + '”.' : '') + '</li>'; return; }
    var h = '', g = null;
    rows.forEach(function (r, i) {
      if (r.group !== g) { g = r.group; h += '<li class="g" role="presentation">' + esc(g) + '</li>'; }
      h += '<li class="r" role="option" data-i="' + i + '"><a href="' + esc(r.href) + '">' + esc(r.title) + (r.sub ? '<small>' + esc(r.sub) + '</small>' : '') + '</a></li>';
    });
    list.innerHTML = h;
    mark();
  }
  function mark() {
    list.querySelectorAll('.r').forEach(function (li) {
      var on = +li.dataset.i === sel;
      li.classList.toggle('on', on);
      li.setAttribute('aria-selected', on);
      if (on) li.scrollIntoView({ block: 'nearest' });
    });
  }
  function move(d) { if (items.length) { sel = (sel + d + items.length) % items.length; mark(); } }
  function go(it, newTab) { if (!it) return; if (newTab) window.open(it.href, '_blank'); else location.href = it.href; }

  function run() {
    var q = input.value.trim(), my = ++seq;
    if (!q) { render(quick, ''); return; }
    var words = q.toLowerCase().split(/\s+/);
    var local = quick.concat(menuItems()).filter(function (m) {
      var t = (m.title + ' ' + m.sub).toLowerCase();
      return words.every(function (w) { return t.indexOf(w) >= 0; });
    }).slice(0, 8);
    render(local, '');
    fetch(base + 'extension/module/dpc_desk/search&q=' + encodeURIComponent(q), { credentials: 'same-origin' })
      .then(function (r) { return r.ok ? r.json() : []; })
      .then(function (rows) { if (my === seq) render((Array.isArray(rows) ? rows : []).concat(local), q); })
      .catch(function () { if (my === seq) render(local, q); });
  }

  function open() {
    if (!box) build();
    box.hidden = false;
    input.value = '';
    render(quick, '');
    input.focus();
  }
  function close() { if (box) box.hidden = true; }

  document.addEventListener('keydown', function (e) {
    if ((e.ctrlKey || e.metaKey) && (e.key === 'k' || e.key === 'K')) { e.preventDefault(); if (box && !box.hidden) close(); else open(); }
  });
  window.dpcSearch = { open: open, close: close };
})();

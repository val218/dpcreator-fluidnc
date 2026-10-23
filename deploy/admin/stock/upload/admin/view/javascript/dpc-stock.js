/* dpCREATOR Purchases and stock: the admin page (talks to extension/module/dpc_stock/api) */
(function () {
  'use strict';
  var API = window.DPS_API, CAN = !!window.DPS_CAN;
  var main = document.getElementById('dps-main');
  var S = null;               // state: items, products, recipes, suppliers, settings, channels
  var tab = 'overview';
  var CHANNELS = ['Elecrow', 'Tindie', 'eBay', 'Etsy', 'Amazon', 'By hand', 'Trade show'];
  var CUR = ['EUR', 'USD', 'GBP', 'CNY', 'PLN', 'SEK'];

  // ---------------------------------------------------------------- helpers
  function esc(s) { return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]; }); }
  function num(v) { var n = parseFloat(String(v == null ? '' : v).replace(',', '.')); return isNaN(n) ? 0 : n; }
  function eur(v) { return (num(v) < 0 ? '−' : '') + '€' + Math.abs(num(v)).toLocaleString('en-GB', { minimumFractionDigits: 2, maximumFractionDigits: 2 }); }
  function q(v) { var n = num(v); return Number.isInteger(n) ? String(n) : String(Math.round(n * 1000) / 1000); }
  function today() { return new Date().toISOString().slice(0, 10); }
  function url(f, extra) { return API + '&f=' + f + (extra || ''); }
  function get(f, extra) { return fetch(url(f, extra), { credentials: 'same-origin' }).then(function (r) { return r.json(); }); }
  function post(f, body) {
    return fetch(url(f), { method: 'POST', credentials: 'same-origin', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) })
      .then(function (r) { return r.json(); }).then(function (j) { if (j.error) throw new Error(j.error); return j; });
  }
  var tt;
  function toast(t, bad) { var el = document.getElementById('dps-toast'); el.textContent = t; el.className = 'on' + (bad ? ' bad' : ''); clearTimeout(tt); tt = setTimeout(function () { el.className = ''; }, 3800); }
  function item(id) { return (S.items || []).find(function (i) { return +i.item_id === +id; }); }
  function kindName(k) { return k === 'product' ? 'Finished product' : k === 'part' ? 'Part' : 'Material'; }
  function itemOptions(sel, kinds, blank) {
    return (blank ? '<option value="">' + esc(blank) + '</option>' : '') + S.items.filter(function (i) { return !kinds || kinds.indexOf(i.kind) >= 0; })
      .map(function (i) { return '<option value="' + i.item_id + '"' + (+sel === +i.item_id ? ' selected' : '') + '>' + esc(i.name) + (i.sku ? ' · ' + esc(i.sku) : '') + ' (' + q(i.have) + ' ' + esc(i.unit) + ')</option>'; }).join('');
  }
  function modal(title, body, foot) {
    var m = document.createElement('div');
    m.className = 'dps-modal';
    m.innerHTML = '<div role="dialog" aria-modal="true" aria-label="' + esc(title) + '"><h3>' + esc(title) + '</h3><div class="b">' + body + '</div><div class="foot">' + foot + '<button type="button" class="dps-btn" data-x>Close</button></div></div>';
    document.body.appendChild(m);
    m.addEventListener('click', function (e) { if (e.target === m || e.target.hasAttribute('data-x')) m.remove(); });
    document.addEventListener('keydown', function k(e) { if (e.key === 'Escape') { m.remove(); document.removeEventListener('keydown', k); } });
    var f = m.querySelector('input,select,textarea'); if (f) f.focus();
    return m;
  }
  function val(m, name) { var el = m.querySelector('[name="' + name + '"]'); return !el ? '' : el.type === 'checkbox' ? el.checked : el.value; }
  function busy(btn, on) { if (btn) { btn.disabled = on; } }

  function load() {
    return get('state').then(function (j) { if (j.error) throw new Error(j.error); S = j; CAN = !!j.can_modify; });
  }

  // ---------------------------------------------------------------- tabs
  document.querySelectorAll('.dps-tabs button').forEach(function (b) {
    b.addEventListener('click', function () { show(b.dataset.tab); });
  });
  function show(t) {
    tab = t;
    document.querySelectorAll('.dps-tabs button').forEach(function (b) { b.setAttribute('aria-selected', b.dataset.tab === t); });
    try { history.replaceState(null, '', location.pathname + location.search.replace(/&tab=\w+/, '') + '&tab=' + t); } catch (e) { /* ignore */ }
    ({ overview: vOverview, invoices: vInvoices, items: vItems, batches: vBatches, sales: vSales, reports: vReports, settings: vSettings })[t]();
  }
  function refresh() { return load().then(function () { show(tab); }); }

  // ---------------------------------------------------------------- overview
  function vOverview() {
    main.innerHTML = '<p class="dps-mut">Loading…</p>';
    Promise.all([get('report'), get('invoices')]).then(function (r) {
      if (tab !== 'overview') return;
      var rep = r[0], inv = r[1].invoices || [];
      var low = S.items.filter(function (i) { return num(i.reorder_at) > 0 && num(i.have) <= num(i.reorder_at); });
      var drafts = inv.filter(function (i) { return i.status === 'draft'; });
      var spend = (rep.spend || []).reduce(function (a, s) { return a + num(s.t); }, 0);
      var outRev = (rep.channels || []).filter(function (c) { return c.name !== 'dpcreator.eu'; }).reduce(function (a, c) { return a + num(c.rev); }, 0);
      var h = '<div class="dps-kpis">' +
        '<div class="dps-kpi"><span>Stock value</span><b>' + eur(rep.stock_value) + '</b><small>at average cost</small></div>' +
        '<div class="dps-kpi"><span>Bought this year</span><b>' + eur(spend) + '</b><small>' + inv.filter(function (i) { return i.date >= rep.from; }).length + ' invoices</small></div>' +
        '<div class="dps-kpi"><span>Built this year</span><b>' + q(rep.batches.units) + ' units</b><small>' + q(rep.batches.n) + ' batches · ' + q(rep.batches.hours) + ' h of work</small></div>' +
        '<div class="dps-kpi"><span>Sold outside the shop</span><b>' + eur(outRev) + '</b><small>this year</small></div></div>';
      h += '<div class="dps-card"><div class="dps-row">' +
        (CAN ? '<button type="button" class="dps-btn pri" data-a="inv"><i class="fa fa-file-text-o"></i> Add an invoice</button><button type="button" class="dps-btn" data-a="batch"><i class="fa fa-wrench"></i> Record a build batch</button><button type="button" class="dps-btn" data-a="sale"><i class="fa fa-handshake-o"></i> Record an outside sale</button><button type="button" class="dps-btn" data-a="item"><i class="fa fa-plus"></i> Add a stock item</button>' : '<span class="dps-mut">You can look, but not change.</span>') + '</div></div>';
      h += '<div class="dps-grid2"><div class="dps-card"><h2>Invoices to put in stock</h2>' + (drafts.length ? table(['Date', 'Supplier', 'Invoice', 'Total'], drafts.map(function (i) { return { id: i.invoice_id, cells: [i.date, esc(i.supplier || '—'), esc(i.number || '#' + i.invoice_id), '<span class="r">' + eur(i.total_eur) + '</span>'] }; }), 'inv') : '<p class="dps-mut">None waiting.</p>') + '</div>';
      h += '<div class="dps-card"><h2>Running low</h2>' + (low.length ? table(['Item', 'In stock', 'Reorder at'], low.map(function (i) { return { id: i.item_id, cells: [esc(i.name), '<b style="color:var(--red)">' + q(i.have) + '</b> ' + esc(i.unit), q(i.reorder_at)] }; }), 'item') : '<p class="dps-mut">' + (S.items.length ? 'Nothing is below its reorder level.' : 'No stock items yet. Start with <b>Add a stock item</b>, or add an invoice and create items as you go.') + '</p>') + '</div></div>';
      main.innerHTML = h;
      wireRows();
      main.querySelectorAll('[data-a]').forEach(function (b) {
        b.addEventListener('click', function () { ({ inv: function () { invoiceForm(null); }, batch: batchForm, sale: saleForm, item: function () { itemForm(null); } })[b.dataset.a](); });
      });
    }).catch(function (e) { main.innerHTML = '<div class="dps-err">' + esc(e.message) + '</div>'; });
  }

  function table(head, rows, kind) {
    return '<table class="dps-t"><thead><tr>' + head.map(function (h) { return '<th>' + h + '</th>'; }).join('') + '</tr></thead><tbody>' +
      rows.map(function (r) { return '<tr class="click" data-k="' + kind + '" data-id="' + r.id + '">' + r.cells.map(function (c) { return '<td>' + c + '</td>'; }).join('') + '</tr>'; }).join('') + '</tbody></table>';
  }
  function wireRows() {
    main.querySelectorAll('tr.click').forEach(function (tr) {
      tr.addEventListener('click', function () {
        var id = tr.dataset.id;
        if (tr.dataset.k === 'inv') get('invoice', '&id=' + id).then(function (j) { invoiceForm(j.invoice); });
        else if (tr.dataset.k === 'item') itemForm(item(id));
      });
    });
  }

  // ---------------------------------------------------------------- invoices
  function vInvoices() {
    main.innerHTML = '<p class="dps-mut">Loading…</p>';
    get('invoices').then(function (j) {
      if (tab !== 'invoices') return;
      var inv = j.invoices || [];
      var h = '<div class="dps-card"><div class="dps-ch"><h2>Supplier invoices</h2>' + (CAN ? '<button type="button" class="dps-btn pri" id="dps-new-inv"><i class="fa fa-plus"></i> Add an invoice</button>' : '') + '</div>';
      h += inv.length ? table(['Date', 'Supplier', 'Invoice', 'Lines', 'Total EUR', 'File', 'Status'], inv.map(function (i) {
        return { id: i.invoice_id, cells: [i.date, esc(i.supplier || '—'), esc(i.number || '#' + i.invoice_id) + (i.currency !== 'EUR' ? '<small>' + esc(i.currency) + '</small>' : ''), i.lines_n, '<span class="r">' + eur(i.total_eur) + '</span>', i.file ? '<i class="fa fa-paperclip" title="' + esc(i.file_name) + '"></i>' : '', '<span class="dps-pill ' + i.status + '">' + (i.status === 'received' ? 'in stock' : 'draft') + '</span>'] };
      }), 'inv') : '<p class="dps-mut">No invoices yet. Add the Elecrow, JLCPCB or LCSC invoices here with their PDF.</p>';
      main.innerHTML = h + '</div>';
      wireRows();
      var nb = document.getElementById('dps-new-inv'); if (nb) nb.onclick = function () { invoiceForm(null); };
    });
  }

  function invoiceForm(inv) {
    var ro = inv && inv.status === 'received';
    inv = inv || { date: today(), currency: 'EUR', rate: 1, shipping: 0, customs: 0, vat: 0, vat_reclaim: 0, other: 0, lines: [{ qty: 1, unit_price: 0 }] };
    var sup = (S.suppliers || []).find(function (s) { return +s.supplier_id === +inv.supplier_id; });
    var b = '<div class="dps-form">' +
      '<label class="w2">Supplier<input type="text" name="supplier_name" list="dps-sups" value="' + esc(sup ? sup.name : '') + '" placeholder="Elecrow, JLCPCB, LCSC …"></label>' +
      '<label>Invoice number<input type="text" name="number" value="' + esc(inv.number || '') + '"></label>' +
      '<label>Date<input type="date" name="date" value="' + esc(inv.date) + '"></label>' +
      '<label>Currency<select name="currency"' + (ro ? ' disabled' : '') + '>' + CUR.map(function (c) { return '<option' + (c === inv.currency ? ' selected' : '') + '>' + c + '</option>'; }).join('') + '</select></label>' +
      '<label>1 unit of the currency in EUR<input type="number" step="any" name="rate" value="' + esc(inv.rate) + '"' + (ro ? ' disabled' : '') + '></label>' +
      '<label class="w2">Note<input type="text" name="note" value="' + esc(inv.note || '') + '"></label></div>' +
      '<datalist id="dps-sups">' + (S.suppliers || []).map(function (s) { return '<option value="' + esc(s.name) + '">'; }).join('') + '<option value="Elecrow"><option value="JLCPCB"><option value="LCSC"><option value="PCBWay"><option value="Mouser"><option value="DigiKey"><option value="AliExpress"><option value="3dp.lv"></datalist>' +
      '<table class="dps-t" id="dps-lines"><thead><tr><th style="width:34%">Stock item</th><th>Description</th><th style="width:90px">Qty</th><th style="width:110px">Unit price</th><th class="r" style="width:110px">Landed each €</th>' + (ro ? '' : '<th></th>') + '</tr></thead><tbody></tbody></table>' +
      (ro ? '' : '<div class="dps-row" style="margin:6px 0 12px"><button type="button" class="dps-btn sm" id="dps-addline"><i class="fa fa-plus"></i> Add a line</button><button type="button" class="dps-btn sm" id="dps-newitem"><i class="fa fa-cube"></i> New stock item</button><span class="dps-mut" style="font-size:12px">Lines without a stock item are kept on the invoice but don\'t go into stock.</span></div>') +
      '<div class="dps-form">' +
      '<label>Shipping<input type="number" step="any" name="shipping" value="' + esc(inv.shipping) + '"' + (ro ? ' disabled' : '') + '></label>' +
      '<label>Customs and duty<input type="number" step="any" name="customs" value="' + esc(inv.customs) + '"' + (ro ? ' disabled' : '') + '></label>' +
      '<label>VAT<input type="number" step="any" name="vat" value="' + esc(inv.vat) + '"' + (ro ? ' disabled' : '') + '></label>' +
      '<label>Other charges<input type="number" step="any" name="other" value="' + esc(inv.other) + '"' + (ro ? ' disabled' : '') + '></label>' +
      '<label class="w4 chk"><input type="checkbox" name="vat_reclaim"' + (+inv.vat_reclaim ? ' checked' : '') + (ro ? ' disabled' : '') + '> The VAT can be reclaimed (then it isn\'t part of the cost of the goods)</label></div>' +
      '<div class="dps-sum" id="dps-invsum"></div>' +
      '<div class="dps-drop" id="dps-drop">' + (inv.file ? '<i class="fa fa-paperclip"></i> <a href="' + esc(API.replace('/api', '/file')) + '&id=' + inv.invoice_id + '" target="_blank" rel="noopener">' + esc(inv.file_name || 'invoice file') + '</a> · drop a file here or click to replace it' : '<i class="fa fa-cloud-upload"></i> Drop the invoice PDF or a photo here, or click to choose it') + '<input type="file" accept=".pdf,image/*" hidden></div>' +
      (ro ? '<p class="dps-mut" style="margin-top:8px">This invoice is in stock, so its lines and amounts are fixed. You can still change the supplier, number, date, note and file.</p>' : '');
    var foot = CAN ? (ro ? '<button type="button" class="dps-btn pri" data-s="save">Save</button>' : (inv.invoice_id ? '<button type="button" class="dps-btn" data-s="del" style="margin-right:auto;color:var(--red)!important">Remove draft</button>' : '') + '<button type="button" class="dps-btn" data-s="save">Save as draft</button><button type="button" class="dps-btn pri" data-s="receive"><i class="fa fa-check"></i> Save and put in stock</button>') : '';
    var m = modal(inv.invoice_id ? 'Invoice ' + (inv.number || '#' + inv.invoice_id) : 'Add an invoice', b, foot);
    var tbody = m.querySelector('#dps-lines tbody');
    var file = { file: inv.file || '', file_name: inv.file_name || '' };
    function lineRow(l) {
      var tr = document.createElement('tr');
      tr.innerHTML = '<td><select name="item_id"' + (ro ? ' disabled' : '') + '>' + itemOptions(l.item_id, null, '— not into stock —') + '</select></td><td><input type="text" name="descr" value="' + esc(l.descr || '') + '"' + (ro ? ' disabled' : '') + '></td>' +
        '<td><input type="number" step="any" name="qty" value="' + esc(l.qty) + '"' + (ro ? ' disabled' : '') + '></td><td><input type="number" step="any" name="unit_price" value="' + esc(l.unit_price) + '"' + (ro ? ' disabled' : '') + '></td><td class="r" data-landed>' + (l.landed_eur ? eur(l.landed_eur) : '') + '</td>' +
        (ro ? '' : '<td><button type="button" class="dps-btn sm" title="Remove line" aria-label="Remove line">×</button></td>');
      if (!ro) tr.querySelector('button').onclick = function () { tr.remove(); calc(); };
      tbody.appendChild(tr);
    }
    (inv.lines && inv.lines.length ? inv.lines : [{ qty: 1, unit_price: 0 }]).forEach(lineRow);
    function lines() {
      return Array.prototype.map.call(tbody.rows, function (tr) {
        return { item_id: tr.querySelector('[name=item_id]').value, descr: tr.querySelector('[name=descr]').value, qty: tr.querySelector('[name=qty]').value, unit_price: tr.querySelector('[name=unit_price]').value };
      });
    }
    function calc() {
      var rate = val(m, 'currency') === 'EUR' ? 1 : num(val(m, 'rate')) || 1;
      var goods = lines().reduce(function (a, l) { return a + num(l.qty) * num(l.unit_price); }, 0);
      var vat = num(val(m, 'vat')), reclaim = val(m, 'vat_reclaim');
      var extra = num(val(m, 'shipping')) + num(val(m, 'customs')) + num(val(m, 'other')) + (reclaim ? 0 : vat);
      Array.prototype.forEach.call(tbody.rows, function (tr, i) {
        var l = lines()[i], v = num(l.qty) * num(l.unit_price), share = goods > 0 ? v / goods : 0;
        tr.querySelector('[data-landed]').textContent = num(l.qty) > 0 ? eur(((v + extra * share) * rate) / num(l.qty)) : '';
      });
      m.querySelector('#dps-invsum').innerHTML = '<div><span>Goods</span><b>' + eur(goods * rate) + '</b></div><div><span>Shipping, customs, other' + (reclaim ? '' : ', VAT') + '</span><b>' + eur(extra * rate) + '</b></div>' +
        (reclaim && vat ? '<div><span>VAT to reclaim</span><b>' + eur(vat * rate) + '</b></div>' : '') + '<div class="big"><span>Invoice total</span><b>' + eur((goods + extra + (reclaim ? vat : 0)) * rate) + '</b></div>';
    }
    m.addEventListener('input', calc); m.addEventListener('change', calc); calc();
    var al = m.querySelector('#dps-addline'); if (al) al.onclick = function () { lineRow({ qty: 1, unit_price: 0 }); };
    var ni = m.querySelector('#dps-newitem'); if (ni) ni.onclick = function () { itemForm(null, function (id) { Array.prototype.forEach.call(tbody.querySelectorAll('select[name=item_id]'), function (s) { var v = s.value; s.innerHTML = itemOptions(v, null, '— not into stock —'); }); var last = tbody.rows[tbody.rows.length - 1].querySelector('select'); if (!last.value) last.value = id; }); };
    // file
    var drop = m.querySelector('#dps-drop'), fin = drop.querySelector('input');
    drop.addEventListener('click', function (e) { if (e.target.tagName !== 'A') fin.click(); });
    drop.addEventListener('dragover', function (e) { e.preventDefault(); drop.classList.add('on'); });
    drop.addEventListener('dragleave', function () { drop.classList.remove('on'); });
    drop.addEventListener('drop', function (e) { e.preventDefault(); drop.classList.remove('on'); if (e.dataTransfer.files[0]) up(e.dataTransfer.files[0]); });
    fin.addEventListener('change', function () { if (fin.files[0]) up(fin.files[0]); });
    function up(f) {
      if (!CAN) return;
      var fd = new FormData(); fd.append('file', f);
      drop.innerHTML = 'Uploading ' + esc(f.name) + '…';
      fetch(url('upload'), { method: 'POST', body: fd, credentials: 'same-origin' }).then(function (r) { return r.json(); }).then(function (j) {
        if (j.error) throw new Error(j.error);
        file = { file: j.file, file_name: j.file_name };
        drop.innerHTML = '<i class="fa fa-paperclip"></i> ' + esc(j.file_name) + ' attached. Save to keep it.';
      }).catch(function (e) { drop.innerHTML = '<span style="color:var(--red)">' + esc(e.message) + '</span>'; });
      drop.appendChild(fin);
    }
    m.querySelectorAll('[data-s]').forEach(function (btn) {
      btn.addEventListener('click', function () {
        var body = { invoice_id: inv.invoice_id || 0, supplier_name: val(m, 'supplier_name'), number: val(m, 'number'), date: val(m, 'date'), currency: val(m, 'currency'), rate: val(m, 'rate'),
          shipping: val(m, 'shipping'), customs: val(m, 'customs'), vat: val(m, 'vat'), vat_reclaim: val(m, 'vat_reclaim') ? 1 : 0, other: val(m, 'other'), note: val(m, 'note'), lines: lines(), file: file.file, file_name: file.file_name };
        if (btn.dataset.s === 'del') {
          if (!confirm('Remove this draft invoice? Nothing is in stock from it yet.')) return;
          busy(btn, true);
          return post('delete_invoice', { invoice_id: inv.invoice_id }).then(function () { m.remove(); toast('Draft removed.'); refresh(); }).catch(function (e) { busy(btn, false); toast(e.message, true); });
        }
        if (btn.dataset.s === 'receive') {
          var miss = lines().filter(function (l) { return num(l.qty) > 0 && !l.item_id; }).length;
          if (miss && !confirm(miss + ' line' + (miss > 1 ? 's have' : ' has') + ' no stock item and won\'t go into stock. Continue?')) return;
        }
        busy(btn, true);
        post('save_invoice', body).then(function (j) {
          return btn.dataset.s === 'receive' ? post('receive', { invoice_id: j.invoice_id }).then(function (r) { toast('Saved and put in stock: ' + r.lines + ' line' + (r.lines === 1 ? '' : 's') + '.'); }) : toast('Invoice saved.');
        }).then(function () { m.remove(); refresh(); }).catch(function (e) { busy(btn, false); toast(e.message, true); });
      });
    });
  }

  // ---------------------------------------------------------------- items
  function vItems() {
    var rows = S.items.map(function (i) {
      var low = num(i.reorder_at) > 0 && num(i.have) <= num(i.reorder_at);
      return { id: i.item_id, cells: ['<span class="dps-pill ' + i.kind + '">' + kindName(i.kind) + '</span>', esc(i.name) + (i.product_name ? '<small>shop: ' + esc(i.product_name) + '</small>' : '') + (i.sku ? '<small>' + esc(i.sku) + '</small>' : ''),
        '<span class="r">' + (low ? '<span class="dps-pill low">' + q(i.have) + '</span>' : q(i.have)) + ' ' + esc(i.unit) + '</span>', '<span class="r">' + eur(i.avg_cost) + '</span>', '<span class="r">' + (i.kind === 'product' ? eur(i.avg_full) : '') + '</span>', '<span class="r">' + eur(i.value) + '</span>'] };
    });
    main.innerHTML = '<div class="dps-card"><div class="dps-ch"><h2>Stock items</h2>' + (CAN ? '<button type="button" class="dps-btn pri" id="dps-new-item"><i class="fa fa-plus"></i> Add a stock item</button>' : '') + '</div>' +
      '<p class="dps-mut" style="margin-top:-4px">Finished products are linked to a shop product: their stock is the shop\'s quantity, so shop orders count too. Parts and materials (PCBs, modules, screens, filament, boxes) are kept here.</p>' +
      (rows.length ? table(['Kind', 'Item', 'In stock', 'Cost each', 'Full cost each', 'Value'], rows, 'item') : '<p class="dps-mut">No stock items yet.</p>') + '</div>';
    wireRows();
    var nb = document.getElementById('dps-new-item'); if (nb) nb.onclick = function () { itemForm(null); };
  }

  function itemForm(it, done) {
    var isNew = !it;
    it = it || { kind: 'part', unit: 'pcs', reorder_at: 0 };
    var used = S.items.filter(function (i) { return +i.product_id && (!it.item_id || +i.item_id !== +it.item_id); }).map(function (i) { return +i.product_id; });
    var b = '<div class="dps-form">' +
      '<label>Kind<select name="kind"><option value="product"' + (it.kind === 'product' ? ' selected' : '') + '>Finished product</option><option value="part"' + (it.kind === 'part' ? ' selected' : '') + '>Part</option><option value="material"' + (it.kind === 'material' ? ' selected' : '') + '>Material</option></select></label>' +
      '<label class="w2" data-prod>Shop product<select name="product_id"><option value="0">— choose —</option>' + S.products.filter(function (p) { return used.indexOf(+p.product_id) < 0; }).map(function (p) { return '<option value="' + p.product_id + '"' + (+p.product_id === +it.product_id ? ' selected' : '') + '>' + esc(p.name) + ' · ' + esc(p.model) + ' (stock ' + p.quantity + ')</option>'; }).join('') + '</select></label>' +
      '<label class="w2">Name<input type="text" name="name" value="' + esc(it.name || '') + '" placeholder="e.g. R3 PCB (assembled), ESP32-S3-WROOM-1-N16R8, PETG black"></label>' +
      '<label>SKU / part number<input type="text" name="sku" value="' + esc(it.sku || '') + '"></label>' +
      '<label>Unit<select name="unit">' + ['pcs', 'g', 'kg', 'm', 'ml'].map(function (u) { return '<option' + (u === it.unit ? ' selected' : '') + '>' + u + '</option>'; }).join('') + '</select></label>' +
      '<label>Warn when at or below<input type="number" step="any" name="reorder_at" value="' + esc(it.reorder_at) + '"></label>' +
      (isNew ? '<label>Cost each of what you already have, €<input type="number" step="any" name="start_cost" value="0"></label>' : '') +
      '<label class="w4">Note<input type="text" name="note" value="' + esc(it.note || '') + '"></label></div>';
    if (!isNew) {
      b += '<div class="dps-sum"><div><span>In stock</span><b>' + q(it.have) + ' ' + esc(it.unit) + '</b></div><div><span>Cost each</span><b>' + eur(it.avg_cost) + '</b></div>' + (it.kind === 'product' ? '<div><span>Full cost each (with labour)</span><b>' + eur(it.avg_full) + '</b></div>' : '') + '<div class="big"><span>Value</span><b>' + eur(it.value) + '</b></div></div>';
      if (CAN) b += '<div class="dps-card" style="background:#FBFCFD"><h2>Stocktake</h2><div class="dps-form"><label>Counted quantity<input type="number" step="any" name="counted" value="' + esc(it.have) + '"></label><label>Cost each for extra stock, €<input type="number" step="any" name="adj_cost" placeholder="average"></label><label class="w2">Why<input type="text" name="adj_note" placeholder="e.g. found 2 in a drawer, 1 broken"></label></div><button type="button" class="dps-btn sm" id="dps-adj">Set the counted quantity</button></div>';
      b += '<h2 style="font-size:15px;margin:10px 0 6px">History</h2><div id="dps-hist" class="dps-mut">Loading…</div>';
    }
    var foot = CAN ? (!isNew ? '<button type="button" class="dps-btn" data-s="archive" style="margin-right:auto">Archive</button>' : '') + '<button type="button" class="dps-btn pri" data-s="save">Save</button>' : '';
    var m = modal(isNew ? 'Add a stock item' : it.name, b, foot);
    function kindChanged() { m.querySelector('[data-prod]').style.display = val(m, 'kind') === 'product' ? '' : 'none'; }
    m.querySelector('[name=kind]').addEventListener('change', kindChanged); kindChanged();
    m.querySelector('[name=product_id]').addEventListener('change', function () {
      var p = S.products.find(function (x) { return +x.product_id === +val(m, 'product_id'); });
      if (p && !val(m, 'name')) m.querySelector('[name=name]').value = p.name.replace(/&amp;/g, '&');
    });
    if (!isNew) {
      get('moves', '&id=' + it.item_id).then(function (j) {
        var rows = (j.moves || []).map(function (mv) { return '<tr><td>' + esc(mv.date.slice(0, 10)) + '</td><td>' + esc(mv.note || mv.kind) + '</td><td class="r" style="color:' + (num(mv.qty) < 0 ? 'var(--red)' : 'var(--ok)') + '">' + (num(mv.qty) > 0 ? '+' : '') + q(mv.qty) + '</td><td class="r">' + eur(mv.unit_cost) + '</td></tr>'; });
        (j.shop || []).forEach(function (o) { rows.push('<tr><td>' + esc(o.date_added.slice(0, 10)) + '</td><td>Shop order #' + o.order_id + '</td><td class="r" style="color:var(--red)">−' + q(o.quantity) + '</td><td></td></tr>'); });
        m.querySelector('#dps-hist').innerHTML = rows.length ? '<table class="dps-t"><thead><tr><th>Date</th><th>What</th><th class="r">Qty</th><th class="r">Cost each</th></tr></thead><tbody>' + rows.join('') + '</tbody></table>' : 'No movements yet.';
      });
      var adj = m.querySelector('#dps-adj');
      if (adj) adj.onclick = function () {
        busy(adj, true);
        post('adjust', { item_id: it.item_id, counted: val(m, 'counted'), unit_cost: val(m, 'adj_cost'), note: val(m, 'adj_note') }).then(function (j) { toast(j.diff ? 'Stock set: ' + (j.diff > 0 ? '+' : '') + q(j.diff) + '.' : 'No change: the count matches.'); m.remove(); refresh(); }).catch(function (e) { busy(adj, false); toast(e.message, true); });
      };
    }
    m.querySelectorAll('[data-s]').forEach(function (btn) {
      btn.addEventListener('click', function () {
        if (btn.dataset.s === 'archive' && !confirm('Archive "' + it.name + '"? It disappears from the lists; its history stays.')) return;
        busy(btn, true);
        post('save_item', { item_id: it.item_id || 0, kind: val(m, 'kind'), product_id: val(m, 'kind') === 'product' ? val(m, 'product_id') : 0, name: val(m, 'name'), sku: val(m, 'sku'), unit: val(m, 'unit'), reorder_at: val(m, 'reorder_at'), start_cost: val(m, 'start_cost'), note: val(m, 'note'), archive: btn.dataset.s === 'archive' ? 1 : 0 })
          .then(function (j) { m.remove(); toast(btn.dataset.s === 'archive' ? 'Archived.' : 'Saved.'); return load().then(function () { if (done) done(j.item_id); else show(tab); }); })
          .catch(function (e) { busy(btn, false); toast(e.message, true); });
      });
    });
  }

  // ---------------------------------------------------------------- batches
  function vBatches() {
    main.innerHTML = '<p class="dps-mut">Loading…</p>';
    get('batches').then(function (j) {
      if (tab !== 'batches') return;
      var bs = j.batches || [];
      main.innerHTML = '<div class="dps-card"><div class="dps-ch"><h2>Build batches</h2>' + (CAN ? '<button type="button" class="dps-btn pri" id="dps-new-b"><i class="fa fa-wrench"></i> Record a build batch</button>' : '') + '</div>' +
        '<p class="dps-mut" style="margin-top:-4px">A batch uses parts from stock and adds the finished units that passed testing, at what they really cost.</p>' +
        (bs.length ? '<table class="dps-t"><thead><tr><th>Date</th><th>Product</th><th class="r">Passed</th><th class="r">Parts</th><th class="r">Printing</th><th class="r">Labour</th><th class="r">Cash each</th><th class="r">Full each</th></tr></thead><tbody>' + bs.map(function (b) {
          return '<tr><td>' + esc(b.date) + '<small>#' + b.batch_id + '</small></td><td>' + esc(b.name) + (b.note ? '<small>' + esc(b.note) + '</small>' : '') + '</td><td class="r">' + b.passed + ' of ' + b.started + '</td><td class="r">' + eur(b.parts_cost) + '</td><td class="r">' + eur(b.print_cost) + '</td><td class="r">' + q(b.labour_hours) + ' h<small>' + eur(b.labour_cost) + '</small></td><td class="r"><b>' + eur(b.passed ? b.cash_cost / b.passed : 0) + '</b></td><td class="r">' + eur(b.passed ? b.full_cost / b.passed : 0) + '</td></tr>';
        }).join('') + '</tbody></table>' : '<p class="dps-mut">No batches yet.</p>') + '</div>';
      var nb = document.getElementById('dps-new-b'); if (nb) nb.onclick = batchForm;
    });
  }

  function batchForm() {
    var prods = S.items.filter(function (i) { return i.kind === 'product'; });
    if (!prods.length) { toast('First add the finished product as a stock item (Stock items › Add, kind: Finished product).', true); return; }
    var st = S.settings;
    var b = '<div class="dps-form">' +
      '<label class="w2">Product made<select name="item_id">' + itemOptions(prods[0].item_id, ['product']) + '</select></label>' +
      '<label>Date<input type="date" name="date" value="' + today() + '"></label>' +
      '<label>Units started<input type="number" step="1" min="1" name="started" value="5"></label>' +
      '<label>Units that passed testing<input type="number" step="1" min="0" name="passed" value="5"></label>' +
      '<label>Labour, hours in total<input type="number" step="any" name="labour_hours" value="0"></label>' +
      '<label>Filament, grams in total<input type="number" step="any" name="print_grams" value="0"></label>' +
      '<label>Print hours in total<input type="number" step="any" name="print_hours" value="0"></label>' +
      '<label>Printing bought in (e.g. a 3dp.lv invoice), €<input type="number" step="any" name="print_invoice" value="0"></label>' +
      '<label>Packaging each, €<input type="number" step="any" name="packaging_each" value="0"></label>' +
      '<label>Other costs, €<input type="number" step="any" name="extra" value="0"></label>' +
      '<label class="w4">Note<input type="text" name="note" placeholder="e.g. first R3 batch, 1 failed on the Ethernet test"></label></div>' +
      '<p class="dps-mut" style="font-size:12px;margin:-4px 0 8px">Rates: labour €' + q(st.rate) + '/h, filament €' + q(st.filament) + '/kg, printer €' + q(st.machine) + '/h. Change them in Settings.</p>' +
      '<table class="dps-t" id="dps-uses"><thead><tr><th style="width:42%">Part or material used</th><th class="r" style="width:110px">Per unit</th><th class="r" style="width:120px">Used in total</th><th class="r">In stock</th><th class="r">Cost</th><th></th></tr></thead><tbody></tbody></table>' +
      '<div class="dps-row" style="margin:6px 0"><button type="button" class="dps-btn sm" id="dps-adduse"><i class="fa fa-plus"></i> Add a part</button><button type="button" class="dps-btn sm" id="dps-saverec"><i class="fa fa-bookmark"></i> Save these amounts as the product\'s recipe</button></div>' +
      '<div id="dps-bwarn"></div><div class="dps-sum" id="dps-bsum"></div>';
    var m = modal('Record a build batch', b, '<label class="chk" style="margin-right:auto"><input type="checkbox" name="allow_short"> Use anyway when stock is short</label><button type="button" class="dps-btn pri" id="dps-saveb"><i class="fa fa-check"></i> Save the batch</button>');
    var tbody = m.querySelector('#dps-uses tbody');
    function useRow(id, per) {
      var tr = document.createElement('tr');
      tr.innerHTML = '<td><select name="u_item">' + itemOptions(id, ['part', 'material'], '— choose —') + '</select></td><td><input type="number" step="any" name="u_per" value="' + esc(per) + '"></td><td><input type="number" step="any" name="u_tot"></td><td class="r" data-have></td><td class="r" data-cost></td><td><button type="button" class="dps-btn sm" aria-label="Remove">×</button></td>';
      tr.querySelector('button').onclick = function () { tr.remove(); calc(); };
      tr.querySelector('[name=u_per]').addEventListener('input', function () { tr.querySelector('[name=u_tot]').value = q(num(this.value) * num(val(m, 'started'))); });
      tr.querySelector('[name=u_tot]').value = q(num(per) * num(val(m, 'started')));
      tbody.appendChild(tr);
    }
    function fromRecipe() {
      tbody.innerHTML = '';
      var r = S.recipes[val(m, 'item_id')], n = num(val(m, 'started'));
      if (r) {
        (r.lines || []).forEach(function (l) { useRow(l.item_id, l.qty); });
        m.querySelector('[name=labour_hours]').value = q(num(r.labour_min) * n / 60);
        m.querySelector('[name=print_grams]').value = q(num(r.print_grams) * n);
        m.querySelector('[name=print_hours]').value = q(num(r.print_hours) * n);
        m.querySelector('[name=packaging_each]').value = q(r.packaging);
      } else useRow('', 1);
      calc();
    }
    function uses() {
      return Array.prototype.map.call(tbody.rows, function (tr) { return { item_id: tr.querySelector('[name=u_item]').value, per: tr.querySelector('[name=u_per]').value, qty: tr.querySelector('[name=u_tot]').value }; }).filter(function (u) { return u.item_id; });
    }
    function body() {
      return { item_id: val(m, 'item_id'), date: val(m, 'date'), started: val(m, 'started'), passed: val(m, 'passed'), labour_hours: val(m, 'labour_hours'), print_grams: val(m, 'print_grams'), print_hours: val(m, 'print_hours'),
        print_invoice: val(m, 'print_invoice'), packaging_each: val(m, 'packaging_each'), extra: val(m, 'extra'), note: val(m, 'note'), uses: uses(), allow_short: val(m, 'allow_short') ? 1 : 0 };
    }
    var ct;
    function calc() {
      Array.prototype.forEach.call(tbody.rows, function (tr) {
        var it = item(tr.querySelector('[name=u_item]').value), tot = num(tr.querySelector('[name=u_tot]').value);
        tr.querySelector('[data-have]').innerHTML = it ? (num(it.have) < tot ? '<b style="color:var(--red)">' + q(it.have) + '</b>' : q(it.have)) + ' ' + esc(it.unit) : '';
        tr.querySelector('[data-cost]').textContent = it ? eur(tot * num(it.avg_cost)) : '';
      });
      clearTimeout(ct);
      ct = setTimeout(function () {
        post('batch_preview', body()).then(function (c) {
          m.querySelector('#dps-bwarn').innerHTML = c.short && c.short.length ? '<div class="dps-warn">Not enough in stock: ' + esc(c.short.join(', ')) + '.</div>' : '';
          m.querySelector('#dps-bsum').innerHTML = '<div><span>Parts</span><b>' + eur(c.parts) + '</b></div><div><span>Printing</span><b>' + eur(c.print) + '</b></div><div><span>Packaging</span><b>' + eur(c.packaging) + '</b></div><div><span>Other</span><b>' + eur(c.extra) + '</b></div>' +
            '<div><span>Cash cost</span><b>' + eur(c.cash) + '</b></div><div><span>Labour</span><b>' + eur(c.labour) + '</b></div><div class="big"><span>Cash cost each</span><b>' + eur(c.unit_cash) + '</b></div><div class="big"><span>Full cost each (with labour)</span><b>' + eur(c.unit_full) + '</b></div>';
        }).catch(function (e) { m.querySelector('#dps-bsum').innerHTML = '<div class="dps-err">' + esc(e.message) + '</div>'; });
      }, 250);
    }
    m.querySelector('[name=item_id]').addEventListener('change', fromRecipe);
    m.querySelector('[name=started]').addEventListener('input', function () {
      var n = num(this.value);
      Array.prototype.forEach.call(tbody.rows, function (tr) { tr.querySelector('[name=u_tot]').value = q(num(tr.querySelector('[name=u_per]').value) * n); });
      var r = S.recipes[val(m, 'item_id')];
      if (r) { m.querySelector('[name=labour_hours]').value = q(num(r.labour_min) * n / 60); m.querySelector('[name=print_grams]').value = q(num(r.print_grams) * n); m.querySelector('[name=print_hours]').value = q(num(r.print_hours) * n); }
      if (num(val(m, 'passed')) > n) m.querySelector('[name=passed]').value = n;
    });
    m.addEventListener('input', calc); m.addEventListener('change', calc);
    m.querySelector('#dps-adduse').onclick = function () { useRow('', 1); };
    m.querySelector('#dps-saverec').onclick = function () {
      var n = num(val(m, 'started')) || 1;
      post('save_recipe', { item_id: val(m, 'item_id'), labour_min: num(val(m, 'labour_hours')) * 60 / n, print_grams: num(val(m, 'print_grams')) / n, print_hours: num(val(m, 'print_hours')) / n, packaging: val(m, 'packaging_each'),
        lines: uses().map(function (u) { return { item_id: u.item_id, qty: u.per }; }) }).then(function () { toast('Recipe saved: the next batch of this product fills itself in.'); load(); }).catch(function (e) { toast(e.message, true); });
    };
    m.querySelector('#dps-saveb').onclick = function () {
      var btn = this; busy(btn, true);
      post('save_batch', body()).then(function (j) { m.remove(); toast('Batch #' + j.batch_id + ' saved and in stock.'); refresh(); }).catch(function (e) { busy(btn, false); toast(e.message, true); });
    };
    fromRecipe();
  }

  // ---------------------------------------------------------------- outside sales
  function vSales() {
    main.innerHTML = '<p class="dps-mut">Loading…</p>';
    get('sales').then(function (j) {
      if (tab !== 'sales') return;
      var ss = j.sales || [];
      main.innerHTML = '<div class="dps-card"><div class="dps-ch"><h2>Sales outside the shop</h2>' + (CAN ? '<button type="button" class="dps-btn pri" id="dps-new-s"><i class="fa fa-plus"></i> Record an outside sale</button>' : '') + '</div>' +
        '<p class="dps-mut" style="margin-top:-4px">Elecrow, Tindie, eBay, a trade show or by hand: the units come off the same stock the shop sells from.</p>' +
        (ss.length ? '<table class="dps-t"><thead><tr><th>Date</th><th>Channel</th><th>Product</th><th class="r">Qty</th><th class="r">Revenue</th><th class="r">Fees + shipping</th><th class="r">Cost</th><th class="r">Profit</th></tr></thead><tbody>' + ss.map(function (s) {
          var rev = num(s.qty) * num(s.price_each), cost = num(s.qty) * num(s.unit_cost), fees = num(s.fees) + num(s.shipping);
          return '<tr><td>' + esc(s.date) + '</td><td>' + esc(s.channel) + (s.customer ? '<small>' + esc(s.customer) + '</small>' : '') + '</td><td>' + esc(s.name) + '</td><td class="r">' + q(s.qty) + '</td><td class="r">' + eur(rev) + '</td><td class="r">' + eur(fees) + '</td><td class="r">' + eur(cost) + '</td><td class="r"><b>' + eur(rev - fees - cost) + '</b></td></tr>';
        }).join('') + '</tbody></table>' : '<p class="dps-mut">No outside sales yet.</p>') + '</div>';
      var nb = document.getElementById('dps-new-s'); if (nb) nb.onclick = saleForm;
    });
  }

  function saleForm() {
    var prods = S.items.filter(function (i) { return i.kind === 'product'; });
    if (!prods.length) { toast('First add the finished product as a stock item.', true); return; }
    var chans = S.channels.concat(CHANNELS.filter(function (c) { return S.channels.indexOf(c) < 0; }));
    var b = '<div class="dps-form">' +
      '<label class="w2">Product<select name="item_id">' + itemOptions(prods[0].item_id, ['product']) + '</select></label>' +
      '<label>Where<input type="text" name="channel" list="dps-chans" placeholder="Elecrow, Tindie …"></label>' +
      '<label>Date<input type="date" name="date" value="' + today() + '"></label>' +
      '<label>Quantity<input type="number" step="1" min="1" name="qty" value="1"></label>' +
      '<label>Price each (what the buyer paid, before fees)<input type="number" step="any" name="price_each" value="0"></label>' +
      '<label>Currency<select name="currency">' + CUR.map(function (c) { return '<option>' + c + '</option>'; }).join('') + '</select></label>' +
      '<label>1 unit in EUR<input type="number" step="any" name="rate" value="1"></label>' +
      '<label>Marketplace and payment fees<input type="number" step="any" name="fees" value="0"></label>' +
      '<label>Shipping you paid<input type="number" step="any" name="shipping" value="0"></label>' +
      '<label class="w2">Customer<input type="text" name="customer"></label>' +
      '<label class="w4">Note<input type="text" name="note" placeholder="order number on the marketplace, …"></label></div>' +
      '<datalist id="dps-chans">' + chans.map(function (c) { return '<option value="' + esc(c) + '">'; }).join('') + '</datalist><div class="dps-sum" id="dps-ssum"></div>';
    var m = modal('Record an outside sale', b, '<button type="button" class="dps-btn pri" id="dps-saves"><i class="fa fa-check"></i> Save the sale</button>');
    function calc() {
      var it = item(val(m, 'item_id')), rate = val(m, 'currency') === 'EUR' ? 1 : num(val(m, 'rate')) || 1, n = num(val(m, 'qty'));
      var rev = n * num(val(m, 'price_each')) * rate, fees = (num(val(m, 'fees')) + num(val(m, 'shipping'))) * rate, cost = it ? n * num(it.avg_cost) : 0;
      m.querySelector('#dps-ssum').innerHTML = '<div><span>In stock</span><b>' + (it ? q(it.have) : '') + '</b></div><div><span>Revenue</span><b>' + eur(rev) + '</b></div><div><span>Fees and shipping</span><b>' + eur(fees) + '</b></div><div><span>Cost of the units</span><b>' + eur(cost) + '</b></div><div class="big"><span>Profit</span><b>' + eur(rev - fees - cost) + '</b></div>';
      m.querySelector('[name=rate]').disabled = val(m, 'currency') === 'EUR';
    }
    m.addEventListener('input', calc); m.addEventListener('change', calc); calc();
    m.querySelector('#dps-saves').onclick = function () {
      var btn = this; busy(btn, true);
      post('save_sale', { item_id: val(m, 'item_id'), channel: val(m, 'channel'), date: val(m, 'date'), qty: val(m, 'qty'), price_each: val(m, 'price_each'), rate: val(m, 'currency') === 'EUR' ? 1 : val(m, 'rate'), fees: val(m, 'fees'), shipping: val(m, 'shipping'), customer: val(m, 'customer'), note: val(m, 'note') })
        .then(function () { m.remove(); toast('Sale saved; stock updated.'); refresh(); }).catch(function (e) { busy(btn, false); toast(e.message, true); });
    };
  }

  // ---------------------------------------------------------------- reports
  function vReports() {
    var y = new Date().getFullYear();
    main.innerHTML = '<div class="dps-card"><div class="dps-row"><label class="dps-mut">From <input type="date" id="dps-from" value="' + y + '-01-01"></label><label class="dps-mut">To <input type="date" id="dps-to" value="' + today() + '"></label><button type="button" class="dps-btn" id="dps-run">Show</button>' +
      '<span style="margin-left:auto" class="dps-row"><span class="dps-mut">CSV for your accountant:</span>' + ['invoices', 'sales', 'batches', 'stock'].map(function (w) { return '<a class="dps-btn sm" data-csv="' + w + '" href="#">' + w.charAt(0).toUpperCase() + w.slice(1) + '</a>'; }).join('') + '</span></div></div><div id="dps-rep"></div>';
    function run() {
      var f = '&from=' + document.getElementById('dps-from').value + '&to=' + document.getElementById('dps-to').value;
      document.getElementById('dps-rep').innerHTML = '<p class="dps-mut">Loading…</p>';
      get('report', f).then(function (r) {
        if (tab !== 'reports') return;
        var h = '<div class="dps-card"><h2>Profit by product</h2>' + (r.products.length ? '<table class="dps-t"><thead><tr><th>Product</th><th class="r">Sold in the shop</th><th class="r">Sold outside</th><th class="r">Revenue</th><th class="r">Fees</th><th class="r">Cost</th><th class="r">Profit</th><th class="r">Profit after your time</th></tr></thead><tbody>' + r.products.map(function (p) {
          return '<tr><td>' + esc(p.name) + '</td><td class="r">' + q(p.shop_q) + '</td><td class="r">' + q(p.out_q) + '</td><td class="r">' + eur(p.rev) + '</td><td class="r">' + eur(p.fees) + '</td><td class="r">' + eur(p.cost) + '</td><td class="r"><b>' + eur(p.profit) + '</b></td><td class="r">' + eur(p.profit_full) + '</td></tr>';
        }).join('') + '</tbody></table><p class="dps-mut" style="font-size:12px;margin:6px 0 0">Shop revenue is without VAT. Shop orders are costed at today\'s average cost; outside sales at the cost when they were sold.</p>' : '<p class="dps-mut">No sales of stock items in this period.</p>') + '</div>';
        h += '<div class="dps-grid2"><div class="dps-card"><h2>By channel</h2>' + (r.channels.length ? '<table class="dps-t"><thead><tr><th>Channel</th><th class="r">Units</th><th class="r">Revenue</th><th class="r">Fees</th><th class="r">Profit</th></tr></thead><tbody>' + r.channels.map(function (c) { return '<tr><td>' + esc(c.name) + '</td><td class="r">' + q(c.q) + '</td><td class="r">' + eur(c.rev) + '</td><td class="r">' + eur(c.fees) + '</td><td class="r"><b>' + eur(c.profit) + '</b></td></tr>'; }).join('') + '</tbody></table>' : '<p class="dps-mut">Nothing sold.</p>') + '</div>';
        h += '<div class="dps-card"><h2>Bought, by supplier</h2>' + (r.spend.length ? '<table class="dps-t"><thead><tr><th>Supplier</th><th class="r">Invoices</th><th class="r">Total</th></tr></thead><tbody>' + r.spend.map(function (s) { return '<tr><td>' + esc(s.name || '—') + '</td><td class="r">' + s.n + '</td><td class="r">' + eur(s.t) + '</td></tr>'; }).join('') + '</tbody></table>' : '<p class="dps-mut">No invoices.</p>') +
          '<div class="dps-sum"><div><span>Batches</span><b>' + q(r.batches.n) + '</b></div><div><span>Units built</span><b>' + q(r.batches.units) + '</b></div><div><span>Your hours</span><b>' + q(r.batches.hours) + '</b></div><div class="big"><span>Stock value now</span><b>' + eur(r.stock_value) + '</b></div></div></div></div>';
        document.getElementById('dps-rep').innerHTML = h;
      });
    }
    document.getElementById('dps-run').onclick = run;
    main.querySelectorAll('[data-csv]').forEach(function (a) {
      a.addEventListener('click', function (e) { e.preventDefault(); location.href = url('csv', '&what=' + a.dataset.csv + '&from=' + document.getElementById('dps-from').value + '&to=' + document.getElementById('dps-to').value); });
    });
    run();
  }

  // ---------------------------------------------------------------- settings
  function vSettings() {
    var s = S.settings;
    main.innerHTML = '<div class="dps-card" style="max-width:720px"><h2>Rates for build batches</h2><div class="dps-form" style="grid-template-columns:repeat(3,minmax(0,1fr))">' +
      '<label>Your time, € per hour<input type="number" step="any" id="dps-rate" value="' + esc(s.rate) + '"></label>' +
      '<label>Filament, € per kg<input type="number" step="any" id="dps-fil" value="' + esc(s.filament) + '"></label>' +
      '<label>Printer, € per print hour<input type="number" step="any" id="dps-mach" value="' + esc(s.machine) + '"></label></div>' +
      '<p class="dps-mut" style="font-size:12.5px">Your time goes only into the <b>full cost</b>; the <b>cash cost</b> is what actually left the business. The printer rate covers electricity and wear (nozzles, beds, belts). The rates apply to new batches; batches already saved keep their costs.</p>' +
      (CAN ? '<button type="button" class="dps-btn pri" id="dps-saveset">Save</button>' : '') + '</div>';
    var b = document.getElementById('dps-saveset');
    if (b) b.onclick = function () { post('settings', { rate: document.getElementById('dps-rate').value, filament: document.getElementById('dps-fil').value, machine: document.getElementById('dps-mach').value }).then(function () { toast('Rates saved.'); load(); }).catch(function (e) { toast(e.message, true); }); };
  }

  // ---------------------------------------------------------------- start
  var start = (location.search.match(/[?&]tab=(\w+)/) || [])[1];
  load().then(function () { show(start && document.querySelector('[data-tab="' + start + '"]') ? start : 'overview'); }).catch(function (e) { main.innerHTML = '<div class="dps-err">' + esc(e.message) + '</div>'; });
})();

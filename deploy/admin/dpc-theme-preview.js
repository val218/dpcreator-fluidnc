/* dpCREATOR light theme generator.
   Reads the shop's stylesheets and writes overrides under html.dpc-light: dark backgrounds become
   light, light text becomes dark, brand accents stay. Runs in the admin; the result is saved as one file. */
(function (global) {
	function clamp(x, a, b) { return Math.max(a, Math.min(b, x)); }
	var NAMED = {white: [255, 255, 255], black: [0, 0, 0]};

	function parse(c) {
		c = c.trim().toLowerCase();
		var m;
		if ((m = c.match(/^#([0-9a-f]{3,8})$/))) {
			var h = m[1];
			if (h.length === 3 || h.length === 4) h = h.split('').map(function (x) { return x + x; }).join('');
			return {r: parseInt(h.slice(0, 2), 16), g: parseInt(h.slice(2, 4), 16), b: parseInt(h.slice(4, 6), 16), a: h.length === 8 ? parseInt(h.slice(6, 8), 16) / 255 : 1};
		}
		if ((m = c.match(/^rgba?\(\s*([\d.]+)[\s,]+([\d.]+)[\s,]+([\d.]+)(?:[\s,/]+([\d.]+%?))?\s*\)$/))) {
			var a = m[4] === undefined ? 1 : (m[4].slice(-1) === '%' ? parseFloat(m[4]) / 100 : parseFloat(m[4]));
			return {r: +m[1], g: +m[2], b: +m[3], a: a};
		}
		if (NAMED[c]) return {r: NAMED[c][0], g: NAMED[c][1], b: NAMED[c][2], a: 1};
		return null;
	}

	function toHsl(c) {
		var r = c.r / 255, g = c.g / 255, b = c.b / 255, mx = Math.max(r, g, b), mn = Math.min(r, g, b), l = (mx + mn) / 2, h = 0, s = 0, d = mx - mn;
		if (d) {
			s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
			h = mx === r ? (g - b) / d + (g < b ? 6 : 0) : mx === g ? (b - r) / d + 2 : (r - g) / d + 4;
			h /= 6;
		}
		return {h: h, s: s, l: l, a: c.a};
	}

	function fromHsl(o) {
		var h = o.h, s = o.s, l = o.l, r, g, b;
		if (!s) { r = g = b = l; } else {
			var f = function (p, q, t) { if (t < 0) t += 1; if (t > 1) t -= 1; if (t < 1 / 6) return p + (q - p) * 6 * t; if (t < 1 / 2) return q; if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6; return p; };
			var q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
			r = f(p, q, h + 1 / 3); g = f(p, q, h); b = f(p, q, h - 1 / 3);
		}
		var R = Math.round(r * 255), G = Math.round(g * 255), B = Math.round(b * 255);
		if (o.a < 1) return 'rgba(' + R + ',' + G + ',' + B + ',' + (Math.round(o.a * 1000) / 1000) + ')';
		return '#' + [R, G, B].map(function (x) { return ('0' + x.toString(16)).slice(-2); }).join('');
	}

	// role: bg | text | border | shadow | var
	function mapColor(str, role) {
		var c = parse(str);
		if (!c) return null;
		var o = toHsl(c);
		var white = c.r > 200 && c.g > 200 && c.b > 200, black = c.r < 40 && c.g < 40 && c.b < 40;
		// translucent overlays: white haze on dark becomes dark haze on light
		if (c.a < 0.6) {
			if (white || (o.l > 0.7 && o.s < 0.5)) return fromHsl({h: 0.58, s: 0.25, l: 0.15, a: clamp(c.a * 1.25, 0.04, 0.5)});
			if (black && role === 'shadow') return fromHsl({h: 0.58, s: 0.3, l: 0.2, a: clamp(c.a * 0.35, 0.03, 0.25)});
			if (black && role === 'bg') return fromHsl({h: 0.58, s: 0.2, l: 0.97, a: c.a});
			if (o.l < 0.35 && role === 'bg') return fromHsl({h: o.h, s: o.s * 0.5, l: 0.96, a: c.a});
			return null;
		}
		if (role === 'shadow') {
			if (o.l < 0.2) return fromHsl({h: 0.58, s: 0.3, l: 0.25, a: 0.18});
			return null;
		}
		var neutral = o.s < 0.32 || o.l < 0.2;
		if (role === 'bg' || role === 'var-bg') {
			if (o.l < 0.36) {
				var nl = 0.985 - (o.l / 0.36) * 0.11;
				return fromHsl({h: o.h, s: neutral ? Math.min(o.s, 0.25) * 0.8 : o.s * 0.55, l: nl, a: c.a});
			}
			return null;
		}
		if (role === 'text') {
			if (o.l > 0.55) {
				if (neutral || o.s < 0.4) return fromHsl({h: o.h, s: Math.min(o.s, 0.3), l: clamp(0.12 + (1 - o.l) * 0.75, 0.1, 0.46), a: c.a});
				return fromHsl({h: o.h, s: Math.min(1, o.s * 1.05), l: Math.min(o.l, 0.38), a: c.a});
			}
			return null;
		}
		if (role === 'border') {
			if (o.l < 0.42 && (neutral || o.s < 0.45)) return fromHsl({h: o.h, s: Math.min(o.s, 0.25), l: clamp(0.88 - o.l * 0.25, 0.78, 0.9), a: c.a});
			return null;
		}
		return null;
	}

	var COLOR_RE = /#[0-9a-fA-F]{3,8}\b|rgba?\([^)]*\)|\b(?:white|black)\b/g;
	function mapValue(v, role) {
		var changed = false;
		var out = v.replace(COLOR_RE, function (m) {
			var r = mapColor(m, role);
			if (r && r !== m) { changed = true; return r; }
			return m;
		});
		return changed ? out : null;
	}

	function roleOf(prop, value) {
		if (/^--/.test(prop)) {
			// guess from the value: dark values are backgrounds, light neutral values are text
			var c = parse((value.match(COLOR_RE) || [''])[0] || '');
			if (!c) return null;
			var o = toHsl(c);
			if (c.a < 0.6) return 'bg';
			if (o.l < 0.36) return 'bg';
			if (o.l > 0.55 && (o.s < 0.4)) return 'text';
			return null; // accents stay
		}
		if (/^background/.test(prop)) return 'bg';
		if (prop === 'color' || prop === '-webkit-text-fill-color' || prop === 'caret-color') return 'text';
		if (/^border|^outline|column-rule-color/.test(prop)) return 'border';
		if (/shadow$/.test(prop)) return 'shadow';
		if (prop === 'fill' || prop === 'stop-color') return 'bg';
		if (prop === 'stroke') return 'border';
		return null;
	}

	// the site header (top bar, logo, menu) stays dark in light mode
	var KEEP = ':not(header):not(header *)';
	function keep(s) {
		var m = s.match(/(::?(?:before|after|first-line|first-letter|placeholder|selection|marker|backdrop|-webkit-[\w-]+|-moz-[\w-]+)(?:\([^)]*\))?)$/i);
		return m ? s.slice(0, s.length - m[1].length) + KEEP + m[1] : s + KEEP;
	}
	function scope(sel) {
		return sel.split(',').map(function (s) {
			s = s.trim();
			if (!s) return s;
			if (/^:root\b/.test(s)) return s.replace(/^:root/, 'html.dpc-light');
			if (/^html\b/.test(s)) return /^html\S*$/.test(s) ? s.replace(/^html/, 'html.dpc-light') : keep(s.replace(/^html/, 'html.dpc-light'));
			return 'html.dpc-light ' + keep(s);
		}).join(',');
	}

	function walk(rules, out, skipSel) {
		for (var i = 0; i < rules.length; i++) {
			var r = rules[i];
			if (r.type === 1) { // style rule
				if (skipSel && skipSel.test(r.selectorText)) continue;
				var decl = [];
				for (var j = 0; j < r.style.length; j++) {
					var p = r.style[j], v = r.style.getPropertyValue(p), imp = r.style.getPropertyPriority(p);
					var role = roleOf(p, v);
					if (!role) continue;
					var nv = mapValue(v, role);
					if (nv) decl.push(p + ':' + nv + (imp ? ' !important' : ''));
				}
				if (decl.length) out.push(scope(r.selectorText) + '{' + decl.join(';') + '}');
			} else if (r.cssRules && (r.type === 4 || r.type === 12 || (r.constructor && /Container|Supports|Layer/.test(r.constructor.name)))) {
				var inner = [];
				walk(r.cssRules, inner, skipSel);
				if (inner.length) {
					var head = r.type === 4 ? '@media ' + r.conditionText : (r.cssText.slice(0, r.cssText.indexOf('{')).trim());
					out.push(head + '{' + inner.join('\n') + '}');
				}
			}
		}
	}

	global.dpcBuildLight = function (cssTexts, skipSel) {
		var out = [];
		cssTexts.forEach(function (t) {
			try {
				var sh = new CSSStyleSheet();
				sh.replaceSync(t.replace(/@import[^;]+;/g, ''));
				walk(sh.cssRules, out, skipSel);
			} catch (e) {}
		});
		// de-duplicate identical lines
		var seen = {}, res = [];
		out.forEach(function (l) { if (!seen[l]) { seen[l] = 1; res.push(l); } });
		return res;
	};
	global.dpcMapColor = mapColor;
})(window);
/* dpCREATOR theme switch: a Light / Dark button in the top bar (and a round button on phones). */
(function () {
	var KEY = 'dpc-theme', H = document.documentElement;
	function get() { try { return localStorage.getItem(KEY) || 'dark'; } catch (e) { return 'dark'; } }
	function put(v) { try { localStorage.setItem(KEY, v); } catch (e) {} }
	function css(cb) {
		var l = document.getElementById('dpc-light-css');
		if (l) { cb(); return; }
		l = document.createElement('link');
		l.rel = 'stylesheet'; l.id = 'dpc-light-css'; l.href = '/index.php?route=extension/module/dpc_theme/css';
		l.onload = cb; l.onerror = cb;
		document.head.appendChild(l);
	}
	// inline style="" colours are not in any stylesheet: adjust them in light mode
	var touched = [];
	function fixInline(on) {
		if (!on) {
			touched.forEach(function (t) { t.el.setAttribute('style', t.orig); });
			touched = [];
			return;
		}
		document.querySelectorAll('[style]').forEach(function (el) {
			if (el.closest('#dpc-theme-btn,#dpc-theme-fab,header')) return;
			var s = el.style, orig = el.getAttribute('style'), changed = false;
			[['background', 'bg'], ['backgroundColor', 'bg'], ['backgroundImage', 'bg'], ['color', 'text'], ['borderColor', 'border'], ['borderTopColor', 'border'], ['borderBottomColor', 'border'], ['boxShadow', 'shadow']].forEach(function (p) {
				var v = s[p[0]];
				if (!v) return;
				var nv = v.replace(/#[0-9a-fA-F]{3,8}\b|rgba?\([^)]*\)/g, function (m) { return window.dpcMapColor(m, p[1]) || m; });
				if (nv !== v) { s[p[0]] = nv; changed = true; }
			});
			if (changed) touched.push({el: el, orig: orig});
		});
	}
	function label(b) {
		var light = H.classList.contains('dpc-light');
		b.setAttribute('aria-pressed', light ? 'true' : 'false');
		b.title = light ? 'Switch to the dark theme' : 'Switch to the light theme';
		b.querySelector('.ic').textContent = light ? '☾' : '☀';
		var t = b.querySelector('.tx');
		if (t) t.textContent = light ? 'Dark' : 'Light';
	}
	function set(v) {
		put(v);
		if (v === 'light') {
			css(function () { H.classList.add('dpc-light'); fixInline(true); all(); });
		} else {
			H.classList.remove('dpc-light'); fixInline(false); all();
		}
	}
	function all() { document.querySelectorAll('#dpc-theme-btn,#dpc-theme-fab').forEach(label); }
	function make(id, cls, withText) {
		var b = document.createElement('button');
		b.type = 'button'; b.id = id; b.className = cls;
		var i = document.createElement('span'); i.className = 'ic'; i.setAttribute('aria-hidden', 'true'); b.appendChild(i);
		if (withText) { var t = document.createElement('span'); t.className = 'tx'; b.appendChild(t); }
		b.addEventListener('click', function () { set(H.classList.contains('dpc-light') ? 'dark' : 'light'); });
		return b;
	}
	function init() {
		var st = document.createElement('style');
		st.textContent = '#dpc-theme-btn{background:none;border:0;color:#B6BBC6;font:inherit;font-size:12px;font-weight:700;letter-spacing:.02em;text-transform:uppercase;cursor:pointer;padding:0 10px;display:inline-flex;align-items:center;gap:5px;height:100%;opacity:.9}#dpc-theme-btn:hover{opacity:1;color:#fff}#dpc-theme-btn .ic{font-size:14px;line-height:1}#dpc-theme-btn:focus-visible,#dpc-theme-fab:focus-visible{outline:2px solid #2b8fe0;outline-offset:2px}'
			+ '#dpc-theme-fab{position:fixed;left:14px;bottom:14px;z-index:9990;width:42px;height:42px;border-radius:50%;border:1px solid rgba(127,140,155,.45);background:#141920;color:#fff;font-size:19px;line-height:1;cursor:pointer;box-shadow:0 4px 14px rgba(0,0,0,.25);display:none}'
			+ 'html.dpc-light #dpc-theme-fab{background:#fff;color:#1e2a35}@media (max-width:991px){#dpc-theme-fab{display:block;bottom:78px}}';
		document.head.appendChild(st);
		var group = [].slice.call(document.querySelectorAll('.top-bar .language-currency')).filter(function (c) { return c.offsetParent !== null; })[0];
		var cands = [].slice.call(document.querySelectorAll('.top-bar .desktop-currency-wrapper, .top-bar [class*="currency"], .desktop-currency-wrapper, #form-currency'));
		var cur = cands.filter(function (c) { return c.offsetParent !== null && !c.closest('.mobile-container, .mobile-header'); })[0];
		var host = cur ? (cur.closest('li') || cur) : null;
		if (group) {
			// inside the centred currency group, after the currency, so the two never overlap
			var gi = document.createElement('div');
			gi.className = 'dpc-theme-item';
			gi.style.display = 'flex'; gi.style.alignItems = 'center'; gi.style.marginLeft = '10px';
			gi.appendChild(make('dpc-theme-btn', '', true));
			group.appendChild(gi);
		} else if (host && host.parentElement) {
			var li = document.createElement(host.tagName === 'LI' ? 'li' : 'div');
			li.className = 'dpc-theme-item';
			li.style.display = 'flex'; li.style.alignItems = 'center';
			li.appendChild(make('dpc-theme-btn', '', true));
			host.parentElement.insertBefore(li, host);
		}
		document.body.appendChild(make('dpc-theme-fab', '', false));
		if (H.classList.contains('dpc-light')) fixInline(true);
		all();
	}
	window.dpcThemeToggle = function () { set(H.classList.contains('dpc-light') ? 'dark' : 'light'); };
	if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init); else init();
})();
/* dpCREATOR reader mode: docs and info pages in a clean, distraction-free reading view. */
(function () {
	var H = document.documentElement, SIZES = [16, 17, 18, 19, 21, 23, 25], SK = 'dpc-reader-size', OK = 'dpc-reader-on';
	function lsGet(k, d) { try { var v = localStorage.getItem(k); return v === null ? d : v; } catch (e) { return d; } }
	function lsSet(k, v) { try { localStorage.setItem(k, v); } catch (e) {} }
	function ssGet(k) { try { return sessionStorage.getItem(k); } catch (e) { return null; } }
	function ssSet(k, v) { try { if (v === null) sessionStorage.removeItem(k); else sessionStorage.setItem(k, v); } catch (e) {} }
	function source() {
		var c = null;
		if (H.classList.contains('route-journal3-blog-post')) c = document.querySelector('.post-details .post-content');
		else if (H.classList.contains('route-information-information')) c = document.querySelector('#content .content') || document.querySelector('#content');
		if (!c || c.textContent.trim().length < 600) return null;
		if (c.querySelector('form, input, select, esp-web-install-button, [data-manifest], canvas')) return null; // interactive pages stay as they are
		return c;
	}
	var src, box, prevScroll = 0, idx;
	function css() {
		var s = document.createElement('style');
		s.textContent = '.dpc-reader-btn{display:inline-flex;align-items:center;gap:7px;margin:4px 0 18px;padding:7px 14px;border-radius:20px;border:1px solid rgba(127,140,155,.45);background:transparent;color:inherit;font:inherit;font-size:14px;font-weight:600;cursor:pointer}'
			+ '.dpc-reader-btn:hover{border-color:#2b8fe0;color:#4fb4ee}.dpc-reader-btn:focus-visible{outline:2px solid #2b8fe0;outline-offset:2px}html.dpc-light .dpc-reader-btn:hover{color:#1f6fb8}'
			+ 'html.dpc-reading,html.dpc-reading body{overflow:hidden!important}'
			+ '#dpc-reader{position:fixed;inset:0;z-index:100000;overflow-y:auto;-webkit-overflow-scrolling:touch;background:#14181c;color:#d6dce1;--dpr-size:19px}'
			+ 'html.dpc-light #dpc-reader{background:#faf8f4;color:#262b30}'
			+ '#dpc-reader .dpr-bar{position:sticky;top:0;display:flex;align-items:center;gap:8px;padding:10px max(16px,calc(50% - 22em));background:inherit;border-bottom:1px solid rgba(127,140,155,.25);font:14px/1.2 Barlow,system-ui,sans-serif;z-index:1}'
			+ '#dpc-reader .dpr-bar button{background:none;border:1px solid rgba(127,140,155,.4);border-radius:8px;color:inherit;font:inherit;font-weight:600;padding:7px 11px;cursor:pointer;min-width:38px}'
			+ '#dpc-reader .dpr-bar button:hover{border-color:#2b8fe0}#dpc-reader button:focus-visible{outline:2px solid #2b8fe0;outline-offset:2px}'
			+ '#dpc-reader .dpr-time{opacity:.7;margin-right:auto;padding-left:6px}'
			+ '#dpc-reader .dpr-prog{position:absolute;left:0;bottom:-1px;height:2px;background:#2b8fe0;width:0}'
			+ '#dpc-reader article{max-width:34em;margin:0 auto;padding:40px 20px 120px;font-family:Charter,"Bitstream Charter","Sitka Text",Cambria,Georgia,serif;font-size:var(--dpr-size);line-height:1.7}'
			+ '#dpc-reader article h1{font:600 2.1em/1.15 "Barlow Condensed",Barlow,sans-serif;margin:0 0 .3em;color:#fff;text-transform:none;letter-spacing:0}'
			+ 'html.dpc-light #dpc-reader article h1,html.dpc-light #dpc-reader article h2,html.dpc-light #dpc-reader article h3{color:#14181c}'
			+ '#dpc-reader .dpr-src{font:14px Barlow,system-ui,sans-serif;opacity:.65;margin-bottom:2em}'
			+ '#dpc-reader article h2,#dpc-reader article h3,#dpc-reader article h4{font-family:"Barlow Condensed",Barlow,sans-serif;font-weight:600;color:#fff;line-height:1.25;margin:1.6em 0 .5em;text-transform:none;letter-spacing:0}'
			+ '#dpc-reader article h2{font-size:1.5em}#dpc-reader article h3{font-size:1.25em}#dpc-reader article h4{font-size:1.1em}'
			+ '#dpc-reader article p,#dpc-reader article li{font-size:1em;line-height:1.7;color:inherit;margin:0 0 1em}'
			+ '#dpc-reader article ul,#dpc-reader article ol{padding-left:1.4em;margin:0 0 1.2em}#dpc-reader article li{margin-bottom:.4em}'
			+ '#dpc-reader article a{color:#4fb4ee;text-decoration:underline;text-underline-offset:2px}html.dpc-light #dpc-reader article a{color:#1f6fb8}'
			+ '#dpc-reader article img,#dpc-reader article video{max-width:100%!important;height:auto!important;border-radius:8px;display:block;margin:1.2em auto}'
			+ '#dpc-reader article figure{margin:1.4em 0}#dpc-reader article figcaption{font:14px Barlow,sans-serif;opacity:.7;text-align:center}'
			+ '#dpc-reader article table{width:100%;border-collapse:collapse;font:15px/1.4 Barlow,sans-serif;margin:1.2em 0;display:block;overflow-x:auto}'
			+ '#dpc-reader article td,#dpc-reader article th{border:1px solid rgba(127,140,155,.3);padding:7px 10px;text-align:left}'
			+ '#dpc-reader article code,#dpc-reader article pre{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:.82em;background:rgba(127,140,155,.15);border-radius:5px;padding:.1em .35em}'
			+ '#dpc-reader article pre{padding:12px 14px;overflow-x:auto;white-space:pre}'
			+ '#dpc-reader article blockquote{border-left:3px solid #2b8fe0;margin:1.2em 0;padding:.2em 0 .2em 1em;opacity:.9}'
			+ '#dpc-reader article [style*="background"]{background:none!important}#dpc-reader article [style*="color"]{color:inherit!important}'
			+ '#dpc-reader article [class*="col-"],#dpc-reader article .row{width:auto!important;float:none!important;max-width:none!important;margin:0!important;padding:0!important}'
			+ '#dpc-reader .dpr-end{margin-top:3em;padding-top:1.2em;border-top:1px solid rgba(127,140,155,.25);font:15px Barlow,sans-serif}'
			+ '@media (max-width:600px){#dpc-reader article{padding:24px 18px 100px}#dpc-reader .dpr-bar{padding:8px 12px;gap:6px}#dpc-reader .dpr-bar button{padding:7px 9px}}'
			+ '@media print{#dpc-reader .dpr-bar{display:none}#dpc-reader{position:static;overflow:visible}}';
		document.head.appendChild(s);
	}
	function size(d) {
		idx = Math.max(0, Math.min(SIZES.length - 1, idx + d));
		box.style.setProperty('--dpr-size', SIZES[idx] + 'px');
		lsSet(SK, String(SIZES[idx]));
	}
	function btn(label, title, fn) {
		var b = document.createElement('button'); b.type = 'button'; b.textContent = label; b.title = title; b.setAttribute('aria-label', title);
		b.addEventListener('click', fn); return b;
	}
	function open() {
		if (box) return;
		prevScroll = window.pageYOffset;
		box = document.createElement('div'); box.id = 'dpc-reader'; box.setAttribute('role', 'dialog'); box.setAttribute('aria-modal', 'true'); box.setAttribute('aria-label', 'Reader mode');
		var bar = document.createElement('div'); bar.className = 'dpr-bar';
		var exit = btn('← Exit reader', 'Exit reader mode (Esc)', close);
		var words = src.textContent.trim().split(/\s+/).length, mins = Math.max(1, Math.round(words / 220));
		var time = document.createElement('span'); time.className = 'dpr-time'; time.textContent = mins + ' min read';
		var prog = document.createElement('span'); prog.className = 'dpr-prog';
		bar.appendChild(exit); bar.appendChild(time);
		bar.appendChild(btn('A−', 'Smaller text', function () { size(-1); }));
		bar.appendChild(btn('A+', 'Larger text', function () { size(1); }));
		if (window.dpcThemeToggle) bar.appendChild(btn('☀ / ☾', 'Switch light or dark', function () { window.dpcThemeToggle(); }));
		bar.appendChild(prog);
		var art = document.createElement('article');
		var h = document.createElement('h1'); var t = document.querySelector('h1.page-title, h1.title'); h.textContent = t ? t.textContent.trim() : document.title;
		var s = document.createElement('div'); s.className = 'dpr-src'; s.textContent = 'dpCREATOR' + (H.classList.contains('route-journal3-blog-post') ? ' Docs' : '');
		var body = src.cloneNode(true);
		body.removeAttribute('id'); body.className = 'dpr-content';
		[].forEach.call(body.querySelectorAll('script, style, .dpc-reader-btn, iframe:not([src*="youtube"]):not([src*="vimeo"])'), function (n) { n.remove(); });
		[].forEach.call(body.querySelectorAll('img'), function (im) {
			var ds = im.getAttribute('data-src') || im.getAttribute('data-lazy-src'); if (ds) im.src = ds;
			var dss = im.getAttribute('data-srcset'); if (dss) im.srcset = dss;
			im.removeAttribute('width'); im.removeAttribute('height'); im.loading = 'lazy';
		});
		[].forEach.call(body.querySelectorAll('[id]'), function (n) { n.removeAttribute('id'); });
		var end = document.createElement('div'); end.className = 'dpr-end';
		end.appendChild(btn('← Back to the page', 'Exit reader mode', close));
		art.appendChild(h); art.appendChild(s); art.appendChild(body); art.appendChild(end);
		box.appendChild(bar); box.appendChild(art);
		idx = SIZES.indexOf(parseInt(lsGet(SK, window.innerWidth < 600 ? '18' : '19'), 10)); if (idx < 0) idx = 3;
		box.style.setProperty('--dpr-size', SIZES[idx] + 'px');
		box.addEventListener('scroll', function () { var m = box.scrollHeight - box.clientHeight; prog.style.width = (m > 0 ? box.scrollTop / m * 100 : 0) + '%'; });
		document.body.appendChild(box);
		H.classList.add('dpc-reading');
		ssSet(OK, '1');
		exit.focus();
	}
	function close() {
		if (!box) return;
		box.remove(); box = null;
		H.classList.remove('dpc-reading');
		ssSet(OK, null);
		window.scrollTo(0, prevScroll);
		var b = document.querySelector('.dpc-reader-btn'); if (b) b.focus();
	}
	function init() {
		src = source();
		if (!src) return;
		css();
		var b = document.createElement('button'); b.type = 'button'; b.className = 'dpc-reader-btn';
		b.innerHTML = '<span aria-hidden="true">📖</span> Reader mode';
		b.addEventListener('click', open);
		var t = document.querySelector('h1.page-title, h1.title');
		if (t && t.parentNode) t.parentNode.insertBefore(b, t.nextSibling); else src.parentNode.insertBefore(b, src);
		document.addEventListener('keydown', function (e) { if (e.key === 'Escape' && box) close(); });
		if (ssGet(OK) === '1' || /[?&]reader=1\b/.test(location.search)) open();
	}
	if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init); else init();
})();

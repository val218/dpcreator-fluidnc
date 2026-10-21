// dpCREATOR mega menus: content for 4 Journal "blocks" modules + their CSS. Run in the admin tab.
window.DPMM = (function () {
	var I = '/image/cache/catalog/dpc-site/';
	function esc(s) { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;'); }
	function card(c) {
		return '<a class="dpmm-card" href="' + c.u + '"><span class="dpmm-img"><img src="' + I + c.i + '" alt="" loading="lazy" width="250" height="188"></span>'
			+ '<span class="dpmm-t">' + esc(c.t) + (c.tag ? ' <span class="dpmm-tag">' + esc(c.tag) + '</span>' : '') + '</span>'
			+ '<span class="dpmm-d">' + esc(c.d) + '</span></a>';
	}
	function panel(p) {
		return '<div class="dpmm dpmm-n' + p.cards.length + '"><div class="dpmm-cards">' + p.cards.map(card).join('') + '</div>'
			+ '<div class="dpmm-side"><div class="dpmm-h">' + esc(p.sideTitle) + '</div>'
			+ p.links.map(function (l) { return '<a href="' + l[1] + '">' + esc(l[0]) + '</a>'; }).join('')
			+ '<a class="dpmm-all" href="' + p.all[1] + '">' + esc(p.all[0]) + ' <span aria-hidden="true">›</span></a></div></div>';
	}
	var menus = {
		Controllers: {width: 700, sideTitle: 'Set up your machine', cards: [
			{t: 'dpCREATOR R3 controller', d: '4-axis FluidNC, wired Ethernet, RS485 for a VFD spindle', u: '/dpcreator-r3-fluidnc-controller', i: 'photo-r2-controller-top-250x250w.jpg'},
			{t: 'R3 + SmartTabUI combo', d: 'The controller and the pendant together, ready to run', u: '/dpcreator-r3-tabui-pendant-combo', i: 'photo-r2-tabui-combo-250x250w.jpg'}
		], links: [['Quick start: first power-up', '/docs/quick-start-first-power-up'], ['Pinout and features', '/docs/controller-pinout-features'], ['Machine types and retrofits', '/docs/machine-types-retrofit'], ['Spindle VFD over RS485', '/docs/spindle-vfd-rs485'], ['Bundles', '/bundles']], all: ['All controllers', '/controllers']},
		Pendants: {width: 1000, sideTitle: 'Pendant guides', cards: [
			{t: 'SmartTabUI, assembled', d: 'Built, flashed and tested. Plug it in and jog', u: '/tabui-pendant-assembled', i: 'photo-tabui-pendant-250x250.jpg'},
			{t: 'DIY pack', d: 'Firmware licence and case files to build your own', u: '/smarttabui-diy-pendant-kit', i: 'tabui-pendant-controls-550x550w.jpg'},
			{t: 'Pendant firmware', d: 'Licence for your own SmartTabUI build', u: '/smarttabui-pendant-firmware', i: 'shots/product-tabui-firmware-250x250.jpg'},
			{t: 'Enclosure 3D print files', d: 'STL files for the case', u: '/smarttabui-pendant-enclosure-3d-print-files', i: 'tabui-render-angle-250x250w.jpg'}
		], links: [['SmartTabUI pendant guide', '/docs/smarttabui-pendant-guide'], ['Build the pendant', '/docs/build-tabui-pendant'], ['Measure and record', '/docs/pendant-measure-record'], ['Install, update, register', '/docs/install-update-register-smarttabui']], all: ['All pendants', '/pendants']},
		Firmware: {width: 1000, sideTitle: 'Firmware and software', cards: [
			{t: 'Flash in your browser', d: 'Chrome or Edge and a USB cable. Nothing to install', u: '/firmware', i: 'shots/product-fluidnc-403-250x250.jpg'},
			{t: 'FluidNC for R2/R3', tag: 'Free', d: 'dpCREATOR firmware with Ethernet support', u: '/fluidnc-firmware-r2-r3', i: 'photo-r2-controller-top-250x250w.jpg'},
			{t: 'dpCREATOR WebUI', d: 'Control panel in any browser on your network', u: '/dpcreator-webui', i: 'shots/product-webui-250x250.jpg'},
			{t: 'Studio Pro', d: 'Laser, 3D relief, V-carve and surfacing', u: '/studio-pro', i: 'shots/studio-preview-250x250w.jpg'}
		], links: [['Studio Basic (free CAD/CAM)', '/dpcreator-studio-cad-cam'], ['Live WebUI demo', '/demo/'], ['Flashable firmware', '/firmware-products/flashable-firmware'], ['Firmware ZIP', '/firmware-products/firmware-zip'], ['Release notes', '/docs/release-notes']], all: ['All firmware', '/firmware-products']},
		Docs: {width: 1000, sideTitle: 'Popular topics', cards: [
			{t: 'Quick start', d: 'First power-up, step by step', u: '/docs/quick-start-first-power-up', i: 'shots/doc-quickstart-335x200h.jpg'},
			{t: 'System overview', d: 'How the controller, pendant and software fit together', u: '/docs/dpcreator-system-overview', i: 'shots/doc-overview-335x200h.jpg'},
			{t: 'SmartTabUI pendant guide', d: 'Connect, jog and run jobs from the pendant', u: '/docs/smarttabui-pendant-guide', i: 'shots/doc-pendant-connect-335x200h.jpg'},
			{t: 'WebUI guide', d: 'Jobs, overrides, recovery and the file manager', u: '/docs/dpcreator-webui-guide', i: 'shots/webui-live-job-335x200w.jpg'}
		], links: [['Flashing firmware from the browser', '/docs/flashing-firmware-browser'], ['Ethernet setup', '/docs/ethernet-w5500-setup'], ['Files and guides', '/files-and-guides'], ['3D print files', '/files-and-guides/pendant-3d-print-files'], ['FAQ', '/faq']], all: ['All documentation', '/docs']}
	};
	var css = '/* dpc mega menus */\n'
		+ '.dpmm{display:grid;grid-template-columns:minmax(0,1fr) 220px;gap:26px;padding:22px 24px 24px;background:#0F1318;border-top:2px solid #2B8FE0;border-radius:0 0 12px 12px;box-shadow:0 18px 40px rgba(0,0,0,.45);color:#C9D2DA;text-align:left}\n'
		+ '.dpmm-cards{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px}\n'
		+ '.dpmm-n2 .dpmm-cards{grid-template-columns:repeat(2,minmax(0,1fr))}\n'
		+ '.dpmm-card{display:flex;flex-direction:column;gap:5px;padding:8px;margin:-8px 0;border-radius:10px;text-decoration:none!important;transition:background-color .15s}\n'
		+ '.dpmm-card:hover,.dpmm-card:focus-visible{background:#18212B}\n'
		+ '.dpmm-img{display:block;aspect-ratio:4/3;border-radius:8px;overflow:hidden;background:#1A2129;margin-bottom:5px}\n'
		+ '.dpmm-img img{width:100%;height:100%;object-fit:cover;display:block;transition:transform .3s}\n'
		+ '.dpmm-card:hover .dpmm-img img{transform:scale(1.04)}\n'
		+ '.dpmm-t{color:#fff;font-weight:700;font-size:14px;line-height:1.3;white-space:normal;text-transform:none;letter-spacing:0}\n'
		+ '.dpmm-tag{display:inline-block;font-size:11px;font-weight:700;color:#5FD38A;border:1px solid rgba(95,211,138,.4);border-radius:20px;padding:0 6px;margin-left:4px;vertical-align:1px}\n'
		+ '.dpmm-d{color:#94A1AE;font-size:12.5px;line-height:1.4;white-space:normal;text-transform:none;font-weight:400;letter-spacing:0}\n'
		+ '.dpmm-side{border-left:1px solid #222B35;padding-left:22px;display:flex;flex-direction:column}\n'
		+ '.dpmm-h{color:#7D8A97;font-size:12.5px;font-weight:700;margin:0 0 8px;text-transform:none;letter-spacing:0}\n'
		+ '.dpmm-side a{color:#D6DDE3!important;font-size:14px;line-height:1.3;padding:6px 0;text-decoration:none!important;text-transform:none;font-weight:400;letter-spacing:0;white-space:normal}\n'
		+ '.dpmm-side a:hover,.dpmm-side a:focus-visible{color:#4FB4EE!important}\n'
		+ '.dpmm-side a.dpmm-all{margin-top:auto;padding-top:12px;color:#4FB4EE!important;font-weight:700}\n'
		+ '@media (max-width:1200px){.dpmm{grid-template-columns:minmax(0,1fr) 190px;gap:18px;padding:18px}.dpmm-cards{gap:8px}}\n'
		+ '/* end dpc mega menus */\n';
	return {menus: menus, panel: panel, css: css};
})();

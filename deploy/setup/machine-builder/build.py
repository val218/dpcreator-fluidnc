import json
d='/tmp/claude-0/scr/'
p=open(d+'viz3/page.html').read()
items=open(d+'setup/config_items.json').read().replace('</','<\\/')
core=open(d+'setup/dpc-setup-core.js').read()
bld=open(d+'viz3/builder.js').read()
p=p.replace('__ITEMS__',items).replace('__CORE__',core).replace('__BUILDER__',bld)
open(d+'viz3/out.html','w').write(p)
open(d+'viz3/wrap.html','w').write('<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"></head><body>'+p+'</body></html>')

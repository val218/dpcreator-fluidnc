# Builds the dpcreator.eu version of the machine builder from page.html + builder.js:
#   dpc-builder.css (scoped to .dpmb, dark by default, light under html.dpc-light)
#   dpc-builder.js  (ids prefixed mb-, items fetched from config_items.json)
#   markup.html     (the information page body)
import re
d='/tmp/claude-0/scr/'
p=open(d+'viz3/page.html').read()
bld=open(d+'viz3/builder.js').read()
css=p[p.index('<style>')+7:p.index('</style>')]
markup=p[p.index('<div class="app">'):p.index('<script type="application/json"')]
js=p[p.index('<script>\n(function')+8:p.rindex('</script>')]

ids=sorted(set(re.findall(r'id="([a-z0-9-]+)"', markup)))
# ---- css
# tokens: light block = first :root{...}; dark = :root[data-theme="dark"]{...}
light=re.search(r':root\{(.*?)\n\}', css, re.S).group(1)
dark=re.search(r':root\[data-theme="dark"\]\{(.*?)\}', css, re.S).group(1)
body=css[css.index('html,body{height:100%}'):]
body=body.replace('html,body{height:100%}\n','')
body=body.replace('body{background:var(--bg);color:var(--fg);font:14px/1.45 var(--f-body);margin:0}','ROOT{background:var(--bg);color:var(--fg);font:14px/1.45 var(--f-body);border-radius:12px;color-scheme:dark}')
body=body.replace('.app{display:grid;grid-template-columns:minmax(320px,440px) minmax(0,1fr);grid-template-rows:auto minmax(0,1fr);gap:12px;height:100%;box-sizing:border-box;padding-block:12px;padding-left:max(16px,env(safe-area-inset-left,0px));padding-right:max(16px,env(safe-area-inset-right,0px))}',
 '.app{display:grid;grid-template-columns:minmax(320px,440px) minmax(0,1fr);grid-template-rows:auto minmax(0,1fr);gap:12px;height:min(940px,calc(100vh - 90px));min-height:700px;box-sizing:border-box;padding:12px 16px}')
body=body.replace('  html,body{height:auto}\n','')
for i in ids: body=re.sub(r'#'+re.escape(i)+r'\b', '#mb-'+i, body)
def prefix(sel):
    out=[]
    for s in sel.split(','):
        s=s.strip()
        if not s: continue
        if s=='ROOT': out.append('.dpmb.dpmb'); continue
        out.append('.dpmb.dpmb '+s)
    return ','.join(out)
res=[]; i=0; n=len(body)
def parse(block):
    out=''; i=0
    while i<len(block):
        j=block.find('{',i)
        if j<0: out+=block[i:]; break
        sel=block[i:j].strip()
        if sel.startswith('@media'):
            depth=1;k=j+1
            while depth:
                if block[k]=='{':depth+=1
                elif block[k]=='}':depth-=1
                k+=1
            out+=sel+'{'+parse(block[j+1:k-1])+'}\n'; i=k
        else:
            k=block.index('}',j)
            out+=prefix(sel)+block[j:k+1]+'\n'; i=k+1
    return out
scoped=parse(body)
site_css=('/* dpCREATOR machine builder (generated from the artifact source by site.py) */\n'
 '.dpmb{'+dark.strip()+';'+re.search(r'(--f-display:.*?;\s*--f-body:.*?;\s*--f-mono:[^;]*;)', light, re.S).group(1)+'}\n'
 'html.dpc-light .dpmb{'+light.strip()+';color-scheme:light}\n'
 '.dpmb.dpmb *{box-sizing:border-box}\n'
 '.dpmb.dpmb button,.dpmb.dpmb select,.dpmb.dpmb input,.dpmb.dpmb textarea{line-height:1.3;text-transform:none;letter-spacing:normal;box-shadow:none;height:auto;margin:0}\n'
 '.dpmb.dpmb input[type=checkbox]{width:auto;margin:0}\n'
 '.dpmb.dpmb code{background:none;color:inherit;padding:0;font-family:var(--f-mono)}\n'
 '.dpmb.dpmb h1,.dpmb.dpmb h3,.dpmb.dpmb h4{color:var(--fg);text-transform:none}\n'
 + scoped)
open(d+'site/dpc-builder.css','w').write(site_css)
# ---- js
sj=js
sj=sj.replace('const $ = (id) => document.getElementById(id);','const $ = (id) => document.getElementById("mb-" + id);')
sj=sj.replace('''const schema = S.Schema(JSON.parse(document.getElementById("items").textContent));''','''const schema = S.Schema(ITEMS_DOC);''')
sj=sj.replace('''const ITEMS = JSON.parse(document.getElementById("items").textContent);''','''const ITEMS = ITEMS_DOC;''')
sj=sj.replace('id="sec-\'', 'id="mb-sec-\'')
assert 'id="sec-' not in sj.replace('id="mb-sec-','')
sj=sj.replace('__BUILDER__',bld)
# wrap: wait for the core and the key list
sj=sj.replace('(function () {\n  "use strict";','function dpmbStart(ITEMS_DOC) {\n  "use strict";',1)
sj=sj.rstrip()
assert sj.endswith('})();'), sj[-40:]
sj=sj[:-5]+'}\n'
loader='''
(function () {
  var root = document.querySelector(".dpmb");
  if (!root) return;
  var base = (document.currentScript && document.currentScript.src || "").replace(/[^/]*$/, "") || "/catalog/view/javascript/dpc-setup/";
  fetch(base + "config_items.json", { credentials: "same-origin" }).then(function (r) { return r.json(); }).then(function (doc) {
    (function wait(n) { if (window.DPCSetup) dpmbStart(doc); else if (n < 100) setTimeout(function () { wait(n + 1); }, 50); })(0);
  }).catch(function (e) { root.innerHTML = '<p style="padding:16px">The machine builder could not load its key list: ' + e.message + "</p>"; });
})();
'''
open(d+'site/dpc-builder.js','w').write('/* dpCREATOR machine builder (generated from the artifact source by site.py) */\n'+sj+loader)
# ---- markup
mk=markup
for i in ids: mk=re.sub(r'(id|aria-controls|aria-labelledby)="'+re.escape(i)+'"', lambda m: m.group(1)+'="mb-'+i+'"', mk)
mk=mk.replace('<h1>Machine Builder</h1>','<h1>Machine builder</h1>')
open(d+'site/markup.html','w').write('<div class="dpmb">\n'+mk.strip()+'\n</div>\n')
print(len(site_css), len(sj), len(mk))

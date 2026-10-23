import { chromium } from "/tmp/claude-0/scr/node_modules/playwright/index.mjs";
const b = await chromium.launch({ executablePath: "/opt/pw-browsers/chromium-1194/chrome-linux/chrome" });
const p = await b.newPage({ viewport: { width: 1400, height: 950 } }); p.setDefaultTimeout(6000);
await p.route(/cdnjs/, r=>r.abort());
const errs=[]; p.on("pageerror",e=>errs.push(e.message)); p.on('dialog', d=>d.accept());
const pick=async(loc,txt)=>{ const v=await loc.evaluate((el,t)=>{const o=[...el.options].find(o=>o.text.includes(t)); return o?o.value:'';},txt); await loc.selectOption(v); };
const toast=async()=> (await p.textContent('#dps-toast')).trim();
await p.goto("http://127.0.0.1:8799/"); await p.waitForTimeout(800);
await p.screenshot({path:'u0.png'});
// add items
const addItem=async(kind,name,extra={})=>{ await p.click('[data-tab=items]'); await p.click('#dps-new-item'); await p.selectOption('.dps-modal [name=kind]',kind); if(extra.product) await p.selectOption('.dps-modal [name=product_id]',extra.product); if(name) await p.fill('.dps-modal [name=name]',name); if(extra.reorder) await p.fill('.dps-modal [name=reorder_at]',extra.reorder); if(extra.unit) await p.selectOption('.dps-modal [name=unit]',extra.unit); await p.click('.dps-modal [data-s=save]'); await p.waitForTimeout(400); console.log('item',name||extra.product, await toast()); };
await addItem('product','',{product:'380',reorder:'1'});
await addItem('part','R3 PCBA',{reorder:'2'});
await addItem('part','ESP32-S3 N16R8',{reorder:'2'});
await addItem('material','PETG black',{unit:'g'});
await p.screenshot({path:'u1.png'});
// invoice
await p.click('[data-tab=invoices]'); await p.click('#dps-new-inv');
await p.fill('.dps-modal [name=supplier_name]','Elecrow'); await p.fill('.dps-modal [name=number]','EC-77');
await p.selectOption('.dps-modal [name=currency]','USD'); await p.fill('.dps-modal [name=rate]','0.92');
const rows=()=>p.locator('#dps-lines tbody tr');
await pick(rows().nth(0).locator('[name=item_id]'),'R3 PCBA'); await rows().nth(0).locator('[name=qty]').fill('5'); await rows().nth(0).locator('[name=unit_price]').fill('20');
await p.click('#dps-addline'); await pick(rows().nth(1).locator('[name=item_id]'),'ESP32'); await rows().nth(1).locator('[name=qty]').fill('5'); await rows().nth(1).locator('[name=unit_price]').fill('4');
await p.fill('.dps-modal [name=shipping]','15'); await p.fill('.dps-modal [name=customs]','10'); await p.fill('.dps-modal [name=vat]','25');
await p.setInputFiles('#dps-drop input', 'inv.pdf'); await p.waitForTimeout(600);
console.log('drop:', (await p.textContent('#dps-drop')).trim().slice(0,60));
console.log('sum:', (await p.textContent('#dps-invsum')).replace(/\s+/g,' '));
await p.screenshot({path:'u2.png'});
await p.click('.dps-modal [data-s=receive]'); await p.waitForTimeout(800); console.log('receive', await toast());
await p.screenshot({path:'u3.png'});
// PETG via stocktake
await p.click('[data-tab=items]'); await p.click('tr.click:has-text("PETG")'); await p.fill('.dps-modal [name=counted]','1000'); await p.fill('.dps-modal [name=adj_cost]','0.022'); await p.click('#dps-adj'); await p.waitForTimeout(500); console.log('adj', await toast());
// batch
await p.click('[data-tab=batches]'); await p.click('#dps-new-b'); await p.waitForTimeout(300);
const urow=()=>p.locator('#dps-uses tbody tr');
await pick(urow().nth(0).locator('[name=u_item]'),'R3 PCBA');
await p.click('#dps-adduse'); await pick(urow().nth(1).locator('[name=u_item]'),'ESP32');
await p.click('#dps-adduse'); await pick(urow().nth(2).locator('[name=u_item]'),'PETG'); await urow().nth(2).locator('[name=u_per]').fill('40');
await p.fill('.dps-modal [name=started]','4'); await p.locator('.dps-modal [name=started]').dispatchEvent('input');
await p.fill('.dps-modal [name=passed]','3'); await p.fill('.dps-modal [name=labour_hours]','2'); await p.fill('.dps-modal [name=print_grams]','160'); await p.fill('.dps-modal [name=print_hours]','8'); await p.fill('.dps-modal [name=packaging_each]','1.5');
await p.waitForTimeout(700); console.log('bsum:', (await p.textContent('#dps-bsum')).replace(/\s+/g,' '), '| warn:', (await p.textContent('#dps-bwarn')).trim());
await p.screenshot({path:'u4.png'});
await p.click('#dps-saverec'); await p.waitForTimeout(400); console.log('recipe', await toast());
await p.click('#dps-saveb'); await p.waitForTimeout(800); console.log('batch', await toast());
// second batch prefilled from recipe
await p.click('#dps-new-b'); await p.waitForTimeout(500); console.log('prefill rows', await urow().count(), 'labour', await p.inputValue('.dps-modal [name=labour_hours]')); await p.click('.dps-modal [data-x]');
// sale
await p.click('[data-tab=sales]'); await p.click('#dps-new-s'); await p.fill('.dps-modal [name=channel]','Elecrow'); await p.fill('.dps-modal [name=price_each]','160'); await p.fill('.dps-modal [name=fees]','12'); await p.fill('.dps-modal [name=shipping]','8');
await p.waitForTimeout(200); console.log('ssum:', (await p.textContent('#dps-ssum')).replace(/\s+/g,' '));
await p.click('#dps-saves'); await p.waitForTimeout(700); console.log('sale', await toast());
await p.screenshot({path:'u5.png'});
await p.click('[data-tab=reports]'); await p.waitForTimeout(800); await p.screenshot({path:'u6.png'});
await p.click('[data-tab=overview]'); await p.waitForTimeout(800); await p.screenshot({path:'u7.png'});
await p.click('[data-tab=settings]'); await p.fill('#dps-rate','30'); await p.click('#dps-saveset'); await p.waitForTimeout(400); console.log('set', await toast());
// reopen received invoice
await p.click('[data-tab=invoices]'); await p.click('tr.click:has-text("EC-77")'); await p.waitForTimeout(500); console.log('ro inputs disabled', await p.locator('#dps-lines [name=qty][disabled]').count(), 'file link', await p.locator('#dps-drop a').count());
await p.screenshot({path:'u8.png'});
console.log(JSON.stringify(errs));
await b.close();

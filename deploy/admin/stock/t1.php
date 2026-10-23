<?php
require 'harness.php';
$db->query("DROP TABLE IF EXISTS oc_dpc_inv_item, oc_dpc_inv_supplier, oc_dpc_inv_invoice, oc_dpc_inv_invoice_line, oc_dpc_inv_move, oc_dpc_inv_recipe, oc_dpc_inv_recipe_line, oc_dpc_inv_batch, oc_dpc_inv_sale");
$s=call('state'); echo "items ".count($s['items'])." products ".count($s['products'])." name: ".$s['products'][1]['name']."\n";
$r3=call('save_item',array('kind'=>'product','product_id'=>380,'name'=>'','reorder_at'=>1))['item_id'];
$pcb=call('save_item',array('kind'=>'part','name'=>'R3 PCBA','sku'=>'R3-PCBA','reorder_at'=>2))['item_id'];
$esp=call('save_item',array('kind'=>'part','name'=>'ESP32-S3 N16R8','reorder_at'=>2))['item_id'];
$petg=call('save_item',array('kind'=>'material','name'=>'PETG','unit'=>'g'))['item_id'];
call('save_item',array('kind'=>'product','product_id'=>380,'name'=>'dup'));
// invoice in USD: 5 PCBA @ 20, 5 ESP @ 4; shipping 15, customs 10, VAT 25 not reclaimable
$inv=call('save_invoice',array('supplier_name'=>'Elecrow','number'=>'EC-1','date'=>'2026-10-20','currency'=>'USD','rate'=>0.92,'shipping'=>15,'customs'=>10,'vat'=>25,'vat_reclaim'=>0,'other'=>0,
 'lines'=>array(array('item_id'=>$pcb,'descr'=>'PCBA','qty'=>5,'unit_price'=>20),array('item_id'=>$esp,'descr'=>'ESP','qty'=>5,'unit_price'=>4),array('item_id'=>0,'descr'=>'stencil','qty'=>1,'unit_price'=>6))))['invoice_id'];
$i=call('invoice',null,array('id'=>$inv))['invoice']; echo "total_eur ".$i['total_eur']." landed: ".implode(', ',array_map(function($l){return $l['descr'].'='.round($l['landed_eur'],3);},$i['lines']))."\n";
// expect goods 126, extra 50 -> 176*0.92=161.92; PCBA line 100/126 share -> (100+50*100/126)*0.92/5
echo "expect PCBA ".round((100+50*100/126)*0.92/5,3)."\n";
print_r(call('receive',array('invoice_id'=>$inv)));
call('receive',array('invoice_id'=>$inv));
call('delete_invoice',array('invoice_id'=>$inv));
$s=call('state'); foreach($s['items'] as $it) echo " ".$it['name']." have=".$it['have']." avg=".round($it['avg_cost'],3)."\n";
// recipe and batch: 1 PCBA + 1 ESP + 40g PETG, 30 min labour, 2 h print
call('save_recipe',array('item_id'=>$r3,'labour_min'=>30,'print_grams'=>40,'print_hours'=>2,'packaging'=>1.5,'lines'=>array(array('item_id'=>$pcb,'qty'=>1),array('item_id'=>$esp,'qty'=>1),array('item_id'=>$petg,'qty'=>40))));
$b=array('item_id'=>$r3,'date'=>'2026-10-22','started'=>4,'passed'=>3,'labour_hours'=>2,'print_grams'=>160,'print_hours'=>8,'print_invoice'=>0,'packaging_each'=>1.5,'extra'=>0,
 'uses'=>array(array('item_id'=>$pcb,'qty'=>4),array('item_id'=>$esp,'qty'=>4),array('item_id'=>$petg,'qty'=>160)));
print_r(call('batch_preview',$b));
call('save_batch',$b);
$b['allow_short']=1; $j=call('save_batch',$b); echo "batch ".json_encode($j)."\n";
$s=call('state'); foreach($s['items'] as $it) echo " ".$it['name']." have=".$it['have']." avg=".round($it['avg_cost'],3)." full=".round($it['avg_full'],3)."\n";
// outside sale 1 R3 on Elecrow
call('save_sale',array('item_id'=>$r3,'channel'=>'Elecrow','date'=>'2026-10-23','qty'=>1,'price_each'=>160,'rate'=>1,'fees'=>12,'shipping'=>8));
call('adjust',array('item_id'=>$esp,'counted'=>3,'note'=>'found'));
$r=call('report',null,array('from'=>'2026-01-01','to'=>'2026-12-31')); echo json_encode($r, JSON_PRETTY_PRINT)."\n";
echo substr(call('csv',null,array('what'=>'stock','from'=>'2026-01-01','to'=>'2026-12-31')),0,400)."\n";
$m=call('moves',null,array('id'=>$r3)); echo "moves ".count($m['moves'])." shop ".count($m['shop'])."\n";

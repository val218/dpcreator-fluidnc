<?php
// dpCREATOR Purchases and stock.
// Supplier invoices (with the PDF) are received into stock at landed cost: shipping, customs and
// other charges are shared over the lines by value. Build batches turn parts into finished products
// and add printing, packaging and (for the full cost) your labour. Sales outside the shop take
// finished products out of stock. Finished products are linked to shop products: their stock is the
// shop's own quantity, so shop orders, batches and outside sales all count against the same number.
// All money is kept in EUR.
class ControllerExtensionModuleDpcStock extends Controller {
	private $P;

	public function __construct($registry) {
		parent::__construct($registry);
		$this->P = DB_PREFIX . 'dpc_inv_';
	}

	// ------------------------------------------------------------------ install
	public function install() {
		$this->tables();
		$this->load->model('setting/setting');
		$this->model_setting_setting->editSetting('module_dpc_stock', array(
			'module_dpc_stock_status' => 1,
			'module_dpc_stock_rate' => 25,
			'module_dpc_stock_filament' => 22,
			'module_dpc_stock_machine' => 0.4
		));
	}

	public function uninstall() {
		// Tables are kept: invoices and stock history are records.
	}

	private function tables() {
		$P = $this->P;
		$this->db->query("CREATE TABLE IF NOT EXISTS `{$P}item` (
			`item_id` INT NOT NULL AUTO_INCREMENT, `kind` VARCHAR(10) NOT NULL DEFAULT 'part', `name` VARCHAR(255) NOT NULL, `sku` VARCHAR(64) NOT NULL DEFAULT '',
			`unit` VARCHAR(10) NOT NULL DEFAULT 'pcs', `product_id` INT NOT NULL DEFAULT 0, `qty` DECIMAL(15,4) NOT NULL DEFAULT 0,
			`avg_cost` DECIMAL(15,6) NOT NULL DEFAULT 0, `avg_full` DECIMAL(15,6) NOT NULL DEFAULT 0, `reorder_at` DECIMAL(15,4) NOT NULL DEFAULT 0,
			`note` TEXT, `status` TINYINT NOT NULL DEFAULT 1, `created` DATETIME NOT NULL, PRIMARY KEY (`item_id`), KEY `product_id` (`product_id`)
		) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
		$this->db->query("CREATE TABLE IF NOT EXISTS `{$P}supplier` (
			`supplier_id` INT NOT NULL AUTO_INCREMENT, `name` VARCHAR(128) NOT NULL, `website` VARCHAR(255) NOT NULL DEFAULT '', `note` TEXT, PRIMARY KEY (`supplier_id`)
		) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
		$this->db->query("CREATE TABLE IF NOT EXISTS `{$P}invoice` (
			`invoice_id` INT NOT NULL AUTO_INCREMENT, `supplier_id` INT NOT NULL DEFAULT 0, `number` VARCHAR(64) NOT NULL DEFAULT '', `date` DATE NOT NULL,
			`currency` VARCHAR(3) NOT NULL DEFAULT 'EUR', `rate` DECIMAL(15,8) NOT NULL DEFAULT 1,
			`shipping` DECIMAL(15,4) NOT NULL DEFAULT 0, `customs` DECIMAL(15,4) NOT NULL DEFAULT 0, `vat` DECIMAL(15,4) NOT NULL DEFAULT 0, `vat_reclaim` TINYINT NOT NULL DEFAULT 0,
			`other` DECIMAL(15,4) NOT NULL DEFAULT 0, `total_eur` DECIMAL(15,4) NOT NULL DEFAULT 0, `file` VARCHAR(255) NOT NULL DEFAULT '', `file_name` VARCHAR(255) NOT NULL DEFAULT '',
			`status` VARCHAR(10) NOT NULL DEFAULT 'draft', `note` TEXT, `created` DATETIME NOT NULL, `received` DATETIME NULL, PRIMARY KEY (`invoice_id`)
		) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
		$this->db->query("CREATE TABLE IF NOT EXISTS `{$P}invoice_line` (
			`line_id` INT NOT NULL AUTO_INCREMENT, `invoice_id` INT NOT NULL, `item_id` INT NOT NULL DEFAULT 0, `descr` VARCHAR(255) NOT NULL DEFAULT '',
			`qty` DECIMAL(15,4) NOT NULL DEFAULT 0, `unit_price` DECIMAL(15,6) NOT NULL DEFAULT 0, `landed_eur` DECIMAL(15,6) NOT NULL DEFAULT 0, PRIMARY KEY (`line_id`), KEY `invoice_id` (`invoice_id`)
		) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
		$this->db->query("CREATE TABLE IF NOT EXISTS `{$P}move` (
			`move_id` INT NOT NULL AUTO_INCREMENT, `item_id` INT NOT NULL, `date` DATETIME NOT NULL, `qty` DECIMAL(15,4) NOT NULL, `unit_cost` DECIMAL(15,6) NOT NULL DEFAULT 0,
			`kind` VARCHAR(12) NOT NULL, `ref` VARCHAR(32) NOT NULL DEFAULT '', `note` VARCHAR(255) NOT NULL DEFAULT '', `user` VARCHAR(64) NOT NULL DEFAULT '',
			PRIMARY KEY (`move_id`), KEY `item_id` (`item_id`, `date`)
		) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
		$this->db->query("CREATE TABLE IF NOT EXISTS `{$P}recipe` (
			`recipe_id` INT NOT NULL AUTO_INCREMENT, `item_id` INT NOT NULL, `labour_min` DECIMAL(10,2) NOT NULL DEFAULT 0, `print_grams` DECIMAL(10,2) NOT NULL DEFAULT 0,
			`print_hours` DECIMAL(10,2) NOT NULL DEFAULT 0, `packaging` DECIMAL(10,4) NOT NULL DEFAULT 0, `note` TEXT, PRIMARY KEY (`recipe_id`), UNIQUE KEY `item_id` (`item_id`)
		) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
		$this->db->query("CREATE TABLE IF NOT EXISTS `{$P}recipe_line` (
			`id` INT NOT NULL AUTO_INCREMENT, `recipe_id` INT NOT NULL, `item_id` INT NOT NULL, `qty` DECIMAL(15,4) NOT NULL DEFAULT 1, PRIMARY KEY (`id`), KEY `recipe_id` (`recipe_id`)
		) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
		$this->db->query("CREATE TABLE IF NOT EXISTS `{$P}batch` (
			`batch_id` INT NOT NULL AUTO_INCREMENT, `item_id` INT NOT NULL, `date` DATE NOT NULL, `started` INT NOT NULL DEFAULT 0, `passed` INT NOT NULL DEFAULT 0,
			`parts_cost` DECIMAL(15,4) NOT NULL DEFAULT 0, `labour_hours` DECIMAL(10,2) NOT NULL DEFAULT 0, `labour_cost` DECIMAL(15,4) NOT NULL DEFAULT 0,
			`print_grams` DECIMAL(10,2) NOT NULL DEFAULT 0, `print_hours` DECIMAL(10,2) NOT NULL DEFAULT 0, `print_cost` DECIMAL(15,4) NOT NULL DEFAULT 0, `print_invoice` DECIMAL(15,4) NOT NULL DEFAULT 0,
			`packaging_cost` DECIMAL(15,4) NOT NULL DEFAULT 0, `extra_cost` DECIMAL(15,4) NOT NULL DEFAULT 0, `cash_cost` DECIMAL(15,4) NOT NULL DEFAULT 0, `full_cost` DECIMAL(15,4) NOT NULL DEFAULT 0,
			`note` TEXT, `created` DATETIME NOT NULL, PRIMARY KEY (`batch_id`)
		) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
		$this->db->query("CREATE TABLE IF NOT EXISTS `{$P}sale` (
			`sale_id` INT NOT NULL AUTO_INCREMENT, `channel` VARCHAR(64) NOT NULL, `date` DATE NOT NULL, `item_id` INT NOT NULL, `qty` DECIMAL(15,4) NOT NULL,
			`price_each` DECIMAL(15,4) NOT NULL DEFAULT 0, `fees` DECIMAL(15,4) NOT NULL DEFAULT 0, `shipping` DECIMAL(15,4) NOT NULL DEFAULT 0,
			`unit_cost` DECIMAL(15,6) NOT NULL DEFAULT 0, `unit_full` DECIMAL(15,6) NOT NULL DEFAULT 0, `customer` VARCHAR(128) NOT NULL DEFAULT '', `note` TEXT, `created` DATETIME NOT NULL, PRIMARY KEY (`sale_id`)
		) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
	}

	private function ready() {
		return (bool)$this->db->query("SHOW TABLES LIKE '" . $this->db->escape($this->P . 'move') . "'")->num_rows;
	}

	// ------------------------------------------------------------------ page
	public function index() {
		if (!$this->ready()) {
			$this->tables();
		}
		$this->document->setTitle('Purchases and stock');
		$token = 'user_token=' . $this->session->data['user_token'];
		$data['api'] = html_entity_decode($this->url->link('extension/module/dpc_stock/api', $token, true), ENT_QUOTES, 'UTF-8');
		$data['desk'] = $this->url->link('extension/module/dpc_desk/desk', $token, true);
		$data['can_modify'] = $this->user->hasPermission('modify', 'extension/module/dpc_stock') ? 1 : 0;
		$data['header'] = $this->load->controller('common/header');
		$data['column_left'] = $this->load->controller('common/column_left');
		$data['footer'] = $this->load->controller('common/footer');
		$this->response->setOutput($this->load->view('extension/module/dpc_stock', $data));
	}

	// ------------------------------------------------------------------ helpers
	private function e($s) { return $this->db->escape((string)$s); }
	private function n($v) { return is_numeric($v) ? (float)$v : (float)str_replace(',', '.', (string)$v); }
	private function d($v) { $t = strtotime((string)$v); return $t ? date('Y-m-d', $t) : date('Y-m-d'); }
	private function cfg($k, $def) { $v = $this->config->get('module_dpc_stock_' . $k); return ($v === null || $v === '') ? $def : (float)$v; }
	private function input() {
		$raw = file_get_contents('php://input');
		$j = json_decode($raw, true);
		return is_array($j) ? $j : $this->request->post;
	}
	private function out($x) {
		$this->response->addHeader('Content-Type: application/json');
		$this->response->setOutput(json_encode($x));
	}
	private function fail($msg) { $this->out(array('error' => $msg)); }

	// Order statuses that count as a sale (the dpCREATOR Sales choice, else processing + complete)
	private function counted() {
		$saved = $this->config->get('dashboard_dpc_sales_statuses');
		if (is_array($saved) && $saved) {
			return array_values(array_filter(array_map('intval', $saved)));
		}
		$ids = array();
		foreach (array('config_processing_status', 'config_complete_status') as $key) {
			$v = $this->config->get($key);
			foreach ((is_array($v) ? $v : array($v)) as $id) {
				$ids[(int)$id] = true;
			}
		}
		unset($ids[(int)$this->config->get('config_order_status_id')], $ids[0]);
		return array_keys($ids);
	}

	// Current quantity of an item: the shop product's quantity for linked products
	private function qtyOf($item) {
		if ((int)$item['product_id']) {
			$r = $this->db->query("SELECT quantity FROM `" . DB_PREFIX . "product` WHERE product_id = '" . (int)$item['product_id'] . "'")->row;
			return $r ? (float)$r['quantity'] : 0.0;
		}
		return (float)$item['qty'];
	}

	private function item($id) {
		$r = $this->db->query("SELECT * FROM `{$this->P}item` WHERE item_id = '" . (int)$id . "'")->row;
		return $r ?: null;
	}

	// Add (qty > 0) or take (qty < 0) stock. Incoming stock updates the moving average costs.
	private function move($item, $qty, $unitCost, $unitFull, $kind, $ref, $note, $date = null) {
		$have = max(0.0, $this->qtyOf($item));
		$set = array();
		if ($qty > 0) {
			// stock already there with no known cost is valued like the incoming units
			$avg = $have > 0 && (float)$item['avg_cost'] > 0 ? (($have * (float)$item['avg_cost']) + ($qty * $unitCost)) / ($have + $qty) : $unitCost;
			$full = $have > 0 && (float)$item['avg_full'] > 0 ? (($have * (float)$item['avg_full']) + ($qty * $unitFull)) / ($have + $qty) : $unitFull;
			$set[] = "avg_cost = '" . round($avg, 6) . "'";
			$set[] = "avg_full = '" . round($full, 6) . "'";
		}
		if ((int)$item['product_id']) {
			$this->db->query("UPDATE `" . DB_PREFIX . "product` SET quantity = quantity + " . (int)round($qty) . " WHERE product_id = '" . (int)$item['product_id'] . "'");
		} else {
			$set[] = "qty = qty + " . round($qty, 4);
		}
		if ($set) {
			$this->db->query("UPDATE `{$this->P}item` SET " . implode(', ', $set) . " WHERE item_id = '" . (int)$item['item_id'] . "'");
		}
		$this->db->query("INSERT INTO `{$this->P}move` SET item_id = '" . (int)$item['item_id'] . "', `date` = '" . $this->e($date ?: date('Y-m-d H:i:s')) . "', qty = '" . round($qty, 4) . "',
			unit_cost = '" . round($qty > 0 ? $unitCost : (float)$item['avg_cost'], 6) . "', kind = '" . $this->e($kind) . "', ref = '" . $this->e($ref) . "', note = '" . $this->e(mb_substr($note, 0, 250)) . "', user = '" . $this->e($this->user->getUserName()) . "'");
	}

	private function money($v) { return round((float)$v, 2); }

	// ------------------------------------------------------------------ API
	public function api() {
		if (!$this->user->hasPermission('access', 'extension/module/dpc_stock')) {
			return $this->fail('No access to Purchases and stock.');
		}
		if (!$this->ready()) {
			$this->tables();
		}
		$f = isset($this->request->get['f']) ? $this->request->get['f'] : 'state';
		$writes = array('save_item', 'save_supplier', 'save_invoice', 'receive', 'delete_invoice', 'upload', 'save_recipe', 'save_batch', 'save_sale', 'adjust', 'settings');
		if (in_array($f, $writes, true) && !$this->user->hasPermission('modify', 'extension/module/dpc_stock')) {
			return $this->fail('You can look but not change Purchases and stock.');
		}
		$m = 'f_' . $f;
		if (!method_exists($this, $m)) {
			return $this->fail('Unknown request.');
		}
		try {
			$this->$m();
		} catch (\Throwable $e) {
			$this->fail($e->getMessage());
		}
	}

	private function f_state() {
		$lang = (int)$this->config->get('config_language_id');
		$items = $this->db->query("SELECT i.*, pd.name AS product_name, p.quantity AS product_qty, p.price AS product_price FROM `{$this->P}item` i
			LEFT JOIN `" . DB_PREFIX . "product` p ON p.product_id = i.product_id LEFT JOIN `" . DB_PREFIX . "product_description` pd ON pd.product_id = i.product_id AND pd.language_id = '" . $lang . "'
			WHERE i.status = 1 ORDER BY FIELD(i.kind, 'product', 'part', 'material'), i.name")->rows;
		foreach ($items as &$i) {
			$i['product_name'] = $i['product_name'] !== null ? html_entity_decode($i['product_name'], ENT_QUOTES, 'UTF-8') : null;
			$i['have'] = (int)$i['product_id'] ? (float)$i['product_qty'] : (float)$i['qty'];
			$i['value'] = round(max(0, $i['have']) * (float)$i['avg_cost'], 2);
		}
		unset($i);
		$products = $this->db->query("SELECT p.product_id, p.model, p.quantity, p.shipping, pd.name FROM `" . DB_PREFIX . "product` p LEFT JOIN `" . DB_PREFIX . "product_description` pd ON pd.product_id = p.product_id AND pd.language_id = '" . $lang . "' ORDER BY p.status DESC, pd.name")->rows;
		foreach ($products as &$p) {
			$p['name'] = html_entity_decode((string)$p['name'], ENT_QUOTES, 'UTF-8');
		}
		unset($p);
		$recipes = array();
		foreach ($this->db->query("SELECT * FROM `{$this->P}recipe`")->rows as $r) {
			$r['lines'] = $this->db->query("SELECT item_id, qty FROM `{$this->P}recipe_line` WHERE recipe_id = '" . (int)$r['recipe_id'] . "'")->rows;
			$recipes[(int)$r['item_id']] = $r;
		}
		$channels = array();
		foreach ($this->db->query("SELECT DISTINCT channel FROM `{$this->P}sale` ORDER BY channel")->rows as $r) {
			$channels[] = $r['channel'];
		}
		$this->out(array(
			'items' => $items, 'products' => $products, 'recipes' => $recipes, 'channels' => $channels,
			'suppliers' => $this->db->query("SELECT * FROM `{$this->P}supplier` ORDER BY name")->rows,
			'settings' => array('rate' => $this->cfg('rate', 25), 'filament' => $this->cfg('filament', 22), 'machine' => $this->cfg('machine', 0.4)),
			'can_modify' => $this->user->hasPermission('modify', 'extension/module/dpc_stock')
		));
	}

	private function f_settings() {
		$in = $this->input();
		$this->load->model('setting/setting');
		$this->model_setting_setting->editSetting('module_dpc_stock', array(
			'module_dpc_stock_status' => 1,
			'module_dpc_stock_rate' => max(0, $this->n(isset($in['rate']) ? $in['rate'] : 0)),
			'module_dpc_stock_filament' => max(0, $this->n(isset($in['filament']) ? $in['filament'] : 0)),
			'module_dpc_stock_machine' => max(0, $this->n(isset($in['machine']) ? $in['machine'] : 0))
		));
		$this->out(array('ok' => 1));
	}

	private function f_save_item() {
		$in = $this->input();
		$name = trim(isset($in['name']) ? $in['name'] : '');
		$pid = (int)(isset($in['product_id']) ? $in['product_id'] : 0);
		if ($name === '' && $pid) {
			$r = $this->db->query("SELECT name FROM `" . DB_PREFIX . "product_description` WHERE product_id = '" . $pid . "' AND language_id = '" . (int)$this->config->get('config_language_id') . "'")->row;
			$name = $r ? html_entity_decode($r['name'], ENT_QUOTES, 'UTF-8') : '';
		}
		if ($name === '') {
			return $this->fail('Give the item a name.');
		}
		$kind = in_array(isset($in['kind']) ? $in['kind'] : '', array('product', 'part', 'material'), true) ? $in['kind'] : 'part';
		if ($kind !== 'product') {
			$pid = 0;
		}
		if ($pid) {
			$dup = $this->db->query("SELECT item_id FROM `{$this->P}item` WHERE product_id = '" . $pid . "' AND status = 1 AND item_id <> '" . (int)(isset($in['item_id']) ? $in['item_id'] : 0) . "'")->row;
			if ($dup) {
				return $this->fail('That shop product already has a stock item.');
			}
		}
		$unit = in_array(isset($in['unit']) ? $in['unit'] : '', array('pcs', 'g', 'kg', 'm', 'ml'), true) ? $in['unit'] : 'pcs';
		$sql = "kind = '" . $this->e($kind) . "', name = '" . $this->e($name) . "', sku = '" . $this->e(isset($in['sku']) ? $in['sku'] : '') . "', unit = '" . $this->e($unit) . "',
			product_id = '" . $pid . "', reorder_at = '" . max(0, $this->n(isset($in['reorder_at']) ? $in['reorder_at'] : 0)) . "', note = '" . $this->e(isset($in['note']) ? $in['note'] : '') . "'";
		if (!empty($in['item_id'])) {
			$this->db->query("UPDATE `{$this->P}item` SET " . $sql . " WHERE item_id = '" . (int)$in['item_id'] . "'");
			$id = (int)$in['item_id'];
		} else {
			$this->db->query("INSERT INTO `{$this->P}item` SET " . $sql . ", created = NOW()");
			$id = (int)$this->db->getLastId();
			// a starting cost lets existing stock be valued
			$start = $this->n(isset($in['start_cost']) ? $in['start_cost'] : 0);
			if ($start > 0) {
				$this->db->query("UPDATE `{$this->P}item` SET avg_cost = '" . $start . "', avg_full = '" . $start . "' WHERE item_id = '" . $id . "'");
			}
		}
		if (!empty($in['archive'])) {
			$this->db->query("UPDATE `{$this->P}item` SET status = 0 WHERE item_id = '" . $id . "'");
		}
		$this->out(array('ok' => 1, 'item_id' => $id));
	}

	private function f_save_supplier() {
		$in = $this->input();
		$name = trim(isset($in['name']) ? $in['name'] : '');
		if ($name === '') {
			return $this->fail('Give the supplier a name.');
		}
		$sql = "name = '" . $this->e($name) . "', website = '" . $this->e(isset($in['website']) ? $in['website'] : '') . "', note = '" . $this->e(isset($in['note']) ? $in['note'] : '') . "'";
		if (!empty($in['supplier_id'])) {
			$this->db->query("UPDATE `{$this->P}supplier` SET " . $sql . " WHERE supplier_id = '" . (int)$in['supplier_id'] . "'");
			$id = (int)$in['supplier_id'];
		} else {
			$this->db->query("INSERT INTO `{$this->P}supplier` SET " . $sql);
			$id = (int)$this->db->getLastId();
		}
		$this->out(array('ok' => 1, 'supplier_id' => $id));
	}

	// ------------------------------------------------------------------ invoices
	private function f_invoices() {
		$rows = $this->db->query("SELECT i.*, s.name AS supplier, (SELECT COUNT(*) FROM `{$this->P}invoice_line` l WHERE l.invoice_id = i.invoice_id) AS lines_n
			FROM `{$this->P}invoice` i LEFT JOIN `{$this->P}supplier` s ON s.supplier_id = i.supplier_id ORDER BY i.date DESC, i.invoice_id DESC LIMIT 500")->rows;
		$this->out(array('invoices' => $rows));
	}

	private function f_invoice() {
		$id = (int)(isset($this->request->get['id']) ? $this->request->get['id'] : 0);
		$inv = $this->db->query("SELECT * FROM `{$this->P}invoice` WHERE invoice_id = '" . $id . "'")->row;
		if (!$inv) {
			return $this->fail('Invoice not found.');
		}
		$inv['lines'] = $this->db->query("SELECT * FROM `{$this->P}invoice_line` WHERE invoice_id = '" . $id . "' ORDER BY line_id")->rows;
		$this->out(array('invoice' => $inv));
	}

	// Landed cost per unit in EUR: line value + its share (by value) of shipping, customs, other
	// and, when VAT can't be reclaimed, VAT.
	private function landed($inv, $lines) {
		$rate = (float)$inv['rate'] > 0 ? (float)$inv['rate'] : 1.0;
		$goods = 0.0;
		foreach ($lines as $l) {
			$goods += (float)$l['qty'] * (float)$l['unit_price'];
		}
		$extra = (float)$inv['shipping'] + (float)$inv['customs'] + (float)$inv['other'] + ((int)$inv['vat_reclaim'] ? 0 : (float)$inv['vat']);
		$out = array();
		foreach ($lines as $k => $l) {
			$val = (float)$l['qty'] * (float)$l['unit_price'];
			$share = $goods > 0 ? $val / $goods : (count($lines) ? 1 / count($lines) : 0);
			$lineTotal = ($val + $extra * $share) * $rate;
			$out[$k] = (float)$l['qty'] > 0 ? $lineTotal / (float)$l['qty'] : 0.0;
		}
		return array('units' => $out, 'total' => ($goods + $extra + ((int)$inv['vat_reclaim'] ? (float)$inv['vat'] : 0)) * $rate);
	}

	private function f_save_invoice() {
		$in = $this->input();
		$id = (int)(isset($in['invoice_id']) ? $in['invoice_id'] : 0);
		if ($id) {
			$old = $this->db->query("SELECT status FROM `{$this->P}invoice` WHERE invoice_id = '" . $id . "'")->row;
			if (!$old) {
				return $this->fail('Invoice not found.');
			}
		}
		$sup = (int)(isset($in['supplier_id']) ? $in['supplier_id'] : 0);
		if (!$sup && !empty($in['supplier_name'])) {
			$ex = $this->db->query("SELECT supplier_id FROM `{$this->P}supplier` WHERE name = '" . $this->e(trim($in['supplier_name'])) . "'")->row;
			if ($ex) {
				$sup = (int)$ex['supplier_id'];
			} else {
				$this->db->query("INSERT INTO `{$this->P}supplier` SET name = '" . $this->e(trim($in['supplier_name'])) . "'");
				$sup = (int)$this->db->getLastId();
			}
		}
		$cur = strtoupper(preg_replace('/[^A-Za-z]/', '', isset($in['currency']) ? $in['currency'] : 'EUR')) ?: 'EUR';
		$rate = $cur === 'EUR' ? 1 : max(0.00000001, $this->n(isset($in['rate']) ? $in['rate'] : 1));
		$fields = array(
			'supplier_id' => $sup, 'number' => isset($in['number']) ? $in['number'] : '', 'date' => $this->d(isset($in['date']) ? $in['date'] : ''),
			'currency' => substr($cur, 0, 3), 'rate' => $rate,
			'shipping' => $this->n(isset($in['shipping']) ? $in['shipping'] : 0), 'customs' => $this->n(isset($in['customs']) ? $in['customs'] : 0),
			'vat' => $this->n(isset($in['vat']) ? $in['vat'] : 0), 'vat_reclaim' => !empty($in['vat_reclaim']) ? 1 : 0, 'other' => $this->n(isset($in['other']) ? $in['other'] : 0),
			'note' => isset($in['note']) ? $in['note'] : ''
		);
		if (!empty($in['file'])) {
			$fields['file'] = basename($in['file']);
			$fields['file_name'] = isset($in['file_name']) ? $in['file_name'] : '';
		}
		$received = $id && isset($old) && $old['status'] === 'received';
		if ($received) {
			// a received invoice keeps its lines and amounts: only the paperwork can change
			$fields = array_intersect_key($fields, array_flip(array('supplier_id', 'number', 'date', 'note', 'file', 'file_name')));
		}
		$set = array();
		foreach ($fields as $k => $v) {
			$set[] = "`" . $k . "` = '" . $this->e($v) . "'";
		}
		if ($id) {
			$this->db->query("UPDATE `{$this->P}invoice` SET " . implode(', ', $set) . " WHERE invoice_id = '" . $id . "'");
		} else {
			$this->db->query("INSERT INTO `{$this->P}invoice` SET " . implode(', ', $set) . ", created = NOW()");
			$id = (int)$this->db->getLastId();
		}
		if (!$received) {
			$this->db->query("DELETE FROM `{$this->P}invoice_line` WHERE invoice_id = '" . $id . "'");
			foreach ((isset($in['lines']) && is_array($in['lines'])) ? $in['lines'] : array() as $l) {
				$q = $this->n(isset($l['qty']) ? $l['qty'] : 0);
				if ($q == 0 && trim(isset($l['descr']) ? $l['descr'] : '') === '') {
					continue;
				}
				$this->db->query("INSERT INTO `{$this->P}invoice_line` SET invoice_id = '" . $id . "', item_id = '" . (int)(isset($l['item_id']) ? $l['item_id'] : 0) . "',
					descr = '" . $this->e(isset($l['descr']) ? $l['descr'] : '') . "', qty = '" . $q . "', unit_price = '" . $this->n(isset($l['unit_price']) ? $l['unit_price'] : 0) . "'");
			}
			$inv = $this->db->query("SELECT * FROM `{$this->P}invoice` WHERE invoice_id = '" . $id . "'")->row;
			$lines = $this->db->query("SELECT * FROM `{$this->P}invoice_line` WHERE invoice_id = '" . $id . "' ORDER BY line_id")->rows;
			$lc = $this->landed($inv, $lines);
			foreach ($lines as $k => $l) {
				$this->db->query("UPDATE `{$this->P}invoice_line` SET landed_eur = '" . round($lc['units'][$k], 6) . "' WHERE line_id = '" . (int)$l['line_id'] . "'");
			}
			$this->db->query("UPDATE `{$this->P}invoice` SET total_eur = '" . round($lc['total'], 4) . "' WHERE invoice_id = '" . $id . "'");
		}
		$this->out(array('ok' => 1, 'invoice_id' => $id));
	}

	// Put an invoice's lines into stock at landed cost (once)
	private function f_receive() {
		$in = $this->input();
		$id = (int)(isset($in['invoice_id']) ? $in['invoice_id'] : 0);
		$inv = $this->db->query("SELECT * FROM `{$this->P}invoice` WHERE invoice_id = '" . $id . "'")->row;
		if (!$inv) {
			return $this->fail('Invoice not found.');
		}
		if ($inv['status'] === 'received') {
			return $this->fail('This invoice is already in stock.');
		}
		$lines = $this->db->query("SELECT * FROM `{$this->P}invoice_line` WHERE invoice_id = '" . $id . "' ORDER BY line_id")->rows;
		$n = 0;
		foreach ($lines as $l) {
			$item = (int)$l['item_id'] ? $this->item($l['item_id']) : null;
			if ($item && (float)$l['qty'] > 0) {
				$this->move($item, (float)$l['qty'], (float)$l['landed_eur'], (float)$l['landed_eur'], 'purchase', 'invoice:' . $id, trim(($inv['number'] ? 'Invoice ' . $inv['number'] : 'Invoice #' . $id) . ' ' . $l['descr']), $inv['date'] . ' 12:00:00');
				$n++;
			}
		}
		$this->db->query("UPDATE `{$this->P}invoice` SET status = 'received', received = NOW() WHERE invoice_id = '" . $id . "'");
		$this->out(array('ok' => 1, 'lines' => $n));
	}

	private function f_delete_invoice() {
		$in = $this->input();
		$id = (int)(isset($in['invoice_id']) ? $in['invoice_id'] : 0);
		$inv = $this->db->query("SELECT status FROM `{$this->P}invoice` WHERE invoice_id = '" . $id . "'")->row;
		if (!$inv || $inv['status'] !== 'draft') {
			return $this->fail('Only a draft that is not in stock yet can be removed.');
		}
		$this->db->query("DELETE FROM `{$this->P}invoice_line` WHERE invoice_id = '" . $id . "'");
		$this->db->query("DELETE FROM `{$this->P}invoice` WHERE invoice_id = '" . $id . "'");
		$this->out(array('ok' => 1));
	}

	// Invoice files live outside the web root (system/storage/upload/dpc-invoices)
	private function fileDir() {
		$dir = DIR_UPLOAD . 'dpc-invoices/';
		if (!is_dir($dir)) {
			mkdir($dir, 0755, true);
			file_put_contents($dir . 'index.html', '');
		}
		return $dir;
	}

	private function f_upload() {
		if (empty($this->request->files['file']['tmp_name']) || !is_uploaded_file($this->request->files['file']['tmp_name'])) {
			return $this->fail('No file arrived.');
		}
		$f = $this->request->files['file'];
		if ($f['size'] > 15 * 1024 * 1024) {
			return $this->fail('The file is larger than 15 MB.');
		}
		$name = basename(html_entity_decode($f['name'], ENT_QUOTES, 'UTF-8'));
		$ext = strtolower(pathinfo($name, PATHINFO_EXTENSION));
		if (!in_array($ext, array('pdf', 'jpg', 'jpeg', 'png', 'webp'), true)) {
			return $this->fail('Use a PDF or a picture (JPG, PNG, WebP).');
		}
		$head = file_get_contents($f['tmp_name'], false, null, 0, 8);
		$ok = ($ext === 'pdf' && substr($head, 0, 4) === '%PDF') || (in_array($ext, array('jpg', 'jpeg'), true) && substr($head, 0, 2) === "\xFF\xD8") || ($ext === 'png' && substr($head, 0, 4) === "\x89PNG") || ($ext === 'webp' && substr($head, 0, 4) === 'RIFF');
		if (!$ok) {
			return $this->fail('The file does not look like a ' . strtoupper($ext) . '.');
		}
		$store = date('Ymd-His') . '-' . bin2hex(random_bytes(6)) . '.' . $ext;
		move_uploaded_file($f['tmp_name'], $this->fileDir() . $store);
		$this->out(array('ok' => 1, 'file' => $store, 'file_name' => $name));
	}

	public function file() {
		if (!$this->user->hasPermission('access', 'extension/module/dpc_stock')) {
			return;
		}
		$id = (int)(isset($this->request->get['id']) ? $this->request->get['id'] : 0);
		$inv = $this->db->query("SELECT file, file_name FROM `{$this->P}invoice` WHERE invoice_id = '" . $id . "'")->row;
		$path = $inv && $inv['file'] ? $this->fileDir() . basename($inv['file']) : '';
		if (!$path || !is_file($path)) {
			$this->response->setOutput('File not found.');
			return;
		}
		$ext = strtolower(pathinfo($path, PATHINFO_EXTENSION));
		$types = array('pdf' => 'application/pdf', 'jpg' => 'image/jpeg', 'jpeg' => 'image/jpeg', 'png' => 'image/png', 'webp' => 'image/webp');
		header('Content-Type: ' . (isset($types[$ext]) ? $types[$ext] : 'application/octet-stream'));
		header('Content-Disposition: inline; filename="' . str_replace('"', '', $inv['file_name'] ?: basename($path)) . '"');
		header('X-Content-Type-Options: nosniff');
		header('Content-Length: ' . filesize($path));
		readfile($path);
		exit;
	}

	// ------------------------------------------------------------------ recipes and batches
	private function f_save_recipe() {
		$in = $this->input();
		$item = $this->item(isset($in['item_id']) ? $in['item_id'] : 0);
		if (!$item || $item['kind'] !== 'product') {
			return $this->fail('A recipe belongs to a finished product.');
		}
		$sql = "item_id = '" . (int)$item['item_id'] . "', labour_min = '" . max(0, $this->n(isset($in['labour_min']) ? $in['labour_min'] : 0)) . "', print_grams = '" . max(0, $this->n(isset($in['print_grams']) ? $in['print_grams'] : 0)) . "',
			print_hours = '" . max(0, $this->n(isset($in['print_hours']) ? $in['print_hours'] : 0)) . "', packaging = '" . max(0, $this->n(isset($in['packaging']) ? $in['packaging'] : 0)) . "', note = '" . $this->e(isset($in['note']) ? $in['note'] : '') . "'";
		$ex = $this->db->query("SELECT recipe_id FROM `{$this->P}recipe` WHERE item_id = '" . (int)$item['item_id'] . "'")->row;
		if ($ex) {
			$rid = (int)$ex['recipe_id'];
			$this->db->query("UPDATE `{$this->P}recipe` SET " . $sql . " WHERE recipe_id = '" . $rid . "'");
		} else {
			$this->db->query("INSERT INTO `{$this->P}recipe` SET " . $sql);
			$rid = (int)$this->db->getLastId();
		}
		$this->db->query("DELETE FROM `{$this->P}recipe_line` WHERE recipe_id = '" . $rid . "'");
		foreach ((isset($in['lines']) && is_array($in['lines'])) ? $in['lines'] : array() as $l) {
			$q = $this->n(isset($l['qty']) ? $l['qty'] : 0);
			$pid = (int)(isset($l['item_id']) ? $l['item_id'] : 0);
			if ($q > 0 && $pid && $pid !== (int)$item['item_id']) {
				$this->db->query("INSERT INTO `{$this->P}recipe_line` SET recipe_id = '" . $rid . "', item_id = '" . $pid . "', qty = '" . $q . "'");
			}
		}
		$this->out(array('ok' => 1));
	}

	// Costs of a batch. $in: item_id, started, passed, labour_hours, print_grams, print_hours, print_invoice, packaging_each, extra, uses[{item_id, qty}]
	private function batchCosts($in) {
		$rate = $this->cfg('rate', 25);
		$fil = $this->cfg('filament', 22);
		$mach = $this->cfg('machine', 0.4);
		$parts = 0.0;
		$uses = array();
		$short = array();
		foreach ((isset($in['uses']) && is_array($in['uses'])) ? $in['uses'] : array() as $u) {
			$it = $this->item(isset($u['item_id']) ? $u['item_id'] : 0);
			$q = $this->n(isset($u['qty']) ? $u['qty'] : 0);
			if (!$it || $q <= 0) {
				continue;
			}
			$have = $this->qtyOf($it);
			if ($have < $q) {
				$short[] = $it['name'] . ' (need ' . (0 + $q) . ', have ' . (0 + $have) . ')';
			}
			$cost = $q * (float)$it['avg_cost'];
			$parts += $cost;
			$uses[] = array('item' => $it, 'qty' => $q, 'cost' => $cost);
		}
		$printCost = $this->n(isset($in['print_grams']) ? $in['print_grams'] : 0) / 1000 * $fil + $this->n(isset($in['print_hours']) ? $in['print_hours'] : 0) * $mach + $this->n(isset($in['print_invoice']) ? $in['print_invoice'] : 0);
		$passed = max(0, (int)(isset($in['passed']) ? $in['passed'] : 0));
		$pack = $this->n(isset($in['packaging_each']) ? $in['packaging_each'] : 0) * $passed;
		$extra = $this->n(isset($in['extra']) ? $in['extra'] : 0);
		$labourH = $this->n(isset($in['labour_hours']) ? $in['labour_hours'] : 0);
		$labour = $labourH * $rate;
		$cash = $parts + $printCost + $pack + $extra;
		$full = $cash + $labour;
		return array(
			'parts' => round($parts, 2), 'print' => round($printCost, 2), 'packaging' => round($pack, 2), 'extra' => round($extra, 2), 'labour' => round($labour, 2),
			'cash' => round($cash, 2), 'full' => round($full, 2), 'unit_cash' => $passed ? round($cash / $passed, 4) : 0, 'unit_full' => $passed ? round($full / $passed, 4) : 0,
			'short' => $short, 'uses' => $uses, 'passed' => $passed, 'labour_hours' => $labourH
		);
	}

	private function f_batch_preview() {
		$c = $this->batchCosts($this->input());
		unset($c['uses']);
		$this->out($c);
	}

	private function f_save_batch() {
		$in = $this->input();
		$item = $this->item(isset($in['item_id']) ? $in['item_id'] : 0);
		if (!$item || $item['kind'] !== 'product') {
			return $this->fail('Choose the finished product the batch makes.');
		}
		$c = $this->batchCosts($in);
		if (!$c['passed']) {
			return $this->fail('How many passed the tests? At least one is needed to put it in stock.');
		}
		if ($c['short'] && empty($in['allow_short'])) {
			return $this->fail('Not enough in stock: ' . implode(', ', $c['short']) . '. Receive the invoice first, or tick "Use anyway".');
		}
		$date = $this->d(isset($in['date']) ? $in['date'] : '');
		$this->db->query("INSERT INTO `{$this->P}batch` SET item_id = '" . (int)$item['item_id'] . "', `date` = '" . $date . "', started = '" . max($c['passed'], (int)(isset($in['started']) ? $in['started'] : 0)) . "', passed = '" . $c['passed'] . "',
			parts_cost = '" . $c['parts'] . "', labour_hours = '" . $c['labour_hours'] . "', labour_cost = '" . $c['labour'] . "', print_grams = '" . $this->n(isset($in['print_grams']) ? $in['print_grams'] : 0) . "',
			print_hours = '" . $this->n(isset($in['print_hours']) ? $in['print_hours'] : 0) . "', print_cost = '" . $c['print'] . "', print_invoice = '" . $this->n(isset($in['print_invoice']) ? $in['print_invoice'] : 0) . "',
			packaging_cost = '" . $c['packaging'] . "', extra_cost = '" . $c['extra'] . "', cash_cost = '" . $c['cash'] . "', full_cost = '" . $c['full'] . "', note = '" . $this->e(isset($in['note']) ? $in['note'] : '') . "', created = NOW()");
		$bid = (int)$this->db->getLastId();
		foreach ($c['uses'] as $u) {
			$this->move($u['item'], -$u['qty'], 0, 0, 'build_use', 'batch:' . $bid, 'Used in batch #' . $bid . ' (' . $item['name'] . ')', $date . ' 12:00:00');
		}
		$item = $this->item($item['item_id']);
		$this->move($item, $c['passed'], $c['unit_cash'], $c['unit_full'], 'build_in', 'batch:' . $bid, 'Batch #' . $bid . ': ' . $c['passed'] . ' passed', $date . ' 12:00:00');
		$this->out(array('ok' => 1, 'batch_id' => $bid));
	}

	private function f_batches() {
		$this->out(array('batches' => $this->db->query("SELECT b.*, i.name FROM `{$this->P}batch` b LEFT JOIN `{$this->P}item` i ON i.item_id = b.item_id ORDER BY b.date DESC, b.batch_id DESC LIMIT 300")->rows));
	}

	// ------------------------------------------------------------------ outside sales and stocktake
	private function f_save_sale() {
		$in = $this->input();
		$item = $this->item(isset($in['item_id']) ? $in['item_id'] : 0);
		$qty = $this->n(isset($in['qty']) ? $in['qty'] : 0);
		$channel = trim(isset($in['channel']) ? $in['channel'] : '');
		if (!$item) {
			return $this->fail('Choose what you sold.');
		}
		if ($qty <= 0) {
			return $this->fail('How many did you sell?');
		}
		if ($channel === '') {
			return $this->fail('Where did you sell it? (Elecrow, Tindie, eBay, by hand …)');
		}
		$rate = max(0.00000001, $this->n(isset($in['rate']) ? $in['rate'] : 1));
		$date = $this->d(isset($in['date']) ? $in['date'] : '');
		$this->db->query("INSERT INTO `{$this->P}sale` SET channel = '" . $this->e(mb_substr($channel, 0, 64)) . "', `date` = '" . $date . "', item_id = '" . (int)$item['item_id'] . "', qty = '" . $qty . "',
			price_each = '" . round($this->n(isset($in['price_each']) ? $in['price_each'] : 0) * $rate, 4) . "', fees = '" . round($this->n(isset($in['fees']) ? $in['fees'] : 0) * $rate, 4) . "',
			shipping = '" . round($this->n(isset($in['shipping']) ? $in['shipping'] : 0) * $rate, 4) . "', unit_cost = '" . (float)$item['avg_cost'] . "', unit_full = '" . (float)$item['avg_full'] . "',
			customer = '" . $this->e(isset($in['customer']) ? $in['customer'] : '') . "', note = '" . $this->e(isset($in['note']) ? $in['note'] : '') . "', created = NOW()");
		$sid = (int)$this->db->getLastId();
		$this->move($item, -$qty, 0, 0, 'sale_out', 'sale:' . $sid, 'Sold on ' . $channel, $date . ' 12:00:00');
		$this->out(array('ok' => 1, 'sale_id' => $sid));
	}

	private function f_sales() {
		$this->out(array('sales' => $this->db->query("SELECT s.*, i.name FROM `{$this->P}sale` s LEFT JOIN `{$this->P}item` i ON i.item_id = s.item_id ORDER BY s.date DESC, s.sale_id DESC LIMIT 500")->rows));
	}

	// Stocktake: set the counted quantity; the difference is booked as an adjustment
	private function f_adjust() {
		$in = $this->input();
		$item = $this->item(isset($in['item_id']) ? $in['item_id'] : 0);
		if (!$item) {
			return $this->fail('Item not found.');
		}
		$diff = $this->n(isset($in['counted']) ? $in['counted'] : 0) - $this->qtyOf($item);
		$cost = $this->n(isset($in['unit_cost']) ? $in['unit_cost'] : 0);
		if ($diff != 0) {
			$this->move($item, $diff, $cost > 0 ? $cost : (float)$item['avg_cost'], $cost > 0 ? $cost : (float)$item['avg_full'], 'adjust', 'count', trim('Stocktake ' . (isset($in['note']) ? $in['note'] : '')));
		}
		$this->out(array('ok' => 1, 'diff' => $diff));
	}

	private function f_moves() {
		$id = (int)(isset($this->request->get['id']) ? $this->request->get['id'] : 0);
		$rows = $this->db->query("SELECT * FROM `{$this->P}move` WHERE item_id = '" . $id . "' ORDER BY `date` DESC, move_id DESC LIMIT 300")->rows;
		// shop orders of a linked product, for the same history
		$item = $this->item($id);
		$shop = array();
		if ($item && (int)$item['product_id']) {
			$in = $this->counted();
			if ($in) {
				$shop = $this->db->query("SELECT o.order_id, o.date_added, op.quantity FROM `" . DB_PREFIX . "order_product` op JOIN `" . DB_PREFIX . "order` o ON o.order_id = op.order_id
					WHERE op.product_id = '" . (int)$item['product_id'] . "' AND o.order_status_id IN (" . implode(',', $in) . ") ORDER BY o.date_added DESC LIMIT 200")->rows;
			}
		}
		$this->out(array('moves' => $rows, 'shop' => $shop));
	}

	// ------------------------------------------------------------------ reports
	private function period() {
		$from = isset($this->request->get['from']) && $this->request->get['from'] !== '' ? $this->d($this->request->get['from']) : date('Y-01-01');
		$to = isset($this->request->get['to']) && $this->request->get['to'] !== '' ? $this->d($this->request->get['to']) : date('Y-m-d');
		return array($from, $to);
	}

	private function f_report() {
		list($from, $to) = $this->period();
		$items = array();
		foreach ($this->db->query("SELECT * FROM `{$this->P}item` WHERE kind = 'product'")->rows as $i) {
			$items[(int)$i['item_id']] = $i + array('shop_q' => 0, 'shop_rev' => 0, 'out_q' => 0, 'out_rev' => 0, 'fees' => 0, 'cost' => 0, 'full' => 0);
		}
		$byPid = array();
		foreach ($items as $id => $i) {
			if ((int)$i['product_id']) {
				$byPid[(int)$i['product_id']] = $id;
			}
		}
		$channels = array();
		$in = $this->counted();
		if ($in && $byPid) {
			$rows = $this->db->query("SELECT op.product_id, SUM(op.quantity) AS q, SUM(op.total) AS rev FROM `" . DB_PREFIX . "order_product` op JOIN `" . DB_PREFIX . "order` o ON o.order_id = op.order_id
				WHERE op.product_id IN (" . implode(',', array_keys($byPid)) . ") AND o.order_status_id IN (" . implode(',', $in) . ") AND o.store_id = 0
				AND DATE(o.date_added) BETWEEN '" . $from . "' AND '" . $to . "' GROUP BY op.product_id")->rows;
			foreach ($rows as $r) {
				$id = $byPid[(int)$r['product_id']];
				$items[$id]['shop_q'] = (float)$r['q'];
				$items[$id]['shop_rev'] = (float)$r['rev'];
				// shop orders are costed at today's average cost
				$items[$id]['cost'] += (float)$r['q'] * (float)$items[$id]['avg_cost'];
				$items[$id]['full'] += (float)$r['q'] * (float)$items[$id]['avg_full'];
			}
			$shopQ = 0; $shopRev = 0; $shopCost = 0;
			foreach ($items as $i) { $shopQ += $i['shop_q']; $shopRev += $i['shop_rev']; $shopCost += $i['shop_q'] * (float)$i['avg_cost']; }
			$channels['dpcreator.eu'] = array('q' => $shopQ, 'rev' => $shopRev, 'fees' => 0, 'cost' => $shopCost);
		}
		foreach ($this->db->query("SELECT * FROM `{$this->P}sale` WHERE `date` BETWEEN '" . $from . "' AND '" . $to . "'")->rows as $s) {
			$id = (int)$s['item_id'];
			$rev = (float)$s['qty'] * (float)$s['price_each'];
			$fees = (float)$s['fees'] + (float)$s['shipping'];
			if (isset($items[$id])) {
				$items[$id]['out_q'] += (float)$s['qty'];
				$items[$id]['out_rev'] += $rev;
				$items[$id]['fees'] += $fees;
				$items[$id]['cost'] += (float)$s['qty'] * (float)$s['unit_cost'];
				$items[$id]['full'] += (float)$s['qty'] * (float)$s['unit_full'];
			}
			$ch = $s['channel'];
			if (!isset($channels[$ch])) {
				$channels[$ch] = array('q' => 0, 'rev' => 0, 'fees' => 0, 'cost' => 0);
			}
			$channels[$ch]['q'] += (float)$s['qty'];
			$channels[$ch]['rev'] += $rev;
			$channels[$ch]['fees'] += $fees;
			$channels[$ch]['cost'] += (float)$s['qty'] * (float)$s['unit_cost'];
		}
		$prod = array();
		foreach ($items as $i) {
			if (!$i['shop_q'] && !$i['out_q']) {
				continue;
			}
			$rev = $i['shop_rev'] + $i['out_rev'];
			$prod[] = array('name' => $i['name'], 'shop_q' => $i['shop_q'], 'out_q' => $i['out_q'], 'rev' => round($rev, 2), 'fees' => round($i['fees'], 2), 'cost' => round($i['cost'], 2),
				'profit' => round($rev - $i['fees'] - $i['cost'], 2), 'profit_full' => round($rev - $i['fees'] - $i['full'], 2));
		}
		$ch = array();
		foreach ($channels as $name => $c) {
			$ch[] = array('name' => $name, 'q' => $c['q'], 'rev' => round($c['rev'], 2), 'fees' => round($c['fees'], 2), 'cost' => round($c['cost'], 2), 'profit' => round($c['rev'] - $c['fees'] - $c['cost'], 2));
		}
		$spend = $this->db->query("SELECT s.name, COUNT(*) AS n, SUM(i.total_eur) AS t FROM `{$this->P}invoice` i LEFT JOIN `{$this->P}supplier` s ON s.supplier_id = i.supplier_id
			WHERE i.date BETWEEN '" . $from . "' AND '" . $to . "' GROUP BY i.supplier_id ORDER BY t DESC")->rows;
		$batches = $this->db->query("SELECT COUNT(*) AS n, COALESCE(SUM(passed), 0) AS units, COALESCE(SUM(cash_cost), 0) AS cash, COALESCE(SUM(labour_hours), 0) AS hours FROM `{$this->P}batch` WHERE `date` BETWEEN '" . $from . "' AND '" . $to . "'")->row;
		$value = 0.0;
		foreach ($this->db->query("SELECT * FROM `{$this->P}item` WHERE status = 1")->rows as $i) {
			$value += max(0, $this->qtyOf($i)) * (float)$i['avg_cost'];
		}
		$this->out(array('from' => $from, 'to' => $to, 'products' => $prod, 'channels' => $ch, 'spend' => $spend, 'batches' => $batches, 'stock_value' => round($value, 2)));
	}

	// CSV for the accountant: ?f=csv&what=invoices|sales|batches|stock
	private function f_csv() {
		list($from, $to) = $this->period();
		$what = isset($this->request->get['what']) ? $this->request->get['what'] : 'invoices';
		$rows = array();
		if ($what === 'invoices') {
			$rows[] = array('Date', 'Supplier', 'Invoice no.', 'Currency', 'Rate to EUR', 'Shipping', 'Customs', 'VAT', 'VAT reclaimable', 'Other', 'Total EUR', 'Status', 'Note');
			foreach ($this->db->query("SELECT i.*, s.name AS supplier FROM `{$this->P}invoice` i LEFT JOIN `{$this->P}supplier` s ON s.supplier_id = i.supplier_id WHERE i.date BETWEEN '" . $from . "' AND '" . $to . "' ORDER BY i.date")->rows as $r) {
				$rows[] = array($r['date'], $r['supplier'], $r['number'], $r['currency'], $r['rate'], $r['shipping'], $r['customs'], $r['vat'], $r['vat_reclaim'] ? 'yes' : 'no', $r['other'], $r['total_eur'], $r['status'], $r['note']);
			}
		} elseif ($what === 'sales') {
			$rows[] = array('Date', 'Channel', 'Product', 'Qty', 'Price each EUR', 'Revenue EUR', 'Fees EUR', 'Shipping paid EUR', 'Cost each EUR', 'Profit EUR', 'Customer', 'Note');
			foreach ($this->db->query("SELECT s.*, i.name FROM `{$this->P}sale` s LEFT JOIN `{$this->P}item` i ON i.item_id = s.item_id WHERE s.date BETWEEN '" . $from . "' AND '" . $to . "' ORDER BY s.date")->rows as $r) {
				$rev = $r['qty'] * $r['price_each'];
				$rows[] = array($r['date'], $r['channel'], $r['name'], $r['qty'], $r['price_each'], round($rev, 2), $r['fees'], $r['shipping'], $r['unit_cost'], round($rev - $r['fees'] - $r['shipping'] - $r['qty'] * $r['unit_cost'], 2), $r['customer'], $r['note']);
			}
		} elseif ($what === 'batches') {
			$rows[] = array('Date', 'Batch', 'Product', 'Started', 'Passed', 'Parts EUR', 'Printing EUR', 'Packaging EUR', 'Other EUR', 'Cash cost EUR', 'Labour hours', 'Labour EUR', 'Full cost EUR', 'Cash per unit', 'Full per unit');
			foreach ($this->db->query("SELECT b.*, i.name FROM `{$this->P}batch` b LEFT JOIN `{$this->P}item` i ON i.item_id = b.item_id WHERE b.date BETWEEN '" . $from . "' AND '" . $to . "' ORDER BY b.date")->rows as $r) {
				$rows[] = array($r['date'], $r['batch_id'], $r['name'], $r['started'], $r['passed'], $r['parts_cost'], $r['print_cost'], $r['packaging_cost'], $r['extra_cost'], $r['cash_cost'], $r['labour_hours'], $r['labour_cost'], $r['full_cost'], $r['passed'] ? round($r['cash_cost'] / $r['passed'], 2) : '', $r['passed'] ? round($r['full_cost'] / $r['passed'], 2) : '');
			}
		} else {
			$rows[] = array('Item', 'Kind', 'SKU', 'In stock', 'Unit', 'Average cost EUR', 'Value EUR');
			foreach ($this->db->query("SELECT * FROM `{$this->P}item` WHERE status = 1 ORDER BY kind, name")->rows as $r) {
				$q = $this->qtyOf($r);
				$rows[] = array($r['name'], $r['kind'], $r['sku'], $q, $r['unit'], round($r['avg_cost'], 4), round(max(0, $q) * $r['avg_cost'], 2));
			}
		}
		$fh = fopen('php://temp', 'w+');
		fwrite($fh, "\xEF\xBB\xBF");
		foreach ($rows as $r) {
			fputcsv($fh, $r);
		}
		rewind($fh);
		$csv = stream_get_contents($fh);
		$this->response->addHeader('Content-Type: text/csv; charset=utf-8');
		$this->response->addHeader('Content-Disposition: attachment; filename="dpcreator-' . $what . '-' . $from . '-' . $to . '.csv"');
		$this->response->setOutput($csv);
	}
}

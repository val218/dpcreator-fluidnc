<?php
// dpCREATOR Workbench: the admin home. Money, what is waiting for you, queues of things to fix,
// the last 30 days, latest orders, best sellers, low stock, and Ctrl+K search on every admin page.
// Reads the follow-ups, Inbox and Visitors data of the other dpCREATOR modules when they are there.
class ControllerExtensionModuleDpcDesk extends Controller {
	private $error = array();

	public function install() {
		$this->load->model('setting/setting');
		$this->model_setting_setting->editSetting('module_dpc_desk', array('module_dpc_desk_status' => 1, 'module_dpc_desk_landing' => 1));
	}

	public function uninstall() {
	}

	// Settings (Extensions > Modules > dpCREATOR Workbench)
	public function index() {
		$this->load->model('setting/setting');
		$this->document->setTitle('dpCREATOR Workbench');
		if (($this->request->server['REQUEST_METHOD'] == 'POST') && $this->validate()) {
			$this->model_setting_setting->editSetting('module_dpc_desk', array(
				'module_dpc_desk_status'  => !empty($this->request->post['module_dpc_desk_status']) ? 1 : 0,
				'module_dpc_desk_landing' => !empty($this->request->post['module_dpc_desk_landing']) ? 1 : 0
			));
			$this->session->data['success'] = 'Saved.';
			$this->response->redirect($this->url->link('marketplace/extension', 'user_token=' . $this->session->data['user_token'] . '&type=module', true));
		}
		$token = 'user_token=' . $this->session->data['user_token'];
		$data['error_warning'] = isset($this->error['warning']) ? $this->error['warning'] : '';
		$data['breadcrumbs'] = array(
			array('text' => 'Home', 'href' => $this->url->link('common/dashboard', $token, true)),
			array('text' => 'Extensions', 'href' => $this->url->link('marketplace/extension', $token . '&type=module', true)),
			array('text' => 'dpCREATOR Workbench', 'href' => $this->url->link('extension/module/dpc_desk', $token, true))
		);
		$data['action'] = $this->url->link('extension/module/dpc_desk', $token, true);
		$data['cancel'] = $this->url->link('marketplace/extension', $token . '&type=module', true);
		$data['desk'] = $this->url->link('extension/module/dpc_desk/desk', $token, true);
		$data['status'] = (int)$this->config->get('module_dpc_desk_status');
		$data['landing'] = (int)$this->config->get('module_dpc_desk_landing');
		$data['header'] = $this->load->controller('common/header');
		$data['column_left'] = $this->load->controller('common/column_left');
		$data['footer'] = $this->load->controller('common/footer');
		$this->response->setOutput($this->load->view('extension/module/dpc_desk_setting', $data));
	}

	protected function validate() {
		if (!$this->user->hasPermission('modify', 'extension/module/dpc_desk')) {
			$this->error['warning'] = 'You do not have permission to change the Workbench.';
		}
		return !$this->error;
	}

	private function hasTable($name) {
		return (bool)$this->db->query("SHOW TABLES LIKE '" . $this->db->escape(DB_PREFIX . $name) . "'")->num_rows;
	}

	private function link($route, $query = '') {
		return $this->url->link($route, 'user_token=' . $this->session->data['user_token'] . ($query !== '' ? '&' . $query : ''), true);
	}

	private function money($v) {
		return $this->currency->format((float)$v, $this->config->get('config_currency'));
	}

	// Order statuses that count as a sale: the ones chosen in dpCREATOR Sales, or else the shop's
	// processing and complete statuses without Pending and statuses that undo a sale.
	private function countedStatuses() {
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
		if ($ids) {
			$q = $this->db->query("SELECT order_status_id, name FROM `" . DB_PREFIX . "order_status` WHERE language_id = '" . (int)$this->config->get('config_language_id') . "' AND order_status_id IN (" . implode(',', array_keys($ids)) . ")");
			foreach ($q->rows as $r) {
				if (preg_match('/revers|refund|cancel|chargeback|denied|fail|void|expired/i', $r['name'])) {
					unset($ids[(int)$r['order_status_id']]);
				}
			}
		}
		return array_keys($ids);
	}

	// The Workbench page
	public function desk() {
		$this->document->setTitle('Workbench');
		$lang = (int)$this->config->get('config_language_id');
		$S = $this->countedStatuses();
		$in = $S ? implode(',', $S) : '0';
		$now = time();

		// Money: four tiles, each with the change against the period before and a small trend line
		$range = function ($from, $to) use ($in) {
			$r = $this->db->query("SELECT COUNT(*) AS n, COALESCE(SUM(total), 0) AS t FROM `" . DB_PREFIX . "order` WHERE order_status_id IN (" . $in . ") AND store_id = 0 AND date_added >= '" . $this->db->escape($from) . "' AND date_added < '" . $this->db->escape($to) . "'")->row;
			return array('n' => (int)$r['n'], 't' => (float)$r['t']);
		};
		$series = function ($fromTs, $count, $step) use ($range) {
			$out = array();
			for ($i = 0; $i < $count; $i++) {
				$a = strtotime('+' . $i . ' ' . $step, $fromTs);
				$b = strtotime('+' . ($i + 1) . ' ' . $step, $fromTs);
				$out[] = $range(date('Y-m-d H:i:s', $a), date('Y-m-d H:i:s', $b));
			}
			return $out;
		};
		$day0 = strtotime(date('Y-m-d 00:00:00', $now));
		$mon0 = strtotime(date('Y-m-01 00:00:00', $now));
		$yr0 = strtotime(date('Y-01-01 00:00:00', $now));
		$end = date('Y-m-d H:i:s', $now + 1);
		$today = $range(date('Y-m-d H:i:s', $day0), $end);
		$yday = $range(date('Y-m-d H:i:s', strtotime('-1 day', $day0)), date('Y-m-d H:i:s', strtotime('-1 day', $now + 1)));
		$month = $range(date('Y-m-d H:i:s', $mon0), $end);
		$pmonth0 = strtotime('-1 month', $mon0);
		$pmonth = $range(date('Y-m-d H:i:s', $pmonth0), date('Y-m-d H:i:s', min(strtotime('-1 month', $now + 1), $mon0)));
		$year = $range(date('Y-m-d H:i:s', $yr0), $end);
		$pyear = $range(date('Y-m-d H:i:s', strtotime('-1 year', $yr0)), date('Y-m-d H:i:s', strtotime('-1 year', $now + 1)));
		$avg = function ($x) { return $x['n'] ? $x['t'] / $x['n'] : 0.0; };
		$plural = function ($n) { return $n . ' order' . ($n == 1 ? '' : 's'); };
		// $what: "yesterday", "last month", "2025"; $kind: '' for money, 'orders' when everything is free
		$delta = function ($cur, $prev, $what, $kind = '') {
			if ($cur == $prev) {
				return array('cls' => 'flat', 'text' => 'Same as ' . $what);
			}
			if ($prev == 0) {
				return array('cls' => 'up', 'text' => ($kind ? 'Up from no ' . $kind : 'Up from 0') . ' ' . (preg_match('/^\d{4}$/', $what) ? 'in ' . $what : $what));
			}
			$p = ($cur - $prev) / $prev * 100;
			return array('cls' => abs($p) < 0.5 ? 'flat' : ($p > 0 ? 'up' : 'down'), 'text' => ($p > 0 ? '+' : '') . round($p) . '%' . ($kind ? ' ' . $kind : '') . ' vs ' . $what);
		};
		$cmp = function ($a, $b, $what) use ($delta) {
			$m = $a['t'] > 0 || $b['t'] > 0;
			return $delta($m ? $a['t'] : $a['n'], $m ? $b['t'] : $b['n'], $what, $m ? '' : 'orders');
		};
		// trend line: revenue, or orders while everything is free
		$spark = function ($pts) {
			$money = 0;
			foreach ($pts as $p) {
				$money += $p['t'];
			}
			$vals = array();
			foreach ($pts as $p) {
				$vals[] = $money > 0 ? $p['t'] : $p['n'];
			}
			$max = max(1e-9, max($vals));
			$n = count($vals);
			$line = array();
			foreach ($vals as $i => $v) {
				$line[] = round($n > 1 ? $i / ($n - 1) * 100 : 50, 2) . ',' . round(30 - $v / $max * 26, 2);
			}
			return array('line' => implode(' ', $line), 'area' => '0,32 ' . implode(' ', $line) . ' 100,32', 'what' => $money > 0 ? 'revenue' : 'orders', 'empty' => max($vals) <= 0);
		};
		$daysInMonth = (int)date('j', $now);
		$monthsInYear = (int)date('n', $now);
		$spDays = $series(strtotime('-13 days', $day0), 14, 'day');
		$spMonth = $series($mon0, $daysInMonth, 'day');
		$spYear = $series($yr0, $monthsInYear, 'month');
		$spAvg = array();
		foreach ($series(strtotime('-5 months', $mon0), 6, 'month') as $p) {
			$spAvg[] = array('t' => $avg($p), 'n' => $p['n']);
		}
		$data['stats'] = array(
			array('label' => 'Today', 'icon' => 'fa-sun-o', 'tone' => 'blue', 'value' => $this->money($today['t']), 'orders' => $plural($today['n']), 'delta' => $cmp($today, $yday, 'yesterday'), 'spark' => $spark($spDays), 'span' => 'last 14 days'),
			array('label' => 'This month', 'icon' => 'fa-calendar', 'tone' => 'green', 'value' => $this->money($month['t']), 'orders' => $plural($month['n']), 'delta' => $cmp($month, $pmonth, 'last month'), 'spark' => $spark($spMonth), 'span' => date('F', $now) . ' by day'),
			array('label' => 'This year', 'icon' => 'fa-line-chart', 'tone' => 'violet', 'value' => $this->money($year['t']), 'orders' => $plural($year['n']), 'delta' => $cmp($year, $pyear, (string)(date('Y', $now) - 1)), 'spark' => $spark($spYear), 'span' => date('Y', $now) . ' by month'),
			array('label' => 'Average order', 'icon' => 'fa-shopping-basket', 'tone' => 'amber', 'value' => $this->money($avg($month)), 'orders' => 'this month', 'delta' => $delta($avg($month), $avg($pmonth), 'last month'), 'spark' => $spark($spAvg), 'span' => 'last 6 months')
		);
		$names = array();
		if ($S) {
			foreach ($this->db->query("SELECT name FROM `" . DB_PREFIX . "order_status` WHERE language_id = '" . $lang . "' AND order_status_id IN (" . $in . ") ORDER BY name")->rows as $r) {
				$names[] = $r['name'];
			}
		}
		$data['counted'] = $names ? implode(', ', $names) : 'none set';

		// Last 30 days
		$days = array();
		for ($i = 29; $i >= 0; $i--) {
			$d = date('Y-m-d', strtotime('-' . $i . ' days', $now));
			$days[$d] = array('d' => $d, 'label' => date('j.m', strtotime($d)), 't' => 0.0, 'n' => 0);
		}
		foreach ($this->db->query("SELECT DATE(date_added) AS d, SUM(total) AS t, COUNT(*) AS n FROM `" . DB_PREFIX . "order` WHERE order_status_id IN (" . $in . ") AND store_id = 0 AND date_added >= '" . $this->db->escape(date('Y-m-d', strtotime('-29 days', $now))) . "' GROUP BY DATE(date_added)")->rows as $r) {
			if (isset($days[$r['d']])) {
				$days[$r['d']]['t'] = (float)$r['t'];
				$days[$r['d']]['n'] = (int)$r['n'];
			}
		}
		$max = 0.0;
		$total30 = 0.0;
		$n30 = 0;
		foreach ($days as $d) {
			$max = max($max, $d['t']);
			$total30 += $d['t'];
			$n30 += $d['n'];
		}
		$data['chart'] = array();
		foreach (array_values($days) as $i => $d) {
			$data['chart'][] = array('i' => $i, 'label' => $d['label'], 'h' => $max > 0 ? round($d['t'] / $max * 100, 1) : 0, 'title' => $d['label'] . ': ' . $this->money($d['t']) . ', ' . $d['n'] . ' order' . ($d['n'] == 1 ? '' : 's'), 'tick' => $i % 5 == 4 || $i == 0);
		}
		$data['chart_total'] = $this->money($total30) . ' · ' . $n30 . ' order' . ($n30 == 1 ? '' : 's');
		$data['chart_max'] = $this->money($max);

		// Waiting for you: the follow-ups list (orders, returns, approvals, reviews, R&D Club, Inbox, tickets)
		$items = array('overdue' => array(), 'waiting' => array(), 'snoozed' => array());
		if (is_file(DIR_APPLICATION . 'model/extension/dashboard/dpc_followup.php')) {
			try {
				$this->load->model('extension/dashboard/dpc_followup');
				$items = $this->model_extension_dashboard_dpc_followup->getItems();
			} catch (\Throwable $e) {
				$items = array('overdue' => array(), 'waiting' => array(), 'snoozed' => array());
			}
		}
		$data['waiting'] = array();
		foreach (array('overdue', 'waiting') as $state) {
			foreach ($items[$state] as $it) {
				$labels = array('mail' => 'Email', 'order' => 'Order', 'rnd' => 'R&D', 'ticket' => 'Ticket', 'return' => 'Return', 'review' => 'Review', 'approval' => 'Account', 'form' => 'Form');
				$data['waiting'][] = array('state' => $state, 'kind' => $it['kind'], 'kind_label' => isset($labels[$it['kind']]) ? $labels[$it['kind']] : ucfirst($it['kind']), 'title' => $it['title'], 'who' => $it['who'], 'email' => isset($it['email']) ? $it['email'] : '', 'amount' => isset($it['amount']) ? $it['amount'] : '', 'hint' => $it['hint'], 'since' => $it['since_text'], 'due' => $it['due_text'], 'link' => $it['link']);
			}
		}
		$data['waiting_count'] = count($data['waiting']);
		$data['snoozed_count'] = count($items['snoozed']);
		$data['waiting'] = array_slice($data['waiting'], 0, 8);
		$data['followup_link'] = $this->link('common/dashboard', 'classic=1');
		$kinds = array();
		foreach (array('overdue', 'waiting') as $state) {
			foreach ($items[$state] as $it) {
				$kinds[$it['kind']] = (isset($kinds[$it['kind']]) ? $kinds[$it['kind']] : 0) + 1;
			}
		}
		$k = function ($kind) use ($kinds) { return isset($kinds[$kind]) ? $kinds[$kind] : 0; };

		// Queues
		$one = function ($sql) { $r = $this->db->query($sql)->row; return (int)reset($r); };
		$P = DB_PREFIX;
		$inbox = array('reply' => 0, 'unread' => 0);
		$inboxOn = is_file(DIR_SYSTEM . 'library/dpcinbox.php') && $this->hasTable('dpc_mail_thread');
		if ($inboxOn) {
			require_once(DIR_SYSTEM . 'library/dpcinbox.php');
			$ib = new DpcInbox($this->db);
			$inbox = $ib->counts();
		}
		$q = array();
		$q[] = array('label' => 'Emails to answer', 'n' => $inbox['reply'], 'link' => $this->link('extension/module/dpc_inbox'), 'alert' => 1, 'hide' => !$inboxOn);
		$q[] = array('label' => 'Overdue', 'n' => count($items['overdue']), 'link' => $data['followup_link'], 'alert' => 1);
		$q[] = array('label' => 'Orders to handle', 'n' => $k('order'), 'link' => $this->link('sale/order'), 'alert' => 1);
		$q[] = array('label' => 'Returns', 'n' => $one("SELECT COUNT(*) FROM `{$P}return` WHERE return_status_id = '" . (int)$this->config->get('config_return_status_id') . "'"), 'link' => $this->link('sale/return'), 'alert' => 1);
		$q[] = array('label' => 'Reviews to publish', 'n' => $one("SELECT COUNT(*) FROM `{$P}review` WHERE status = 0"), 'link' => $this->link('catalog/review', 'filter_status=0'), 'alert' => 0);
		$q[] = array('label' => 'Customers to approve', 'n' => $one("SELECT COUNT(*) FROM `{$P}customer_approval`"), 'link' => $this->link('customer/customer_approval'), 'alert' => 1);
		$q[] = array('label' => 'R&D Club to review', 'n' => $k('rnd'), 'link' => HTTPS_CATALOG . 'rnd.php?f=admin', 'alert' => 1, 'hide' => !$this->hasTable('dpc_rnd_member'));
		$q[] = array('label' => 'Support tickets', 'n' => $k('ticket'), 'link' => $this->link('extension/module/dpc_support', 'status=open'), 'alert' => 1, 'hide' => !$this->hasTable('dpc_ticket'));
		$q[] = array('label' => 'Low stock (3 or less)', 'n' => $one("SELECT COUNT(*) FROM `{$P}product` WHERE status = 1 AND subtract = 1 AND shipping = 1 AND quantity <= 3"), 'link' => $this->link('catalog/product', 'sort=p.quantity&order=ASC'), 'alert' => 0);
		$q[] = array('label' => 'Shipped products without weight', 'n' => $one("SELECT COUNT(*) FROM `{$P}product` WHERE status = 1 AND shipping = 1 AND weight <= 0"), 'link' => '#dpd-noweight', 'alert' => 1);
		$q[] = array('label' => 'Products without a picture', 'n' => $one("SELECT COUNT(*) FROM `{$P}product` WHERE status = 1 AND (image IS NULL OR image = '' OR image = 'no_image.png')"), 'link' => '#dpd-noimage', 'alert' => 0);
		$q[] = array('label' => 'Unfinished checkouts (30 days)', 'n' => $one("SELECT COUNT(*) FROM `{$P}order` WHERE order_status_id = 0 AND date_added >= '" . date('Y-m-d', strtotime('-30 days', $now)) . "'"), 'link' => $this->link('sale/order', 'filter_order_status_id=0'), 'alert' => 0);
		$q[] = array('label' => 'New customers this month', 'n' => $one("SELECT COUNT(*) FROM `{$P}customer` WHERE date_added >= '" . date('Y-m-01', $now) . "'"), 'link' => $this->link('customer/customer', 'sort=c.date_added&order=DESC'), 'alert' => 0);
		$data['queues'] = array();
		foreach ($q as $x) {
			if (!empty($x['hide'])) {
				continue;
			}
			$x['cls'] = $x['n'] ? ($x['alert'] ? 'red' : 'on') : '';
			$x['open'] = substr($x['link'], 0, 1) === '#' ? substr($x['link'], 1) : '';
			$data['queues'][] = $x;
		}
		$data['inbox'] = $inbox;
		$data['inbox_on'] = $inboxOn;
		$data['inbox_link'] = $this->link('extension/module/dpc_inbox');

		// Products to fix (shown under the queues)
		$fix = function ($where) use ($P, $lang) {
			$out = array();
			foreach ($this->db->query("SELECT p.product_id, p.model, pd.name FROM `{$P}product` p LEFT JOIN `{$P}product_description` pd ON pd.product_id = p.product_id AND pd.language_id = '" . $lang . "' WHERE " . $where . " ORDER BY pd.name LIMIT 12")->rows as $r) {
				$out[] = array('name' => $r['name'] ?: $r['model'], 'model' => $r['model'], 'link' => $this->link('catalog/product/edit', 'product_id=' . (int)$r['product_id']));
			}
			return $out;
		};
		$data['noweight'] = $fix("p.status = 1 AND p.shipping = 1 AND p.weight <= 0");
		$data['noimage'] = $fix("p.status = 1 AND (p.image IS NULL OR p.image = '' OR p.image = 'no_image.png')");

		// Latest orders
		$data['orders'] = array();
		foreach ($this->db->query("SELECT o.order_id, o.firstname, o.lastname, o.email, o.total, o.currency_code, o.currency_value, o.date_added, os.name AS status FROM `{$P}order` o LEFT JOIN `{$P}order_status` os ON os.order_status_id = o.order_status_id AND os.language_id = '" . $lang . "' WHERE o.order_status_id > 0 ORDER BY o.order_id DESC LIMIT 6")->rows as $r) {
			$data['orders'][] = array('id' => (int)$r['order_id'], 'who' => trim($r['firstname'] . ' ' . $r['lastname']) ?: $r['email'], 'total' => $this->currency->format($r['total'], $r['currency_code'], $r['currency_value']), 'status' => $r['status'], 'date' => date('j M H:i', strtotime($r['date_added'])), 'link' => $this->link('sale/order/info', 'order_id=' . (int)$r['order_id']));
		}
		$data['orders_link'] = $this->link('sale/order');

		// Best sellers, 30 days
		$data['popular'] = array();
		foreach ($this->db->query("SELECT op.product_id, op.name, SUM(op.quantity) AS q, SUM(op.total + op.tax * op.quantity) AS t FROM `{$P}order_product` op JOIN `{$P}order` o ON o.order_id = op.order_id WHERE o.order_status_id IN (" . $in . ") AND o.date_added >= '" . date('Y-m-d', strtotime('-30 days', $now)) . "' GROUP BY op.product_id, op.name ORDER BY q DESC LIMIT 6")->rows as $r) {
			$data['popular'][] = array('name' => $r['name'], 'q' => (int)$r['q'], 't' => $this->money($r['t']), 'link' => $this->link('catalog/product/edit', 'product_id=' . (int)$r['product_id']));
		}

		// Low stock
		$data['lowstock'] = array();
		foreach ($this->db->query("SELECT p.product_id, p.model, p.quantity, pd.name FROM `{$P}product` p LEFT JOIN `{$P}product_description` pd ON pd.product_id = p.product_id AND pd.language_id = '" . $lang . "' WHERE p.status = 1 AND p.subtract = 1 AND p.shipping = 1 AND p.quantity <= 3 ORDER BY p.quantity ASC, pd.name LIMIT 8")->rows as $r) {
			$data['lowstock'][] = array('name' => $r['name'] ?: $r['model'], 'q' => (int)$r['quantity'], 'cls' => (int)$r['quantity'] <= 0 ? 'red' : 'amb', 'link' => $this->link('catalog/product/edit', 'product_id=' . (int)$r['product_id']));
		}

		// Visitors
		$data['visitors'] = null;
		if (is_file(DIR_SYSTEM . 'library/dpcvisit.php')) {
			require_once(DIR_SYSTEM . 'library/dpcvisit.php');
			$lib = new DpcVisit($this->db);
			if ($lib->ready()) {
				$v = array('now' => 0, 'today' => 0, 'customers' => 0, 'carts' => 0, 'bots' => 0);
				$cut = date('Y-m-d H:i:s', strtotime('-15 minutes'));
				foreach ($lib->visits(date('Y-m-d 00:00:00'), 3000) as $row) {
					list($kind) = DpcVisit::classify($row);
					if ($kind === 'human' || $kind === 'customer') {
						$v['today']++;
						if ($row['last_seen'] >= $cut) {
							$v['now']++;
						}
						if ((int)$row['cart']) {
							$v['carts']++;
						}
					}
					if ($kind === 'customer') {
						$v['customers']++;
					}
					if ($kind === 'bot' || $kind === 'likely_bot') {
						$v['bots']++;
					}
				}
				$v['link'] = $this->link('extension/module/dpc_visitors');
				$data['visitors'] = $v;
			}
		}

		// Dashboard widgets shown as they are on the classic dashboard
		$data['widgets'] = array();
		foreach (array('dpc_sales', 'dpc_devices', 'dpc_visitors_list') as $code) {
			if ($this->config->get('dashboard_' . $code . '_status') && is_file(DIR_APPLICATION . 'controller/extension/dashboard/' . $code . '.php') && $this->user->hasPermission('access', 'extension/dashboard/' . $code)) {
				try {
					$html = $this->load->controller('extension/dashboard/' . $code . '/dashboard');
				} catch (\Throwable $e) {
					$html = '';
				}
				if ($html) {
					$data['widgets'][] = array('code' => $code, 'html' => $html);
				}
			}
		}

		$data['classic'] = $this->link('common/dashboard', 'classic=1');
		$data['settings'] = $this->link('extension/module/dpc_desk');
		$data['store'] = HTTPS_CATALOG;
		$data['username'] = $this->user->getUserName();
		$data['hello'] = (int)date('G') < 12 ? 'Good morning' : ((int)date('G') < 18 ? 'Good afternoon' : 'Good evening');
		$data['date'] = date('l, j F Y');
		$data['header'] = $this->load->controller('common/header');
		$data['column_left'] = $this->load->controller('common/column_left');
		$data['footer'] = $this->load->controller('common/footer');
		$this->response->setOutput($this->load->view('extension/module/dpc_desk', $data));
	}

	// Ctrl+K search: orders, customers, products, pages and Inbox conversations
	public function search() {
		$json = array();
		$q = isset($this->request->get['q']) ? trim((string)$this->request->get['q']) : '';
		if ($q !== '' && $this->user->hasPermission('access', 'extension/module/dpc_desk')) {
			$P = DB_PREFIX;
			$lang = (int)$this->config->get('config_language_id');
			$like = "'%" . $this->db->escape(addcslashes($q, '%_\\')) . "%'";
			$num = ctype_digit($q) ? (int)$q : 0;
			$add = function ($group, $title, $sub, $href) use (&$json) {
				$json[] = array('group' => $group, 'title' => html_entity_decode((string)$title, ENT_QUOTES, 'UTF-8'), 'sub' => html_entity_decode((string)$sub, ENT_QUOTES, 'UTF-8'), 'href' => html_entity_decode($href, ENT_QUOTES, 'UTF-8'));
			};
			if ($this->user->hasPermission('access', 'sale/order')) {
				$rows = $this->db->query("SELECT o.order_id, o.firstname, o.lastname, o.email, o.total, o.currency_code, o.currency_value, o.date_added, os.name AS status FROM `{$P}order` o LEFT JOIN `{$P}order_status` os ON os.order_status_id = o.order_status_id AND os.language_id = '" . $lang . "' WHERE o.order_status_id > 0 AND (" . ($num ? "o.order_id = '" . $num . "' OR " : '') . "CONCAT(o.firstname, ' ', o.lastname) LIKE " . $like . " OR o.email LIKE " . $like . " OR o.telephone LIKE " . $like . ") ORDER BY o.order_id DESC LIMIT 6")->rows;
				foreach ($rows as $r) {
					$add('Orders', 'Order #' . $r['order_id'] . ' · ' . (trim($r['firstname'] . ' ' . $r['lastname']) ?: $r['email']), $r['status'] . ' · ' . $this->currency->format($r['total'], $r['currency_code'], $r['currency_value']) . ' · ' . date('j M Y', strtotime($r['date_added'])), $this->link('sale/order/info', 'order_id=' . (int)$r['order_id']));
				}
			}
			if ($this->user->hasPermission('access', 'customer/customer')) {
				$rows = $this->db->query("SELECT customer_id, firstname, lastname, email, date_added FROM `{$P}customer` WHERE " . ($num ? "customer_id = '" . $num . "' OR " : '') . "CONCAT(firstname, ' ', lastname) LIKE " . $like . " OR email LIKE " . $like . " OR telephone LIKE " . $like . " ORDER BY customer_id DESC LIMIT 5")->rows;
				foreach ($rows as $r) {
					$add('Customers', trim($r['firstname'] . ' ' . $r['lastname']) ?: $r['email'], $r['email'] . ' · since ' . date('j M Y', strtotime($r['date_added'])), $this->link('customer/customer/edit', 'customer_id=' . (int)$r['customer_id']));
				}
			}
			if ($this->user->hasPermission('access', 'catalog/product')) {
				$rows = $this->db->query("SELECT p.product_id, p.model, p.quantity, p.price, p.status, pd.name FROM `{$P}product` p LEFT JOIN `{$P}product_description` pd ON pd.product_id = p.product_id AND pd.language_id = '" . $lang . "' WHERE " . ($num ? "p.product_id = '" . $num . "' OR " : '') . "pd.name LIKE " . $like . " OR p.model LIKE " . $like . " OR p.sku LIKE " . $like . " ORDER BY p.status DESC, pd.name LIMIT 6")->rows;
				foreach ($rows as $r) {
					$add('Products', $r['name'] ?: $r['model'], $r['model'] . ' · ' . $this->money($r['price']) . ' · stock ' . (int)$r['quantity'] . ($r['status'] ? '' : ' · disabled'), $this->link('catalog/product/edit', 'product_id=' . (int)$r['product_id']));
				}
			}
			if ($this->user->hasPermission('access', 'catalog/information')) {
				$rows = $this->db->query("SELECT i.information_id, id.title FROM `{$P}information` i LEFT JOIN `{$P}information_description` id ON id.information_id = i.information_id AND id.language_id = '" . $lang . "' WHERE id.title LIKE " . $like . " LIMIT 4")->rows;
				foreach ($rows as $r) {
					$add('Pages', $r['title'], 'Information page #' . $r['information_id'], $this->link('catalog/information/edit', 'information_id=' . (int)$r['information_id']));
				}
			}
			if ($this->hasTable('dpc_mail_thread') && $this->user->hasPermission('access', 'extension/module/dpc_inbox')) {
				$rows = $this->db->query("SELECT thread_id, subject, contact_name, contact_email, last_in, last_out FROM `{$P}dpc_mail_thread` WHERE subject LIKE " . $like . " OR contact_name LIKE " . $like . " OR contact_email LIKE " . $like . " ORDER BY GREATEST(COALESCE(last_in, 0), COALESCE(last_out, 0)) DESC LIMIT 5")->rows;
				foreach ($rows as $r) {
					$add('Inbox', $r['subject'] !== '' ? $r['subject'] : '(no subject)', ($r['contact_name'] ?: $r['contact_email']) . ' · ' . $r['contact_email'], $this->link('extension/module/dpc_inbox', 'thread_id=' . (int)$r['thread_id']));
				}
			}
		}
		$this->response->addHeader('Content-Type: application/json');
		$this->response->setOutput(json_encode($json));
	}
}

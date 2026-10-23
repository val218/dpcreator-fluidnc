<?php
$u = parse_url($_SERVER['REQUEST_URI']);
if (preg_match('#^/view/javascript/(.+)$#', $u['path'], $m)) { header('Content-Type: application/javascript'); readfile('/tmp/claude-0/scr/stock/upload/admin/view/javascript/'.$m[1]); return; }
if ($u['path'] === '/' ) { readfile(__DIR__.'/page.html'); return; }
if ($u['path'] === '/index.php') {
  define('DB_PREFIX', 'oc_'); define('DIR_UPLOAD', __DIR__ . '/upl/'); define('DIR_APPLICATION', '/tmp/claude-0/scr/stock/upload/admin/'); define('HTTPS_CATALOG','https://x/');
  $src = file_get_contents('harness.php'); $src = preg_replace('/^<\?php/', '', $src);
  $src = substr($src, 0, strpos($src, '$GLOBALS[\'INPUT\']=\'\';'));
  $src = str_replace(array("define('DB_PREFIX', 'oc_'); define('DIR_UPLOAD', __DIR__ . '/upl/'); define('DIR_APPLICATION', '/tmp/claude-0/scr/stock/upload/admin/'); define('HTTPS_CATALOG','https://x/');"), '', $src);
  eval($src);
  $req->get = $_GET; $req->files = $_FILES; $req->post = $_POST;
  $c = new ControllerExtensionModuleDpcStock(null);
  $route = $_GET['route'];
  if (substr($route, -4) === 'file') $c->file(); else $c->api();
  foreach (array() as $h) {}
  if (isset($_GET['f']) && $_GET['f'] === 'csv') header('Content-Type: text/csv'); else header('Content-Type: application/json');
  echo $res->out; return;
}
http_response_code(404);

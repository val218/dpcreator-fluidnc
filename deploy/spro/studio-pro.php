<?php
/**
 * dpCREATOR Studio Pro: licences, hosted Pro app and Pro board installs.
 * Upload to the web root of dpcreator.eu (next to index.php, studio.php and webui.php).
 *
 * The Pro build comes from the release "studio-latest" of the private repo val218/dpcreator-studio
 * (studio-pro-web.zip, studio-pro-index.html, version.txt), fetched server-side with the read-only
 * FW_GITHUB_TOKEN from fw-config.php. Pro files are only served to customers with Pro.
 *
 *   GET  /studio-pro.php?f=access            licence state of the logged-in customer (JSON)
 *   GET  /studio-pro.php?f=licence           the customer's licence as a text file
 *   POST /studio-pro.php?f=email             e-mail the licence to the account address
 *   POST /studio-pro.php?f=install {"board":"84F703A1B2C4"}
 *                                            Pro one-file copy, licensed to that board, gzipped
 *                                            (same protocol as studio.php?f=install)
 *   GET  /studio-pro.php?f=version           build of the Pro release (public)
 *   GET  /studio-pro-app/...                 hosted Pro app (via studio-pro-app/.htaccess -> f=app)
 *   GET  /studio-pro.php?f=sample            shop admins: sample licences for testing
 *   GET  /studio-pro.php?f=diag              shop admins: setup check
 *
 * Licence (same key and format as the Studio and WebUI licences; ECDSA P-256, SHA-256 over
 * everything before "sig=", including the final newline; signature base64 DER):
 *   DPCL1
 *   product=studio-pro
 *   id=<board ID, or order-<order_id> for the account copy>
 *   type=pro
 *   issued=YYYY-MM-DD
 *   to=<customer name>
 *   expires=YYYY-MM-DD     (only for time-limited plans)
 *   updates=YYYY-MM-DD     (only for the one-time plan: updates included until this date)
 *   sig=<base64>
 *
 * Who has Pro: completed orders (OpenCart "Complete Order Status") of the products below, matched
 * by MODEL code, so prices and product IDs can change freely in the admin.
 */

// Plans by product model code: months of Pro (0 = no end), months of updates (one-time plan only).
$PLANS = [
    'STUDIO-PRO-M' => ['pro' => 1,  'updates' => 0,  'label' => 'monthly'],
    'STUDIO-PRO-Y' => ['pro' => 12, 'updates' => 0,  'label' => 'yearly'],
    'STUDIO-PRO-L' => ['pro' => 0,  'updates' => 12, 'label' => 'one-time'],
    'STUDIO-PRO-U' => ['pro' => -1, 'updates' => 12, 'label' => 'update year'],   // extends updates of a one-time licence
];
// dpCREATOR hardware owners: Pro included for this many months after the order (0 = off, -1 = no end).
const HW_PRO_MONTHS = 0;
$HW_PRODUCTS = [381, 380, 382];   // R3 controller, R3 + SmartTabUI combo, assembled pendant

const STUDIO_REPO = 'val218/dpcreator-studio';
const STUDIO_TAG  = 'studio-latest';
const PRO_HTML    = 'studio-pro-index.html';
const PRO_ZIP     = 'studio-pro-web.zip';
const CHECK_EVERY = 600;          // seconds between checks for a new build
const MAX_FILE    = 33554432;     // 32 MB
const MAX_INSTALLS_PER_HOUR = 20;
const BUY_URL     = '/studio-pro';
const APP_PATH    = '/studio-pro-app/';

if (is_file(__DIR__ . '/fw-config.php')) {
    require_once __DIR__ . '/fw-config.php';
}
if (!defined('FW_GITHUB_TOKEN')) {
    define('FW_GITHUB_TOKEN', getenv('FW_GITHUB_TOKEN') ?: '');
}
$cacheDir = __DIR__ . '/fw-cache/studio-pro';

header('X-Content-Type-Options: nosniff');

function json_out($code, $data)
{
    http_response_code($code);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: private, no-store');
    echo json_encode($data, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    exit;
}

// ---------------------------------------------------------------------------
// Shop database and session

function oc_db()
{
    if (isset($GLOBALS['oc_db'])) {
        return $GLOBALS['oc_db'];
    }
    $cfg = getenv('STUDIO_PRO_CONFIG') ?: __DIR__ . '/config.php';
    if (!is_file($cfg)) {
        return null;
    }
    require_once $cfg;
    $db = @new mysqli(DB_HOSTNAME, DB_USERNAME, DB_PASSWORD, DB_DATABASE, (int)(defined('DB_PORT') ? DB_PORT : 3306));
    if ($db->connect_errno) {
        return null;
    }
    $db->set_charset('utf8mb4');
    $GLOBALS['oc_db'] = $db;
    return $db;
}

function setting($key, $default = '')
{
    $st = oc_db()->prepare("SELECT `value`, `serialized` FROM `" . DB_PREFIX . "setting` WHERE `store_id` = 0 AND `key` = ? LIMIT 1");
    $st->bind_param('s', $key);
    $st->execute();
    $st->bind_result($v, $ser);
    $ok = $st->fetch();
    $st->close();
    if (!$ok || $v === '') {
        return $default;
    }
    return $ser ? json_decode($v, true) : $v;
}

function session_data()
{
    static $s = null;
    if ($s !== null) {
        return $s;
    }
    $s = [];
    $name = setting('config_session_name', 'OCSESSID');
    $sid = isset($_COOKIE[$name]) ? $_COOKIE[$name] : '';
    if (!preg_match('/^[A-Za-z0-9,-]{20,64}$/', $sid)) {
        return $s;
    }
    $data = null;
    $st = oc_db()->prepare("SELECT `data` FROM `" . DB_PREFIX . "session` WHERE `session_id` = ? LIMIT 1");
    $st->bind_param('s', $sid);
    $st->execute();
    $st->bind_result($raw);
    if ($st->fetch()) {
        $data = $raw;
    }
    $st->close();
    if ($data === null && defined('DIR_STORAGE') && is_file(DIR_STORAGE . 'session/sess_' . $sid)) {
        $data = file_get_contents(DIR_STORAGE . 'session/sess_' . $sid);
    }
    if ($data !== null) {
        $d = json_decode($data, true);
        if (!is_array($d)) {
            $d = @unserialize($data, ['allowed_classes' => false]);
        }
        if (is_array($d)) {
            $s = $d;
        }
    }
    return $s;
}

function customer_id()
{
    $s = session_data();
    return !empty($s['customer_id']) ? (int)$s['customer_id'] : 0;
}

function is_admin()
{
    $s = session_data();
    if (empty($s['user_id'])) {
        return false;
    }
    $r = oc_db()->query("SELECT `status` FROM `" . DB_PREFIX . "user` WHERE `user_id` = " . (int)$s['user_id'] . " LIMIT 1");
    $row = $r ? $r->fetch_assoc() : null;
    return $row && (int)$row['status'] === 1;
}

function customer($cid)
{
    $r = oc_db()->query("SELECT `firstname`, `lastname`, `email` FROM `" . DB_PREFIX . "customer` WHERE `customer_id` = " . (int)$cid . " LIMIT 1");
    return $r ? $r->fetch_assoc() : null;
}

function ensure_tables()
{
    $db = oc_db();
    $db->query("CREATE TABLE IF NOT EXISTS `" . DB_PREFIX . "dpc_studio_pro_licence` (
        `customer_id` INT NOT NULL,
        `scope` VARCHAR(40) NOT NULL,
        `licence` TEXT NOT NULL,
        `terms` VARCHAR(80) NOT NULL,
        `created` DATETIME NOT NULL,
        PRIMARY KEY (`customer_id`, `scope`)
    ) DEFAULT CHARSET=utf8mb4");
    $db->query("CREATE TABLE IF NOT EXISTS `" . DB_PREFIX . "dpc_studio_pro_event` (
        `customer_id` INT NOT NULL,
        `event` VARCHAR(16) NOT NULL,
        `created` DATETIME NOT NULL,
        KEY `ce` (`customer_id`, `event`, `created`)
    ) DEFAULT CHARSET=utf8mb4");
}

// ---------------------------------------------------------------------------
// Who has Pro, and until when

function add_months($date, $months)
{
    $d = new DateTime(substr($date, 0, 10), new DateTimeZone('UTC'));
    $d->modify('+' . (int)$months . ' month');
    return $d->format('Y-m-d');
}

/**
 * Pro rights of one customer from their completed orders:
 * ['pro' => bool, 'expires' => 'Y-m-d'|'' (no end), 'updates' => 'Y-m-d'|'', 'plan' => label, 'order_id' => int]
 */
function entitlement($cid)
{
    global $PLANS, $HW_PRODUCTS;
    $db = oc_db();
    $complete = setting('config_complete_status', []);
    $complete = array_values(array_filter(array_map('intval', is_array($complete) ? $complete : [$complete])));
    $none = ['pro' => false, 'expires' => '', 'updates' => '', 'plan' => '', 'order_id' => 0];
    if (!$cid || !$complete) {
        return $none;
    }
    $models = array_map(function ($m) use ($db) { return "'" . $db->real_escape_string($m) . "'"; }, array_keys($PLANS));
    $hw = implode(',', array_map('intval', $HW_PRODUCTS)) ?: '0';
    $sql = "SELECT o.order_id, DATE(o.date_added) AS d, op.product_id, p.model, op.quantity
              FROM `" . DB_PREFIX . "order` o
              JOIN `" . DB_PREFIX . "order_product` op ON op.order_id = o.order_id
              LEFT JOIN `" . DB_PREFIX . "product` p ON p.product_id = op.product_id
             WHERE (o.customer_id = " . (int)$cid . "
                    OR (o.customer_id = 0 AND o.email <> '' AND o.email = (SELECT c.email FROM `" . DB_PREFIX . "customer` c WHERE c.customer_id = " . (int)$cid . ")))
               AND o.order_status_id IN (" . implode(',', $complete) . ")
               AND (p.model IN (" . implode(',', $models) . ") OR op.product_id IN ($hw))
             ORDER BY o.date_added ASC";
    $r = $db->query($sql);
    $today = gmdate('Y-m-d');
    $proEnd = null;          // null = none yet, '' = no end
    $updEnd = '';
    $plan = '';
    $orderId = 0;
    $lifetime = false;
    while ($r && ($row = $r->fetch_assoc())) {
        $qty = max(1, (int)$row['quantity']);
        $p = isset($PLANS[$row['model']]) ? $PLANS[$row['model']] : null;
        if (!$p && in_array((int)$row['product_id'], array_map('intval', $HW_PRODUCTS), true)) {
            if (HW_PRO_MONTHS === 0) {
                continue;
            }
            $p = ['pro' => HW_PRO_MONTHS === -1 ? 0 : HW_PRO_MONTHS, 'updates' => 0, 'label' => 'with dpCREATOR hardware'];
        }
        if (!$p) {
            continue;
        }
        if ($p['pro'] === -1) {                       // update year for a one-time licence
            if ($lifetime) {
                $from = max($updEnd, $row['d']);
                $updEnd = add_months($from, $p['updates'] * $qty);
            }
            continue;
        }
        if ($p['pro'] === 0) {                        // no end
            $lifetime = $lifetime || $p['updates'] > 0;
            $proEnd = '';
            if ($p['updates'] > 0) {
                $updEnd = max($updEnd, add_months($row['d'], $p['updates']));
            }
        } elseif ($proEnd !== '') {                   // time-limited: stacks on top of an active period
            $from = ($proEnd !== null && $proEnd > $row['d']) ? $proEnd : $row['d'];
            $proEnd = add_months($from, $p['pro'] * $qty);
        }
        if ($plan === '' || $p['pro'] === 0) {
            $plan = $p['label'];
        }
        $orderId = (int)$row['order_id'];
    }
    if ($proEnd === null || ($proEnd !== '' && $proEnd < $today)) {
        $out = $none;
        $out['expired'] = $proEnd !== null ? $proEnd : '';
        return $out;
    }
    if (!$lifetime) {
        $updEnd = '';                                 // subscriptions get every update while active
    }
    return ['pro' => true, 'expires' => $proEnd, 'updates' => $updEnd, 'plan' => $plan, 'order_id' => $orderId];
}

// ---------------------------------------------------------------------------
// Signing (same key as webui.php / fw.php)

function signing_key()
{
    $outside = dirname(__DIR__) . '/dpc-licence-key.pem';
    $path = is_file($outside) ? $outside : __DIR__ . '/fw-cache/dpc-licence-key.pem';
    return is_file($path) ? openssl_pkey_get_private(file_get_contents($path)) : false;
}

function clean_field($v)
{
    return trim(preg_replace('/[\x00-\x1F\x7F]+/', ' ', (string)$v));
}

/** Signed licence text, or null when the key is missing. */
function make_licence($id, $to, $expires, $updates, $issued = null)
{
    $key = signing_key();
    if (!$key) {
        return null;
    }
    $body = "DPCL1\nproduct=studio-pro\nid=" . clean_field($id) . "\ntype=pro\nissued=" . ($issued ?: gmdate('Y-m-d'))
          . "\nto=" . clean_field($to) . "\n";
    if ($expires !== '') {
        $body .= "expires=$expires\n";
    }
    if ($updates !== '') {
        $body .= "updates=$updates\n";
    }
    if (!openssl_sign($body, $sig, $key, OPENSSL_ALGO_SHA256)) {
        return null;
    }
    return $body . 'sig=' . base64_encode($sig) . "\n";
}

/** The customer's licence for a scope ("account" or a board ID), reused while the terms stay the same. */
function licence_for($cid, $ent, $scope)
{
    $db = oc_db();
    $terms = 'e' . $ent['expires'] . '|u' . $ent['updates'];
    $st = $db->prepare("SELECT `licence`, `terms` FROM `" . DB_PREFIX . "dpc_studio_pro_licence` WHERE `customer_id` = ? AND `scope` = ? LIMIT 1");
    $st->bind_param('is', $cid, $scope);
    $st->execute();
    $st->bind_result($lic, $t);
    $found = $st->fetch();
    $st->close();
    if ($found && $t === $terms) {
        return $lic;
    }
    $c = customer($cid);
    $to = $c ? trim($c['firstname'] . ' ' . $c['lastname']) : '';
    $id = $scope === 'account' ? 'order-' . $ent['order_id'] : $scope;
    $lic = make_licence($id, $to, $ent['expires'], $ent['updates']);
    if ($lic === null) {
        return null;
    }
    $st = $db->prepare("REPLACE INTO `" . DB_PREFIX . "dpc_studio_pro_licence` (`customer_id`, `scope`, `licence`, `terms`, `created`) VALUES (?, ?, ?, ?, NOW())");
    $st->bind_param('isss', $cid, $scope, $lic, $terms);
    $st->execute();
    return $lic;
}

function inject_licence($html, $lic)
{
    $tag = '<script>window.DPC_STUDIO_LICENSE=' . json_encode($lic) . '</script>';
    $n = 0;
    $out = preg_replace('/<head[^>]*>/i', '$0' . $tag, $html, 1, $n);
    return $n ? $out : $tag . $html;
}

// ---------------------------------------------------------------------------
// The Pro build from GitHub (cached)

function http_get($url, array $headers, $max, &$location = null)
{
    $ch = curl_init($url);
    curl_setopt_array($ch, [
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_FOLLOWLOCATION => false,
        CURLOPT_CONNECTTIMEOUT => 10,
        CURLOPT_TIMEOUT        => 90,
        CURLOPT_USERAGENT      => 'dpcreator-studio-pro',
        CURLOPT_HTTPHEADER     => $headers,
        CURLOPT_PROTOCOLS      => CURLPROTO_HTTPS,
    ]);
    $data = curl_exec($ch);
    $code = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    $location = curl_getinfo($ch, CURLINFO_REDIRECT_URL);
    curl_close($ch);
    if ($code >= 300 && $code < 400) {
        return false;
    }
    return ($data === false || $code !== 200 || strlen($data) > $max) ? null : $data;
}

function gh_assets()
{
    static $rel = null;
    if ($rel !== null) {
        return $rel;
    }
    $rel = [];
    if (FW_GITHUB_TOKEN === '') {
        return $rel;
    }
    $j = http_get('https://api.github.com/repos/' . STUDIO_REPO . '/releases/tags/' . rawurlencode(STUDIO_TAG),
        ['Authorization: Bearer ' . FW_GITHUB_TOKEN, 'X-GitHub-Api-Version: 2022-11-28', 'Accept: application/vnd.github+json'], 1048576);
    $j = $j ? json_decode($j, true) : null;
    foreach ((array)(isset($j['assets']) ? $j['assets'] : []) as $a) {
        $rel[$a['name']] = (int)$a['id'];
    }
    return $rel;
}

function gh_asset($name, $max)
{
    $a = gh_assets();
    if (empty($a[$name])) {
        return null;
    }
    $loc = null;
    $auth = ['Authorization: Bearer ' . FW_GITHUB_TOKEN, 'X-GitHub-Api-Version: 2022-11-28', 'Accept: application/octet-stream'];
    $data = http_get('https://api.github.com/repos/' . STUDIO_REPO . '/releases/assets/' . $a[$name], $auth, $max, $loc);
    if ($data === false && $loc) {
        $data = http_get($loc, [], $max);
    }
    return $data ?: null;
}

function remove_tree($dir)
{
    if (!is_dir($dir)) {
        return;
    }
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($dir, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $f) {
        $f->isDir() ? @rmdir($f->getPathname()) : @unlink($f->getPathname());
    }
    @rmdir($dir);
}

/** Keep the Pro build cached: one-file copy + unpacked web app. Returns meta. */
function pro_build($cacheDir, $force = false)
{
    if (!is_dir($cacheDir)) {
        @mkdir($cacheDir, 0755, true);
    }
    $deny = dirname($cacheDir) . '/.htaccess';
    if (!is_file($deny)) {
        @file_put_contents($deny, "Require all denied\nDeny from all\n");
    }
    $metaPath = "$cacheDir/meta.json";
    $meta = is_file($metaPath) ? (array)json_decode(file_get_contents($metaPath), true) : [];
    $now = time();
    if (!$force && isset($meta['checked']) && $now - $meta['checked'] < CHECK_EVERY && is_file("$cacheDir/one.html")) {
        return $meta;
    }
    $lock = fopen("$metaPath.lock", 'c');
    if (!$lock || !flock($lock, LOCK_EX | LOCK_NB)) {
        return $meta;
    }
    $version = gh_asset('version.txt', 4096);
    $version = $version !== null ? trim($version) : (isset($meta['version']) ? $meta['version'] : '');
    if ($version !== '' && ($force || !is_file("$cacheDir/one.html") || !is_dir("$cacheDir/web") || (isset($meta['version']) ? $meta['version'] : '') !== $version)) {
        $html = gh_asset(PRO_HTML, MAX_FILE);
        $zip = gh_asset(PRO_ZIP, MAX_FILE);
        if ($html !== null && $zip !== null && class_exists('ZipArchive')) {
            $tmpZip = "$cacheDir/web.zip.tmp";
            file_put_contents($tmpZip, $zip);
            $za = new ZipArchive();
            $newDir = "$cacheDir/web.new";
            remove_tree($newDir);
            if ($za->open($tmpZip) === true && @mkdir($newDir, 0755) && $za->extractTo($newDir)) {
                $za->close();
                remove_tree("$cacheDir/web.old");
                if (is_dir("$cacheDir/web")) {
                    rename("$cacheDir/web", "$cacheDir/web.old");
                }
                rename($newDir, "$cacheDir/web");
                remove_tree("$cacheDir/web.old");
                file_put_contents("$cacheDir/one.html.tmp", $html);
                rename("$cacheDir/one.html.tmp", "$cacheDir/one.html");
                $meta['version'] = $version;
                $meta['updated'] = $now;
            }
            @unlink($tmpZip);
        }
    }
    $meta['checked'] = $now;
    file_put_contents($metaPath, json_encode($meta));
    flock($lock, LOCK_UN);
    fclose($lock);
    return $meta;
}

function gz_for_board($html)
{
    // XModem pads with Ctrl-Z (0x1A) and the board strips trailing 0x1A bytes: the gzip must not end with one.
    for ($i = 0; $i < 8; $i++) {
        $gz = gzencode($html, 9);
        if ($gz !== false && substr($gz, -1) !== "\x1A") {
            return $gz;
        }
        $html .= ' ';
    }
    return null;
}

function count_events($cid, $event, $seconds)
{
    $r = oc_db()->query("SELECT COUNT(*) AS n FROM `" . DB_PREFIX . "dpc_studio_pro_event` WHERE `customer_id` = " . (int)$cid
        . " AND `event` = '" . oc_db()->real_escape_string($event) . "' AND `created` > NOW() - INTERVAL " . (int)$seconds . " SECOND");
    return $r ? (int)$r->fetch_assoc()['n'] : 0;
}

function log_event($cid, $event)
{
    $st = oc_db()->prepare("INSERT INTO `" . DB_PREFIX . "dpc_studio_pro_event` (`customer_id`, `event`, `created`) VALUES (?, ?, NOW())");
    $st->bind_param('is', $cid, $event);
    $st->execute();
}

// ---------------------------------------------------------------------------
// Hosted Pro app

function mime_for($path)
{
    $ext = strtolower(pathinfo($path, PATHINFO_EXTENSION));
    $map = ['html' => 'text/html; charset=utf-8', 'js' => 'text/javascript; charset=utf-8', 'mjs' => 'text/javascript; charset=utf-8',
        'css' => 'text/css; charset=utf-8', 'json' => 'application/json', 'webmanifest' => 'application/manifest+json',
        'png' => 'image/png', 'jpg' => 'image/jpeg', 'jpeg' => 'image/jpeg', 'svg' => 'image/svg+xml', 'ico' => 'image/x-icon',
        'webp' => 'image/webp', 'woff' => 'font/woff', 'woff2' => 'font/woff2', 'ttf' => 'font/ttf', 'wasm' => 'application/wasm',
        'txt' => 'text/plain; charset=utf-8', 'stl' => 'model/stl'];
    return isset($map[$ext]) ? $map[$ext] : 'application/octet-stream';
}

function no_pro_page($loggedIn, $ent)
{
    http_response_code($loggedIn ? 402 : 401);
    header('Content-Type: text/html; charset=utf-8');
    header('Cache-Control: private, no-store');
    $expired = !empty($ent['expired']);
    $title = !$loggedIn ? 'Log in to open Studio Pro' : ($expired ? 'Your Studio Pro has ended' : 'Studio Pro is not on this account');
    $text = !$loggedIn ? 'Studio Pro runs here for the account that bought it.'
        : ($expired ? 'It ended on ' . htmlspecialchars($ent['expired']) . '. Renew it to carry on; your designs stay in this browser.'
                    : 'Studio Pro opens here once your order is complete. The free Studio Basic is always available.');
    echo '<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="robots" content="noindex"><title>dpCREATOR Studio Pro</title>'
        . '<style>body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;background:#0D1014;color:#C3CCD5;font:18px/1.55 system-ui,Segoe UI,Roboto,sans-serif}main{max-width:560px;padding:28px}h1{color:#EEF2F5;font-size:34px;line-height:1.1;margin:0 0 12px}a.b{display:inline-block;margin:16px 12px 0 0;background:#1F7BC7;color:#fff;text-decoration:none;font-weight:600;padding:12px 22px;border-radius:8px}a.s{background:#1A2129;border:1px solid #33404C}</style></head><body><main>'
        . '<h1>' . $title . '</h1><p>' . $text . '</p>'
        . (!$loggedIn ? '<a class="b" href="/index.php?route=account/login">Log in</a>' : '<a class="b" href="' . BUY_URL . '">' . ($expired ? 'Renew Studio Pro' : 'Get Studio Pro') . '</a>')
        . '<a class="b s" href="/studio/">Open Studio Basic (free)</a></main></body></html>';
    exit;
}

// ---------------------------------------------------------------------------
// Routes

$f = isset($_GET['f']) ? (string)$_GET['f'] : '';
if (!oc_db()) {
    json_out(503, ['error' => 'not_available']);
}
ensure_tables();
$method = isset($_SERVER['REQUEST_METHOD']) ? $_SERVER['REQUEST_METHOD'] : 'GET';

if ($f === 'version') {
    $meta = pro_build($cacheDir);
    header('Cache-Control: public, max-age=300');
    json_out(200, ['version' => isset($meta['version']) ? $meta['version'] : '']);
}

if ($f === 'sample' || $f === 'diag') {
    if (!is_admin()) {
        json_out(403, ['error' => 'Log in to the shop admin first, then open this page again in the same browser.']);
    }
    if ($f === 'sample') {
        header('Content-Type: text/plain; charset=utf-8');
        header('Cache-Control: no-store');
        $today = gmdate('Y-m-d');
        $a = make_licence('order-TEST', 'Test order', '', '');
        if ($a === null) {
            exit("No signing key (same key as the WebUI licences: dpc-licence-key.pem). Run fw.php?f=keysetup first.\n");
        }
        echo "# Studio Pro sample licences, signed " . $today . ". Paste one block (from DPCL1 to sig=) into Studio Pro.\n\n";
        echo "# 1. No end date\n" . $a . "\n";
        echo "# 2. Yearly plan (expires in a year)\n" . make_licence('order-TEST', 'Test order', add_months($today, 12), '') . "\n";
        echo "# 3. One-time plan (no end, updates for a year)\n" . make_licence('order-TEST', 'Test order', '', add_months($today, 12)) . "\n";
        echo "# 4. Already expired (should be refused once Studio checks expires=)\n" . make_licence('order-TEST', 'Test order', '2026-01-01', '') . "\n";
        echo "# 5. Board install copy (id = board ID)\n" . make_licence('84F703A1B2C4', 'Test order', '', '') . "\n";
        exit;
    }
    $meta = pro_build($cacheDir, isset($_GET['refresh']));
    json_out(200, [
        'script'      => 'studio-pro.php 2026-10-21',
        'signing_key' => signing_key() !== false,
        'token_set'   => FW_GITHUB_TOKEN !== '',
        'assets'      => array_keys(gh_assets()),
        'build'       => $meta,
        'one_file'    => is_file("$cacheDir/one.html"),
        'web_app'     => is_file("$cacheDir/web/index.html"),
        'zip_support' => class_exists('ZipArchive'),
        'plans'       => $GLOBALS['PLANS'],
        'hardware'    => HW_PRO_MONTHS,
    ]);
}

$cid = customer_id();
$ent = $cid ? entitlement($cid) : ['pro' => false, 'expires' => '', 'updates' => '', 'plan' => '', 'order_id' => 0];

if ($f === 'app') {
    $p = isset($_GET['p']) ? (string)$_GET['p'] : '';
    $p = ltrim(str_replace('\\', '/', $p), '/');
    if ($p === '' || substr($p, -1) === '/') {
        $p .= 'index.html';
    }
    if (strpos($p, '..') !== false || !preg_match('#^[A-Za-z0-9._/@-]+$#', $p)) {
        http_response_code(400);
        exit;
    }
    $isPage = substr($p, -5) === '.html';
    if (!$ent['pro']) {
        if ($isPage) {
            no_pro_page($cid > 0, $ent);
        }
        http_response_code(403);
        exit;
    }
    $meta = pro_build($cacheDir);
    $file = "$cacheDir/web/$p";
    if (!is_file($file)) {
        if (!$isPage && strpos($p, '.') !== false) {
            http_response_code(404);
            exit;
        }
        $file = "$cacheDir/web/index.html";   // app routes
        $isPage = true;
    }
    header('Content-Type: ' . mime_for($file));
    header('Cache-Control: private, ' . ($isPage || basename($file) === 'sw.js' ? 'no-cache' : 'max-age=86400'));
    if (basename($file) === 'sw.js') {
        header('Service-Worker-Allowed: ' . APP_PATH);
    }
    if ($isPage) {
        $lic = licence_for($cid, $ent, 'account');
        $html = file_get_contents($file);
        echo $lic !== null ? inject_licence($html, $lic) : $html;
        exit;
    }
    readfile($file);
    exit;
}

if ($f === 'access') {
    $out = ['logged_in' => $cid > 0, 'pro' => $ent['pro'], 'plan' => $ent['plan'], 'expires' => $ent['expires'],
            'updates' => $ent['updates'], 'expired' => isset($ent['expired']) ? $ent['expired'] : '',
            'app' => APP_PATH, 'buy' => BUY_URL];
    if ($ent['pro']) {
        $out['licence'] = licence_for($cid, $ent, 'account');
    }
    json_out(200, $out);
}

if (!$cid) {
    json_out(401, ['error' => 'login_required']);
}
if (!$ent['pro']) {
    json_out(403, ['error' => 'no_pro']);
}

if ($f === 'licence') {
    $lic = licence_for($cid, $ent, 'account');
    if ($lic === null) {
        json_out(503, ['error' => 'not_available']);
    }
    header('Content-Type: text/plain; charset=utf-8');
    header('Content-Disposition: attachment; filename="dpcreator-studio-pro-licence.txt"');
    header('Cache-Control: private, no-store');
    echo $lic;
    exit;
}

if ($method !== 'POST') {
    json_out(405, ['error' => 'post_only']);
}
$host = isset($_SERVER['HTTP_HOST']) ? strtolower($_SERVER['HTTP_HOST']) : '';
$src = isset($_SERVER['HTTP_ORIGIN']) ? $_SERVER['HTTP_ORIGIN'] : (isset($_SERVER['HTTP_REFERER']) ? $_SERVER['HTTP_REFERER'] : '');
if ($host === '' || strtolower((string)parse_url($src, PHP_URL_HOST)) !== preg_replace('/:\d+$/', '', $host)) {
    json_out(403, ['error' => 'wrong_origin']);
}

if ($f === 'email') {
    if (count_events($cid, 'email', 3600) >= 3) {
        json_out(429, ['error' => 'too_many']);
    }
    $lic = licence_for($cid, $ent, 'account');
    $c = customer($cid);
    $from = setting('config_email', '');
    if ($lic === null || !$c || $from === '' || !function_exists('mail')) {
        json_out(503, ['error' => 'not_available']);
    }
    log_event($cid, 'email');
    $text = "Hello " . $c['firstname'] . ",\n\nhere is your dpCREATOR Studio Pro licence. Open Studio Pro at https://" . $host . APP_PATH
        . " (it is already unlocked there when you are logged in), or paste all the lines below, unchanged, into the unlock page of a Studio Pro copy:\n\n"
        . $lic . "\nKeep this e-mail. You can also see the licence any time on https://" . $host . BUY_URL . " when logged in.\n\ndpCREATOR\n";
    $ok = @mail($c['email'], '=?UTF-8?B?' . base64_encode('Your dpCREATOR Studio Pro licence') . '?=', $text,
        "Content-Type: text/plain; charset=UTF-8\r\nFrom: dpCREATOR <" . $from . ">");
    json_out($ok ? 200 : 503, $ok ? ['ok' => true] : ['error' => 'not_available']);
}

if ($f === 'install') {
    if (empty($_SERVER['HTTP_X_DPC_INSTALLER'])) {
        json_out(403, ['error' => 'wrong_origin']);
    }
    $in = json_decode((string)file_get_contents('php://input'), true);
    $board = strtoupper(preg_replace('/[^A-Fa-f0-9]/', '', isset($in['board']) ? (string)$in['board'] : ''));
    if (strlen($board) < 6 || strlen($board) > 32) {
        $board = 'order-' . $ent['order_id'];        // stock FluidNC without a board ID: the account copy
    }
    if (count_events($cid, 'install', 3600) >= MAX_INSTALLS_PER_HOUR) {
        json_out(429, ['error' => 'too_many_installs']);
    }
    $meta = pro_build($cacheDir);
    if (!is_file("$cacheDir/one.html")) {
        json_out(503, ['error' => 'not_available']);
    }
    $lic = licence_for($cid, $ent, $board === 'order-' . $ent['order_id'] ? 'account' : $board);
    if ($lic === null) {
        json_out(503, ['error' => 'not_available']);
    }
    $gz = gz_for_board(inject_licence(file_get_contents("$cacheDir/one.html"), $lic));
    if ($gz === null) {
        json_out(503, ['error' => 'not_available']);
    }
    log_event($cid, 'install');
    header('Content-Type: application/gzip');
    header('Cache-Control: private, no-store');
    header('X-DPC-Sha256: ' . hash('sha256', $gz));
    header('X-DPC-Version: ' . preg_replace('/[^A-Za-z0-9 ._:-]/', '', strtok(isset($meta['version']) ? $meta['version'] : '', "\n")));
    header('Access-Control-Expose-Headers: X-DPC-Sha256, X-DPC-Version');
    echo $gz;
    exit;
}

json_out(404, ['error' => 'unknown']);

<?php
/**
 * dpCREATOR WebUI licences and installs
 * Upload to the web root of dpcreator.eu (next to index.php and fw.php).
 *
 * The WebUI page is built in the private repo val218/dpcreator-webui and
 * published as the release "webui-latest" (webui-index.html + version.txt).
 * This script fetches it server-side with the read-only FW_GITHUB_TOKEN (the
 * same token fw.php uses), puts the customer's licence in for one board,
 * gzips it and hands it to the installer on the Flash Firmware page, which
 * sends it to the board over USB. It is never offered as a download.
 *
 *   GET  /webui.php?f=version              -> {"version":...}            (public)
 *   GET  /webui.php?f=access               -> licence state for the logged-in customer
 *   POST /webui.php?f=install  {"board":"84F703A1B2C4"}
 *        -> the gzipped page licensed to that board (registers the board if needed)
 *   POST /webui.php?f=release  {"board":"<last 6 digits>"}
 *        -> frees one board slot (at most once every RELEASE_DAYS days)
 *   GET  /webui.php?f=licence&board=84F703A1B2C4   (public, CORS *, no cookies)
 *        -> {"status":"ok","licence":"DPCL1..."} | {"status":"released"} | {"status":"unknown"}
 *           The board's licence for its licence.txt: the WebUI asks once a day (update check) and
 *           keeps the copy when it is newer. "issued" (date and time, UTC) changes only when the
 *           licence changes: a new owner, a new name, a release and a new install. Released: the
 *           WebUI deletes licence.txt (it keeps working, with the unregistered notice).
 *   GET  /webui.php?f=lite-version         -> {"version":...} of WebUI Lite  (public)
 *   POST /webui.php?f=lite  {"board":"..."}
 *        -> WebUI Lite gzipped, for ESP32 (WROOM-32) boards: free with any account, no
 *           licence and no board slot. Release asset webui-lite-index.html (same release,
 *           same version.txt). The installer puts it in the flash as index.html.gz
 *           (stock page kept once as legacy.html.gz), the same as on the R3.
 *
 * Who gets a licence (completed order, OpenCart "Complete Order Status"):
 *   - the WebUI product or the old SmartTabUI + WebUI bundle   ($PAID_PRODUCTS)
 *   - free with the dpCREATOR R3 (on its own or in the R3 + SmartTabUI combo)  ($HARDWARE_PRODUCTS)
 *   - pendant orders (assembled pendant, SmartTabUI firmware, DIY pack) placed before
 *     PENDANT_WEBUI_UNTIL keep the WebUI they were sold with   ($PENDANT_PRODUCTS)
 *     R3 controller, R3 + SmartTabUI combo, assembled pendant,
 *     SmartTabUI firmware and the SmartTabUI DIY pack
 * Each licence covers MAX_BOARDS boards per customer account.
 *
 * Shop admins (Flash station in the OpenCart admin: admin login in the same browser and
 * the X-DPC-Admin: 1 header) install without an account, without the board limit: those
 * boards are registered to the shop (customer 0). A board registered to the shop is
 * the chip's own licence: any logged-in customer can later install on it without using a slot.
 * Admins can remove any customer's board and clear a customer's 30-day wait in
 * fw.php?f=licences.
 */

// Product IDs in the shop.
$PAID_PRODUCTS     = [383, 384];   // dpCREATOR WebUI, SmartTabUI firmware + WebUI bundle (disabled, kept for earlier buyers)
$HARDWARE_PRODUCTS = [381, 380];             // R3 controller, R3 + SmartTabUI combo: the full WebUI is free only with the R3
$PENDANT_PRODUCTS  = [382, 377, 385];        // assembled pendant, SmartTabUI firmware, DIY pack: WebUI included only before the date below
const PENDANT_WEBUI_UNTIL = '2026-10-29 00:00:00';

const WEBUI_REPO   = 'val218/dpcreator-webui';
const WEBUI_TAG    = 'webui-latest';
const WEBUI_ASSET  = 'webui-index.html';
const LITE_ASSET   = 'webui-lite-index.html';   // WebUI Lite for ESP32 (WROOM-32) boards, free
const MAX_BOARDS   = 2;
const RELEASE_DAYS = 30;        // a customer can free a board slot once per 30 days
const MAX_INSTALLS_PER_HOUR = 20;
const CHECK_EVERY  = 120;       // seconds between checks for a new GitHub build (installs always check)
const MAX_PAGE     = 4194304;   // refuse a page bigger than 4 MB

// Read-only GitHub token for the private WebUI repo (same as fw.php).
// The token lives in fw-config.php (next to this file), so updates of this
// script don't need it re-entered: <?php define('FW_GITHUB_TOKEN', 'github_pat_...');
if (is_file(__DIR__ . '/fw-config.php')) {
    require_once __DIR__ . '/fw-config.php';
}
if (!defined('FW_GITHUB_TOKEN')) {
    define('FW_GITHUB_TOKEN', getenv('FW_GITHUB_TOKEN') ?: '');
}

$cacheDir = __DIR__ . '/fw-cache';

// ---------------------------------------------------------------------------

header('X-Content-Type-Options: nosniff');
header('Cache-Control: private, no-store');

$f = isset($_GET['f']) ? (string)$_GET['f'] : '';
if (!in_array($f, ['version', 'access', 'install', 'release', 'diag', 'lite', 'lite-version', 'licence'], true)) {
    http_response_code(404);
    exit('Unknown request');
}

function json_out($code, array $data)
{
    http_response_code($code);
    header('Content-Type: application/json');
    echo json_encode($data, JSON_UNESCAPED_SLASHES);
    exit;
}

// ---------------------------------------------------------------------------
// OpenCart customer session (same logic as fw.php)

function oc_db()
{
    if (isset($GLOBALS['oc_db'])) {
        return $GLOBALS['oc_db'];
    }
    if (!is_file(__DIR__ . '/config.php')) {
        return null;
    }
    require_once __DIR__ . '/config.php';
    $db = @new mysqli(DB_HOSTNAME, DB_USERNAME, DB_PASSWORD, DB_DATABASE, (int)(defined('DB_PORT') ? DB_PORT : 3306));
    if ($db->connect_errno) {
        return null;
    }
    $db->set_charset('utf8mb4');
    $GLOBALS['oc_db'] = $db;

    // Same clock as OpenCart: it runs PHP in the shop's time zone and sets the
    // MySQL session to that offset, and writes session expiry times with it.
    $tz = $db->query("SELECT `value` FROM `" . DB_PREFIX . "setting` WHERE `store_id` = 0 AND `key` = 'config_timezone' LIMIT 1");
    if ($tz && ($row = $tz->fetch_assoc()) && $row['value'] !== '' && in_array($row['value'], timezone_identifiers_list(), true)) {
        date_default_timezone_set($row['value']);
    } else {
        date_default_timezone_set('UTC');   // OpenCart's default (system/config/default.php)
    }
    $db->query("SET time_zone = '" . $db->real_escape_string(date('P')) . "'");
    return $db;
}

/** Logged-in OpenCart customer id, or 0. */
function oc_customer_id()
{
    $db = oc_db();
    if (!$db) {
        return 0;
    }
    $name = 'OCSESSID';
    $res = $db->query("SELECT `value` FROM `" . DB_PREFIX . "setting` WHERE `store_id` = 0 AND `key` = 'config_session_name' LIMIT 1");
    if ($res && ($row = $res->fetch_assoc()) && $row['value'] !== '') {
        $name = $row['value'];
    }
    $sid = isset($_COOKIE[$name]) ? $_COOKIE[$name] : '';
    if (!preg_match('/^[A-Za-z0-9,-]{20,64}$/', $sid)) {
        return 0;
    }
    $data = null;
    // No expiry check here: OpenCart decides when a session ends (its expiry
    // times depend on PHP/MySQL time zone settings); a logged-out session no
    // longer has customer_id in it.
    $stmt = $db->prepare("SELECT `data` FROM `" . DB_PREFIX . "session` WHERE `session_id` = ? LIMIT 1");
    if ($stmt) {
        $stmt->bind_param('s', $sid);
        $stmt->execute();
        $stmt->bind_result($raw);
        if ($stmt->fetch()) {
            $data = $raw;
        }
        $stmt->close();
    }
    if ($data === null && defined('DIR_STORAGE') && is_file(DIR_STORAGE . 'session/sess_' . $sid)) {
        $data = file_get_contents(DIR_STORAGE . 'session/sess_' . $sid);
    }
    if ($data === null) {
        return 0;
    }
    $s = json_decode($data, true);
    if (!is_array($s)) {
        $s = @unserialize($data, ['allowed_classes' => false]);
    }
    return (is_array($s) && !empty($s['customer_id'])) ? (int)$s['customer_id'] : 0;
}

/** The visitor's session data (customer and admin share the OpenCart session). */
function oc_session_data()
{
    static $s = null;
    if ($s !== null) {
        return $s;
    }
    $s = [];
    $db = oc_db();
    if (!$db) {
        return $s;
    }
    $name = 'OCSESSID';
    $res = $db->query("SELECT `value` FROM `" . DB_PREFIX . "setting` WHERE `store_id` = 0 AND `key` = 'config_session_name' LIMIT 1");
    if ($res && ($row = $res->fetch_assoc()) && $row['value'] !== '') {
        $name = $row['value'];
    }
    $sid = isset($_COOKIE[$name]) ? $_COOKIE[$name] : '';
    if (!preg_match('/^[A-Za-z0-9,-]{20,64}$/', $sid)) {
        return $s;
    }
    $stmt = $db->prepare("SELECT `data` FROM `" . DB_PREFIX . "session` WHERE `session_id` = ? LIMIT 1");
    $data = null;
    if ($stmt) {
        $stmt->bind_param('s', $sid);
        $stmt->execute();
        $stmt->bind_result($raw);
        if ($stmt->fetch()) {
            $data = $raw;
        }
        $stmt->close();
    }
    if ($data === null && defined('DIR_STORAGE') && is_file(DIR_STORAGE . 'session/sess_' . $sid)) {
        $data = file_get_contents(DIR_STORAGE . 'session/sess_' . $sid);
    }
    if ($data !== null) {
        $d = json_decode($data, true);
        if (!is_array($d)) {
            $d = @unserialize($data, ['allowed_classes' => false]);
        }
        $s = is_array($d) ? $d : [];
    }
    return $s;
}

/** A logged-in, enabled shop admin asking from the admin Flash station. */
function shop_admin()
{
    if (($_SERVER['HTTP_X_DPC_ADMIN'] ?? '') !== '1') {
        return false;
    }
    $s = oc_session_data();
    $db = oc_db();
    if (empty($s['user_id']) || !$db) {
        return false;
    }
    $r = $db->query("SELECT `status` FROM `" . DB_PREFIX . "user` WHERE `user_id` = " . (int)$s['user_id'] . " LIMIT 1");
    $row = $r ? $r->fetch_assoc() : null;
    return $row && (int)$row['status'] === 1;
}

/** Completed order for any of the products? */
function oc_has_purchased($customerId, array $products, $before = null)
{
    $db = oc_db();
    $products = array_filter(array_map('intval', $products));
    if (!$db || !$customerId || !$products) {
        return false;
    }
    $statuses = [];
    $res = $db->query("SELECT `value` FROM `" . DB_PREFIX . "setting` WHERE `store_id` = 0 AND `key` = 'config_complete_status' LIMIT 1");
    if ($res && ($row = $res->fetch_assoc())) {
        $statuses = array_map('intval', (array)json_decode($row['value'], true));
    }
    if (!$statuses) {
        return false;
    }
    $sql = "SELECT 1 FROM `" . DB_PREFIX . "order` o JOIN `" . DB_PREFIX . "order_product` op ON op.order_id = o.order_id"
         // the customer's orders, plus guest orders placed with the same email
         . " WHERE (o.customer_id = " . (int)$customerId
         . "   OR (o.customer_id = 0 AND o.email <> '' AND o.email = (SELECT c.email FROM `" . DB_PREFIX . "customer` c WHERE c.customer_id = " . (int)$customerId . ")))"
         . " AND op.product_id IN (" . implode(',', $products) . ")"
         . " AND o.order_status_id IN (" . implode(',', $statuses) . ")"
         . ($before ? " AND o.date_added < '" . $db->real_escape_string($before) . "'" : "")
         . " LIMIT 1";
    $res = $db->query($sql);
    return $res && $res->num_rows > 0;
}

// ---------------------------------------------------------------------------
// Board registry

function secret()
{
    return hash('sha256', 'dpc-webui|' . (defined('DB_PASSWORD') ? DB_PASSWORD : '') . '|' . (defined('DB_DATABASE') ? DB_DATABASE : '') . '|' . __FILE__);
}

/** The chip ID is stored only as a keyed hash, plus its last 6 digits for display. */
function board_hash($board)
{
    return hash_hmac('sha256', $board, secret());
}

function ensure_tables($db)
{
    $db->query("CREATE TABLE IF NOT EXISTS `" . DB_PREFIX . "dpc_webui_board` (
        `customer_id` INT NOT NULL,
        `board_hash` CHAR(64) NOT NULL,
        `board_tail` CHAR(6) NOT NULL,
        `created` DATETIME NOT NULL,
        `last_install` DATETIME NOT NULL,
        `installs` INT NOT NULL DEFAULT 0,
        PRIMARY KEY (`customer_id`, `board_hash`)
    ) DEFAULT CHARSET=utf8mb4");
    $db->query("CREATE TABLE IF NOT EXISTS `" . DB_PREFIX . "dpc_webui_event` (
        `id` INT NOT NULL AUTO_INCREMENT,
        `customer_id` INT NOT NULL,
        `event` VARCHAR(16) NOT NULL,
        `board_tail` CHAR(6) NOT NULL,
        `created` DATETIME NOT NULL,
        PRIMARY KEY (`id`),
        KEY `customer_event` (`customer_id`, `event`, `created`)
    ) DEFAULT CHARSET=utf8mb4");
}

// One row per board: who the licence is for and when it last changed (the "issued" of licence.txt)
function ensure_licence_table($db)
{
    $db->query("CREATE TABLE IF NOT EXISTS `" . DB_PREFIX . "dpc_webui_licence` (
        `board_hash` CHAR(64) NOT NULL,
        `board_tail` CHAR(6) NOT NULL,
        `owner_id` INT NOT NULL DEFAULT 0,
        `owner_name` VARCHAR(80) NOT NULL DEFAULT '',
        `issued` DATETIME NOT NULL,
        `released` DATETIME NULL,
        `last_check` DATETIME NULL,
        PRIMARY KEY (`board_hash`), KEY `owner` (`owner_id`, `board_tail`)
    ) DEFAULT CHARSET=utf8mb4");
    $db->query("CREATE TABLE IF NOT EXISTS `" . DB_PREFIX . "dpc_webui_check` (
        `ip_hash` CHAR(64) NOT NULL,
        `window` DATETIME NOT NULL,
        `n` INT NOT NULL DEFAULT 0,
        PRIMARY KEY (`ip_hash`)
    ) DEFAULT CHARSET=utf8mb4");
}

/** The name in a customer's licences: first and last name from the account (shop boards: empty). */
function owner_name($db, $customerId)
{
    if ($customerId <= 0) {
        return '';
    }
    $res = $db->query("SELECT `firstname`, `lastname` FROM `" . DB_PREFIX . "customer` WHERE `customer_id` = " . (int)$customerId);
    $row = $res ? $res->fetch_assoc() : null;
    if (!$row) {
        return '';
    }
    $n = trim(html_entity_decode($row['firstname'] . ' ' . $row['lastname'], ENT_QUOTES, 'UTF-8'));
    $n = preg_replace('/[\x00-\x1F\x7F]+/u', ' ', $n);
    return mb_substr($n, 0, 40);
}

function licence_row($db, $hash)
{
    $res = $db->query("SELECT * FROM `" . DB_PREFIX . "dpc_webui_licence` WHERE `board_hash` = '" . $db->real_escape_string($hash) . "'");
    $row = $res ? $res->fetch_assoc() : null;
    return $row ?: null;
}

/** Records who a board's licence is for; "issued" moves only when the owner or the name changes (or after a release). */
function licence_set($db, $hash, $tail, $ownerId)
{
    $name = owner_name($db, $ownerId);
    $old  = licence_row($db, $hash);
    if ($old && (int)$old['owner_id'] === (int)$ownerId && $old['owner_name'] === $name && $old['released'] === null) {
        return $old;
    }
    $now = gmdate('Y-m-d H:i:s');
    $st = $db->prepare("INSERT INTO `" . DB_PREFIX . "dpc_webui_licence` (`board_hash`, `board_tail`, `owner_id`, `owner_name`, `issued`, `released`) VALUES (?, ?, ?, ?, ?, NULL)"
                     . " ON DUPLICATE KEY UPDATE `board_tail` = VALUES(`board_tail`), `owner_id` = VALUES(`owner_id`), `owner_name` = VALUES(`owner_name`), `issued` = VALUES(`issued`), `released` = NULL");
    $st->bind_param('ssiss', $hash, $tail, $ownerId, $name, $now);
    $st->execute();
    $st->close();
    return licence_row($db, $hash);
}

/** Owner of a board from the board slots: a customer's slot (latest install), else the shop; null if none. */
function slot_owner($db, $hash)
{
    $res = $db->query("SELECT `customer_id`, `board_tail` FROM `" . DB_PREFIX . "dpc_webui_board` WHERE `board_hash` = '" . $db->real_escape_string($hash) . "' ORDER BY (`customer_id` = 0), `last_install` DESC LIMIT 1");
    $row = $res ? $res->fetch_assoc() : null;
    return $row ?: null;
}

function boards($db, $customerId)
{
    $out = [];
    $res = $db->query("SELECT `board_hash`, `board_tail`, `created`, `last_install` FROM `" . DB_PREFIX . "dpc_webui_board`"
                    . " WHERE `customer_id` = " . (int)$customerId . " ORDER BY `created`");
    while ($res && ($row = $res->fetch_assoc())) {
        $out[] = $row;
    }
    return $out;
}

function log_event($db, $customerId, $event, $tail)
{
    $stmt = $db->prepare("INSERT INTO `" . DB_PREFIX . "dpc_webui_event` (`customer_id`, `event`, `board_tail`, `created`) VALUES (?, ?, ?, NOW())");
    $stmt->bind_param('iss', $customerId, $event, $tail);
    $stmt->execute();
    $stmt->close();
}

function count_events($db, $customerId, $event, $seconds)
{
    $res = $db->query("SELECT COUNT(*) AS n FROM `" . DB_PREFIX . "dpc_webui_event` WHERE `customer_id` = " . (int)$customerId
                    . " AND `event` = '" . $db->real_escape_string($event) . "' AND `created` > NOW() - INTERVAL " . (int)$seconds . " SECOND");
    $row = $res ? $res->fetch_assoc() : null;
    return $row ? (int)$row['n'] : 0;
}

function last_release($db, $customerId)
{
    // a release counts for the 30-day wait unless a shop admin cleared the wait after it ('reset')
    $res = $db->query("SELECT MAX(IF(`event` = 'release', `created`, NULL)) AS t, MAX(IF(`event` = 'reset', `created`, NULL)) AS r FROM `" . DB_PREFIX . "dpc_webui_event` WHERE `customer_id` = " . (int)$customerId . " AND `event` IN ('release', 'reset')");
    $row = $res ? $res->fetch_assoc() : null;
    if (!$row || !$row['t']) {
        return 0;
    }
    return ($row['r'] && $row['r'] >= $row['t']) ? 0 : strtotime($row['t']);
}

/** Is this board registered to the shop (installed by a shop admin)? */
function shop_board($db, $hash)
{
    $st = $db->prepare("SELECT 1 FROM `" . DB_PREFIX . "dpc_webui_board` WHERE `customer_id` = 0 AND `board_hash` = ? LIMIT 1");
    $st->bind_param('s', $hash);
    $st->execute();
    $st->store_result();
    $ok = $st->num_rows > 0;
    $st->close();
    return $ok;
}

// ---------------------------------------------------------------------------
// The page from GitHub (cached)

function http_get($url, array $headers, $max, &$location = null)
{
    $ch = curl_init($url);
    curl_setopt_array($ch, [
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_FOLLOWLOCATION => false,
        CURLOPT_CONNECTTIMEOUT => 10,
        CURLOPT_TIMEOUT        => 60,
        CURLOPT_USERAGENT      => 'dpcreator-webui-proxy',
        CURLOPT_HTTPHEADER     => $headers,
        CURLOPT_PROTOCOLS      => CURLPROTO_HTTPS,
    ]);
    $data     = curl_exec($ch);
    $code     = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    $location = curl_getinfo($ch, CURLINFO_REDIRECT_URL);
    curl_close($ch);
    if ($code >= 300 && $code < 400) {
        return false;
    }
    return ($data === false || $code !== 200 || strlen($data) > $max) ? null : $data;
}

/** A release asset of the private repo, through the API with the read-only token. */
function gh_asset($asset, $max)
{
    if (FW_GITHUB_TOKEN === '') {
        return null;
    }
    $auth = ['Authorization: Bearer ' . FW_GITHUB_TOKEN, 'X-GitHub-Api-Version: 2022-11-28'];
    $rel  = http_get('https://api.github.com/repos/' . WEBUI_REPO . '/releases/tags/' . rawurlencode(WEBUI_TAG),
                     array_merge($auth, ['Accept: application/vnd.github+json']), 1048576);
    $rel  = $rel ? json_decode($rel, true) : null;
    $id   = null;
    foreach ((array)($rel['assets'] ?? []) as $a) {
        if (($a['name'] ?? '') === $asset) {
            $id = (int)$a['id'];
        }
    }
    if (!$id) {
        return null;
    }
    $loc  = null;
    $data = http_get('https://api.github.com/repos/' . WEBUI_REPO . "/releases/assets/$id",
                     array_merge($auth, ['Accept: application/octet-stream']), $max, $loc);
    if ($data === false && $loc) {
        $data = http_get($loc, [], $max);   // signed CDN URL: without the token
    }
    return $data ?: null;
}

/** The build a page was made from: the first word of version.txt is printed in <span id="ui-ver">. */
function page_build($html)
{
    return ($html !== null && preg_match('/id="ui-ver">\s*([0-9a-f]{7,40})\s*</', $html, $m)) ? $m[1] : '';
}

function version_build($version)
{
    return strtok(trim((string)$version), " \t\r\n") ?: '';
}

/**
 * Cached page + meta. The cache is kept only while version.txt of the release is unchanged:
 * installs ($fresh) always ask GitHub, other requests at most every CHECK_EVERY seconds.
 * A download is kept only when the page's own build matches version.txt (the release is deleted
 * and created again on every push, so a request in between can see a mix of old and new).
 */
function webui_page($cacheDir, $fresh = false)
{
    if (!is_dir($cacheDir)) {
        @mkdir($cacheDir, 0755, true);
        @file_put_contents($cacheDir . '/.htaccess', "Require all denied\nDeny from all\n");
    }
    $page = "$cacheDir/webui.html";
    $metaPath = "$cacheDir/webui.json";
    $meta = is_file($metaPath) ? (array)json_decode(file_get_contents($metaPath), true) : [];
    $now  = time();
    if ($fresh || !is_file($page) || !isset($meta['checked']) || $now - $meta['checked'] >= CHECK_EVERY) {
        $lock = fopen("$metaPath.lock", 'c');
        // an install waits for a check that is already running instead of using the old copy
        if ($lock && flock($lock, $fresh ? LOCK_EX : (LOCK_EX | LOCK_NB))) {
            $meta = is_file($metaPath) ? (array)json_decode(file_get_contents($metaPath), true) : [];
            $version = gh_asset('version.txt', 4096);
            $known = $version !== null;
            $version = $known ? trim($version) : ($meta['version'] ?? '');
            $want = version_build($version);
            $have = is_file($page) ? page_build(file_get_contents($page)) : '';
            $ok = true;
            if (!is_file($page) || ($meta['version'] ?? null) !== $version || ($want !== '' && $have !== '' && $have !== $want)) {
                $html = gh_asset(WEBUI_ASSET, MAX_PAGE);
                $got = page_build($html);
                if ($html !== null && strpos($html, '<!--DPC_LICENSE-->') !== false && ($want === '' || $got === '' || $got === $want)) {
                    file_put_contents("$page.tmp", $html);
                    rename("$page.tmp", $page);
                    $meta['version'] = $version;
                    $meta['updated'] = $now;
                } else {
                    $ok = false;   // not there yet, or a half-made release: try again on the next request
                }
            }
            if ($ok && $known) {
                $meta['checked'] = $now;
            }
            file_put_contents($metaPath, json_encode($meta));
            flock($lock, LOCK_UN);
        }
        if ($lock) {
            fclose($lock);
        }
    }
    return is_file($page) ? [file_get_contents($page), $meta] : [null, $meta];
}

/** WebUI Lite: cached like the full page, no licence marker needed. */
function lite_page($cacheDir, $fresh = false)
{
    if (!is_dir($cacheDir)) {
        @mkdir($cacheDir, 0755, true);
        @file_put_contents($cacheDir . '/.htaccess', "Require all denied\nDeny from all\n");
    }
    $page = "$cacheDir/webui-lite.html";
    $metaPath = "$cacheDir/webui-lite.json";
    $meta = is_file($metaPath) ? (array)json_decode(file_get_contents($metaPath), true) : [];
    $now  = time();
    if ($fresh || !is_file($page) || !isset($meta['checked']) || $now - $meta['checked'] >= CHECK_EVERY) {
        $lock = fopen("$metaPath.lock", 'c');
        if ($lock && flock($lock, $fresh ? LOCK_EX : (LOCK_EX | LOCK_NB))) {
            $meta = is_file($metaPath) ? (array)json_decode(file_get_contents($metaPath), true) : [];
            $version = gh_asset('version.txt', 4096);
            $known = $version !== null;
            $version = $known ? trim($version) : ($meta['version'] ?? '');
            $want = version_build($version);
            $have = is_file($page) ? page_build(file_get_contents($page)) : '';
            $ok = true;
            if (!is_file($page) || ($meta['version'] ?? null) !== $version || ($want !== '' && $have !== '' && $have !== $want)) {
                $html = gh_asset(LITE_ASSET, MAX_PAGE);
                $got = page_build($html);
                if ($html !== null && stripos($html, '<html') !== false && ($want === '' || $got === '' || $got === $want)) {
                    file_put_contents("$page.tmp", $html);
                    rename("$page.tmp", $page);
                    $meta['version'] = $version;
                    $meta['updated'] = $now;
                } else {
                    $ok = false;
                }
            }
            if ($ok && $known) {
                $meta['checked'] = $now;
            }
            file_put_contents($metaPath, json_encode($meta));
            flock($lock, LOCK_UN);
        }
        if ($lock) {
            fclose($lock);
        }
    }
    return is_file($page) ? [file_get_contents($page), $meta] : [null, $meta];
}

/** Gzip a page for XModem: the file must not end with 0x1A (see licensed_gz). */
function plain_gz($html)
{
    $html = str_replace('<!--DPC_LICENSE-->', '', $html);
    for ($i = 0; $i < 8; $i++) {
        $gz = gzencode($html, 9);
        if ($gz !== false && substr($gz, -1) !== "\x1A") {
            return $gz;
        }
        $html .= ' ';
    }
    return null;
}

/**
 * The page for one board, gzipped. XModem pads the last packet with Ctrl-Z
 * (0x1A) and the board strips trailing 0x1A bytes, so the file must not end
 * with one: if the gzip does, add a space to the page and compress again.
 */
/**
 * Signed licence for one board (same key and format as fw.php for the pendant):
 * the WebUI checks it against the board's own ID, with the public key built in.
 */
function webui_licence($board, $issued = null, $to = '')
{
    $outside = dirname(__DIR__) . '/dpc-licence-key.pem';
    $path    = is_file($outside) ? $outside : __DIR__ . '/fw-cache/dpc-licence-key.pem';
    $key     = is_file($path) ? openssl_pkey_get_private(file_get_contents($path)) : false;
    if (!$key) {
        return null;
    }
    $when = $issued !== null ? gmdate('Y-m-d\\TH:i:s\\Z', strtotime($issued . ' UTC')) : gmdate('Y-m-d\\TH:i:s\\Z');
    $to   = str_replace(["\r", "\n"], ' ', (string)$to);
    $body = "DPCL1\nproduct=webui\nid=$board\ntype=full\nissued=$when\nto=$to\n";
    if (!openssl_sign($body, $sig, $key, OPENSSL_ALGO_SHA256)) {
        return null;
    }
    return $body . 'sig=' . base64_encode($sig) . "\n";
}

function licensed_gz($html, $board, $customerId, $lic = null)
{
    $text = $lic ? webui_licence($board, $lic['issued'], $lic['owner_name']) : webui_licence($board);
    if ($text === null) {
        return null;   // no signing key: fw.php?f=keysetup
    }
    $lic = '<script>window.DPC_LICENSE=' . json_encode($text) . '</script>';
    $html = str_replace('<!--DPC_LICENSE-->', $lic, $html);
    for ($i = 0; $i < 8; $i++) {
        $gz = gzencode($html, 9);
        if ($gz !== false && substr($gz, -1) !== "\x1A") {
            return $gz;
        }
        $html .= ' ';
    }
    return null;
}

// ---------------------------------------------------------------------------

/** Why a logged-in customer may not be recognised (no secrets: no IDs, no session data). */
function session_diag()
{
    $db = oc_db();
    if (!$db) {
        return ['db' => false];
    }
    $name = 'OCSESSID';
    $res = $db->query("SELECT `value` FROM `" . DB_PREFIX . "setting` WHERE `store_id` = 0 AND `key` = 'config_session_name' LIMIT 1");
    if ($res && ($row = $res->fetch_assoc()) && $row['value'] !== '') {
        $name = $row['value'];
    }
    $sid = isset($_COOKIE[$name]) ? $_COOKIE[$name] : '';
    $out = ['cookie_name' => $name, 'cookie_present' => $sid !== '', 'cookie_format_ok' => (bool)preg_match('/^[A-Za-z0-9,-]{20,64}$/', $sid),
            'cookies_seen' => array_keys($_COOKIE)];
    $tbl = $db->query("SHOW TABLES LIKE '" . $db->real_escape_string(DB_PREFIX . "session") . "'");
    $out['session_table'] = $tbl && $tbl->num_rows > 0;
    if ($sid !== '' && $out['session_table']) {
        $stmt = $db->prepare("SELECT `data`, `expire` > NOW() FROM `" . DB_PREFIX . "session` WHERE `session_id` = ? LIMIT 1");
        $stmt->bind_param('s', $sid);
        $stmt->execute();
        $stmt->bind_result($raw, $alive);
        $found = $stmt->fetch();
        $stmt->close();
        $out['db_row'] = (bool)$found;
        $out['db_row_alive'] = $found ? (bool)$alive : null;
        $t = $db->query("SELECT NOW() AS n, UTC_TIMESTAMP() AS u, @@session.time_zone AS z");
        $t = $t ? $t->fetch_assoc() : [];
        $e = $db->prepare("SELECT `expire` FROM `" . DB_PREFIX . "session` WHERE `session_id` = ? LIMIT 1");
        $e->bind_param('s', $sid);
        $e->execute();
        $e->bind_result($exp);
        $e->fetch();
        $e->close();
        $out['clock'] = ['expire' => $exp, 'mysql_now' => $t['n'] ?? null, 'mysql_utc' => $t['u'] ?? null, 'mysql_tz' => $t['z'] ?? null,
                         'php_now' => date('Y-m-d H:i:s'), 'php_tz' => date_default_timezone_get()];
        if ($found) {
            $j = json_decode($raw, true);
            $out['data_format'] = is_array($j) ? 'json' : (@unserialize($raw, ['allowed_classes' => false]) !== false ? 'php' : 'other');
            $arr = is_array($j) ? $j : @unserialize($raw, ['allowed_classes' => false]);
            $out['data_keys'] = is_array($arr) ? array_keys($arr) : [];
        }
    }
    $out['file_engine_dir'] = defined('DIR_STORAGE') ? is_dir(DIR_STORAGE . 'session/') : null;
    $out['file_session'] = ($sid !== '' && defined('DIR_STORAGE')) ? is_file(DIR_STORAGE . 'session/sess_' . basename($sid)) : null;
    $out['customer_found'] = oc_customer_id() > 0;
    return $out;
}

// Setup check: is the token set and can it see the private release? (no secrets in the output)
if ($f === 'diag') {
    $auth = ['Authorization: Bearer ' . FW_GITHUB_TOKEN, 'X-GitHub-Api-Version: 2022-11-28', 'Accept: application/vnd.github+json'];
    $ch = curl_init('https://api.github.com/repos/' . WEBUI_REPO . '/releases/tags/' . rawurlencode(WEBUI_TAG));
    curl_setopt_array($ch, [CURLOPT_RETURNTRANSFER => true, CURLOPT_HTTPHEADER => $auth, CURLOPT_USERAGENT => 'dpcreator-webui-proxy', CURLOPT_TIMEOUT => 20]);
    $body = curl_exec($ch);
    $code = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    $err  = curl_error($ch);
    curl_close($ch);
    $rel = json_decode((string)$body, true);
    json_out(200, [
        'script'         => 'webui.php 2026-10-17 (signed licences)',
        'licence_key'    => webui_licence('000000000000') !== null,
        'token_set'      => FW_GITHUB_TOKEN !== '',
        'token_prefix'   => substr(FW_GITHUB_TOKEN, 0, 11),
        'release_http'   => $code,
        'curl_error'     => $err,
        'github_message' => is_array($rel) ? ($rel['message'] ?? null) : null,
        'assets'         => is_array($rel) ? array_map(function ($a) { return $a['name'] ?? ''; }, (array)($rel['assets'] ?? [])) : [],
        'cache_writable' => is_dir($cacheDir) ? is_writable($cacheDir) : is_writable(__DIR__),
        'session'        => session_diag(),
    ]);
}

if ($f === 'version') {
    header('Cache-Control: no-cache');
    list($html, $meta) = webui_page($cacheDir);
    json_out(200, ['version' => $meta['version'] ?? '', 'updated' => isset($meta['updated']) ? gmdate('c', $meta['updated']) : null]);
}

$db = oc_db();

// ---------------------------------------------------------------------------
// WebUI Lite (ESP32 / WROOM-32 boards): free with any account (or the admin Flash station)
if ($f === 'lite-version') {
    header('Cache-Control: no-cache');
    list($html, $meta) = lite_page($cacheDir);
    json_out(200, ['version' => $html !== null ? ($meta['version'] ?? '') : '', 'updated' => isset($meta['updated']) ? gmdate('c', $meta['updated']) : null]);
}
if ($f === 'lite') {
    $dest = isset($_SERVER['HTTP_SEC_FETCH_DEST']) ? $_SERVER['HTTP_SEC_FETCH_DEST'] : '';
    if ($_SERVER['REQUEST_METHOD'] !== 'POST' || ($_SERVER['HTTP_X_DPC_INSTALLER'] ?? '') !== '1'
        || in_array($dest, ['document', 'iframe', 'frame', 'embed', 'object'], true)) {
        json_out(400, ['error' => 'installer_only']);
    }
    $admin = $db && shop_admin();
    $cid   = $admin ? 0 : oc_customer_id();
    if (!$db || (!$admin && !$cid)) {
        json_out(401, ['error' => 'login_required']);
    }
    ensure_tables($db);
    if (!$admin && count_events($db, $cid, 'lite', 3600) >= MAX_INSTALLS_PER_HOUR) {
        json_out(429, ['error' => 'too_many_installs']);
    }
    $body  = json_decode(file_get_contents('php://input'), true);
    $board = strtoupper((string)($body['board'] ?? ''));
    $tail  = preg_match('/^[0-9A-F]{12}$/', $board) ? substr($board, -6) : '';
    list($html, $meta) = lite_page($cacheDir, true);
    if ($html === null) {
        json_out(503, ['error' => 'not_available']);
    }
    $gz = plain_gz($html);
    if ($gz === null) {
        json_out(500, ['error' => 'build_failed']);
    }
    log_event($db, $cid, 'lite', $tail);
    header('Content-Type: application/octet-stream');
    header('Content-Length: ' . strlen($gz));
    header('X-DPC-Sha256: ' . hash('sha256', $gz));
    header('X-DPC-Version: ' . preg_replace('/[^A-Za-z0-9 ._:-]/', '', strtok($meta['version'] ?? '', "\n")));
    header('Access-Control-Expose-Headers: X-DPC-Sha256, X-DPC-Version');
    echo $gz;
    exit;
}

// ---------------------------------------------------------------------------
// The board's licence for licence.txt (public: the board's page asks from http://<board>, without cookies)
if ($f === 'licence') {
    header('Access-Control-Allow-Origin: *');
    header('Access-Control-Allow-Methods: GET');
    header('Vary: Origin');
    if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
        http_response_code(204);
        exit;
    }
    $board = strtoupper((string)($_GET['board'] ?? ''));
    if (!preg_match('/^[0-9A-F]{12}$/', $board) || $board === '000000000000') {
        json_out(400, ['status' => 'error', 'error' => 'bad_board']);
    }
    if (!$db) {
        json_out(503, ['status' => 'error', 'error' => 'not_available']);
    }
    ensure_tables($db);
    ensure_licence_table($db);
    // at most 120 checks an hour from one address
    $ip = hash_hmac('sha256', (string)($_SERVER['REMOTE_ADDR'] ?? ''), secret());
    $db->query("INSERT INTO `" . DB_PREFIX . "dpc_webui_check` (`ip_hash`, `window`, `n`) VALUES ('$ip', NOW(), 1)"
             . " ON DUPLICATE KEY UPDATE `n` = IF(`window` < NOW() - INTERVAL 1 HOUR, 1, `n` + 1), `window` = IF(`window` < NOW() - INTERVAL 1 HOUR, NOW(), `window`)");
    $res = $db->query("SELECT `n` FROM `" . DB_PREFIX . "dpc_webui_check` WHERE `ip_hash` = '$ip'");
    $row = $res ? $res->fetch_assoc() : null;
    if ($row && (int)$row['n'] > 120) {
        json_out(429, ['status' => 'error', 'error' => 'too_many_checks']);
    }
    $hash = board_hash($board);
    $lic  = licence_row($db, $hash);
    $slot = slot_owner($db, $hash);
    if (!$lic) {
        if (!$slot) {
            json_out(200, ['status' => 'unknown']);
        }
        // installed before licence.txt existed: start its record now
        $lic = licence_set($db, $hash, $slot['board_tail'], (int)$slot['customer_id']);
    } elseif ($lic['released'] !== null) {
        json_out(200, ['status' => 'released']);
    } else {
        $owner = (int)$lic['owner_id'];
        // still registered? A customer's own slot, or a board the shop installed (its own licence)
        $ok = false;
        if ($slot) {
            $st = $db->prepare("SELECT 1 FROM `" . DB_PREFIX . "dpc_webui_board` WHERE `board_hash` = ? AND (`customer_id` = ? OR `customer_id` = 0) LIMIT 1");
            $st->bind_param('si', $hash, $owner);
            $st->execute();
            $st->store_result();
            $ok = $st->num_rows > 0;
            $st->close();
        }
        if (!$ok) {
            $db->query("UPDATE `" . DB_PREFIX . "dpc_webui_licence` SET `released` = UTC_TIMESTAMP() WHERE `board_hash` = '" . $db->real_escape_string($hash) . "'");
            json_out(200, ['status' => 'released']);
        }
        // the account name changed: a new licence
        $lic = licence_set($db, $hash, $lic['board_tail'], $owner);
    }
    $db->query("UPDATE `" . DB_PREFIX . "dpc_webui_licence` SET `last_check` = UTC_TIMESTAMP() WHERE `board_hash` = '" . $db->real_escape_string($hash) . "'");
    $text = webui_licence($board, $lic['issued'], $lic['owner_name']);
    if ($text === null) {
        json_out(503, ['status' => 'error', 'error' => 'not_available']);
    }
    json_out(200, ['status' => 'ok', 'licence' => $text]);
}

// ---------------------------------------------------------------------------
// Shop admin (Flash station): no account, no board limit; boards go to the shop (customer 0)
if ($db && in_array($f, ['access', 'install'], true) && shop_admin()) {
    ensure_tables($db);
    if ($f === 'access') {
        json_out(200, ['logged_in' => true, 'entitled' => true, 'via' => 'shop', 'admin' => true, 'max_boards' => 9999,
                       'boards' => [], 'can_release' => false, 'next_release' => null]);
    }
    if ($_SERVER['REQUEST_METHOD'] !== 'POST' || ($_SERVER['HTTP_X_DPC_INSTALLER'] ?? '') !== '1') {
        json_out(400, ['error' => 'installer_only']);
    }
    $body  = json_decode(file_get_contents('php://input'), true);
    $board = strtoupper((string)($body['board'] ?? ''));
    if (!preg_match('/^[0-9A-F]{12}$/', $board) || $board === '000000000000') {
        json_out(400, ['error' => 'bad_board']);
    }
    list($html, $meta) = webui_page($cacheDir, true);
    if ($html === null) {
        json_out(503, ['error' => 'not_available']);
    }
    ensure_licence_table($db);
    $old = licence_row($db, board_hash($board));
    // re-flashing a customer's board (service) keeps it theirs
    $lic = licence_set($db, board_hash($board), substr($board, -6), ($old && $old['released'] === null) ? (int)$old['owner_id'] : 0);
    $gz = licensed_gz($html, $board, 0, $lic);
    if ($gz === null) {
        json_out(500, ['error' => 'build_failed']);
    }
    $hash = board_hash($board);
    $tail = substr($board, -6);
    $stmt = $db->prepare("INSERT INTO `" . DB_PREFIX . "dpc_webui_board` (`customer_id`, `board_hash`, `board_tail`, `created`, `last_install`, `installs`) VALUES (0, ?, ?, NOW(), NOW(), 1)"
                       . " ON DUPLICATE KEY UPDATE `last_install` = NOW(), `installs` = `installs` + 1");
    $stmt->bind_param('ss', $hash, $tail);
    $stmt->execute();
    $stmt->close();
    log_event($db, 0, 'install', $tail);
    header('Content-Type: application/octet-stream');
    header('Content-Length: ' . strlen($gz));
    header('X-DPC-Sha256: ' . hash('sha256', $gz));
    header('X-DPC-Version: ' . preg_replace('/[^A-Za-z0-9 ._:-]/', '', strtok($meta['version'] ?? '', "\n")));
    header('Access-Control-Expose-Headers: X-DPC-Sha256, X-DPC-Version');
    echo $gz;
    exit;
}

$customerId = oc_customer_id();
if (!$customerId || !$db) {
    json_out($f === 'access' ? 200 : 401, ['logged_in' => false, 'entitled' => false, 'error' => 'login_required']);
}
ensure_tables($db);

$via = null;
if (oc_has_purchased($customerId, $PAID_PRODUCTS)) {
    $via = 'purchase';
} elseif (oc_has_purchased($customerId, $HARDWARE_PRODUCTS) || oc_has_purchased($customerId, $PENDANT_PRODUCTS, PENDANT_WEBUI_UNTIL)) {
    $via = 'hardware';
}
$list = boards($db, $customerId);
$lastRelease = last_release($db, $customerId);
$canRelease  = $lastRelease < time() - RELEASE_DAYS * 86400;

$state = [
    'logged_in'   => true,
    'entitled'    => $via !== null,
    'via'         => $via,
    'max_boards'  => MAX_BOARDS,
    'boards'      => array_map(function ($b) {
        return ['board' => $b['board_tail'], 'added' => substr($b['created'], 0, 10), 'last_install' => substr($b['last_install'], 0, 10)];
    }, $list),
    'can_release' => $canRelease,
    'next_release' => $canRelease ? null : gmdate('Y-m-d', $lastRelease + RELEASE_DAYS * 86400),
];

if ($f === 'access') {
    json_out(200, $state);
}

// install / release: POST from the installer script only
$dest = isset($_SERVER['HTTP_SEC_FETCH_DEST']) ? $_SERVER['HTTP_SEC_FETCH_DEST'] : '';
if ($_SERVER['REQUEST_METHOD'] !== 'POST' || ($_SERVER['HTTP_X_DPC_INSTALLER'] ?? '') !== '1'
    || in_array($dest, ['document', 'iframe', 'frame', 'embed', 'object'], true)) {
    json_out(400, ['error' => 'installer_only']);
}
$body = json_decode(file_get_contents('php://input'), true);
if (!$via) {
    // no licence on the account: only a board the shop installed (its own licence) can be updated
    $peek = strtoupper((string)($body['board'] ?? ''));
    if (!($f === 'install' && preg_match('/^[0-9A-F]{12}$/', $peek) && shop_board($db, board_hash($peek)))) {
        json_out(403, ['error' => 'licence_required'] + $state);
    }
}

if ($f === 'release') {
    $tail = strtoupper((string)($body['board'] ?? ''));
    if (!preg_match('/^[0-9A-F]{6}$/', $tail)) {
        json_out(400, ['error' => 'bad_board']);
    }
    if (!$canRelease) {
        json_out(429, ['error' => 'release_wait'] + $state);
    }
    $stmt = $db->prepare("DELETE FROM `" . DB_PREFIX . "dpc_webui_board` WHERE `customer_id` = ? AND `board_tail` = ? LIMIT 1");
    $stmt->bind_param('is', $customerId, $tail);
    $stmt->execute();
    $done = $stmt->affected_rows > 0;
    $stmt->close();
    // its licence.txt is withdrawn at the board's next check (also a shop board this customer installed on)
    ensure_licence_table($db);
    $stmt = $db->prepare("UPDATE `" . DB_PREFIX . "dpc_webui_licence` SET `released` = UTC_TIMESTAMP() WHERE `owner_id` = ? AND `board_tail` = ? AND `released` IS NULL");
    $stmt->bind_param('is', $customerId, $tail);
    $stmt->execute();
    $done = $done || $stmt->affected_rows > 0;
    $stmt->close();
    if (!$done) {
        json_out(404, ['error' => 'unknown_board']);
    }
    log_event($db, $customerId, 'release', $tail);
    json_out(200, ['ok' => true]);
}

// f=install
$board = strtoupper((string)($body['board'] ?? ''));
if (!preg_match('/^[0-9A-F]{12}$/', $board) || $board === '000000000000') {
    json_out(400, ['error' => 'bad_board']);
}
if (count_events($db, $customerId, 'install', 3600) >= MAX_INSTALLS_PER_HOUR) {
    json_out(429, ['error' => 'too_many_installs']);
}
$hash = board_hash($board);
$tail = substr($board, -6);
$known = false;
foreach ($list as $b) {
    if (hash_equals($b['board_hash'], $hash)) {
        $known = true;
    }
}
// a board the shop installed (sold with the WebUI) is licensed by itself: no slot used
$shopBoard = !$known && shop_board($db, $hash);
if (!$known && !$shopBoard && count($list) >= MAX_BOARDS) {
    json_out(409, ['error' => 'board_limit'] + $state);
}

list($html, $meta) = webui_page($cacheDir, true);
if ($html === null) {
    json_out(503, ['error' => 'not_available']);
}
ensure_licence_table($db);
// the licence is for this customer (a shop board they install on becomes theirs: new name, new "issued")
$lic = licence_set($db, $hash, $tail, $customerId);
$gz = licensed_gz($html, $board, $customerId, $lic);
if ($gz === null) {
    json_out(500, ['error' => 'build_failed']);
}

if ($known || $shopBoard) {
    $owner = $known ? $customerId : 0;
    $stmt = $db->prepare("UPDATE `" . DB_PREFIX . "dpc_webui_board` SET `last_install` = NOW(), `installs` = `installs` + 1 WHERE `customer_id` = ? AND `board_hash` = ?");
    $stmt->bind_param('is', $owner, $hash);
} else {
    $stmt = $db->prepare("INSERT INTO `" . DB_PREFIX . "dpc_webui_board` (`customer_id`, `board_hash`, `board_tail`, `created`, `last_install`, `installs`) VALUES (?, ?, ?, NOW(), NOW(), 1)");
    $stmt->bind_param('iss', $customerId, $hash, $tail);
}
$stmt->execute();
$stmt->close();
log_event($db, $customerId, 'install', $tail);

header('Content-Type: application/octet-stream');
header('Content-Length: ' . strlen($gz));
header('X-DPC-Sha256: ' . hash('sha256', $gz));
header('X-DPC-Version: ' . preg_replace('/[^A-Za-z0-9 ._:-]/', '', strtok($meta['version'] ?? '', "\n")));
header('Access-Control-Expose-Headers: X-DPC-Sha256, X-DPC-Version');
echo $gz;

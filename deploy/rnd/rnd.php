<?php
/**
 * dpCREATOR R&D Club members: public profiles, build updates, admin approval.
 *
 * Lives in the shop's web root next to OpenCart's config.php (like webui.php).
 *
 *   GET  /rnd.php?f=list            public: approved members and their approved updates (JSON)
 *   GET  /rnd.php?f=me              logged-in customer: own profile, draft, all own updates (JSON)
 *   POST /rnd.php?f=save            member: save profile draft (JSON body); goes live after approval
 *   POST /rnd.php?f=stage           member: set build stage (live at once)
 *   POST /rnd.php?f=photo           member: upload one photo (multipart "photo"); returns its path
 *   POST /rnd.php?f=update          member: post a build update (JSON body); goes live after approval
 *   POST /rnd.php?f=delete_update   member: delete one own update
 *   POST /rnd.php?f=delete_me       member: delete the whole profile, all updates and photos
 *   GET  /rnd.php?f=admin           shop admin: review page (HTML)
 *   POST /rnd.php?f=admin_action    shop admin: invite, approve, reject, feedback, top build, remove
 *
 * Only customers the admin has invited (by account e-mail) can create a profile.
 * Nothing a member writes is public until the admin approves it.
 * Written for PHP 7.2+.
 */

error_reporting(0);
const RND_VERSION   = '2026-10-21';
const RND_MAX_PHOTO = 12 * 1024 * 1024;   // bytes per upload
const RND_MAX_FILES = 80;                 // photos per member
const RND_MAX_EDGE  = 1600;               // px, longest side after resize
const RND_STAGES    = ['licences', 'parts', 'building', 'running', 'report'];
const RND_MEMBERS_URL = '/rnd-members';
const RND_LINKS     = ['youtube', 'instagram', 'facebook', 'tiktok', 'website', 'github', 'other'];

function out_json($code, $data)
{
    http_response_code($code);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store');
    echo json_encode($data, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    exit;
}

// ---------------------------------------------------------------------------
// OpenCart database and session (same approach as webui.php / studio.php)

function oc_db()
{
    if (isset($GLOBALS['oc_db'])) {
        return $GLOBALS['oc_db'];
    }
    $cfg = getenv('RND_CONFIG') ?: __DIR__ . '/config.php';
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
    $db = oc_db();
    $stmt = $db->prepare("SELECT `value` FROM `" . DB_PREFIX . "setting` WHERE `store_id` = 0 AND `key` = ? LIMIT 1");
    $stmt->bind_param('s', $key);
    $stmt->execute();
    $stmt->bind_result($v);
    $val = $stmt->fetch() ? $v : $default;
    $stmt->close();
    return $val === '' ? $default : $val;
}

/** The OpenCart session (customer and admin share the session cookie). */
function session_data()
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
    $name = setting('config_session_name', 'OCSESSID');
    $sid = isset($_COOKIE[$name]) ? $_COOKIE[$name] : '';
    if (!preg_match('/^[A-Za-z0-9,-]{20,64}$/', $sid)) {
        return $s;
    }
    $data = null;
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
    $uid = (int)$s['user_id'];
    $r = oc_db()->query("SELECT `status` FROM `" . DB_PREFIX . "user` WHERE `user_id` = " . $uid . " LIMIT 1");
    $row = $r ? $r->fetch_assoc() : null;
    return $row && (int)$row['status'] === 1;
}

/** POSTs must come from a page on this site (stops cross-site form posts). */
function require_same_origin()
{
    $host = isset($_SERVER['HTTP_HOST']) ? strtolower($_SERVER['HTTP_HOST']) : '';
    $src = isset($_SERVER['HTTP_ORIGIN']) ? $_SERVER['HTTP_ORIGIN'] : (isset($_SERVER['HTTP_REFERER']) ? $_SERVER['HTTP_REFERER'] : '');
    $h = strtolower((string)parse_url($src, PHP_URL_HOST));
    $port = parse_url($src, PHP_URL_PORT);
    if ($port) {
        $h .= ':' . $port;
    }
    if ($host === '' || $h !== $host) {
        out_json(403, ['error' => 'Please use the form on dpcreator.eu.']);
    }
    if (empty($_SERVER['HTTP_X_RND']) ) {
        out_json(403, ['error' => 'Please use the form on dpcreator.eu.']);
    }
}

function body_json()
{
    $d = json_decode((string)file_get_contents('php://input'), true);
    return is_array($d) ? $d : [];
}

// ---------------------------------------------------------------------------
// Storage

function T($name)
{
    return '`' . DB_PREFIX . 'dpc_rnd_' . $name . '`';
}

function ensure_tables()
{
    $db = oc_db();
    $db->query("CREATE TABLE IF NOT EXISTS " . T('member') . " (
        `customer_id` INT NOT NULL PRIMARY KEY,
        `pid` CHAR(10) NOT NULL,
        `status` VARCHAR(12) NOT NULL DEFAULT 'invited',
        `stage` VARCHAR(12) NOT NULL DEFAULT 'licences',
        `top_build` TINYINT NOT NULL DEFAULT 0,
        `consent_at` DATETIME NULL,
        `draft` MEDIUMTEXT NULL,
        `pub` MEDIUMTEXT NULL,
        `pending` TINYINT NOT NULL DEFAULT 0,
        `reject_note` VARCHAR(500) NOT NULL DEFAULT '',
        `feedback` TEXT NULL,
        `invited_at` DATETIME NOT NULL,
        `pub_at` DATETIME NULL,
        `updated_at` DATETIME NULL,
        UNIQUE KEY `pid` (`pid`)
    ) DEFAULT CHARSET=utf8mb4");
    $db->query("CREATE TABLE IF NOT EXISTS " . T('update') . " (
        `id` INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
        `customer_id` INT NOT NULL,
        `title` VARCHAR(120) NOT NULL,
        `body` TEXT NOT NULL,
        `photos` TEXT NOT NULL,
        `video` VARCHAR(300) NOT NULL DEFAULT '',
        `status` VARCHAR(10) NOT NULL DEFAULT 'pending',
        `reject_note` VARCHAR(500) NOT NULL DEFAULT '',
        `feedback` TEXT NULL,
        `created` DATETIME NOT NULL,
        `reviewed` DATETIME NULL,
        KEY `cust` (`customer_id`),
        KEY `status` (`status`, `created`)
    ) DEFAULT CHARSET=utf8mb4");
}

function member($cid)
{
    $r = oc_db()->query("SELECT * FROM " . T('member') . " WHERE `customer_id` = " . (int)$cid . " LIMIT 1");
    return $r ? $r->fetch_assoc() : null;
}

function photo_dir($cid)
{
    return rtrim(DIR_IMAGE, '/') . '/catalog/rnd/' . (int)$cid . '/';
}

function photo_url($rel)
{
    return '/image/' . ltrim($rel, '/');
}

/** A photo path a member may reference: their own folder, a file that exists. */
function own_photo($cid, $rel)
{
    $rel = (string)$rel;
    if (!preg_match('#^catalog/rnd/' . (int)$cid . '/[a-f0-9]{16}\.jpg$#', $rel)) {
        return '';
    }
    return is_file(rtrim(DIR_IMAGE, '/') . '/' . $rel) ? $rel : '';
}

function remove_dir($dir)
{
    if (!is_dir($dir)) {
        return;
    }
    foreach (glob($dir . '*') ?: [] as $f) {
        if (is_file($f)) {
            unlink($f);
        }
    }
    @rmdir($dir);
}

// ---------------------------------------------------------------------------
// Validation

function clean_text($v, $max, $multiline = false)
{
    $v = is_string($v) ? $v : '';
    $v = str_replace("\r", '', $v);
    $v = preg_replace($multiline ? '/[^\P{C}\n]/u' : '/\p{C}/u', '', $v);
    if (!$multiline) {
        $v = preg_replace('/\s+/u', ' ', $v);
    } else {
        $v = preg_replace("/\n{3,}/", "\n\n", $v);
    }
    $v = trim((string)$v);
    if (function_exists('mb_substr')) {
        return mb_substr($v, 0, $max, 'UTF-8');
    }
    return substr($v, 0, $max);
}

function clean_url($v)
{
    $v = trim(is_string($v) ? $v : '');
    if ($v === '') {
        return '';
    }
    if (!preg_match('#^https?://#i', $v)) {
        $v = 'https://' . $v;
    }
    $p = parse_url($v);
    if (!$p || empty($p['host']) || !preg_match('/^[a-z0-9.-]+\.[a-z]{2,}$/i', $p['host']) || strlen($v) > 300) {
        return null;   // invalid
    }
    return $v;
}

function youtube_id($url)
{
    if (preg_match('#(?:youtube\.com/(?:watch\?(?:.*&)?v=|shorts/|embed/|live/)|youtu\.be/)([A-Za-z0-9_-]{11})#', $url, $m)) {
        return $m[1];
    }
    return '';
}

function clean_profile($in, $cid)
{
    $err = [];
    $p = [
        'name'       => clean_text(isset($in['name']) ? $in['name'] : '', 60),
        'location'   => clean_text(isset($in['location']) ? $in['location'] : '', 60),
        'machine'    => clean_text(isset($in['machine']) ? $in['machine'] : '', 140),
        'controller' => clean_text(isset($in['controller']) ? $in['controller'] : '', 90),
        'work'       => clean_text(isset($in['work']) ? $in['work'] : '', 140),
        'bio'        => clean_text(isset($in['bio']) ? $in['bio'] : '', 900, true),
        'avatar'     => own_photo($cid, isset($in['avatar']) ? $in['avatar'] : ''),
        'links'      => [],
    ];
    if ($p['name'] === '') {
        $err['name'] = 'Enter a name or nickname.';
    }
    if ($p['machine'] === '') {
        $err['machine'] = 'Tell people what machine you are building on.';
    }
    $links = isset($in['links']) && is_array($in['links']) ? $in['links'] : [];
    foreach (RND_LINKS as $k) {
        $u = clean_url(isset($links[$k]) ? $links[$k] : '');
        if ($u === null) {
            $err['link_' . $k] = 'This does not look like a web address.';
        } elseif ($u !== '') {
            $p['links'][$k] = $u;
        }
    }
    return [$p, $err];
}

// ---------------------------------------------------------------------------
// Photos

function save_photo($cid, $tmp)
{
    if (!function_exists('imagecreatefromstring')) {
        return [null, 'Photo uploads are not available on this server.'];
    }
    $raw = file_get_contents($tmp);
    $info = @getimagesizefromstring($raw);
    if (!$info || !in_array($info[2], [IMAGETYPE_JPEG, IMAGETYPE_PNG, IMAGETYPE_WEBP], true)) {
        return [null, 'Use a JPG, PNG or WebP photo.'];
    }
    if ($info[0] * $info[1] > 60000000) {
        return [null, 'This photo is too large.'];
    }
    $img = @imagecreatefromstring($raw);
    if (!$img) {
        return [null, 'This photo could not be read.'];
    }
    // Phone photos: apply the EXIF rotation, because re-encoding drops the EXIF data (and GPS with it).
    if ($info[2] === IMAGETYPE_JPEG && function_exists('exif_read_data')) {
        $exif = @exif_read_data('data://image/jpeg;base64,' . base64_encode($raw));
        $o = isset($exif['Orientation']) ? (int)$exif['Orientation'] : 1;
        if ($o === 3) { $img = imagerotate($img, 180, 0); }
        if ($o === 6) { $img = imagerotate($img, -90, 0); }
        if ($o === 8) { $img = imagerotate($img, 90, 0); }
    }
    $w = imagesx($img);
    $h = imagesy($img);
    $scale = min(1, RND_MAX_EDGE / max($w, $h));
    $nw = max(1, (int)round($w * $scale));
    $nh = max(1, (int)round($h * $scale));
    $dst = imagecreatetruecolor($nw, $nh);
    imagefill($dst, 0, 0, imagecolorallocate($dst, 255, 255, 255));
    imagecopyresampled($dst, $img, 0, 0, 0, 0, $nw, $nh, $w, $h);
    $dir = photo_dir($cid);
    if (!is_dir($dir) && !@mkdir($dir, 0755, true)) {
        return [null, 'Could not store the photo.'];
    }
    if (count(glob($dir . '*.jpg') ?: []) >= RND_MAX_FILES) {
        return [null, 'You have reached the photo limit. Delete an old update first.'];
    }
    $name = bin2hex(random_bytes(8)) . '.jpg';
    if (!imagejpeg($dst, $dir . $name, 84)) {
        return [null, 'Could not store the photo.'];
    }
    return ['catalog/rnd/' . (int)$cid . '/' . $name, null];
}

// ---------------------------------------------------------------------------
// Output shapes

function profile_out($json)
{
    $p = json_decode((string)$json, true);
    if (!is_array($p)) {
        return null;
    }
    $p['avatar_url'] = !empty($p['avatar']) ? photo_url($p['avatar']) : '';
    return $p;
}

function update_out($u, $own)
{
    $photos = json_decode($u['photos'], true) ?: [];
    $o = [
        'id'       => (int)$u['id'],
        'title'    => $u['title'],
        'body'     => $u['body'],
        'photos'   => array_map('photo_url', $photos),
        'video'    => $u['video'],
        'youtube'  => youtube_id($u['video']),
        'date'     => substr($u['created'], 0, 10),
        'feedback' => (string)$u['feedback'],
    ];
    if ($own) {
        $o['status'] = $u['status'];
        $o['reject_note'] = $u['reject_note'];
        $o['photo_paths'] = $photos;
    }
    return $o;
}

function notify_admin($subject, $text)
{
    $to = setting('config_email', '');
    if ($to === '' || !function_exists('mail')) {
        return;
    }
    $link = (isset($_SERVER['HTTP_HOST']) ? 'https://' . $_SERVER['HTTP_HOST'] : '') . '/rnd.php?f=admin';
    @mail($to, '=?UTF-8?B?' . base64_encode('R&D Club: ' . $subject) . '?=', $text . "\n\nReview it: " . $link . "\n",
        "Content-Type: text/plain; charset=UTF-8\r\nFrom: " . $to);
}

// ---------------------------------------------------------------------------
// Admin review page

function h($v)
{
    return htmlspecialchars((string)$v, ENT_QUOTES, 'UTF-8');
}

function admin_page()
{
    $db = oc_db();
    $rows = [];
    $r = $db->query("SELECT m.*, c.firstname, c.lastname, c.email FROM " . T('member') . " m LEFT JOIN `" . DB_PREFIX . "customer` c ON c.customer_id = m.customer_id ORDER BY m.pending DESC, m.invited_at DESC");
    while ($r && ($m = $r->fetch_assoc())) {
        $rows[] = $m;
    }
    $names = [];
    foreach ($rows as $m) {
        $p = json_decode((string)($m['pub'] ?: $m['draft']), true);
        $names[$m['customer_id']] = !empty($p['name']) ? $p['name'] : trim($m['firstname'] . ' ' . $m['lastname']);
    }
    $ups = [];
    $r = $db->query("SELECT * FROM " . T('update') . " ORDER BY FIELD(`status`, 'pending', 'approved', 'rejected'), `created` DESC LIMIT 200");
    while ($r && ($u = $r->fetch_assoc())) {
        $ups[] = $u;
    }
    $stageLabel = ['licences' => 'Got the licences', 'parts' => 'Parts ready', 'building' => 'Building', 'running' => 'First job run', 'report' => 'Report sent'];
    header('Content-Type: text/html; charset=utf-8');
    header('Cache-Control: no-store');
    header('X-Robots-Tag: noindex');
    ?><!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="robots" content="noindex">
<title>R&amp;D Club: review</title>
<style>
:root{--bg:#0D1014;--card:#151B22;--line:#26303A;--text:#EEF2F5;--body:#C3CCD5;--muted:#95A2AF;--blue:#1F7BC7;--green:#2E9B4A;--red:#C94040;--amber:#E7B566}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--body);font:16px/1.55 system-ui,Segoe UI,Roboto,sans-serif}
main{max-width:1200px;margin:0 auto;padding:28px 18px 60px}h1{color:var(--text);font-size:30px;margin:0 0 4px}h2{color:var(--text);font-size:22px;margin:34px 0 12px}
.muted{color:var(--muted)}.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px 18px;margin:0 0 14px}
.card.pending{border-left:4px solid var(--amber)}.row{display:flex;gap:14px;flex-wrap:wrap;align-items:flex-start}
.pill{display:inline-block;font-size:13px;padding:1px 9px;border-radius:999px;border:1px solid var(--line);color:var(--body)}
.pill.pending{border-color:var(--amber);color:var(--amber)}.pill.approved,.pill.active{border-color:var(--green);color:#7FD99A}.pill.rejected,.pill.hidden{border-color:var(--red);color:#F08F8F}
button{font:inherit;font-size:14px;border:1px solid #33404C;background:#1A2129;color:var(--text);border-radius:6px;padding:6px 12px;cursor:pointer}
button.ok{background:var(--green);border-color:var(--green)}button.no{background:#3A1E22;border-color:#5A2A2E}button.blue{background:var(--blue);border-color:var(--blue)}
input,textarea{font:inherit;font-size:15px;background:#0A0E13;color:var(--text);border:1px solid #33404C;border-radius:6px;padding:7px 10px}
textarea{width:100%;min-height:70px}.photos{display:flex;gap:8px;flex-wrap:wrap;margin:8px 0}.photos img{height:110px;border-radius:6px;border:1px solid var(--line)}
.avatar{width:72px;height:72px;border-radius:50%;object-fit:cover;border:1px solid var(--line)}dl{display:grid;grid-template-columns:120px 1fr;gap:2px 12px;margin:8px 0}dt{color:var(--muted)}dd{margin:0;white-space:pre-wrap;word-break:break-word}
table{width:100%;border-collapse:collapse}td,th{border-bottom:1px solid var(--line);padding:8px 6px;text-align:left;vertical-align:top}th{color:var(--muted);font-weight:500}
.actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}#msg{position:fixed;right:16px;bottom:16px;background:#13202B;border:1px solid #234257;color:#D6E6F1;padding:10px 14px;border-radius:8px;display:none}
a{color:#7CCBF7}
</style></head><body><main>
<h1>R&amp;D Club: review</h1>
<p class="muted">Nothing a member writes is public until you approve it. Public page: <a href="<?php echo h(RND_MEMBERS_URL); ?>">R&amp;D Club members</a>.</p>

<h2>Invite a tester</h2>
<div class="card"><p class="muted" style="margin-top:0">They need a dpcreator.eu account. Enter the e-mail of that account; they can then fill in their profile on "My R&amp;D profile".</p>
<div class="row"><input id="invite" type="email" placeholder="tester@example.com" style="min-width:300px"><button class="blue" onclick="act({action:'invite',email:document.getElementById('invite').value})">Invite</button></div></div>

<h2>Profiles</h2>
<?php if (!$rows) { echo '<p class="muted">No members yet.</p>'; } ?>
<?php foreach ($rows as $m):
    $d = json_decode((string)$m['draft'], true);
    $cid = (int)$m['customer_id'];
    ?>
<div class="card<?php echo (int)$m['pending'] ? ' pending' : ''; ?>">
  <div class="row">
    <?php if (!empty($d['avatar'])): ?><img class="avatar" src="<?php echo h(photo_url($d['avatar'])); ?>" alt=""><?php endif; ?>
    <div style="flex:1;min-width:260px">
      <strong style="color:var(--text);font-size:18px"><?php echo h($names[$cid]); ?></strong>
      <span class="pill <?php echo h($m['status']); ?>"><?php echo h($m['status']); ?></span>
      <?php if ((int)$m['pending']): ?><span class="pill pending">changes to review</span><?php endif; ?>
      <?php if ((int)$m['top_build']): ?><span class="pill approved">top build</span><?php endif; ?>
      <div class="muted"><?php echo h($m['email']); ?> · stage: <?php echo h($stageLabel[$m['stage']] ?? $m['stage']); ?> · invited <?php echo h(substr($m['invited_at'], 0, 10)); ?></div>
      <?php if (is_array($d)): ?>
      <dl>
        <dt>Machine</dt><dd><?php echo h($d['machine']); ?></dd>
        <dt>Controller</dt><dd><?php echo h($d['controller']); ?></dd>
        <dt>Work</dt><dd><?php echo h($d['work']); ?></dd>
        <dt>Location</dt><dd><?php echo h($d['location']); ?></dd>
        <dt>About</dt><dd><?php echo h($d['bio']); ?></dd>
        <dt>Links</dt><dd><?php foreach ((array)$d['links'] as $k => $u) { echo h($k) . ': <a href="' . h($u) . '" rel="nofollow noopener" target="_blank">' . h($u) . '</a><br>'; } ?></dd>
      </dl>
      <?php else: ?><p class="muted">No profile yet.</p><?php endif; ?>
      <label class="muted">Your feedback on this build (public)</label>
      <textarea id="mf<?php echo $cid; ?>"><?php echo h($m['feedback']); ?></textarea>
      <div class="actions">
        <?php if ((int)$m['pending']): ?>
        <button class="ok" onclick="act({action:'approve_profile',customer_id:<?php echo $cid; ?>})">Approve profile</button>
        <button class="no" onclick="reject('reject_profile',{customer_id:<?php echo $cid; ?>})">Send back with a note</button>
        <?php endif; ?>
        <button onclick="act({action:'member_feedback',customer_id:<?php echo $cid; ?>,feedback:document.getElementById('mf<?php echo $cid; ?>').value})">Save feedback</button>
        <button onclick="act({action:'top_build',customer_id:<?php echo $cid; ?>,value:<?php echo (int)$m['top_build'] ? 0 : 1; ?>})"><?php echo (int)$m['top_build'] ? 'Remove top build' : 'Mark as top build'; ?></button>
        <?php if ($m['status'] === 'active'): ?><button onclick="act({action:'hide',customer_id:<?php echo $cid; ?>})">Hide from public</button><?php endif; ?>
        <?php if ($m['status'] === 'hidden'): ?><button onclick="act({action:'unhide',customer_id:<?php echo $cid; ?>})">Show again</button><?php endif; ?>
        <button class="no" onclick="if(confirm('End this membership? Their profile, updates and photos are removed. The account stays.'))act({action:'remove_member',customer_id:<?php echo $cid; ?>})">End membership</button>
      </div>
    </div>
  </div>
</div>
<?php endforeach; ?>

<h2>Build updates</h2>
<?php if (!$ups) { echo '<p class="muted">No updates yet.</p>'; } ?>
<?php foreach ($ups as $u): $id = (int)$u['id']; ?>
<div class="card<?php echo $u['status'] === 'pending' ? ' pending' : ''; ?>">
  <strong style="color:var(--text)"><?php echo h($u['title']); ?></strong>
  <span class="pill <?php echo h($u['status']); ?>"><?php echo h($u['status']); ?></span>
  <span class="muted"> · <?php echo h($names[$u['customer_id']] ?? ('customer ' . $u['customer_id'])); ?> · <?php echo h(substr($u['created'], 0, 16)); ?></span>
  <p style="white-space:pre-wrap"><?php echo h($u['body']); ?></p>
  <div class="photos"><?php foreach (json_decode($u['photos'], true) ?: [] as $ph) { echo '<a href="' . h(photo_url($ph)) . '" target="_blank"><img src="' . h(photo_url($ph)) . '" alt=""></a>'; } ?></div>
  <?php if ($u['video'] !== ''): ?><p>Video: <a href="<?php echo h($u['video']); ?>" target="_blank" rel="nofollow noopener"><?php echo h($u['video']); ?></a></p><?php endif; ?>
  <?php if ($u['reject_note'] !== ''): ?><p class="muted">Note sent: <?php echo h($u['reject_note']); ?></p><?php endif; ?>
  <label class="muted">Your feedback (shown under the update)</label>
  <textarea id="uf<?php echo $id; ?>"><?php echo h($u['feedback']); ?></textarea>
  <div class="actions">
    <?php if ($u['status'] !== 'approved'): ?><button class="ok" onclick="act({action:'approve_update',id:<?php echo $id; ?>})">Approve</button><?php endif; ?>
    <?php if ($u['status'] !== 'rejected'): ?><button class="no" onclick="reject('reject_update',{id:<?php echo $id; ?>})">Reject with a note</button><?php endif; ?>
    <button onclick="act({action:'update_feedback',id:<?php echo $id; ?>,feedback:document.getElementById('uf<?php echo $id; ?>').value})">Save feedback</button>
  </div>
</div>
<?php endforeach; ?>
<div id="msg"></div>
<script>
function act(body){
  fetch('rnd.php?f=admin_action',{method:'POST',headers:{'Content-Type':'application/json','X-RND':'1'},body:JSON.stringify(body)})
  .then(function(r){return r.json();}).then(function(j){
    var m=document.getElementById('msg'); m.textContent=j.error||j.message||'Saved.'; m.style.display='block';
    if(!j.error) setTimeout(function(){location.reload();},700); else setTimeout(function(){m.style.display='none';},4000);
  });
}
function reject(action,o){var n=prompt('Note for the member (they see it):','');if(n===null)return;o.action=action;o.note=n;act(o);}
</script>
</main></body></html>
<?php
}

// ---------------------------------------------------------------------------
// Routes

$f = isset($_GET['f']) ? $_GET['f'] : 'list';
if (!oc_db()) {
    out_json(500, ['error' => 'The club is not available right now.']);
}
ensure_tables();
$db = oc_db();
$method = isset($_SERVER['REQUEST_METHOD']) ? $_SERVER['REQUEST_METHOD'] : 'GET';

if ($f === 'list') {
    $members = [];
    $r = $db->query("SELECT * FROM " . T('member') . " WHERE `status` = 'active' AND `pub` IS NOT NULL AND `consent_at` IS NOT NULL ORDER BY `top_build` DESC, `pub_at` ASC");
    while ($r && ($m = $r->fetch_assoc())) {
        $p = profile_out($m['pub']);
        if (!$p) {
            continue;
        }
        $ups = [];
        $u = $db->query("SELECT * FROM " . T('update') . " WHERE `customer_id` = " . (int)$m['customer_id'] . " AND `status` = 'approved' ORDER BY `created` DESC");
        while ($u && ($row = $u->fetch_assoc())) {
            $ups[] = update_out($row, false);
        }
        unset($p['avatar']);
        $members[] = [
            'pid'       => $m['pid'],
            'profile'   => $p,
            'stage'     => $m['stage'],
            'top_build' => (int)$m['top_build'] === 1,
            'feedback'  => (string)$m['feedback'],
            'since'     => substr((string)$m['pub_at'], 0, 10),
            'updates'   => $ups,
        ];
    }
    header('Cache-Control: public, max-age=120');
    out_json(200, ['version' => RND_VERSION, 'stages' => RND_STAGES, 'members' => $members]);
}

if ($f === 'admin' || $f === 'admin_action') {
    if (!is_admin()) {
        if ($f === 'admin') {
            http_response_code(403);
            header('Content-Type: text/plain; charset=utf-8');
            exit("Log in to the shop admin first, then open this page again in the same browser.\n");
        }
        out_json(403, ['error' => 'Admins only.']);
    }
    if ($f === 'admin_action') {
        if ($method !== 'POST') {
            out_json(405, ['error' => 'POST only.']);
        }
        require_same_origin();
        $in = body_json();
        $a = isset($in['action']) ? $in['action'] : '';
        $cid = isset($in['customer_id']) ? (int)$in['customer_id'] : 0;
        $id = isset($in['id']) ? (int)$in['id'] : 0;
        $note = clean_text(isset($in['note']) ? $in['note'] : '', 500);
        if ($a === 'invite') {
            $email = trim(strtolower(isset($in['email']) ? $in['email'] : ''));
            $stmt = $db->prepare("SELECT `customer_id`, `firstname`, `lastname` FROM `" . DB_PREFIX . "customer` WHERE LOWER(`email`) = ? LIMIT 1");
            $stmt->bind_param('s', $email);
            $stmt->execute();
            $stmt->bind_result($c, $fn, $ln);
            $found = $stmt->fetch();
            $stmt->close();
            if (!$found) {
                out_json(404, ['error' => 'No account with that e-mail. Ask the tester to register first.']);
            }
            if (member($c)) {
                $db->query("UPDATE " . T('member') . " SET `status` = IF(`status` = 'removed', 'invited', `status`) WHERE `customer_id` = " . (int)$c);
                out_json(200, ['ok' => true, 'message' => $fn . ' ' . $ln . ' is already invited.']);
            }
            $pid = substr(bin2hex(random_bytes(5)), 0, 10);
            $stmt = $db->prepare("INSERT INTO " . T('member') . " (`customer_id`, `pid`, `status`, `invited_at`) VALUES (?, ?, 'invited', NOW())");
            $stmt->bind_param('is', $c, $pid);
            $stmt->execute();
            out_json(200, ['ok' => true, 'message' => $fn . ' ' . $ln . ' can now create a profile.']);
        }
        if ($a === 'approve_profile' || $a === 'reject_profile') {
            $m = member($cid);
            if (!$m || $m['draft'] === null) {
                out_json(404, ['error' => 'Nothing to review.']);
            }
            if ($a === 'approve_profile') {
                $stmt = $db->prepare("UPDATE " . T('member') . " SET `pub` = `draft`, `pending` = 0, `reject_note` = '', `status` = 'active', `pub_at` = IFNULL(`pub_at`, NOW()) WHERE `customer_id` = ?");
                $stmt->bind_param('i', $cid);
            } else {
                $stmt = $db->prepare("UPDATE " . T('member') . " SET `pending` = 0, `reject_note` = ? WHERE `customer_id` = ?");
                $stmt->bind_param('si', $note, $cid);
            }
            $stmt->execute();
            out_json(200, ['ok' => true]);
        }
        if ($a === 'approve_update' || $a === 'reject_update') {
            $st = $a === 'approve_update' ? 'approved' : 'rejected';
            $stmt = $db->prepare("UPDATE " . T('update') . " SET `status` = ?, `reject_note` = ?, `reviewed` = NOW() WHERE `id` = ?");
            $stmt->bind_param('ssi', $st, $note, $id);
            $stmt->execute();
            out_json(200, ['ok' => true]);
        }
        if ($a === 'update_feedback') {
            $fb = clean_text(isset($in['feedback']) ? $in['feedback'] : '', 1500, true);
            $stmt = $db->prepare("UPDATE " . T('update') . " SET `feedback` = ? WHERE `id` = ?");
            $stmt->bind_param('si', $fb, $id);
            $stmt->execute();
            out_json(200, ['ok' => true]);
        }
        if ($a === 'member_feedback') {
            $fb = clean_text(isset($in['feedback']) ? $in['feedback'] : '', 1500, true);
            $stmt = $db->prepare("UPDATE " . T('member') . " SET `feedback` = ? WHERE `customer_id` = ?");
            $stmt->bind_param('si', $fb, $cid);
            $stmt->execute();
            out_json(200, ['ok' => true]);
        }
        if ($a === 'top_build') {
            $v = !empty($in['value']) ? 1 : 0;
            $db->query("UPDATE " . T('member') . " SET `top_build` = " . $v . " WHERE `customer_id` = " . $cid);
            out_json(200, ['ok' => true]);
        }
        if ($a === 'hide' || $a === 'unhide') {
            $st = $a === 'hide' ? 'hidden' : 'active';
            $db->query("UPDATE " . T('member') . " SET `status` = '" . $st . "' WHERE `customer_id` = " . $cid . " AND `status` IN ('active','hidden')");
            out_json(200, ['ok' => true]);
        }
        if ($a === 'remove_member') {
            // Ends membership: the profile, updates and photos are removed (the account itself stays).
            $db->query("DELETE FROM " . T('update') . " WHERE `customer_id` = " . $cid);
            $db->query("DELETE FROM " . T('member') . " WHERE `customer_id` = " . $cid);
            remove_dir(photo_dir($cid));
            out_json(200, ['ok' => true]);
        }
        out_json(400, ['error' => 'Unknown action.']);
    }
    admin_page();
    exit;
}

// ---- member routes ----
$cid = customer_id();
if ($f === 'me') {
    if (!$cid) {
        out_json(200, ['logged_in' => false]);
    }
    $m = member($cid);
    if (!$m || $m['status'] === 'removed') {
        out_json(200, ['logged_in' => true, 'member' => false]);
    }
    $ups = [];
    $u = $db->query("SELECT * FROM " . T('update') . " WHERE `customer_id` = " . $cid . " ORDER BY `created` DESC");
    while ($u && ($row = $u->fetch_assoc())) {
        $ups[] = update_out($row, true);
    }
    $draft = $m['draft'] !== null ? json_decode($m['draft'], true) : null;
    if (is_array($draft)) {
        $draft['avatar_url'] = !empty($draft['avatar']) ? photo_url($draft['avatar']) : '';
    }
    out_json(200, [
        'logged_in'   => true,
        'member'      => true,
        'pid'         => $m['pid'],
        'status'      => $m['status'],
        'stage'       => $m['stage'],
        'stages'      => RND_STAGES,
        'links'       => RND_LINKS,
        'consent'     => $m['consent_at'] !== null,
        'draft'       => $draft,
        'published'   => $m['pub'] !== null,
        'pending'     => (int)$m['pending'] === 1,
        'reject_note' => $m['reject_note'],
        'feedback'    => (string)$m['feedback'],
        'updates'     => $ups,
    ]);
}

if ($method !== 'POST') {
    out_json(405, ['error' => 'Unknown request.']);
}
require_same_origin();
if (!$cid) {
    out_json(401, ['error' => 'Log in to your dpcreator.eu account first.']);
}
$m = member($cid);
if (!$m || $m['status'] === 'removed') {
    out_json(403, ['error' => 'Profiles are for R&D Club members. Apply on the R&D Club page.']);
}

if ($f === 'photo') {
    if (empty($_FILES['photo']) || !is_uploaded_file($_FILES['photo']['tmp_name'])) {
        out_json(400, ['error' => 'Choose a photo.']);
    }
    if ($_FILES['photo']['size'] > RND_MAX_PHOTO) {
        out_json(400, ['error' => 'Photos can be up to 12 MB.']);
    }
    list($rel, $err) = save_photo($cid, $_FILES['photo']['tmp_name']);
    if ($err) {
        out_json(400, ['error' => $err]);
    }
    out_json(200, ['path' => $rel, 'url' => photo_url($rel)]);
}

if ($f === 'save') {
    $in = body_json();
    if (empty($in['consent'])) {
        out_json(400, ['errors' => ['consent' => 'Tick the box to agree that your profile is shown publicly.']]);
    }
    list($p, $err) = clean_profile($in, $cid);
    if ($err) {
        out_json(400, ['errors' => $err]);
    }
    $json = json_encode($p, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    $changed = $json !== $m['pub'];
    $pending = $changed ? 1 : 0;
    $stmt = $db->prepare("UPDATE " . T('member') . " SET `draft` = ?, `pending` = ?, `reject_note` = '', `consent_at` = IFNULL(`consent_at`, NOW()), `updated_at` = NOW() WHERE `customer_id` = ?");
    $stmt->bind_param('sii', $json, $pending, $cid);
    $stmt->execute();
    if ($changed) {
        notify_admin('profile to review', $p['name'] . ' saved their profile.');
    }
    out_json(200, ['ok' => true, 'pending' => $changed]);
}

if ($f === 'stage') {
    $in = body_json();
    $st = isset($in['stage']) ? $in['stage'] : '';
    if (!in_array($st, RND_STAGES, true)) {
        out_json(400, ['error' => 'Unknown stage.']);
    }
    $stmt = $db->prepare("UPDATE " . T('member') . " SET `stage` = ?, `updated_at` = NOW() WHERE `customer_id` = ?");
    $stmt->bind_param('si', $st, $cid);
    $stmt->execute();
    out_json(200, ['ok' => true]);
}

if ($f === 'update') {
    $in = body_json();
    $title = clean_text(isset($in['title']) ? $in['title'] : '', 120);
    $body = clean_text(isset($in['body']) ? $in['body'] : '', 3000, true);
    $err = [];
    if ($title === '') {
        $err['title'] = 'Give the update a short title.';
    }
    if ($body === '') {
        $err['body'] = 'Write what you did, what worked and what did not.';
    }
    $photos = [];
    foreach ((isset($in['photos']) && is_array($in['photos']) ? $in['photos'] : []) as $ph) {
        $ok = own_photo($cid, $ph);
        if ($ok !== '' && !in_array($ok, $photos, true)) {
            $photos[] = $ok;
        }
    }
    $photos = array_slice($photos, 0, 8);
    $video = clean_url(isset($in['video']) ? $in['video'] : '');
    if ($video === null) {
        $err['video'] = 'This does not look like a web address.';
    }
    if ($err) {
        out_json(400, ['errors' => $err]);
    }
    // Light flood control: at most 10 updates a day.
    $r = $db->query("SELECT COUNT(*) AS n FROM " . T('update') . " WHERE `customer_id` = " . $cid . " AND `created` > NOW() - INTERVAL 1 DAY");
    if ($r && (int)$r->fetch_assoc()['n'] >= 10) {
        out_json(429, ['error' => 'That is a lot of updates for one day. Try again tomorrow.']);
    }
    $pj = json_encode($photos, JSON_UNESCAPED_SLASHES);
    $v = (string)$video;
    $stmt = $db->prepare("INSERT INTO " . T('update') . " (`customer_id`, `title`, `body`, `photos`, `video`, `status`, `created`) VALUES (?, ?, ?, ?, ?, 'pending', NOW())");
    $stmt->bind_param('issss', $cid, $title, $body, $pj, $v);
    $stmt->execute();
    notify_admin('build update to review', $title);
    out_json(200, ['ok' => true, 'id' => $stmt->insert_id]);
}

if ($f === 'delete_update') {
    $in = body_json();
    $id = isset($in['id']) ? (int)$in['id'] : 0;
    $r = $db->query("SELECT `photos` FROM " . T('update') . " WHERE `id` = " . $id . " AND `customer_id` = " . $cid . " LIMIT 1");
    $row = $r ? $r->fetch_assoc() : null;
    if (!$row) {
        out_json(404, ['error' => 'Update not found.']);
    }
    $db->query("DELETE FROM " . T('update') . " WHERE `id` = " . $id . " AND `customer_id` = " . $cid);
    // Remove its photos unless the profile picture uses one of them.
    $keep = [];
    foreach ([$m['draft'], $m['pub']] as $pj) {
        $p = json_decode((string)$pj, true);
        if (!empty($p['avatar'])) {
            $keep[] = $p['avatar'];
        }
    }
    foreach (json_decode($row['photos'], true) ?: [] as $ph) {
        if (own_photo($cid, $ph) !== '' && !in_array($ph, $keep, true)) {
            unlink(rtrim(DIR_IMAGE, '/') . '/' . $ph);
        }
    }
    out_json(200, ['ok' => true]);
}

if ($f === 'delete_me') {
    $db->query("DELETE FROM " . T('update') . " WHERE `customer_id` = " . $cid);
    // Keep the invitation so they can start again; everything they wrote and uploaded goes.
    $db->query("UPDATE " . T('member') . " SET `status` = 'invited', `draft` = NULL, `pub` = NULL, `pending` = 0, `consent_at` = NULL, `feedback` = NULL, `top_build` = 0, `pub_at` = NULL, `reject_note` = '' WHERE `customer_id` = " . $cid);
    remove_dir(photo_dir($cid));
    out_json(200, ['ok' => true]);
}

out_json(404, ['error' => 'Unknown request.']);

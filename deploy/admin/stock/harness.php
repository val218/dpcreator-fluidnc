<?php
define('DB_PREFIX', 'oc_'); define('DIR_UPLOAD', __DIR__ . '/upl/'); define('DIR_APPLICATION', '/tmp/claude-0/scr/stock/upload/admin/'); define('HTTPS_CATALOG','https://x/');
class DB { public $m; function __construct(){ $this->m=new mysqli('localhost','t','t','oct'); $this->m->set_charset('utf8mb4'); }
 function query($sql){ $r=$this->m->query($sql); if($r===false) throw new Exception($this->m->error."\n".$sql); if($r===true) return true; $o=new stdClass; $o->rows=$r->fetch_all(MYSQLI_ASSOC); $o->row=$o->rows?$o->rows[0]:array(); $o->num_rows=count($o->rows); return $o; }
 function escape($s){ return $this->m->real_escape_string($s); } function getLastId(){ return $this->m->insert_id; } }
class Cfg { public $d=array('config_language_id'=>1,'config_processing_status'=>array(2),'config_complete_status'=>array(5),'config_order_status_id'=>1); function get($k){ return isset($this->d[$k])?$this->d[$k]:null; } }
class U { function hasPermission($a,$r){ return true; } function getUserName(){ return 'val'; } }
class Req { public $get=array(), $post=array(), $files=array(), $server=array('REQUEST_METHOD'=>'GET'); }
class Res { public $out; function addHeader($h){} function setOutput($o){ $this->out=$o; } function redirect($u){} }
class Controller { protected $registry; function __construct($r){ $this->db=$GLOBALS['db']; $this->config=$GLOBALS['cfg']; $this->user=new U; $this->request=$GLOBALS['req']; $this->response=$GLOBALS['res']; $this->session=(object)array('data'=>array('user_token'=>'x')); } }
$db=new DB; $cfg=new Cfg; $req=new Req; $res=new Res;
require DIR_APPLICATION . 'controller/extension/module/dpc_stock.php';
$GLOBALS['INPUT']='';
function call($f,$in=null,$get=array()){ global $req,$res; $req->get=array_merge(array('f'=>$f),$get); $GLOBALS['INPUT']=$in===null?'':json_encode($in); $c=new ControllerExtensionModuleDpcStock(null); $c->api(); $j=json_decode($res->out,true); if(isset($j['error'])) echo "  ERR($f): ".$j['error']."\n"; return $j===null?$res->out:$j; }

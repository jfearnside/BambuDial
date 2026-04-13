#pragma once

/* Embedded HTML for the setup portal — served at http://192.168.4.1/ */
static const char SETUP_HTML[] = R"rawhtml(
<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BambuDial Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,sans-serif;background:#111;color:#fff;padding:16px;max-width:480px;margin:0 auto}
h1{font-size:22px;margin-bottom:4px;color:#00FF00}
.sub{color:#666;font-size:13px;margin-bottom:20px}
fieldset{border:1px solid #333;border-radius:12px;padding:16px;margin-bottom:16px}
legend{color:#87CEEB;font-size:14px;padding:0 8px}
label{display:block;color:#aaa;font-size:12px;margin:10px 0 4px}
label:first-child{margin-top:0}
input,select{width:100%;padding:10px;background:#1a1a1a;border:1px solid #333;border-radius:8px;color:#fff;font-size:14px}
input:focus,select:focus{border-color:#00FF00;outline:none}
.row{display:flex;gap:8px}
.row>*{flex:1}
.printer-card{background:#1a1a1a;border:1px solid #333;border-radius:8px;padding:12px;margin:8px 0}
.printer-card h3{font-size:14px;color:#94A3B8;margin-bottom:8px}
.btn{display:block;width:100%;padding:14px;border:none;border-radius:12px;font-size:16px;font-weight:600;cursor:pointer;margin-top:8px}
.btn-save{background:#00FF00;color:#000}
.btn-add{background:#333;color:#87CEEB;font-size:13px;padding:10px}
.btn-remove{background:none;border:none;color:#FF6666;font-size:12px;cursor:pointer;float:right}
.cloud-section{display:none}
.toggle{display:flex;gap:0;margin:4px 0}
.toggle label{flex:1;text-align:center;padding:10px;background:#1a1a1a;border:1px solid #333;cursor:pointer;font-size:13px;color:#aaa;margin:0}
.toggle label:first-child{border-radius:8px 0 0 8px}
.toggle label:last-child{border-radius:0 8px 8px 0}
.toggle input{display:none}
.toggle input:checked+span{color:#00FF00}
.msg{padding:12px;border-radius:8px;margin:12px 0;display:none}
.msg.ok{display:block;background:#0a2a0a;border:1px solid #00FF00;color:#00FF00}
.msg.err{display:block;background:#2a0a0a;border:1px solid #FF3333;color:#FF3333}
.help{color:#555;font-size:11px;margin-top:4px}
</style>
</head><body>
<h1>BambuDial Setup</h1>
<p class="sub">Configure your printer monitor</p>
<div id="msg" class="msg"></div>

<form id="form" method="POST" action="/api/config">

<fieldset>
<legend>WiFi Network</legend>
<label>SSID</label>
<input name="wifi_ssid" id="wifi_ssid" required placeholder="Your WiFi network name">
<label>Password</label>
<input name="wifi_pass" id="wifi_pass" type="password" placeholder="WiFi password">
</fieldset>

<fieldset>
<legend>Connection Mode</legend>
<div class="toggle">
<label><input type="radio" name="mode" value="local" checked onchange="toggleCloud()"><span>Local MQTT</span></label>
<label><input type="radio" name="mode" value="cloud" onchange="toggleCloud()"><span>Cloud</span></label>
</div>
<p class="help">Local connects directly to printers on your network. Cloud auto-discovers printers via Bambu Lab account.</p>
</fieldset>

<fieldset class="cloud-section" id="cloud-section">
<legend>Bambu Lab Cloud</legend>
<label>Email</label>
<input name="cloud_email" placeholder="your@email.com">
<label>Password</label>
<input name="cloud_pass" type="password" placeholder="Bambu Lab password">
<label>Region</label>
<select name="cloud_region">
<option value="us">Americas (US)</option>
<option value="eu">Europe (EU)</option>
<option value="cn">China (CN)</option>
</select>
<label>JWT Token (optional, for 2FA accounts)</label>
<input name="cloud_token" placeholder="Paste token here if 2FA required">
</fieldset>

<fieldset id="printers-section">
<legend>Printers (Local Mode)</legend>
<div id="printers"></div>
<button type="button" class="btn btn-add" onclick="addPrinter()">+ Add Printer</button>
<p class="help">Find these in your printer's LCD under Network settings, or in Bambu Studio's config file.</p>
</fieldset>

<fieldset>
<legend>Display Settings</legend>
<label>Timezone</label>
<select name="timezone" id="timezone">
<option value="EST5EDT,M3.2.0,M11.1.0">US Eastern (New York)</option>
<option value="CST6CDT,M3.2.0,M11.1.0">US Central (Chicago)</option>
<option value="MST7MDT,M3.2.0,M11.1.0">US Mountain (Denver)</option>
<option value="PST8PDT,M3.2.0,M11.1.0">US Pacific (Los Angeles)</option>
<option value="GMT0BST-1,M3.5.0/1,M10.5.0/2">UK (London)</option>
<option value="CET-1CEST,M3.5.0,M10.5.0/3">Central Europe</option>
<option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Eastern Europe</option>
<option value="IST-5:30">India</option>
<option value="CST-8">China / Singapore</option>
<option value="JST-9">Japan / Korea</option>
<option value="AEST-10AEDT,M10.1.0,M4.1.0/3">Australia Eastern</option>
<option value="NZST-12NZDT,M9.5.0,M4.1.0/3">New Zealand</option>
</select>
<div class="row" style="margin-top:10px">
<div>
<label>Auto-rotate</label>
<select name="auto_rotate" id="auto_rotate">
<option value="1">Enabled</option>
<option value="0">Disabled</option>
</select>
</div>
<div>
<label>Interval (sec)</label>
<input name="auto_rotate_s" id="auto_rotate_s" type="number" value="30" min="5" max="300">
</div>
</div>
</fieldset>

<button type="submit" class="btn btn-save">Save & Reboot</button>
</form>

<script>
let printerCount=0;
function addPrinter(name,ip,serial,code){
  if(printerCount>=3)return;
  const i=printerCount++;
  const d=document.createElement('div');
  d.className='printer-card';
  d.id='prn_'+i;
  d.innerHTML=`<h3>Printer ${i+1} <button type="button" class="btn-remove" onclick="removePrinter(${i})">Remove</button></h3>
<div class="row"><div><label>Name</label><input name="prn_${i}_name" value="${name||''}" placeholder="e.g. X1C"></div>
<div><label>IP Address</label><input name="prn_${i}_ip" value="${ip||''}" placeholder="192.168.1.100"></div></div>
<div class="row"><div><label>Serial Number</label><input name="prn_${i}_serial" value="${serial||''}" placeholder="From printer LCD"></div>
<div><label>Access Code</label><input name="prn_${i}_code" value="${code||''}" placeholder="LAN code"></div></div>`;
  document.getElementById('printers').appendChild(d);
}
function removePrinter(i){
  const el=document.getElementById('prn_'+i);
  if(el)el.remove();
  // Renumber remaining cards
  printerCount=0;
  document.querySelectorAll('.printer-card').forEach((card,idx)=>{
    card.id='prn_'+idx;
    card.querySelector('h3').innerHTML=`Printer ${idx+1} <button type="button" class="btn-remove" onclick="removePrinter(${idx})">Remove</button>`;
    card.querySelectorAll('input').forEach(inp=>{
      inp.name=inp.name.replace(/prn_\d+/,'prn_'+idx);
    });
    printerCount=idx+1;
  });
}
function toggleCloud(){
  const isCloud=document.querySelector('input[name=mode]:checked').value==='cloud';
  document.getElementById('cloud-section').style.display=isCloud?'block':'none';
  document.getElementById('printers-section').style.display=isCloud?'none':'block';
}
// Load current config
fetch('/api/config').then(r=>r.json()).then(cfg=>{
  if(cfg.wifi_ssid)document.getElementById('wifi_ssid').value=cfg.wifi_ssid;
  if(cfg.wifi_pass)document.getElementById('wifi_pass').value=cfg.wifi_pass;
  if(cfg.mode==='cloud'){document.querySelector('input[value=cloud]').checked=true;toggleCloud();}
  if(cfg.timezone){const tz=document.getElementById('timezone');for(let o of tz.options)if(o.value===cfg.timezone){o.selected=true;break;}}
  if(cfg.auto_rotate!==undefined)document.getElementById('auto_rotate').value=cfg.auto_rotate?'1':'0';
  if(cfg.auto_rotate_s)document.getElementById('auto_rotate_s').value=cfg.auto_rotate_s;
  if(cfg.cloud_email)document.querySelector('[name=cloud_email]').value=cfg.cloud_email;
  if(cfg.cloud_region)document.querySelector('[name=cloud_region]').value=cfg.cloud_region;
  if(cfg.printers&&cfg.printers.length){
    cfg.printers.forEach(p=>addPrinter(p.name,p.ip,p.serial,p.access_code));
  }else{addPrinter();}
}).catch(()=>{addPrinter();});

document.getElementById('form').onsubmit=function(e){
  e.preventDefault();
  const fd=new FormData(this);
  const data={};
  fd.forEach((v,k)=>data[k]=v);
  // Collect printers
  data.printers=[];
  for(let i=0;i<printerCount;i++){
    const n=fd.get('prn_'+i+'_name'),ip=fd.get('prn_'+i+'_ip'),s=fd.get('prn_'+i+'_serial'),c=fd.get('prn_'+i+'_code');
    if(ip||s)data.printers.push({name:n||'Printer '+(i+1),ip:ip||'',serial:s||'',access_code:c||''});
  }
  data.auto_rotate=parseInt(data.auto_rotate);
  data.auto_rotate_s=parseInt(data.auto_rotate_s)||30;
  const msg=document.getElementById('msg');
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})
  .then(r=>{if(!r.ok)throw new Error('Save failed');return r.json()})
  .then(()=>{msg.className='msg ok';msg.textContent='Saved! Rebooting...';msg.style.display='block';setTimeout(()=>location.reload(),5000);})
  .catch(err=>{msg.className='msg err';msg.textContent='Error: '+err.message;msg.style.display='block';});
};
</script>
</body></html>
)rawhtml";

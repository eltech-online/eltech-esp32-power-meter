#pragma once

// The meter's web page, served as-is by handleRoot(). Its JavaScript (bottom of
// this file) asks the board for /data once a second and updates the readings.
// The buttons send a POST to /zero, /reset or /volts. The colours and layout
// come from /style.css (see eltech_wifi.h).
const char PAGE_TEMPLATE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ElTech-Online Power Meter</title>
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <h1>ElTech-Online</h1>
  <div class="sub">ESP32 Power Meter &mdash; updated every second</div>
  <div class="cards">
    <div class="card"><div class="label">Current</div><div class="value" id="amps">--</div>
      <div class="state"><span class="badge" id="over">--</span></div></div>
    <div class="card"><div class="label">Power</div><div class="value" id="watts">--</div></div>
    <div class="card"><div class="label">Charge used</div><div class="value" id="mah">--</div></div>
  </div>
  <div class="panel">
    <div class="head"><h2>Totals and limit</h2></div>
    <div class="row"><span class="name">Energy used</span><span id="wh">--</span></div>
    <div class="row"><span class="name">Warning limit (turn the knob)</span><span id="limit">--</span></div>
    <div class="row"><span class="name">Supply voltage of your load (V)</span>
      <input type="number" id="volts" min="0" max="60" step="0.1" onchange="post('/volts', 'v=' + this.value)"></div>
    <div class="buttons"><button class="quiet" onclick="post('/reset', '')">Reset totals</button></div>
  </div>
  <div class="panel">
    <div class="head"><h2>Zero the sensor</h2><span class="badge" id="zerostate">--</span></div>
    <div class="row"><span class="name">Sensor output now</span><span id="sensor">--</span></div>
    <div class="row"><span class="name">Zero point (no current)</span><span id="zero">--</span></div>
    <ol class="hint">
      <li>Disconnect the load so no current flows through the sensor.</li>
      <li>Press <b>Set zero</b>. Do this once; it is saved.</li>
    </ol>
    <div class="buttons"><button onclick="post('/zero', '')">Set zero</button></div>
    <div class="msg" id="msg"></div>
  </div>
  <div class="footer">ESP32 + ACS712 + ADS1115 &middot; low-voltage DC only &middot; <a href="https://github.com/eltech-online/eltech-esp32-power-meter" target="_blank">github.com/eltech-online/eltech-esp32-power-meter</a></div>
  <script>
    const el = (id) => document.getElementById(id);
    // Puts the board's answer on the page.
    function show(d) {
      el('amps').textContent = d.ads ? d.amps : 'n/a';
      el('watts').textContent = d.ads ? d.watts : 'n/a';
      el('mah').textContent = d.mah;
      el('wh').textContent = d.wh;
      el('limit').textContent = d.limit;
      el('over').textContent = !d.ads ? 'ADS1115 NOT FOUND' : d.over ? 'OVER LIMIT' : 'WITHIN LIMIT';
      el('over').className = 'badge ' + (!d.ads || d.over ? 'fail' : 'ok');
      el('sensor').textContent = d.sensor_mv;
      el('zero').textContent = d.zero_mv;
      el('zerostate').textContent = d.zero_saved ? 'SAVED' : 'NOT SET';
      el('zerostate').className = 'badge ' + (d.zero_saved ? 'ok' : 'warn');
      if (document.activeElement !== el('volts')) el('volts').value = d.volts;
      if (d.msg) el('msg').textContent = d.msg;
    }
    async function post(path, query) {
      try {
        const r = await fetch(path + '?' + query, { method: 'POST' });
        show(await r.json());
      } catch (e) { el('msg').textContent = 'Could not reach the board. Try again.'; }
    }
    async function refresh() {
      try { show(await (await fetch('/data')).json()); } catch (e) {}
    }
    refresh();
    setInterval(refresh, 1000);
  </script>
</body>
</html>
)HTML";

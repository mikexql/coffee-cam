#pragma once

static const char* index_ov5640_html = R"raw(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Evaluator Monitor</title>
  <style>
    body { font-family: sans-serif; background: #121212; color: #e0e0e0; text-align: center; padding: 20px; }
    .card { background: #1e1e1e; border-radius: 12px; padding: 20px; margin-bottom: 20px; border: 1px solid #333; }
    button { width: 100%; padding: 15px; font-size: 18px; border: none; border-radius: 8px; cursor: pointer; color: white; margin-bottom: 10px; font-weight: bold; }
    .btn-empty { background-color: #3700b3; } .btn-start { background-color: #018786; } .btn-end { background-color: #cf6679; color: #000; }
    .val { font-family: monospace; color: #fff; font-weight: bold; }
    .status-badge { padding: 15px; border-radius: 8px; font-weight: bold; font-size: 1.5rem; margin-top: 15px; color: #000; }
    .status-under { background-color: #ffb74d; } .status-good { background-color: #66bb6a; } .status-over { background-color: #ef5350; }
    img { width: 100%; border-radius: 8px; margin-top: 15px; border: 1px solid #444; }
  </style>
</head>
<body>
  <div style="max-width:600px; margin:auto;">
    <h2>Microfoam Monitor</h2>
    <div class="card">
        <button class="btn-empty" onclick="triggerAction('empty')">1. Capture Empty</button>
        <div id="raw-empty" style="color:#888;">Raw: --, --, --</div>
        <div>Avg: <span id="val-empty" class="val">--</span></div>
    </div>
    <div class="card">
        <button class="btn-start" onclick="triggerAction('start')">2. Capture Start</button>
        <div id="raw-start" style="color:#888;">Raw: --, --, --</div>
        <div>Avg: <span id="val-start" class="val">--</span></div>
    </div>
    <div class="card">
        <button class="btn-end" onclick="triggerAction('end')">3. Capture End & Eval</button>
        <div id="raw-end" style="color:#888;">Raw: --, --, --</div>
        <div>Avg: <span id="val-end" class="val">--</span></div>
        <div id="img-container"></div>
    </div>
    <div id="res-box" class="card" style="display:none;">
        <h3>Expansion: <span id="res-pct">0%</span></h3>
        <div id="res-status" class="status-badge">--</div>
    </div>
    <button style="background:#444" onclick="triggerAction('reset')">Reset</button>
  </div>
  <script>
    function triggerAction(cmd) {
      fetch('/action?cmd=' + cmd).then(r => r.json()).then(data => {
        if(data.empty > 0) {
            document.getElementById('val-empty').innerText = data.empty + " mm";
            if(cmd == 'empty') document.getElementById('raw-empty').innerText = "Raw: " + data.raw.join(", ");
        }
        if(data.start > 0) {
            document.getElementById('val-start').innerText = data.start + " mm";
            if(cmd == 'start') document.getElementById('raw-start').innerText = "Raw: " + data.raw.join(", ");
        }
        if(data.end > 0) {
            document.getElementById('val-end').innerText = data.end + " mm";
            if(cmd == 'end') {
                document.getElementById('raw-end').innerText = "Raw: " + data.raw.join(", ");
                document.getElementById('img-container').innerHTML = `<img src="/capture?t=${Date.now()}">`;
                document.getElementById('res-box').style.display = "block";
                document.getElementById('res-pct').innerText = data.pct.toFixed(1) + "%";
                const b = document.getElementById('res-status');
                b.innerText = data.status; b.className = "status-badge " + (data.status == "WELL FROTHED" ? "status-good" : (data.status == "UNDERFROTHED" ? "status-under" : "status-over"));
            }
        }
        if(cmd == 'reset') location.reload();
      });
    }
  </script>
</body>
</html>
)raw";
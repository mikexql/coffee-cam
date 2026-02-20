#pragma once

static const char *index_ov5640_html = R"raw(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Microfoam Evaluator</title>
  <style>
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background: #121212; color: #e0e0e0; text-align: center; padding: 20px; }
    .card { background: #1e1e1e; border-radius: 12px; padding: 20px; margin-bottom: 20px; border: 1px solid #333; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
    button { width: 100%; padding: 15px; font-size: 18px; border: none; border-radius: 8px; cursor: pointer; color: white; margin-bottom: 10px; font-weight: bold; transition: opacity 0.2s; }
    button:active { opacity: 0.8; }
    .btn-empty { background-color: #3700b3; } 
    .btn-end { background-color: #cf6679; color: #000; }
    .val { font-family: monospace; color: #fff; font-weight: bold; }
    
    /* Updated Image Grid Styles */
    .img-grid { 
        display: flex; 
        flex-wrap: wrap; 
        gap: 10px; 
        justify-content: center; 
        margin-top: 15px; 
    }
    .img-item { 
        flex: 1; 
        min-width: 100px; 
        max-width: 320px; 
        background: #000;
        border-radius: 8px;
        overflow: hidden;
        border: 1px solid #444;
    }
    .img-item img { width: 100%; display: block; }
    .img-caption { 
        font-size: 0.75rem; 
        padding: 5px; 
        color: #aaa; 
        background: #252525;
        border-top: 1px solid #444;
    }

    .status-badge { padding: 15px; border-radius: 8px; font-weight: bold; font-size: 1.5rem; margin-top: 15px; color: #000; }
    .status-under { background-color: #ffb74d; } 
    .status-good { background-color: #66bb6a; } 
    .status-over { background-color: #ef5350; }

    .ai-box { margin-top: 15px; padding-top: 15px; border-top: 1px solid #333; }
    .ai-label { font-size: 1.6rem; font-weight: bold; color: #4fc3f7; }
    .ai-conf { font-size: 1rem; color: #888; margin-left: 5px; }
    
    input[type=number] {
        width: 90%; padding: 15px; font-size: 18px; 
        border-radius: 8px; border: 1px solid #444; 
        background: #333; color: white; text-align: center;
        margin-bottom: 10px;
    }
  </style>
</head>
<body>
  <div style="max-width:800px; margin:auto;">
    <h2>Microfoam Monitor</h2>
    
    <div class="card">
        <div style="margin-bottom:10px; font-weight:bold; color:#ccc;">Step 1: Enter Milk Volume</div>
        <input type="number" id="milk-vol" placeholder="e.g. 200 (ml)">
    </div>
    <div class="card">
        <div style="margin-bottom:10px; font-weight:bold; color:#ccc;">Lighting</div>
            <label style="display:flex; align-items:center; justify-content:center; gap:10px;">
                <input type="checkbox" id="adaptive-light" checked>
                <span>Adaptive lighting (setLux)</span>
            </label>
    </div>

    <div class="card">
        <button class="btn-empty" onclick="triggerAction('empty')">2. Capture Empty Pitcher</button>
        <div id="raw-empty" style="color:#888;">Raw: --, --, --</div>
        <div>Avg: <span id="val-empty" class="val">--</span></div>
    </div>

    <div class="card">
        <button class="btn-end" onclick="triggerAction('end')">3. Evaluate Froth</button>
        <div id="raw-end" style="color:#888;">Raw: --, --, --</div>
        <div>Avg: <span id="val-end" class="val">--</span></div>
        
        <div id="img-container" class="img-grid"></div>
    </div>

    <div class="card">
        <div style="margin-bottom:10px; font-weight:bold; color:#ccc;">Dataset Tools</div>
        <button style="background-color: #00897b;" onclick="window.location.href='/download'">
            Capture & Download JPEG
        </button>
    </div>

    <div id="res-box" class="card" style="display:none;">
        <h3>Expansion: <span id="res-pct">0%</span></h3>
        <div id="res-status" class="status-badge">--</div>

        <div class="ai-box">
            <div style="color:#bbb; margin-bottom:5px; font-size:0.9rem; text-transform:uppercase; letter-spacing:1px;">AI Visual Analysis</div>
            <div>
                <span id="ai-label" class="ai-label">--</span>
                <span id="ai-conf" class="ai-conf"></span>
            </div>
        </div>
    </div>

    <button style="background:#444" onclick="triggerAction('reset')">Reset</button>
  </div>

  <script>
    function triggerAction(cmd) {
      if(cmd == 'end') document.querySelector('.btn-end').innerText = "Processing...";

      const vol = document.getElementById('milk-vol').value || 0;
      const adaptive = document.getElementById('adaptive-light').checked ? 1 : 0;

      fetch('/action?cmd=' + cmd + '&vol=' + vol + '&adaptive=' + adaptive)
      .then(r => r.json())
      .then(data => {
        if(cmd == 'end') document.querySelector('.btn-end').innerText = "3. Evaluate Froth";

        if(data.empty > 0) {
            document.getElementById('val-empty').innerText = data.empty + " mm";
            if(cmd == 'empty') document.getElementById('raw-empty').innerText = "Raw: " + data.raw.join(", ");
        }
        
        if(data.end > 0) {
            document.getElementById('val-end').innerText = data.end + " mm";
            
            if(cmd == 'end') {
                document.getElementById('raw-end').innerText = "Raw: " + data.raw.join(", ");
                
                // --- INJECT 3 IMAGES ---
                const ts = Date.now();
                const html = `
                    <div class="img-item">
                        <img src="/capture_full?t=${ts}">
                        <div class="img-caption">Full View (320x240)</div>
                    </div>
                    <div class="img-item">
                        <img src="/capture_color?t=${ts}">
                        <div class="img-caption">Cropped (Color)</div>
                    </div>
                    <div class="img-item">
                        <img src="/capture?t=${ts}">
                        <div class="img-caption">AI Input (Gray)</div>
                    </div>
                `;
                document.getElementById('img-container').innerHTML = html;
                // -----------------------

                document.getElementById('res-box').style.display = "block";
                
                document.getElementById('res-pct').innerText = data.pct.toFixed(1) + "%";
                const b = document.getElementById('res-status');
                b.innerText = data.status; 
                b.className = "status-badge " + (data.status == "WELL FROTHED" ? "status-good" : (data.status == "UNDERFROTHED" ? "status-under" : "status-over"));

                document.getElementById('ai-label').innerText = data.ml_label;
                if(data.ml_conf > 0) {
                    document.getElementById('ai-conf').innerText = "(" + (data.ml_conf * 100).toFixed(0) + "%)";
                } else {
                    document.getElementById('ai-conf').innerText = "";
                }
            }
        }
        if(cmd == 'reset') location.reload();
      })
      .catch(err => {
        console.error(err);
        if(cmd == 'end') document.querySelector('.btn-end').innerText = "Error (Try Again)";
      });
    }
  </script>
</body>
</html>
)raw";
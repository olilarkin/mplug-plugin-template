#pragma once

#include <string>

// Self-contained editor UI shared by every platform backend (macOS / Windows).
// HTML/CSS/JS are embedded as a string — the cross-platform-safe approach (no
// resource files to bundle into each plugin format). The two JS-callable
// bindings (setParameter / getParameter) are wired up identically by each
// platform's createEditor().
namespace myplugin
{

inline std::string generateEditorHTML(double /*gainDb*/)
{
  return R"HTML(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      background: #1e1e2e; color: #cdd6f4;
      height: 100vh; display: flex; flex-direction: column;
      align-items: center; justify-content: center; gap: 20px; user-select: none;
    }
    h1 { font-size: 16px; font-weight: 600; letter-spacing: 0.5px; }
    .readout { font-size: 40px; font-weight: 700; color: #89b4fa; font-variant-numeric: tabular-nums; }
    input[type="range"] {
      -webkit-appearance: none; width: 280px; height: 6px;
      background: #313244; border-radius: 3px; outline: none;
    }
    input[type="range"]::-webkit-slider-thumb {
      -webkit-appearance: none; width: 20px; height: 20px;
      background: #89b4fa; border-radius: 50%; cursor: pointer;
    }
  </style>
</head>
<body>
  <h1>GAIN</h1>
  <div class="readout" id="readout">0.0 dB</div>
  <input type="range" id="gain" min="-60" max="12" step="0.1" value="0">
  <script>
    const slider = document.getElementById('gain');
    const readout = document.getElementById('readout');

    function render(db) {
      readout.textContent = db.toFixed(1) + ' dB';
    }

    slider.addEventListener('input', () => {
      const db = parseFloat(slider.value);
      render(db);
      setParameter(0, db);          // -> C++ binding
    });

    // Pull the current value from the plugin on load.
    getParameter(0).then((db) => {
      slider.value = db;
      render(db);
    });
  </script>
</body>
</html>)HTML";
}

}  // namespace myplugin

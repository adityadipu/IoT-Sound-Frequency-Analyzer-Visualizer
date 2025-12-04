#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// ================== USER CONFIG ==================
const char* WIFI_SSID     = "Wifi_name";
const char* WIFI_PASSWORD = "@Wifi_pass";

const char* AP_SSID       = "ESP8266-Noise";
const char* AP_PASSWORD   = "12345678";

const uint8_t D0_PIN      = D1;
const uint8_t LED_PIN     = D2;

// Configurable settings
uint16_t SAMPLE_INTERVAL_MS = 5;      // ~200 Hz
uint8_t  LED_THRESH_PERCENT = 20;
uint8_t  LED_BRIGHTNESS     = 255;
bool     AUTO_BRIGHTNESS    = true;
bool     SHOW_SPECTRUM      = true;   // Enable frequency spectrum
// =================================================

ESP8266WebServer server(80);

// Sound processing state
int   baseline           = 0;
bool  baselineReady      = false;
float levelSmoothed      = 0.0f;
float dynamicMaxAmp      = 30.0f;
int   lastRaw            = 0;
int   lastLevelPercent   = 0;
int   peakLevelPercent   = 0;
bool  lastDigital        = false;

// Frequency analysis (simple FFT-like)
#define FFT_SIZE 64                     // Power of 2 for simple FFT
#define FREQ_BANDS 8                    // Number of frequency bands to display
float fftBuffer[FFT_SIZE];
int fftIndex = 0;
float freqBands[FREQ_BANDS] = {0};      // Store frequency band magnitudes
float freqBandsPeak[FREQ_BANDS] = {0};  // Peak values for each band

unsigned long lastSampleMillis = 0;
unsigned long lastSpectrumUpdate = 0;

// Configuration structure
struct Config {
  uint16_t sampleInterval;
  uint8_t  ledThreshold;
  uint8_t  ledBrightness;
  bool     autoBrightness;
  bool     showSpectrum;
} config;

// ---------- HTML PAGES ----------
const char INDEX_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>ESP8266 Sound Frequency Analyzer</title>
  <style>
    body { 
      font-family: Arial, sans-serif; 
      background: #0f172a; 
      color: #e5e7eb; 
      margin: 0; 
      padding: 20px;
    }
    .container { max-width: 1000px; margin: 0 auto; }
    .header { text-align: center; margin-bottom: 20px; padding-bottom: 10px; border-bottom: 1px solid #334155; }
    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin-bottom: 20px; }
    @media (max-width: 768px) { .grid { grid-template-columns: 1fr; } }
    .card { background: #1e293b; border-radius: 10px; padding: 20px; margin-bottom: 20px; }
    .tabs { display: flex; gap: 10px; margin-bottom: 20px; flex-wrap: wrap; }
    .tab { padding: 10px 20px; background: #334155; border: none; color: white; border-radius: 5px; cursor: pointer; transition: 0.3s; }
    .tab:hover { background: #475569; }
    .tab.active { background: #3b82f6; }
    .tab-content { display: none; }
    .tab-content.active { display: block; }
    
    /* Level Display */
    .level-display { text-align: center; }
    .level-value { font-size: 60px; font-weight: bold; margin: 10px 0; background: linear-gradient(90deg, #22c55e, #f97316, #ef4444); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
    .level-bar { height: 30px; background: #334155; border-radius: 15px; overflow: hidden; margin: 20px 0; position: relative; }
    .level-fill { height: 100%; background: linear-gradient(90deg, #22c55e, #f97316, #ef4444); width: 0%; transition: width 0.2s; }
    .peak-marker { position: absolute; top: 0; width: 2px; height: 100%; background: #fbbf24; opacity: 0.8; }
    
    /* Spectrum Display */
    .spectrum-container { margin-top: 30px; }
    .spectrum-title { text-align: center; margin-bottom: 15px; color: #94a3b8; }
    .spectrum-bars { display: flex; height: 200px; align-items: flex-end; justify-content: center; gap: 8px; padding: 0 10px; }
    .spectrum-bar { width: 40px; background: linear-gradient(to top, #22c55e, #f97316, #ef4444); border-radius: 5px 5px 0 0; position: relative; transition: height 0.15s; }
    .spectrum-peak { position: absolute; top: -4px; left: 0; right: 0; height: 4px; background: #fbbf24; border-radius: 2px; }
    .freq-labels { display: flex; justify-content: space-between; margin-top: 10px; padding: 0 10px; color: #94a3b8; font-size: 12px; }
    
    /* Configuration */
    .config-group { margin: 15px 0; }
    label { display: block; margin-bottom: 5px; color: #94a3b8; }
    input[type="range"] { width: 100%; height: 6px; border-radius: 3px; background: #334155; outline: none; }
    .value-label { display: inline-block; min-width: 40px; text-align: right; color: #e5e7eb; }
    button { background: #3b82f6; color: white; border: none; padding: 10px 20px; border-radius: 5px; cursor: pointer; transition: 0.3s; }
    button:hover { background: #2563eb; }
    .status { padding: 10px; border-radius: 5px; margin-top: 10px; display: none; }
    .status.success { background: #065f46; color: #a7f3d0; }
    .status.error { background: #7f1d1d; color: #fecaca; }
    
    /* Info Panel */
    .info-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; }
    .info-item { background: #0f172a; padding: 10px; border-radius: 5px; }
    .info-label { font-size: 12px; color: #94a3b8; }
    .info-value { font-size: 18px; font-weight: bold; }
    
    /* Frequency Info */
    .freq-info { display: flex; justify-content: space-between; margin-top: 10px; }
    .dom-freq { background: rgba(59, 130, 246, 0.2); padding: 5px 10px; border-radius: 5px; }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>ESP8266 Sound Frequency Analyzer</h1>
      <p id="headerInfo">IP: Loading... | Sampling: 200 Hz</p>
    </div>
    
    <div class="tabs">
      <button class="tab active" onclick="showTab('dashboard')">Dashboard</button>
      <button class="tab" onclick="showTab('spectrum')">Spectrum</button>
      <button class="tab" onclick="showTab('config')">Configuration</button>
      <button class="tab" onclick="showTab('info')">System Info</button>
    </div>
    
    <!-- Dashboard Tab -->
    <div id="dashboard" class="tab-content active">
      <div class="grid">
        <div class="card">
          <h2>Sound Level</h2>
          <div class="level-display">
            <div class="level-value" id="levelValue">0 %</div>
            <div class="level-bar">
              <div class="level-fill" id="levelBar"></div>
              <div class="peak-marker" id="peakMarker"></div>
            </div>
            <div class="freq-info">
              <div>Raw: <span id="rawValue">0</span></div>
              <div class="dom-freq">Dominant: <span id="domFreq">0 Hz</span></div>
              <div>LED: <span id="ledStatus">OFF</span></div>
            </div>
          </div>
        </div>
        
        <div class="card">
          <h2>Live Waveform</h2>
          <canvas id="waveform" width="400" height="150"></canvas>
          <div style="text-align: center; margin-top: 10px; color: #94a3b8;">
            Last 64 samples (~320ms)
          </div>
        </div>
      </div>
      
      <div class="card">
        <h2>LED Control</h2>
        <div style="display: flex; align-items: center; gap: 20px; flex-wrap: wrap;">
          <div style="flex: 1;">
            <label>Brightness: <span id="brightnessValue">255</span></label>
            <input type="range" id="brightnessSlider" min="0" max="255" value="255" oninput="updateBrightness(this.value)">
          </div>
          <div style="flex: 1;">
            <label>Threshold: <span id="thresholdValue">20</span>%</label>
            <input type="range" id="thresholdSlider" min="1" max="100" value="20" oninput="updateThreshold(this.value)">
          </div>
          <button onclick="toggleLED()" id="ledToggle">Toggle LED</button>
          <button onclick="calibrate()">Calibrate</button>
        </div>
      </div>
    </div>
    
    <!-- Spectrum Tab -->
    <div id="spectrum" class="tab-content">
      <div class="card">
        <div class="spectrum-container">
          <h2 class="spectrum-title">Frequency Spectrum</h2>
          <div class="spectrum-bars" id="spectrumBars"></div>
          <div class="freq-labels">
            <span>Bass</span>
            <span>Low Mid</span>
            <span>Mid</span>
            <span>High Mid</span>
            <span>Treble</span>
          </div>
        </div>
      </div>
      
      <div class="card">
        <h2>Spectrum Information</h2>
        <div class="info-grid">
          <div class="info-item">
            <div class="info-label">Dominant Band</div>
            <div class="info-value" id="domBand">1</div>
          </div>
          <div class="info-item">
            <div class="info-label">Peak Frequency</div>
            <div class="info-value" id="peakFreq">0 Hz</div>
          </div>
          <div class="info-item">
            <div class="info-label">Band Energy</div>
            <div class="info-value" id="bandEnergy">0%</div>
          </div>
          <div class="info-item">
            <div class="info-label">Spectrum Rate</div>
            <div class="info-value" id="spectrumRate">200 Hz</div>
          </div>
        </div>
      </div>
    </div>
    
    <!-- Configuration Tab -->
    <div id="config" class="tab-content">
      <div class="card">
        <h2>Device Settings</h2>
        <div class="config-group">
          <label>Sample Rate: <span id="sampleValue">200</span> Hz</label>
          <input type="range" id="sampleSlider" min="50" max="500" value="200" oninput="updateSample(this.value)">
        </div>
        
        <div class="config-group">
          <label>
            <input type="checkbox" id="autoBrightness" checked onchange="toggleAutoBrightness(this.checked)">
            Auto LED Brightness
          </label>
        </div>
        
        <div class="config-group">
          <label>
            <input type="checkbox" id="showSpectrum" checked onchange="toggleSpectrum(this.checked)">
            Show Frequency Spectrum
          </label>
        </div>
        
        <div class="config-group">
          <button onclick="saveConfig()">Save Settings</button>
          <button onclick="resetConfig()" style="background: #dc2626;">Reset to Defaults</button>
        </div>
        
        <div id="configStatus" class="status"></div>
      </div>
    </div>
    
    <!-- System Info Tab -->
    <div id="info" class="tab-content">
      <div class="card">
        <h2>System Information</h2>
        <div class="info-grid">
          <div class="info-item">
            <div class="info-label">Uptime</div>
            <div class="info-value" id="uptime">0s</div>
          </div>
          <div class="info-item">
            <div class="info-label">WiFi RSSI</div>
            <div class="info-value" id="rssi">0 dBm</div>
          </div>
          <div class="info-item">
            <div class="info-label">Free Heap</div>
            <div class="info-value" id="heap">0 bytes</div>
          </div>
          <div class="info-item">
            <div class="info-label">Sample Rate</div>
            <div class="info-value" id="sampleRate">200 Hz</div>
          </div>
          <div class="info-item">
            <div class="info-label">FFT Size</div>
            <div class="info-value">64 points</div>
          </div>
          <div class="info-item">
            <div class="info-label">Frequency Bands</div>
            <div class="info-value">8 bands</div>
          </div>
        </div>
        <div style="text-align: center; margin-top: 20px;">
          <button onclick="refreshInfo()">Refresh Info</button>
          <div id="infoStatus" class="status" style="margin-top: 10px;"></div>
        </div>
      </div>
    </div>
  </div>

  <script>
    let currentTab = 'dashboard';
    let waveformData = [];
    let spectrumData = [0,0,0,0,0,0,0,0];
    let spectrumPeaks = [0,0,0,0,0,0,0,0];
    let systemInfoUpdated = false;
    
    function showTab(tabName) {
      document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
      document.querySelectorAll('.tab').forEach(el => el.classList.remove('active'));
      document.getElementById(tabName).classList.add('active');
      document.querySelector(`[onclick="showTab('${tabName}')"]`).classList.add('active');
      currentTab = tabName;
      
      // Auto-refresh info tab when opened
      if (tabName === 'info' && !systemInfoUpdated) {
        setTimeout(refreshInfo, 100);
      }
    }
    
    function updateBrightness(val) {
      document.getElementById('brightnessValue').textContent = val;
      fetch(`/set?brightness=${val}`).catch(err => console.log('Set brightness error:', err));
    }
    
    function updateThreshold(val) {
      document.getElementById('thresholdValue').textContent = val;
      fetch(`/set?threshold=${val}`).catch(err => console.log('Set threshold error:', err));
    }
    
    function updateSample(val) {
      const freq = Math.round(1000/val);
      document.getElementById('sampleValue').textContent = freq;
      fetch(`/set?sample=${val}`).catch(err => console.log('Set sample error:', err));
    }
    
    function toggleAutoBrightness(checked) {
      fetch(`/set?autobright=${checked ? 1 : 0}`).catch(err => console.log('Auto brightness error:', err));
    }
    
    function toggleSpectrum(checked) {
      fetch(`/set?spectrum=${checked ? 1 : 0}`).catch(err => console.log('Set spectrum error:', err));
    }
    
    function toggleLED() {
      fetch('/set?ledtoggle=1').catch(err => console.log('Toggle LED error:', err));
    }
    
    function calibrate() {
      fetch('/calibrate')
        .then(r => {
          if (r.ok) {
            showStatus('Calibration started!', 'success', 'configStatus');
          }
        })
        .catch(err => {
          showStatus('Calibration failed!', 'error', 'configStatus');
          console.log('Calibrate error:', err);
        });
    }
    
    function saveConfig() {
      const sample = document.getElementById('sampleSlider').value;
      const autoBright = document.getElementById('autoBrightness').checked ? 1 : 0;
      const spectrum = document.getElementById('showSpectrum').checked ? 1 : 0;
      
      fetch(`/save?sample=${sample}&autobright=${autoBright}&spectrum=${spectrum}`)
        .then(r => r.text())
        .then(text => {
          showStatus('Settings saved!', 'success', 'configStatus');
        })
        .catch(err => {
          showStatus('Save failed!', 'error', 'configStatus');
          console.log('Save error:', err);
        });
    }
    
    function resetConfig() {
      if (confirm('Reset all settings to defaults?')) {
        fetch('/reset')
          .then(() => {
            showStatus('Settings reset! Reloading...', 'success', 'configStatus');
            setTimeout(() => location.reload(), 2000);
          })
          .catch(err => {
            showStatus('Reset failed!', 'error', 'configStatus');
            console.log('Reset error:', err);
          });
      }
    }
    
    function showStatus(message, type, elementId) {
      const status = document.getElementById(elementId);
      status.textContent = message;
      status.className = 'status ' + type;
      status.style.display = 'block';
      setTimeout(() => status.style.display = 'none', 3000);
    }
    
    function refreshInfo() {
      const infoStatus = document.getElementById('infoStatus');
      infoStatus.textContent = 'Updating...';
      infoStatus.className = 'status';
      infoStatus.style.display = 'block';
      
      fetch('/info.json')
        .then(r => {
          if (!r.ok) throw new Error('Network response was not ok');
          return r.json();
        })
        .then(data => {
          // Format uptime
          const uptimeSeconds = parseInt(data.uptime) || 0;
          const hours = Math.floor(uptimeSeconds / 3600);
          const minutes = Math.floor((uptimeSeconds % 3600) / 60);
          const seconds = uptimeSeconds % 60;
          let uptimeStr = '';
          if (hours > 0) uptimeStr += `${hours}h `;
          if (minutes > 0 || hours > 0) uptimeStr += `${minutes}m `;
          uptimeStr += `${seconds}s`;
          
          // Update info boxes
          document.getElementById('uptime').textContent = uptimeStr || '0s';
          document.getElementById('rssi').textContent = (data.rssi || 0) + ' dBm';
          document.getElementById('heap').textContent = (data.heap || 0).toLocaleString() + ' bytes';
          document.getElementById('sampleRate').textContent = (data.sampleRate || 200) + ' Hz';
          
          infoStatus.textContent = 'Updated successfully!';
          infoStatus.className = 'status success';
          systemInfoUpdated = true;
          setTimeout(() => infoStatus.style.display = 'none', 2000);
        })
        .catch(err => {
          console.log('Info fetch error:', err);
          document.getElementById('uptime').textContent = 'Error';
          document.getElementById('rssi').textContent = '--';
          document.getElementById('heap').textContent = '--';
          document.getElementById('sampleRate').textContent = '--';
          
          infoStatus.textContent = 'Update failed!';
          infoStatus.className = 'status error';
          setTimeout(() => infoStatus.style.display = 'none', 3000);
        });
    }
    
    // Draw waveform
    function drawWaveform(data) {
      const canvas = document.getElementById('waveform');
      if (!canvas) return;
      const ctx = canvas.getContext('2d');
      const w = canvas.width;
      const h = canvas.height;
      
      ctx.clearRect(0, 0, w, h);
      
      if (!data || data.length < 2) return;
      
      // Draw grid
      ctx.strokeStyle = '#334155';
      ctx.lineWidth = 1;
      for (let i = 0; i <= 4; i++) {
        const y = i * (h / 4);
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(w, y);
        ctx.stroke();
      }
      
      // Draw waveform
      ctx.beginPath();
      ctx.strokeStyle = '#3b82f6';
      ctx.lineWidth = 2;
      
      for (let i = 0; i < data.length; i++) {
        const x = i * (w / (data.length - 1));
        const y = h/2 - (data[i] * h/2 / 512);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.stroke();
    }
    
    // Draw spectrum bars
    function drawSpectrum(bands, peaks) {
      const container = document.getElementById('spectrumBars');
      if (!container) return;
      container.innerHTML = '';
      
      // Calculate maximum value for scaling
      const maxVal = Math.max(0.1, ...bands);
      
      for (let i = 0; i < bands.length; i++) {
        const height = Math.min((bands[i] / maxVal) * 100, 100);
        const peakHeight = Math.min((peaks[i] / maxVal) * 100, 100);
        
        const bar = document.createElement('div');
        bar.className = 'spectrum-bar';
        bar.style.height = height + '%';
        bar.title = `Band ${i+1}: ${bands[i].toFixed(1)}`;
        
        const peak = document.createElement('div');
        peak.className = 'spectrum-peak';
        peak.style.bottom = peakHeight + '%';
        
        bar.appendChild(peak);
        container.appendChild(bar);
      }
    }
    
    // Live data updates
    function updateData() {
      fetch('/data.json')
        .then(r => {
          if (!r.ok) throw new Error('Network response was not ok');
          return r.json();
        })
        .then(data => {
          // Update dashboard
          document.getElementById('levelValue').textContent = (data.level || 0) + '%';
          document.getElementById('levelBar').style.width = (data.level || 0) + '%';
          document.getElementById('peakMarker').style.left = (data.peak || 0) + '%';
          document.getElementById('rawValue').textContent = data.raw || 0;
          document.getElementById('ledStatus').textContent = data.ledState || 'OFF';
          document.getElementById('ledToggle').textContent = data.ledState === 'ON' ? 'LED ON' : 'LED OFF';
          document.getElementById('domFreq').textContent = (data.domFreq || 0) + ' Hz';
          
          // Update waveform if data available
          if (data.waveform && data.waveform.length > 0) {
            waveformData = data.waveform;
            if (currentTab === 'dashboard') {
              drawWaveform(waveformData);
            }
          }
          
          // Update spectrum if data available
          if (data.spectrum && data.spectrum.length > 0) {
            spectrumData = data.spectrum.map(val => parseFloat(val) || 0);
            
            // Calculate peaks if provided
            if (data.spectrumPeaks && data.spectrumPeaks.length > 0) {
              spectrumPeaks = data.spectrumPeaks.map(val => parseFloat(val) || 0);
            } else {
              // Update peaks manually
              spectrumData.forEach((val, i) => {
                if (val > spectrumPeaks[i]) {
                  spectrumPeaks[i] = val;
                } else {
                  spectrumPeaks[i] = Math.max(0, spectrumPeaks[i] * 0.995);
                }
              });
            }
            
            if (currentTab === 'spectrum') {
              drawSpectrum(spectrumData, spectrumPeaks);
              document.getElementById('domBand').textContent = data.domBand || '1';
              document.getElementById('peakFreq').textContent = (data.peakFreq || 0) + ' Hz';
              document.getElementById('bandEnergy').textContent = (data.energy || 0) + '%';
              document.getElementById('spectrumRate').textContent = (data.spectrumRate || 200) + ' Hz';
            }
          }
        })
        .catch(err => {
          console.log('Update error:', err);
          // Show error state
          document.getElementById('levelValue').textContent = 'Err';
          document.getElementById('levelBar').style.width = '0%';
        });
    }
    
    // Initialize
    document.addEventListener('DOMContentLoaded', function() {
      // Initial spectrum bars
      drawSpectrum(spectrumData, spectrumPeaks);
      
      // Update header with actual IP
      document.getElementById('headerInfo').textContent = 'IP: ' + window.location.hostname + ' | Sampling: 200 Hz';
      
      // Auto-refresh system info every 30 seconds when on info tab
      setInterval(() => {
        if (currentTab === 'info') {
          refreshInfo();
        }
      }, 30000);
      
      // Start data updates
      setInterval(updateData, 200); // Update every 200ms (5Hz)
      updateData();
    });
  </script>
</body>
</html>
)rawliteral";

// ---------- Frequency Analysis Functions ----------
void performFFT() {
  // Simple bandpass filter approach
  for (int band = 0; band < FREQ_BANDS; band++) {
    float sum = 0;
    int samplesPerBand = FFT_SIZE / FREQ_BANDS;
    int startIdx = band * samplesPerBand;
    
    // Simple band energy calculation
    for (int i = 0; i < samplesPerBand; i++) {
      int idx = (fftIndex + startIdx + i) % FFT_SIZE;
      sum += fabs(fftBuffer[idx]);
    }
    
    // Exponential moving average for smoothing
    float newVal = sum / samplesPerBand;
    freqBands[band] = 0.7 * freqBands[band] + 0.3 * newVal;
    
    // Update peaks
    if (freqBands[band] > freqBandsPeak[band]) {
      freqBandsPeak[band] = freqBands[band];
    } else {
      freqBandsPeak[band] *= 0.995; // Slow peak decay
    }
  }
}

String getSpectrumJSON() {
  String json = "[";
  for (int i = 0; i < FREQ_BANDS; i++) {
    json += String(freqBands[i], 1);
    if (i < FREQ_BANDS - 1) json += ",";
  }
  json += "]";
  return json;
}

String getSpectrumPeaksJSON() {
  String json = "[";
  for (int i = 0; i < FREQ_BANDS; i++) {
    json += String(freqBandsPeak[i], 1);
    if (i < FREQ_BANDS - 1) json += ",";
  }
  json += "]";
  return json;
}

int getDominantBand() {
  int dominant = 0;
  float maxVal = freqBands[0];
  for (int i = 1; i < FREQ_BANDS; i++) {
    if (freqBands[i] > maxVal) {
      maxVal = freqBands[i];
      dominant = i;
    }
  }
  return dominant;
}

// ---------- API Handlers ----------
void handleRoot() {
  String html = FPSTR(INDEX_PAGE);
  html.replace("%IP%", WiFi.localIP().toString());
  server.send(200, "text/html", html);
}

void handleDataJson() {
  // Prepare waveform data (last 64 samples)
  String waveform = "[";
  for (int i = 0; i < FFT_SIZE; i++) {
    int idx = (fftIndex + i) % FFT_SIZE;
    waveform += String(fftBuffer[idx]);
    if (i < FFT_SIZE - 1) waveform += ",";
  }
  waveform += "]";
  
  // Calculate dominant frequency band
  int domBand = getDominantBand();
  int domFreq = map(domBand, 0, FREQ_BANDS, 50, 1000);
  
  // Calculate band energy (0-100%)
  float bandEnergy = 0;
  for (int i = 0; i < FREQ_BANDS; i++) {
    bandEnergy += freqBands[i];
  }
  bandEnergy = constrain((bandEnergy / FREQ_BANDS) * 3.0, 0, 100); // Proper scaling
  
  // Prepare response
  String json = "{";
  json += "\"level\":" + String(lastLevelPercent) + ",";
  json += "\"raw\":" + String(lastRaw) + ",";
  json += "\"digital\":" + String(lastDigital ? "true" : "false") + ",";
  json += "\"peak\":" + String(peakLevelPercent) + ",";
  json += "\"ledState\":\"" + String(digitalRead(LED_PIN) ? "ON" : "OFF") + "\",";
  json += "\"domFreq\":" + String(domFreq) + ",";
  json += "\"domBand\":\"" + String(domBand + 1) + "\",";
  json += "\"peakFreq\":" + String(domFreq) + ",";
  json += "\"energy\":" + String((int)bandEnergy) + ",";
  json += "\"spectrumRate\":" + String(1000/SAMPLE_INTERVAL_MS) + ",";
  
  if (SHOW_SPECTRUM) {
    json += "\"spectrum\":" + getSpectrumJSON() + ",";
    json += "\"spectrumPeaks\":" + getSpectrumPeaksJSON() + ",";
  }
  
  json += "\"waveform\":" + waveform;
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleInfoJson() {
  String json = "{";
  json += "\"uptime\":" + String(millis()/1000) + ",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"sampleRate\":" + String(1000/SAMPLE_INTERVAL_MS);
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleSet() {
  if (server.hasArg("brightness")) {
    LED_BRIGHTNESS = server.arg("brightness").toInt();
    analogWrite(LED_PIN, LED_BRIGHTNESS);
  }
  if (server.hasArg("threshold")) {
    LED_THRESH_PERCENT = server.arg("threshold").toInt();
  }
  if (server.hasArg("autobright")) {
    AUTO_BRIGHTNESS = server.arg("autobright").toInt();
  }
  if (server.hasArg("spectrum")) {
    SHOW_SPECTRUM = server.arg("spectrum").toInt();
  }
  if (server.hasArg("sample")) {
    SAMPLE_INTERVAL_MS = server.arg("sample").toInt();
    if (SAMPLE_INTERVAL_MS < 2) SAMPLE_INTERVAL_MS = 2;
    if (SAMPLE_INTERVAL_MS > 50) SAMPLE_INTERVAL_MS = 50;
  }
  if (server.hasArg("ledtoggle")) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }
  server.send(200, "text/plain", "OK");
}

void handleCalibrate() {
  // Reset calibration
  baselineReady = false;
  dynamicMaxAmp = 30.0f;
  for (int i = 0; i < FREQ_BANDS; i++) {
    freqBands[i] = 0;
    freqBandsPeak[i] = 0;
  }
  server.send(200, "text/plain", "Calibration reset. Wait 2-3 seconds for new baseline.");
}

void handleSave() {
  if (server.hasArg("sample")) {
    config.sampleInterval = server.arg("sample").toInt();
    SAMPLE_INTERVAL_MS = config.sampleInterval;
  }
  if (server.hasArg("autobright")) {
    config.autoBrightness = server.arg("autobright").toInt();
    AUTO_BRIGHTNESS = config.autoBrightness;
  }
  if (server.hasArg("spectrum")) {
    config.showSpectrum = server.arg("spectrum").toInt();
    SHOW_SPECTRUM = config.showSpectrum;
  }
  server.send(200, "text/plain", "Settings saved! Refresh page to see changes.");
}

void handleReset() {
  // Reset to defaults
  SAMPLE_INTERVAL_MS = 5;
  LED_THRESH_PERCENT = 20;
  LED_BRIGHTNESS = 255;
  AUTO_BRIGHTNESS = true;
  SHOW_SPECTRUM = true;
  
  // Reset config struct
  config.sampleInterval = 5;
  config.ledThreshold = 20;
  config.ledBrightness = 255;
  config.autoBrightness = true;
  config.showSpectrum = true;
  
  server.send(200, "text/plain", "All settings reset to defaults!");
}

// ---------- Setup WiFi ----------
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname("esp-noise-spectrum");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  Serial.print(F("Connecting to WiFi"));
  const unsigned long startAttempt = millis();
  
  while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt) < 15000) {
    delay(500);
    Serial.print(F("."));
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("Connected! IP address: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("WiFi connect failed, starting AP mode..."));
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.print(F("AP IP address: "));
    Serial.println(WiFi.softAPIP());
  }
}

// ---------- Updated Sample Function ----------
void sampleSound() {
  int raw = analogRead(A0);
  lastRaw = raw;
  
  // Store in FFT buffer
  fftBuffer[fftIndex] = raw - 512; // Center around 0
  fftIndex = (fftIndex + 1) % FFT_SIZE;
  
  if (!baselineReady) {
    baseline = raw;
    baselineReady = true;
  }
  
  baseline = baseline + (raw - baseline) / 100;
  int wave = raw - baseline;
  int amp = abs(wave);
  
  const float alpha = 0.25f;
  levelSmoothed += alpha * ((float)amp - levelSmoothed);
  
  dynamicMaxAmp *= 0.995f;
  if (dynamicMaxAmp < 20.0f) dynamicMaxAmp = 20.0f;
  if (levelSmoothed > dynamicMaxAmp) {
    dynamicMaxAmp = levelSmoothed;
  }
  
  lastLevelPercent = constrain((int)(levelSmoothed * 100.0f / dynamicMaxAmp), 0, 100);
  
  if (lastLevelPercent > peakLevelPercent) {
    peakLevelPercent = lastLevelPercent;
  } else if (peakLevelPercent > 0) {
    peakLevelPercent--;
  }
  
  lastDigital = (digitalRead(D0_PIN) == HIGH);
  
  // Update frequency spectrum periodically
  unsigned long now = millis();
  if (now - lastSpectrumUpdate >= 100) { // Update spectrum every 100ms
    lastSpectrumUpdate = now;
    performFFT();
  }
  
  // LED Control
  if (lastLevelPercent > LED_THRESH_PERCENT) {
    if (AUTO_BRIGHTNESS) {
      uint8_t autoBright = map(lastLevelPercent, LED_THRESH_PERCENT, 100, 50, 255);
      analogWrite(LED_PIN, autoBright);
    } else {
      analogWrite(LED_PIN, LED_BRIGHTNESS);
    }
  } else {
    analogWrite(LED_PIN, 0);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\nStarting ESP8266 Sound Frequency Analyzer v1.2..."));
  
  pinMode(D0_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  analogWriteFreq(1000); // Set PWM frequency to 1kHz
  
  // Initialize config
  config.sampleInterval = 5;
  config.ledThreshold = 20;
  config.ledBrightness = 255;
  config.autoBrightness = true;
  config.showSpectrum = true;
  
  // Initialize FFT buffer
  for (int i = 0; i < FFT_SIZE; i++) {
    fftBuffer[i] = 0;
  }
  
  setupWiFi();
  
  // HTTP routes
  server.on("/", handleRoot);
  server.on("/data.json", handleDataJson);
  server.on("/info.json", handleInfoJson);
  server.on("/set", handleSet);
  server.on("/calibrate", handleCalibrate);
  server.on("/save", handleSave);
  server.on("/reset", handleReset);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });
  
  server.begin();
  Serial.println(F("HTTP server started"));
  Serial.println(F("Frequency bands: ") + String(FREQ_BANDS));
  Serial.println(F("Sample rate: ") + String(1000/SAMPLE_INTERVAL_MS) + F(" Hz"));
  Serial.println(F("Open browser to: http://") + WiFi.localIP().toString());
}

void loop() {
  unsigned long now = millis();
  if (now - lastSampleMillis >= SAMPLE_INTERVAL_MS) {
    lastSampleMillis = now;
    sampleSound();
  }
  server.handleClient();
}

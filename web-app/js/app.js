// Sentino BLE Tool — Main Application Controller
console.log('[App] Loading modules...');
import * as api from './api.js';
import * as ble from './ble.js';
console.log('[App] Modules loaded');

// ── State ────────────────────────────────────

const state = {
  assetId: null,
  userId: null,
  mqttUrl: null,
  mqttPort: 1883,
  mqttSslPort: '8883',
  deviceInfo: null, // from BLE device.information.get
  notifCount: 0,
};

// ── DOM refs ─────────────────────────────────

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => document.querySelectorAll(sel);

// ── Debug logger ─────────────────────────────

const debugContent = $('#debug-content');

function debugLog(msg, tag = 'ble') {
  const ts = new Date().toLocaleTimeString('en-GB', { hour12: false });
  const line = document.createElement('div');
  line.className = 'debug-line';
  const tagClass = tag === 'api' ? 'api-tag' : tag === 'err' ? 'err-tag' : 'ble-tag';
  line.innerHTML = `<span class="ts">${ts}</span> <span class="${tagClass}">[${tag.toUpperCase()}]</span> <span class="msg">${escapeHtml(msg)}</span>`;
  debugContent.appendChild(line);
  debugContent.scrollTop = debugContent.scrollHeight;
}

function escapeHtml(str) {
  return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

// Wire BLE logger
ble.setLogger((msg) => debugLog(msg, 'ble'));

// ── Interaction data display ────────────────

function logIx(_screenId, tag, title, reqData, resData) {
  const container = $('#ix-global');
  if (!container) return;
  const ts = new Date().toLocaleTimeString('en-GB', { hour12: false });
  const card = document.createElement('div');
  card.className = 'ix-card';
  const reqLabel = tag === 'api' ? 'Request' : 'TX';
  const resLabel = tag === 'api' ? 'Response' : 'RX';
  let html = `<div class="ix-header"><span class="ix-tag ${tag}">${tag.toUpperCase()}</span><span class="ix-name">${escapeHtml(title)}</span><span class="ix-ts">${ts}</span></div>`;
  if (reqData != null) {
    const str = typeof reqData === 'string' ? reqData : JSON.stringify(reqData, null, 2);
    html += `<div class="ix-section"><span class="ix-label">▶ ${reqLabel}</span><pre class="ix-json">${escapeHtml(str)}</pre></div>`;
  }
  if (resData != null) {
    const str = typeof resData === 'string' ? resData : JSON.stringify(resData, null, 2);
    html += `<div class="ix-section"><span class="ix-label">◀ ${resLabel}</span><pre class="ix-json">${escapeHtml(str)}</pre></div>`;
  }
  card.innerHTML = html;
  container.appendChild(card);
  container.scrollTop = container.scrollHeight;
}

// ── Screen management ────────────────────────

const screenLabels = {
  'screen-login': 'authenticate',
  'screen-scan': 'scan',
  'screen-config': 'provision',
  'screen-progress': 'provisioning',
  'screen-control': 'control',
};

function showScreen(id) {
  $$('.screen').forEach((s) => s.classList.remove('active'));
  $(`#${id}`).classList.add('active');
  $('#header-screen-label').textContent = screenLabels[id] || '';
}

// ── Connection status ────────────────────────

function updateConnectionStatus() {
  const dot = $('#status-dot');
  const text = $('#status-text');
  if (ble.isConnected()) {
    dot.className = 'status-dot connected';
    text.textContent = 'connected';
  } else {
    dot.className = 'status-dot';
    text.textContent = 'disconnected';
  }
}

// ── Button loading helper ────────────────────

function setLoading(btn, loading) {
  if (loading) {
    btn.classList.add('loading');
    btn.disabled = true;
  } else {
    btn.classList.remove('loading');
    btn.disabled = false;
  }
}

// ── SCREEN 1: LOGIN ─────────────────────────

$('#form-login').addEventListener('submit', async (e) => {
  e.preventDefault();
  const btn = $('#btn-login');
  const uid = $('#input-uid').value.trim();
  const password = $('#input-password').value.trim();

  if (!uid || !password) return;

  setLoading(btn, true);
  try {
    debugLog(`login(uid=${uid})`, 'api');
    const loginReq = { grant_type: 'uid', uid, area_code: '86', user_country_key: 'CN' };
    const loginData = await api.login(uid, password);
    logIx('ix-login', 'api', 'POST /auth/oauth/token', loginReq, loginData);
    debugLog(`login success, token=${loginData.access_token.slice(0, 20)}...`, 'api');

    state.userId = loginData.userId || uid;

    // Fetch asset tree
    debugLog('getAssetTree()', 'api');
    const assetTree = await api.getAssetTree();
    logIx('ix-login', 'api', 'POST /v1/asset/assetTree', {}, assetTree);
    state.assetId = assetTree.assetId;
    debugLog(`assetId=${state.assetId}`, 'api');

    // Fetch data centers
    debugLog('getDataCenterList()', 'api');
    try {
      const dcs = await api.getDataCenterList();
      logIx('ix-login', 'api', 'POST /v1/common/getDataCenterList', {}, dcs);
      if (dcs && dcs.length > 0) {
        // API may return mqttUrl as "host:port"; BLE protocol requires bare host in `mq`.
        const raw = dcs[0].mqttUrl || 'mqtt-iot.sentino.jp';
        const colon = raw.lastIndexOf(':');
        if (colon > 0 && /^\d+$/.test(raw.slice(colon + 1))) {
          state.mqttUrl = raw.slice(0, colon);
          state.mqttPort = parseInt(raw.slice(colon + 1), 10);
        } else {
          state.mqttUrl = raw;
        }
        state.mqttSslPort = dcs[0].mqttSslPort || '8883';
        debugLog(`mqttUrl=${state.mqttUrl} port=${state.mqttPort}`, 'api');
      }
    } catch (err) {
      debugLog(`getDataCenterList warning: ${err.message}`, 'err');
      state.mqttUrl = 'mqtt-iot.sentino.jp';
    }

    // Show result
    $('#info-userId').textContent = state.userId;
    $('#info-assetId').textContent = state.assetId;
    $('#info-token').textContent = loginData.access_token;
    $('#login-result').classList.remove('hidden');

    // Populate scan screen
    $('#scan-userId').textContent = state.userId;
    $('#scan-assetId').textContent = state.assetId;

    // Transition after a moment
    setTimeout(() => showScreen('screen-scan'), 600);
  } catch (err) {
    debugLog(`login error: ${err.message}`, 'err');
    alert(`Login failed: ${err.message}`);
  } finally {
    setLoading(btn, false);
  }
});

// ── SCREEN 2: SCAN ──────────────────────────

$('#btn-scan').addEventListener('click', async () => {
  const btn = $('#btn-scan');
  console.log('[App] Scan button clicked');
  debugLog('Scan button clicked', 'ble');

  // Check Bluetooth availability first
  if (!navigator.bluetooth) {
    alert('Web Bluetooth API not available.\n\nPlease use Chrome and ensure:\n1. chrome://flags/#enable-web-bluetooth is enabled\n2. System Settings > Privacy > Bluetooth allows Chrome');
    return;
  }

  setLoading(btn, true);
  try {
    const available = await navigator.bluetooth.getAvailability();
    debugLog(`Bluetooth available: ${available}`, 'ble');
    if (!available) {
      alert('Bluetooth is not available on this device.\n\nCheck System Settings > Bluetooth.');
      return;
    }
  } catch (e) {
    debugLog(`getAvailability not supported, continuing...`, 'ble');
  }

  try {
    await ble.scanAndConnect();
    updateConnectionStatus();

    // Get device info
    debugLog('Requesting device.information.get via BLE...', 'ble');
    const devInfoReq = { type: 'device.information.get', ts: Math.floor(Date.now() / 1000) };
    const resp = await ble.request(devInfoReq, 'device.information.get.response', 10000);
    logIx('ix-scan', 'ble', 'device.information.get', devInfoReq, resp);
    state.deviceInfo = resp.data;
    debugLog(`Device info: ${JSON.stringify(resp.data)}`, 'ble');

    // Populate config screen
    $('#dev-pid').textContent = resp.data.pid || '—';
    $('#dev-version').textContent = resp.data.version || '—';
    $('#dev-bind').textContent = resp.data.bind ? 'Bound' : 'Unbound';
    $('#dev-wifi-mac').textContent = resp.data.wifi_mac || '—';
    $('#dev-ble-mac').textContent = resp.data.ble_mac || '—';

    // Try to get device UUID from BLE advertisement manufacturer data
    // ble.js already calls watchAdvertisements() and parses it
    // Also check if device info response has it (some firmware versions may include it)
    const detectedUuid = ble.getDeviceUuid()
      || resp.data.uuid || resp.data.deviceId || resp.data.sn || '';
    $('#dev-uuid').textContent = detectedUuid || '(unknown)';
    $('#input-device-uuid').value = detectedUuid || '';
    debugLog(`Device UUID: ${detectedUuid || 'not available'}`, 'ble');
    debugLog(`Full device info: ${JSON.stringify(resp.data)}`, 'ble');

    showScreen('screen-config');
  } catch (err) {
    console.error('[App] Scan error:', err);
    debugLog(`Scan error: ${err.name}: ${err.message}`, 'err');
    // Open debug panel to show error
    if (!debugOpen) {
      debugOpen = true;
      $('#debug-panel').classList.remove('collapsed');
    }
    if (err.name !== 'NotFoundError') {
      alert(`Scan failed: ${err.message}`);
    }
  } finally {
    setLoading(btn, false);
    updateConnectionStatus();
  }
});

// ── SCREEN 3: CONFIG ────────────────────────

// Scan WiFi
$('#btn-scan-wifi').addEventListener('click', async () => {
  const btn = $('#btn-scan-wifi');
  setLoading(btn, true);
  try {
    debugLog('Requesting thing.network.getwifis via BLE...', 'ble');
    const wifiReq = { type: 'thing.network.getwifis', scan: 1 };
    const resp = await ble.request(wifiReq, 'thing.network.getwifis.response', 15000);
    logIx('ix-config', 'ble', 'thing.network.getwifis', wifiReq, resp);

    const wifis = resp.data?.wifis || [];
    debugLog(`Found ${wifis.length} WiFi networks`, 'ble');

    const select = $('#select-ssid');
    select.innerHTML = '<option value="">Select from scan...</option>';
    for (const w of wifis) {
      const opt = document.createElement('option');
      opt.value = w.ssid;
      opt.textContent = `${w.ssid} (${w.rssi}dBm)`;
      select.appendChild(opt);
    }
    select.classList.remove('hidden');

    select.addEventListener('change', () => {
      if (select.value) {
        $('#input-ssid').value = select.value;
      }
    });
  } catch (err) {
    debugLog(`WiFi scan error: ${err.message}`, 'err');
    alert(`WiFi scan failed: ${err.message}`);
  } finally {
    setLoading(btn, false);
  }
});

// Start provisioning
$('#btn-provision').addEventListener('click', () => {
  const ssid = $('#input-ssid').value.trim();
  const pw = $('#input-wifi-pw').value;

  if (!ssid) {
    alert('Please enter WiFi SSID');
    return;
  }

  showScreen('screen-progress');
  runProvisioning(ssid, pw);
});

// ── SCREEN 4: PROVISIONING ──────────────────

function setStepState(stepName, state) {
  const step = $(`.step[data-step="${stepName}"]`);
  step.className = `step ${state}`; // '', 'active', 'done', 'error'
}

function setStepDetail(stepName, text) {
  $(`#step-detail-${stepName}`).textContent = text;
}

function resetSteps() {
  $$('.step').forEach((s) => (s.className = 'step'));
  $$('.step-detail').forEach((d) => (d.textContent = ''));
  $('#progress-actions').classList.add('hidden');
  $('#progress-error').classList.add('hidden');
}

async function runProvisioning(ssid, wifiPassword) {
  resetSteps();

  // WiFi device: UUID from BLE advertisement manufacturer data (ID Type=0)
  // Fallback to manual input (for 4G devices, UUID comes from QR code scan)
  const deviceUuid = ble.getDeviceUuid()
    || $('#input-device-uuid').value.trim()
    || '';

  debugLog(`Device UUID for bind polling: ${deviceUuid || '(none)'}`, 'ble');

  const provisionContent = {
    sid: ssid,
    pw: wifiPassword,
    bid: state.assetId,
    userId: state.userId,
    mq: state.mqttUrl || 'mqtt-iot.sentino.jp',
    port: state.mqttPort,
    mqttSslPort: state.mqttSslPort,
    country: 'CN',
    areaCode: 'CN',
    tz: Intl.DateTimeFormat().resolvedOptions().timeZone,
    force_bind: true,
  };

  // Listen for BLE status notifications during provisioning
  const statusCodes = {
    1006: 'WiFi connected',
    1701: 'JSON parsed OK',
    1703: 'MQTT server connected',
    1801: 'Bind success',
  };
  let bleBindSuccess = false;

  function onProvisionStatus(msg) {
    if (msg.type === 'thing.network.set.response' || msg.code !== undefined) {
      const code = msg.code ?? msg.data?.code;
      const label = statusCodes[code] || `status ${code}`;
      debugLog(`Device status: ${label} (code=${code})`, 'ble');
      logIx('ix-progress', 'ble', `notify (${label})`, null, msg);
      setStepDetail('poll', label);
      if (code === 1801) bleBindSuccess = true;
    }
  }
  ble.onMessage(onProvisionStatus);

  try {
    // Step 1: Skip encryption, send plaintext (device parses JSON directly)
    setStepState('encrypt', 'done');
    setStepDetail('encrypt', 'plaintext');

    // Step 2: Send via BLE
    setStepState('send', 'active');
    debugLog('Sending thing.network.set via BLE (plaintext)...', 'ble');
    const ts = Date.now();
    const bleMsg = {
      type: 'thing.network.set',
      msgId: `${ts}001`,
      ts,
      data: provisionContent,
    };
    debugLog(`Payload: ${JSON.stringify(bleMsg)}`, 'ble');

    // Step 3: Wait for response
    const resp = await ble.request(bleMsg, 'thing.network.set.response', 15000);
    logIx('ix-progress', 'ble', 'thing.network.set', bleMsg, resp);
    setStepState('send', 'done');
    setStepDetail('send', 'sent');

    setStepState('response', resp.code === 0 ? 'done' : 'error');
    setStepDetail('response', `code=${resp.code}`);

    if (resp.code !== 0) {
      throw new Error(`Device rejected config (code=${resp.code})`);
    }
    debugLog(`thing.network.set.response code=${resp.code}`, 'ble');

    // Step 4: Poll bind status
    setStepState('poll', 'active');
    const pollStart = Date.now();
    const POLL_TIMEOUT = 60000;
    const POLL_INTERVAL = 3000;
    const POLL_START_DELAY = 5000; // Wait 5s for device to connect WiFi + MQTT before polling

    let bound = false;

    // Wait a few seconds for device to connect WiFi and MQTT
    setStepDetail('poll', 'waiting for device...');
    debugLog('Waiting 5s for device to connect WiFi + MQTT...', 'ble');
    await new Promise((r) => setTimeout(r, POLL_START_DELAY));

    while (Date.now() - pollStart < POLL_TIMEOUT) {
      const elapsed = Math.round((Date.now() - pollStart) / 1000);

      // Check BLE notification shortcut
      if (bleBindSuccess) {
        debugLog('Bind confirmed via BLE notification (code=1801)', 'ble');
        bound = true;
        break;
      }

      // Poll API if we have a UUID
      if (deviceUuid) {
        setStepDetail('poll', `polling... ${elapsed}s`);
        try {
          const result = await api.checkBindResult(deviceUuid);
          logIx('ix-progress', 'api', `checkBindResult/${deviceUuid}`, null, { data: result });
          debugLog(`checkBindResult: ${result}`, 'api');
          if (result === 0) {
            bound = true;
            break;
          }
        } catch (err) {
          debugLog(`Poll: ${err.message}`, 'err');
        }
      } else {
        setStepDetail('poll', `waiting... ${elapsed}s (no UUID for API poll)`);
      }

      await new Promise((r) => setTimeout(r, POLL_INTERVAL));
    }

    if (!bound) {
      setStepState('poll', 'error');
      setStepDetail('poll', 'timeout');
      throw new Error('Bind timeout. Device may have bound successfully — check serial log.');
    }

    setStepState('poll', 'done');
    setStepDetail('poll', 'success');

    // Step 5: Done
    setStepState('done', 'done');
    setStepDetail('done', 'Device bound to account');
    debugLog('Provisioning complete!', 'ble');

    $('#progress-actions').classList.remove('hidden');
  } catch (err) {
    debugLog(`Provisioning failed: ${err.message}`, 'err');
    $('#progress-error-msg').textContent = err.message;
    $('#progress-error').classList.remove('hidden');
  } finally {
    ble.offMessage(onProvisionStatus);
  }
}

$('#btn-to-control').addEventListener('click', () => {
  showScreen('screen-control');
  startNotificationListener();
});

$('#btn-retry-provision').addEventListener('click', () => {
  showScreen('screen-config');
});

// ── SCREEN 5: DEVICE CONTROL ────────────────

// Get properties
$('#btn-get-props').addEventListener('click', async () => {
  const btn = $('#btn-get-props');
  setLoading(btn, true);
  try {
    debugLog('Requesting thing.property.get via BLE...', 'ble');
    const getPropReq = { type: 'thing.property.get' };
    const resp = await ble.request(getPropReq, 'thing.property.get.response', 10000);
    logIx('ix-control', 'ble', 'thing.property.get', getPropReq, resp);
    $('#props-output').textContent = JSON.stringify(resp.data || resp, null, 2);
    debugLog(`Properties: ${JSON.stringify(resp.data)}`, 'ble');
  } catch (err) {
    debugLog(`Get properties error: ${err.message}`, 'err');
    $('#props-output').textContent = `Error: ${err.message}`;
  } finally {
    setLoading(btn, false);
  }
});

// Set property
$('#btn-set-prop').addEventListener('click', async () => {
  const btn = $('#btn-set-prop');
  const key = $('#input-prop-key').value.trim();
  const rawValue = $('#input-prop-value').value.trim();

  if (!key) return;

  // Try to parse value as JSON/number, fallback to string
  let value;
  try {
    value = JSON.parse(rawValue);
  } catch {
    value = rawValue;
  }

  setLoading(btn, true);
  try {
    const msg = { type: 'thing.property.set', data: { [key]: value } };
    debugLog(`Setting property: ${key}=${JSON.stringify(value)}`, 'ble');
    const resp = await ble.request(msg, 'thing.property.set.response', 10000);
    logIx('ix-control', 'ble', 'thing.property.set', msg, resp);
    debugLog(`Set property response: code=${resp.code}`, 'ble');

    if (resp.code !== 0) {
      alert(`Set failed (code=${resp.code})`);
    }
  } catch (err) {
    debugLog(`Set property error: ${err.message}`, 'err');
    alert(`Set failed: ${err.message}`);
  } finally {
    setLoading(btn, false);
  }
});

// Notification listener
function startNotificationListener() {
  ble.onMessage(handleNotification);
}

function handleNotification(msg) {
  if (msg.type !== 'thing.property.report') return;

  logIx('ix-control', 'ble', 'thing.property.report', null, msg);
  state.notifCount++;
  $('#notif-count').textContent = state.notifCount;

  const log = $('#notif-log');
  const empty = log.querySelector('.notif-empty');
  if (empty) empty.remove();

  const entry = document.createElement('div');
  entry.className = 'notif-entry';
  const ts = new Date().toLocaleTimeString('en-GB', { hour12: false });
  entry.innerHTML = `<span class="notif-ts">${ts}</span><span class="notif-data">${escapeHtml(JSON.stringify(msg.data))}</span>`;
  log.appendChild(entry);
  log.scrollTop = log.scrollHeight;
}

// Disconnect
$('#btn-disconnect').addEventListener('click', () => {
  ble.offMessage(handleNotification);
  ble.disconnect();
  updateConnectionStatus();
  state.deviceInfo = null;
  state.notifCount = 0;
  showScreen('screen-scan');
});

// ── DEBUG PANEL ─────────────────────────────

let debugOpen = false;

$('#btn-toggle-debug').addEventListener('click', () => {
  debugOpen = !debugOpen;
  $('#debug-panel').classList.toggle('collapsed', !debugOpen);
});

$('#btn-collapse-debug').addEventListener('click', () => {
  debugOpen = false;
  $('#debug-panel').classList.add('collapsed');
});

$('#btn-clear-debug').addEventListener('click', () => {
  debugContent.innerHTML = '';
});

$('#btn-clear-ix').addEventListener('click', () => {
  $('#ix-global').innerHTML = '';
});

// ── SIDEBAR TAB SWITCHING ──────────────────

$$('.ix-tab').forEach(tab => {
  tab.addEventListener('click', () => {
    $$('.ix-tab').forEach(t => t.classList.remove('active'));
    $$('.ix-tab-body').forEach(b => b.classList.remove('active'));
    tab.classList.add('active');
    $(`#tab-${tab.dataset.tab}`).classList.add('active');
  });
});

// ── SERIAL PORT (Web Serial API) ───────────

const serial = {
  port: null,
  reader: null,
  reading: false,
  lineBuffer: '',
  MAX_LINES: 5000,
  lineCount: 0,
};

$('#btn-serial').addEventListener('click', async () => {
  if (serial.port) {
    await serialDisconnect();
  } else {
    await serialConnect();
  }
});

async function serialConnect() {
  if (!('serial' in navigator)) {
    alert('Web Serial API not supported. Use Chrome/Edge.');
    return;
  }
  try {
    serial.port = await navigator.serial.requestPort({
      filters: [
        { usbVendorId: 0x0403 }, // FTDI
        { usbVendorId: 0x10C4 }, // CP210x (Silicon Labs)
        { usbVendorId: 0x1A86 }, // CH340/CH341
        { usbVendorId: 0x067B }, // Prolific PL2303
      ],
    });
    const baudRate = parseInt($('#serial-baud').value, 10);
    await serial.port.open({ baudRate });

    $('#btn-serial').classList.add('serial-connected');
    $('#btn-serial-text').textContent = 'Serial ON';
    const statusEl = $('#serial-status');
    statusEl.textContent = `Connected — ${baudRate} baud`;
    statusEl.classList.add('connected');

    // Switch to serial tab
    $$('.ix-tab').forEach(t => t.classList.remove('active'));
    $$('.ix-tab-body').forEach(b => b.classList.remove('active'));
    $('.ix-tab[data-tab="serial"]').classList.add('active');
    $('#tab-serial').classList.add('active');

    debugLog(`Serial connected (${baudRate} baud)`, 'ble');
    serialRead();
  } catch (e) {
    console.error('Serial connect failed:', e);
    serial.port = null;
  }
}

async function serialDisconnect() {
  serial.reading = false;
  try {
    if (serial.reader) {
      await serial.reader.cancel();
      serial.reader = null;
    }
    if (serial.port) {
      await serial.port.close();
    }
  } catch (e) {
    console.warn('Serial close error:', e);
  }
  serial.port = null;
  serial.reader = null;

  $('#btn-serial').classList.remove('serial-connected');
  $('#btn-serial-text').textContent = 'Serial';
  const statusEl = $('#serial-status');
  statusEl.textContent = 'Not connected';
  statusEl.classList.remove('connected');
  debugLog('Serial disconnected', 'ble');
}

async function serialRead() {
  const decoder = new TextDecoderStream();
  serial.port.readable.pipeTo(decoder.writable);
  serial.reader = decoder.readable.getReader();
  serial.reading = true;

  const output = $('#serial-output');
  const autoscroll = () => $('#serial-autoscroll').checked;

  try {
    while (serial.reading) {
      const { value, done } = await serial.reader.read();
      if (done) break;
      if (value) {
        serial.lineBuffer += value;
        const lines = serial.lineBuffer.split('\n');
        serial.lineBuffer = lines.pop(); // keep incomplete tail

        for (const line of lines) {
          const lineEl = document.createElement('span');
          lineEl.className = 'serial-line';
          if (line.includes(' E ') || line.includes('ERROR') || line.includes('LOGE')) {
            lineEl.style.color = 'var(--danger)';
          } else if (line.includes(' W ') || line.includes('WARN') || line.includes('LOGW')) {
            lineEl.style.color = 'var(--warning)';
          } else if (line.includes(' I ') || line.includes('LOGI')) {
            lineEl.style.color = 'var(--accent)';
          }
          lineEl.textContent = line + '\n';
          output.appendChild(lineEl);
          serial.lineCount++;
        }

        // Trim old lines
        while (serial.lineCount > serial.MAX_LINES) {
          const first = output.firstChild;
          if (first) { output.removeChild(first); serial.lineCount--; }
          else break;
        }

        if (autoscroll()) {
          output.scrollTop = output.scrollHeight;
        }
      }
    }
  } catch (e) {
    if (serial.reading) {
      console.error('Serial read error:', e);
    }
  }
}

$('#btn-clear-serial').addEventListener('click', () => {
  $('#serial-output').innerHTML = '';
  serial.lineCount = 0;
});

// ── COPY TO CLIPBOARD ──────────────────────

function showCopyToast() {
  const toast = document.createElement('div');
  toast.className = 'copy-toast';
  toast.textContent = 'Copied!';
  document.body.appendChild(toast);
  setTimeout(() => toast.remove(), 1400);
}

$('#btn-copy-ix').addEventListener('click', () => {
  const text = $('#ix-global').innerText;
  if (!text.trim()) return;
  navigator.clipboard.writeText(text).then(() => showCopyToast());
});

$('#btn-copy-serial').addEventListener('click', () => {
  const text = $('#serial-output').innerText;
  if (!text.trim()) return;
  navigator.clipboard.writeText(text).then(() => showCopyToast());
});

// ── INIT ────────────────────────────────────

// Check Web Bluetooth support
if (!navigator.bluetooth) {
  debugLog('Web Bluetooth API not available. Use Chrome on macOS/Windows/ChromeOS.', 'err');
}

debugLog('Sentino BLE Tool initialized', 'ble');
updateConnectionStatus();

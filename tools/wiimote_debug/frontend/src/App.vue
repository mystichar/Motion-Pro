<script setup>
import { computed, onMounted, onUnmounted, ref } from "vue";

const state = ref(null);
const platform = ref(null);
const devices = ref([]);
const address = ref("");
const busy = ref(false);
const wsError = ref("");
let socket = null;
let reconnectTimer = null;

const connected = computed(() => state.value?.connected === true);
const orientation = computed(() => state.value?.orientation_deg ?? { pitch: 0, roll: 0 });
const remoteStyle = computed(() => ({
  transform: `rotateX(${orientation.value.pitch}deg) rotateY(${orientation.value.roll}deg)`,
}));

const buttonNames = [
  "Up",
  "Down",
  "Left",
  "Right",
  "A",
  "B",
  "Minus",
  "Plus",
  "One",
  "Two",
  "Home",
];

async function api(path, options = {}) {
  const res = await fetch(path, {
    headers: { "Content-Type": "application/json" },
    ...options,
  });
  const body = await res.json().catch(() => ({}));
  if (!res.ok) {
    throw new Error(body.detail || res.statusText);
  }
  return body;
}

async function refreshStatus() {
  const data = await api("/api/status");
  state.value = data.state;
  platform.value = data.platform;
}

async function scanDevices() {
  busy.value = true;
  try {
    const data = await api("/api/devices");
    devices.value = data.devices ?? [];
  } finally {
    busy.value = false;
  }
}

async function connect(opts = {}) {
  busy.value = true;
  wsError.value = "";
  try {
    const payload = { mock: !!opts.mock };
    if (address.value.trim()) {
      payload.address = address.value.trim();
    }
    state.value = await api("/api/connect", {
      method: "POST",
      body: JSON.stringify(payload),
    });
  } catch (err) {
    wsError.value = err.message;
  } finally {
    busy.value = false;
  }
}

async function disconnect() {
  busy.value = true;
  try {
    state.value = await api("/api/disconnect", { method: "POST" });
  } finally {
    busy.value = false;
  }
}

function connectWs() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  const url = `${proto}://${location.host}/ws/stream`;
  socket = new WebSocket(url);
  socket.onmessage = (event) => {
    const msg = JSON.parse(event.data);
    if (msg.type === "state") {
      state.value = msg;
    }
  };
  socket.onclose = () => {
    reconnectTimer = setTimeout(connectWs, 1500);
  };
}

onMounted(async () => {
  await refreshStatus();
  await scanDevices();
  connectWs();
});

onUnmounted(() => {
  if (socket) socket.close();
  if (reconnectTimer) clearTimeout(reconnectTimer);
});
</script>

<template>
  <div class="layout">
    <header>
      <h1>Wii Remote Debug</h1>
      <p>Bluetooth orientation and raw report viewer for Motion Pro development.</p>
    </header>

    <section class="panel">
      <h2>Connection</h2>
      <div class="row">
        <span class="status-dot" :class="{ on: connected }"></span>
        <span>{{ connected ? "Connected" : "Disconnected" }}</span>
        <span v-if="state?.backend" class="hint">via {{ state.backend }}</span>
        <span v-if="state?.packets_per_second" class="hint">{{ state.packets_per_second }} pkt/s</span>
      </div>
      <div class="row" style="margin-top: 0.75rem">
        <input v-model="address" placeholder="BT address or HID path (optional)" />
      </div>
      <div class="row" style="margin-top: 0.75rem">
        <button class="primary" :disabled="busy || connected" @click="connect()">Connect</button>
        <button :disabled="busy || connected" @click="connect({ mock: true })">Mock</button>
        <button :disabled="busy" @click="scanDevices()">Scan</button>
        <button :disabled="busy || !connected" @click="disconnect()">Disconnect</button>
      </div>
      <p v-if="wsError" class="error">{{ wsError }}</p>
      <ul v-if="devices.length" class="device-list">
        <li v-for="d in devices" :key="d.address">
          {{ d.name }} — <code>{{ d.address }}</code>
          <span v-if="d.via"> ({{ d.via }})</span>
        </li>
      </ul>
      <p v-else class="hint">No remotes found. Hold 1+2 while pairing, then scan again.</p>
      <p v-if="platform" class="hint">
        Platform: {{ platform.os }} · L2CAP {{ platform.l2cap ? "yes" : "no" }} · HID
        {{ platform.hid ? "yes" : "no" }}
      </p>
    </section>

    <div class="grid">
      <section class="panel">
        <h2>Orientation</h2>
        <div class="attitude-wrap">
          <div class="remote" :style="remoteStyle">
            <div class="remote-face">TOP</div>
          </div>
        </div>
        <dl class="metric">
          <dt>Pitch</dt>
          <dd>{{ orientation.pitch?.toFixed(1) ?? "—" }}°</dd>
          <dt>Roll</dt>
          <dd>{{ orientation.roll?.toFixed(1) ?? "—" }}°</dd>
          <dt>|g|</dt>
          <dd>{{ state?.magnitude_g?.toFixed(3) ?? "—" }}</dd>
        </dl>
      </section>

      <section class="panel">
        <h2>Accelerometer</h2>
        <dl class="metric">
          <dt>Raw X / Y / Z</dt>
          <dd>{{ state?.accel_raw?.join(" / ") ?? "—" }}</dd>
          <dt>g X</dt>
          <dd>{{ state?.accel_g?.[0]?.toFixed(4) ?? "—" }}</dd>
          <dt>g Y</dt>
          <dd>{{ state?.accel_g?.[1]?.toFixed(4) ?? "—" }}</dd>
          <dt>g Z</dt>
          <dd>{{ state?.accel_g?.[2]?.toFixed(4) ?? "—" }}</dd>
          <dt>Battery low</dt>
          <dd>{{ state?.battery_low ? "yes" : "no" }}</dd>
          <dt>Extension</dt>
          <dd>{{ state?.extension_connected ? "yes" : "no" }}</dd>
        </dl>
      </section>

      <section class="panel">
        <h2>Buttons</h2>
        <div class="btn-grid">
          <div
            v-for="name in buttonNames"
            :key="name"
            class="btn-chip"
            :class="{ active: state?.buttons?.[name] }"
          >
            {{ name }}
          </div>
        </div>
      </section>

      <section class="panel">
        <h2>Raw report</h2>
        <div class="raw">{{ state?.report_hex || "—" }}</div>
        <p class="hint">Report type: 0x{{ state?.report_type?.toString(16).padStart(2, "0") ?? "—" }}</p>
        <p v-if="state?.error" class="error">{{ state.error }}</p>
      </section>
    </div>
  </div>
</template>

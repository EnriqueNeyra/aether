// --- Tabs ---
    const tabButtons = document.querySelectorAll('.tab-btn');
    const tabPanels = {
      env: document.getElementById('tab-env'),
      fw: document.getElementById('tab-fw')
    };

    tabButtons.forEach(btn => {
      btn.addEventListener('click', () => {
        const tab = btn.dataset.tab;
        // activate button
        tabButtons.forEach(b => b.classList.toggle('tab-btn--active', b === btn));
        // show panel
        Object.entries(tabPanels).forEach(([key, panel]) => {
          panel.classList.toggle('tab-panel--active', key === tab);
        });
      });
    });

    // --- Live metrics + firmware status + temp unit ---

    const metricsEl = document.getElementById('metrics');
    const lastUpdatedChip = document.getElementById('chip-last-updated');
    const statusText = document.getElementById('status-text');

    const fwCurrentEl = document.getElementById('fw-current');
    const fwLatestEl = document.getElementById('fw-latest');
    const fwBadgeEl = document.getElementById('fw-badge');
    const fwStatusTextEl = document.getElementById('fw-status-text');
    const fwUpdateBtn = document.getElementById('fw-update-btn');

    const unitButtons = document.querySelectorAll('.unit-btn');

    let currentFwVersion = null;
    let latestFwVersion = null;
    let updateState = 'unknown';
    let updateProgress = 0.0;
    let hasUpdate = false;
    let currentTempUnit = 'F';

    function formatNumber(v, digits = 1) {
      if (v === null || v === undefined || Number.isNaN(v)) return '—';
      return Number(v).toFixed(digits);
    }

    function applyUnitToUI(unit) {
      currentTempUnit = unit;
      unitButtons.forEach(btn => {
        btn.classList.toggle('unit-btn--active', btn.dataset.unit === unit);
      });
    }

    unitButtons.forEach(btn => {
      btn.addEventListener('click', async () => {
        const unit = btn.dataset.unit;
        const previousUnit = currentTempUnit;
        applyUnitToUI(unit);
        try {
          const res = await fetch('/api/temp_unit?unit=' + encodeURIComponent(unit), {
            method: 'POST'
          });
          if (!res.ok) throw new Error('bad status');
          const json = await res.json();
          if (json.temp_unit === 'C' || json.temp_unit === 'F') {
            applyUnitToUI(json.temp_unit);
          }
          await pollState();
        } catch (e) {
          applyUnitToUI(previousUnit);
        }
      });
    });

    function renderMetrics(data) {
      const unit = (data.temp_unit === 'C' || data.temp_unit === 'F') ? data.temp_unit : 'F';
      applyUnitToUI(unit);

      const tempRaw = data.temp;
      const temp = (tempRaw !== null && tempRaw !== undefined && !Number.isNaN(tempRaw))
        ? (unit === 'F' ? (tempRaw * 9/5 + 32) : tempRaw)
        : null;

      const items = [
        { label: 'CO\u2082',  value: data.co2,    unit: 'ppm',      decimals: 1 },
        { label: 'Temp',      value: temp,        unit: '°' + unit, decimals: 1 },
        { label: 'RH',        value: data.rh, unit: '%',        decimals: 1 },
        { label: 'PM1.0',     value: data.pm1,    unit: 'µg/m³',    decimals: 1 },
        { label: 'PM2.5',     value: data.pm25,   unit: 'µg/m³',    decimals: 1 },
        { label: 'PM4.0',     value: data.pm4,    unit: 'µg/m³',    decimals: 1 },
        { label: 'PM10',      value: data.pm10,   unit: 'µg/m³',    decimals: 1 },
        { label: 'VOC Index', value: data.voc,    unit: '',         decimals: 0 },
        { label: 'NOx Index', value: data.nox,    unit: '',         decimals: 0 }
      ];

      metricsEl.innerHTML = items.map(m => {
        const val = formatNumber(m.value, m.decimals);
        return `
          <div class="metric">
            <div class="metric-label">${m.label}</div>
            <div class="metric-main">
              <div class="metric-value">${val}</div>
              <div class="metric-unit">${m.unit}</div>
            </div>
          </div>
        `;
      }).join('');

      const now = new Date();
      lastUpdatedChip.textContent =
        'Last updated: ' + now.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
    }

    function renderFirmwareStatus() {
      fwCurrentEl.textContent = currentFwVersion || '—';
      fwLatestEl.textContent = latestFwVersion || '—';

      if (updateState === 'installing') {
        fwBadgeEl.textContent = 'Installing update';
        fwBadgeEl.className = 'fw-badge fw-badge--warn';
        if (updateProgress > 0) {
          fwStatusTextEl.textContent = 'Installing firmware update... ' + Math.round(updateProgress) + '%';
        } else {
          fwStatusTextEl.textContent = 'Starting firmware update... device will reboot when done.';
        }
        fwUpdateBtn.disabled = true;
        fwUpdateBtn.textContent = 'Installing...';
        return;
      }

      if (!latestFwVersion) {
        fwBadgeEl.textContent = 'Status unknown';
        fwBadgeEl.className = 'fw-badge fw-badge--unknown';
        fwStatusTextEl.textContent = 'Awaiting update status from the device...';
        fwUpdateBtn.disabled = true;
        fwUpdateBtn.textContent = 'Update firmware';
        return;
      }

      if (hasUpdate) {
        fwBadgeEl.textContent = 'Update available';
        fwBadgeEl.className = 'fw-badge fw-badge--warn';
        fwStatusTextEl.textContent = 'A newer firmware is available. Click the button to install it over Wi-Fi.';
        fwUpdateBtn.disabled = false;
        fwUpdateBtn.textContent = 'Update to ' + latestFwVersion;
      } else {
        fwBadgeEl.textContent = 'Up to date';
        fwBadgeEl.className = 'fw-badge fw-badge--ok';
        fwStatusTextEl.textContent = 'This device is running the latest known firmware.';
        fwUpdateBtn.disabled = true;
        fwUpdateBtn.textContent = 'Up to date';
      }
    }

    function renderFirmwareFromState(data) {
      currentFwVersion = data.fw_version || null;
      latestFwVersion = data.latest_version || null;
      updateState = data.update_state || 'unknown';
      updateProgress = data.update_progress || 0.0;
      hasUpdate = data.has_update || false;
      renderFirmwareStatus();
    }

    fwUpdateBtn.addEventListener('click', async () => {
      if (fwUpdateBtn.disabled) return;
      fwUpdateBtn.disabled = true;
      fwStatusTextEl.textContent = 'Starting firmware update… device will reboot when done.';

      try {
        const res = await fetch('/api/perform_update', { method: 'POST' });
        if (!res.ok) throw new Error('bad status');
        // If the device reboots quickly, this page will drop connection anyway.
        fwStatusTextEl.textContent = 'Update started. This page may become unreachable while the device reboots.';
      } catch (e) {
        fwStatusTextEl.textContent = 'Failed to start update. Check connection and try again.';
        // Re-enable after a short delay
        setTimeout(() => {
          renderFirmwareStatus();
        }, 3000);
      }
    });

    async function pollState() {
      try {
        const res = await fetch('/api/state', { cache: 'no-store' });
        if (!res.ok) throw new Error('bad status');
        const json = await res.json();
        renderMetrics(json);
        renderFirmwareFromState(json);
        statusText.textContent = 'Live';
      } catch (e) {
        statusText.textContent = 'Loading';
      }
    }

    // Poll local state every 5s, which includes the backend's OTA status
    setInterval(pollState, 5000);
    pollState();

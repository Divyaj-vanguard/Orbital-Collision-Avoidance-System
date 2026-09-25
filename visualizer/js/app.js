/**
 * ============================================================================
 * OCAS: Autonomous Orbital Collision Avoidance System
 * Mission Control Visualizer Application Controller
 * Team ID: DSCPP-III-2026-T018 | Graphic Era University
 * ============================================================================
 */

document.addEventListener('DOMContentLoaded', () => {
  const visualizer = new window.OrbitVisualizer3D('canvas-container');
  let autoPilotInterval = null;

  // UI Element References
  const elSatAlt = document.getElementById('sat-alt');
  const elSatVel = document.getElementById('sat-vel');
  const elSatFuel = document.getElementById('sat-fuel');
  const elSatFuelBar = document.getElementById('sat-fuel-bar');
  const elSatMass = document.getElementById('sat-mass');
  const elMetClock = document.getElementById('met-clock');
  const elRingCount = document.getElementById('ring-count');
  const elRingOverwritten = document.getElementById('ring-overwritten');
  const elFifoCount = document.getElementById('fifo-count');
  const elFifoDropped = document.getElementById('fifo-dropped');
  const elStackDepth = document.getElementById('stack-depth');
  const elThreatCount = document.getElementById('threat-count');
  const elThreatLevel = document.getElementById('threat-level-badge');
  const elRootThreatName = document.getElementById('root-threat-name');
  const elRootThreatProb = document.getElementById('root-threat-prob');
  const elRootThreatAction = document.getElementById('root-threat-action');
  const elHeapTableBody = document.getElementById('heap-table-body');
  const elLedgerTableBody = document.getElementById('ledger-table-body');
  const elAuditSummary = document.getElementById('audit-summary-metrics');
  const elLogConsole = document.getElementById('log-console');

  // Explainability Bars
  const elBarDist = document.getElementById('bar-f-dist');
  const elBarVel = document.getElementById('bar-f-vel');
  const elBarTca = document.getElementById('bar-f-tca');
  const elBarCov = document.getElementById('bar-f-cov');
  const elValDist = document.getElementById('val-f-dist');
  const elValVel = document.getElementById('val-f-vel');
  const elValTca = document.getElementById('val-f-tca');
  const elValCov = document.getElementById('val-f-cov');

  let missionStartTime = Date.now();

  function updateMET() {
    const elapsedSec = Math.floor((Date.now() - missionStartTime) / 1000);
    const hrs = String(Math.floor(elapsedSec / 3600)).padStart(2, '0');
    const mins = String(Math.floor((elapsedSec % 3600) / 60)).padStart(2, '0');
    const secs = String(elapsedSec % 60).padStart(2, '0');
    if (elMetClock) elMetClock.textContent = `T+${hrs}:${mins}:${secs}`;
  }
  setInterval(updateMET, 1000);

  // Poll Telemetry State from Backend
  async function fetchTelemetry() {
    try {
      const res = await fetch('/api/telemetry');
      if (!res.ok) throw new Error(`HTTP error ${res.status}`);
      const data = await res.json();
      updateDashboard(data);
    } catch (err) {
      console.warn('[TELEMETRY] Polling fallback/local simulation:', err.message);
    }
  }

  // Poll Audit Logs
  async function fetchLogs() {
    try {
      const res = await fetch('/api/logs');
      if (!res.ok) return;
      const logs = await res.json();
      renderLogs(logs);
    } catch (err) {
      // ignore
    }
  }

  function updateDashboard(data) {
    if (!data || !data.satellite) return;

    const sat = data.satellite;
    const rMag = Math.sqrt(sat.position_km[0]**2 + sat.position_km[1]**2 + sat.position_km[2]**2);
    const altKm = Math.max(0, rMag - 6378.137);
    const vMag = Math.sqrt(sat.velocity_kms[0]**2 + sat.velocity_kms[1]**2 + sat.velocity_kms[2]**2);

    if (elSatAlt) elSatAlt.textContent = altKm.toFixed(1);
    if (elSatVel) elSatVel.textContent = vMag.toFixed(2);
    if (elSatFuel) elSatFuel.textContent = sat.fuel_mass_kg.toFixed(1);
    if (elSatMass) elSatMass.textContent = sat.total_mass_kg.toFixed(0);

    const fuelPercent = Math.max(0, Math.min(100, (sat.fuel_mass_kg / 200.0) * 100));
    if (elSatFuelBar) {
      elSatFuelBar.style.width = `${fuelPercent}%`;
      elSatFuelBar.className = 'progress-fill ' + (fuelPercent < 20 ? 'critical' : (fuelPercent < 50 ? 'warning' : ''));
    }

    // Ingestion Layer Stats
    if (data.ring_buffer) {
      if (elRingCount) elRingCount.textContent = `${data.ring_buffer.count}/${data.ring_buffer.capacity}`;
      if (elRingOverwritten) elRingOverwritten.textContent = data.ring_buffer.overwritten;
    }
    if (data.cdm_fifo) {
      if (elFifoCount) elFifoCount.textContent = `${data.cdm_fifo.count}/${data.cdm_fifo.capacity}`;
      if (elFifoDropped) elFifoDropped.textContent = data.cdm_fifo.dropped;
    }
    if (data.rollback_stack && elStackDepth) {
      elStackDepth.textContent = `${data.rollback_stack.depth}/${data.rollback_stack.capacity}`;
    }

    // Update 3D Satellite
    visualizer.updateSatellite({
      r: sat.position_km,
      v: sat.velocity_kms
    });

    // Heap & Threat Assessment
    if (data.max_heap) {
      const items = data.max_heap.items || [];
      if (elThreatCount) elThreatCount.textContent = items.length;
      visualizer.updateThreats(items);

      if (items.length > 0) {
        const top = items[0];
        if (elRootThreatName) elRootThreatName.textContent = `#${top.id} ${top.name}`;
        if (elRootThreatProb) elRootThreatProb.textContent = (top.risk_score * 100).toFixed(1) + '%';
        if (elRootThreatAction) elRootThreatAction.textContent = top.action;

        if (elThreatLevel) {
          elThreatLevel.textContent = top.level;
          elThreatLevel.className = 'status-pill ' + (top.level === 'CRITICAL' ? 'danger' : (top.level === 'HIGH' ? 'warning' : ''));
        }

        // Explainability breakdown
        if (top.explain) {
          const exp = top.explain;
          if (elBarDist) elBarDist.style.width = `${(exp.f_d * 100).toFixed(0)}%`;
          if (elBarVel)  elBarVel.style.width  = `${(exp.f_v * 100).toFixed(0)}%`;
          if (elBarTca)  elBarTca.style.width  = `${(exp.f_t * 100).toFixed(0)}%`;
          if (elBarCov)  elBarCov.style.width  = `${(exp.f_sigma * 100).toFixed(0)}%`;

          if (elValDist) elValDist.textContent = exp.f_d.toFixed(2);
          if (elValVel)  elValVel.textContent  = exp.f_v.toFixed(2);
          if (elValTca)  elValTca.textContent  = exp.f_t.toFixed(2);
          if (elValCov)  elValCov.textContent  = exp.f_sigma.toFixed(2);
        }
      } else {
        if (elRootThreatName) elRootThreatName.textContent = 'NO ACTIVE THREATS';
        if (elRootThreatProb) elRootThreatProb.textContent = '0.0%';
        if (elRootThreatAction) elRootThreatAction.textContent = 'NOMINAL_PASS';
        if (elThreatLevel) {
          elThreatLevel.textContent = 'NOMINAL';
          elThreatLevel.className = 'status-pill';
        }
      }

      // Render Max-Heap Table
      renderHeapTable(items);
    }

    // Ledger & Summary
    if (data.ledger) {
      renderLedgerTable(data.ledger.records || []);
      if (data.ledger.summary && elAuditSummary) {
        const s = data.ledger.summary;
        elAuditSummary.innerHTML = `
          <span>Total Maneuvers: <b>${s.total_maneuvers}</b></span> |
          <span>Cum. &Delta;V: <b>${s.total_delta_v.toFixed(1)} m/s</b></span> |
          <span>Propellant: <b>${s.total_propellant.toFixed(2)} kg</b></span> |
          <span>Chemical: <b>${s.chemical_burns}</b></span> |
          <span>Cold Gas: <b>${s.cold_gas_burns}</b></span>
        `;
      }
    }
  }

  function renderHeapTable(items) {
    if (!elHeapTableBody) return;
    elHeapTableBody.innerHTML = items.map((item, idx) => {
      const tagClass = item.level.toLowerCase();
      return `
        <tr>
          <td>${idx === 0 ? '<b>[ROOT]</b> ' : ''}#${item.id}</td>
          <td title="${item.name}">${item.name.substring(0, 18)}</td>
          <td>${Math.round(item.d_miss_m)}m</td>
          <td>${item.t_tca_s.toFixed(0)}s</td>
          <td><b>${(item.risk_score * 100).toFixed(1)}%</b></td>
          <td><span class="risk-tag ${tagClass}">${item.level}</span></td>
        </tr>
      `;
    }).join('');
  }

  function renderLedgerTable(records) {
    if (!elLedgerTableBody) return;
    elLedgerTableBody.innerHTML = records.map(rec => `
      <tr>
        <td>#${rec.id}</td>
        <td>${rec.target.substring(0, 16)}</td>
        <td>${rec.delta_v_actual.toFixed(2)} m/s</td>
        <td>${rec.propellant_spent.toFixed(2)} kg</td>
        <td>${rec.duration_s.toFixed(1)}s</td>
        <td><span class="badge-tag">${rec.engine}</span></td>
      </tr>
    `).join('');
  }

  function renderLogs(logs) {
    if (!elLogConsole || !Array.isArray(logs)) return;
    elLogConsole.innerHTML = logs.slice(-15).map(log => `
      <div class="log-entry">
        <span class="log-time">${log.timestamp ? log.timestamp.substring(11, 19) : ''}</span>
        <span class="log-event">[${log.event}]</span>
        <span class="log-msg">${log.notes || ''}</span>
      </div>
    `).join('');
    elLogConsole.scrollTop = elLogConsole.scrollHeight;
  }

  // Interactive Action Dispatcher
  async function triggerAction(actionName, payload = {}) {
    try {
      const res = await fetch('/api/action', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: actionName, ...payload })
      });
      const data = await res.json();
      console.log(`[ACTION: ${actionName}]`, data);

      if (data.burn_executed) {
        visualizer.triggerBurnAnimation(data.engine_used, data.burn_duration_s || 5);
      }

      fetchTelemetry();
      fetchLogs();
    } catch (err) {
      console.error(`[ACTION ERROR: ${actionName}]`, err);
    }
  }

  // Bind Buttons
  document.getElementById('btn-step')?.addEventListener('click', () => triggerAction('step'));
  document.getElementById('btn-anomaly')?.addEventListener('click', () => triggerAction('inject_fault'));
  document.getElementById('btn-surge')?.addEventListener('click', () => triggerAction('debris_surge'));
  document.getElementById('btn-rollback')?.addEventListener('click', () => triggerAction('rollback'));
  document.getElementById('btn-reset')?.addEventListener('click', () => triggerAction('reset'));

  // Camera Controls
  document.getElementById('btn-view-orbit')?.addEventListener('click', () => visualizer.setViewMode('orbit'));
  document.getElementById('btn-view-top')?.addEventListener('click', () => visualizer.setViewMode('top'));
  document.getElementById('btn-view-follow')?.addEventListener('click', () => visualizer.setViewMode('follow'));

  // Auto-Pilot Toggle
  const btnAuto = document.getElementById('btn-auto');
  if (btnAuto) {
    btnAuto.addEventListener('click', () => {
      if (autoPilotInterval) {
        clearInterval(autoPilotInterval);
        autoPilotInterval = null;
        btnAuto.textContent = '▶ Auto-Pilot Loop';
        btnAuto.classList.remove('danger');
        btnAuto.classList.add('primary');
      } else {
        autoPilotInterval = setInterval(() => triggerAction('step'), 2500);
        btnAuto.textContent = '⏸ Pause Auto-Pilot';
        btnAuto.classList.remove('primary');
        btnAuto.classList.add('danger');
      }
    });
  }

  // Initial Poll & Interval
  fetchTelemetry();
  fetchLogs();
  setInterval(fetchTelemetry, 1500);
  setInterval(fetchLogs, 2000);
});

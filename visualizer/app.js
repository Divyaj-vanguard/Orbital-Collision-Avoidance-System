// OCAS Mission View. Reads window.OCAS_DATA (written by build_data.py).
// Orbits: positions precomputed by build_data.py with SGP4 from CelesTrak's
// latest orbital elements, every 5 s around each closest approach.
(() => {
  "use strict";
  const D = window.OCAS_DATA;
  if (!D) {
    document.getElementById("sources").textContent =
      "No data. Run: python3 visualizer/build_data.py (from the repo root), then reload.";
    return;
  }

  const RE = 6378.137, DEG = Math.PI / 180;
  const css = (n) => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
  const C = { obj1: css("--obj1"), obj2: css("--obj2"), accent: css("--accent"), line: css("--line"),
              muted: css("--muted"), ink: css("--ink"), ok: css("--ok"), warn: css("--warn"), bad: css("--bad") };
  const fmt = (n) => n.toLocaleString("en-US");
  const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
  const clean = (name) => name.replace(/\s*\[[+-]\]\s*$/, "");

  // ---------------------------------------------------------------- header
  const E = D.engine, S = D.socrates;
  document.getElementById("sources").textContent =
    `Built ${D.generated} · ESA Kelvins test set (${fmt(E.alerts)} real alerts) · CelesTrak ${S.file}`;
  document.getElementById("soc-summary").textContent =
    `${fmt(S.pairs)} predicted approaches in the next 7 days · ${fmt(S.under_1km)} under 1 km · ${fmt(S.under_100m)} under 100 m`;

  // ---------------------------------------------------------------- tracks
  function at(track, key, minutes) {  // linear interpolation between 5 s samples
    const pts = track[key], f = (minutes + track.window_min) / track.step_min;
    const k = Math.max(0, Math.min(pts.length - 2, Math.floor(f))), w = Math.max(0, Math.min(1, f - k));
    return [0, 1, 2].map((c) => pts[k][c] * (1 - w) + pts[k + 1][c] * w);
  }

  // ---------------------------------------------------------------- globe
  const canvas = document.getElementById("globe"), g = canvas.getContext("2d");
  const view = { yaw: 0.6, pitch: 0.35, zoom: 1 };
  let pair = null, tOffsetMin = -5;

  function project(v) {
    const cy = Math.cos(view.yaw), sy = Math.sin(view.yaw), cp = Math.cos(view.pitch), sp = Math.sin(view.pitch);
    const x1 = cy * v[0] - sy * v[1], y1 = sy * v[0] + cy * v[1];
    const y2 = cp * y1 - sp * v[2], z2 = sp * y1 + cp * v[2];
    const scale = (Math.min(canvas.width, canvas.height) / 2 - 20) / 8200 * view.zoom;
    return { x: canvas.width / 2 + x1 * scale, y: canvas.height / 2 - z2 * scale, depth: y2, scale };
  }
  const hidden = (q) => q.depth > 0 && Math.hypot(q.x - canvas.width / 2, q.y - canvas.height / 2) < RE * q.scale;

  function drawEarth() {
    const c = project([0, 0, 0]), R = RE * c.scale;
    const grad = g.createRadialGradient(c.x - R * 0.3, c.y - R * 0.3, R * 0.1, c.x, c.y, R);
    grad.addColorStop(0, "#2b5b86"); grad.addColorStop(1, "#11253a");
    g.fillStyle = grad; g.beginPath(); g.arc(c.x, c.y, R, 0, 2 * Math.PI); g.fill();
    g.strokeStyle = "rgba(160,200,235,0.18)"; g.lineWidth = 1;
    const line = (pts) => {
      g.beginPath(); let pen = false;
      for (const v of pts) {
        const q = project(v);
        if (q.depth > 0) { pen = false; continue; }  // far side of the Earth
        pen ? g.lineTo(q.x, q.y) : g.moveTo(q.x, q.y); pen = true;
      }
      g.stroke();
    };
    for (let lat = -60; lat <= 60; lat += 30) {
      const pts = [];
      for (let lon = 0; lon <= 360; lon += 5) pts.push(sph(lat, lon));
      line(pts);
    }
    for (let lon = 0; lon < 360; lon += 30) {
      const pts = [];
      for (let lat = -90; lat <= 90; lat += 5) pts.push(sph(lat, lon));
      line(pts);
    }
  }
  const sph = (lat, lon) => [RE * Math.cos(lat * DEG) * Math.cos(lon * DEG), RE * Math.cos(lat * DEG) * Math.sin(lon * DEG), RE * Math.sin(lat * DEG)];

  function drawPath(pts, color) {
    g.lineWidth = 1.6;
    let prev = null;
    for (const v of pts) {
      if (!v) { prev = null; continue; }
      const q = project(v);
      if (prev) {
        g.strokeStyle = color; g.globalAlpha = hidden(q) ? 0.18 : 0.9;
        g.beginPath(); g.moveTo(prev.x, prev.y); g.lineTo(q.x, q.y); g.stroke();
      }
      prev = q;
    }
    g.globalAlpha = 1;
  }

  function drawObject(v, color, label) {
    const q = project(v), behind = hidden(q);
    g.globalAlpha = behind ? 0.35 : 1;
    g.fillStyle = color; g.beginPath(); g.arc(q.x, q.y, 6, 0, 2 * Math.PI); g.fill();
    g.strokeStyle = "#0d1218"; g.lineWidth = 2; g.stroke();
    g.font = "12px " + css("--font"); g.fillStyle = C.ink; g.fillText(label, q.x + 9, q.y - 8);
    g.globalAlpha = 1;
  }

  function draw() {
    g.clearRect(0, 0, canvas.width, canvas.height);
    drawEarth();
    if (!pair || !pair.track) {
      g.fillStyle = C.muted; g.font = "13px " + css("--font");
      g.fillText("Orbit data not available for this pair", 16, 24);
      return;
    }
    drawPath(pair.track.path_a, C.obj1);
    drawPath(pair.track.path_b, C.obj2);
    const p1 = at(pair.track, "a", tOffsetMin), p2 = at(pair.track, "b", tOffsetMin);
    drawObject(p1, C.obj1, clean(pair.name1));
    drawObject(p2, C.obj2, clean(pair.name2));
    const d = Math.hypot(p1[0] - p2[0], p1[1] - p2[1], p1[2] - p2[2]);
    g.fillStyle = C.muted; g.font = "12.5px " + css("--font");
    g.fillText(`Separation now: ${d < 10 ? d.toFixed(2) : fmt(Math.round(d))} km`, 14, canvas.height - 14);
  }

  // drag + zoom
  let drag = null;
  canvas.addEventListener("pointerdown", (ev) => { drag = { x: ev.clientX, y: ev.clientY }; canvas.setPointerCapture(ev.pointerId); });
  canvas.addEventListener("pointermove", (ev) => {
    if (!drag) return;
    view.yaw += (ev.clientX - drag.x) * 0.008;
    view.pitch = Math.max(-1.5, Math.min(1.5, view.pitch + (ev.clientY - drag.y) * 0.008));
    drag = { x: ev.clientX, y: ev.clientY }; draw();
  });
  canvas.addEventListener("pointerup", () => { drag = null; });
  canvas.addEventListener("wheel", (ev) => {
    ev.preventDefault();
    view.zoom = Math.max(0.6, Math.min(4, view.zoom * (ev.deltaY < 0 ? 1.1 : 1 / 1.1))); draw();
  }, { passive: false });

  // ---------------------------------------------------------------- pair list
  const pairs = S.top.map((p) => ({ ...p, tca: Date.parse(p.tca) }));
  const list = document.getElementById("pairs");
  list.innerHTML = pairs.map((p, k) => `
    <li><button type="button" data-k="${k}" aria-pressed="false">
      <span class="names">${esc(clean(p.name1))} × ${esc(clean(p.name2))}</span>
      <span class="meta">${p.range_m} m · ${p.speed_kms.toFixed(1)} km/s · ${new Date(p.tca).toISOString().slice(5, 16).replace("T", " ")} UTC</span>
    </button></li>`).join("");
  list.addEventListener("click", (ev) => {
    const b = ev.target.closest("button[data-k]");
    if (b) select(+b.dataset.k);
  });

  function select(k) {
    pair = pairs[k];
    list.querySelectorAll("button").forEach((b) => b.setAttribute("aria-pressed", String(+b.dataset.k === k)));
    const alt = (o) => (o ? `${fmt(o.alt_km)} km, ${o.incl.toFixed(1)}°` : "n/a");
    const tr = pair.track;
    const kv = (k2, v) => `<div class="kv"><span class="k">${k2}</span><span class="v">${v}</span></div>`;
    document.getElementById("pair-detail").innerHTML =
      kv("Object 1", `<span class="dot" style="background:${C.obj1}"></span>${esc(clean(pair.name1))} (#${pair.id1})`) +
      kv("Object 2", `<span class="dot" style="background:${C.obj2}"></span>${esc(clean(pair.name2))} (#${pair.id2})`) +
      kv("Closest approach", new Date(pair.tca).toISOString().replace("T", " ").slice(0, 19) + " UTC") +
      kv("Miss distance, CelesTrak prediction", `${pair.range_m} m`) +
      kv("Miss distance, latest orbit data", tr ? `${tr.min_sep_km < 10 ? tr.min_sep_km.toFixed(2) : fmt(Math.round(tr.min_sep_km))} km` : "n/a") +
      kv("Relative speed", `${pair.speed_kms.toFixed(2)} km/s`) +
      kv("Max collision probability", pair.max_prob.toExponential(2)) +
      kv("Orbits (altitude, inclination)", `${alt(pair.orbit1)} · ${alt(pair.orbit2)}`);
    draw();
  }

  // time slider
  const slider = document.getElementById("time"), out = document.getElementById("time-out");
  const setTime = (m) => {
    tOffsetMin = m; slider.value = m;
    out.textContent = `${m < 0 ? "−" : "+"}${Math.abs(m).toFixed(1)} min`; draw();
  };
  slider.addEventListener("input", () => setTime(+slider.value));
  let timer = null;
  const playBtn = document.getElementById("play");
  playBtn.addEventListener("click", () => {
    if (timer) { clearInterval(timer); timer = null; playBtn.textContent = "Play"; return; }
    playBtn.textContent = "Pause";
    timer = setInterval(() => setTime(tOffsetMin >= 30 ? -30 : Math.round((tOffsetMin + 1 / 12) * 12) / 12), 40);
  });

  // ---------------------------------------------------------------- engine panel
  document.getElementById("engine-note").textContent =
    `${E.regime} satellite at ${E.altitude_km} km · risk model ${E.model} · fuel budget ${E.budget} m/s Δv`;
  const tile = (v, k) => `<div class="tile"><div class="v">${v}</div><div class="k">${k}</div></div>`;
  document.getElementById("tiles").innerHTML =
    tile(fmt(E.alerts), "alerts checked") + tile(fmt(E.dangerous), "judged dangerous") +
    tile(fmt(E.planned), "manoeuvres planned") + tile(fmt(E.committed), "sent to OEM") +
    tile(fmt(E.no_fuel), "no fuel left") + tile(fmt(E.too_large), "too large for one burn");

  // fuel chart
  (() => {
    const cv = document.getElementById("fuel"), c = cv.getContext("2d");
    const W = cv.width, H = cv.height, L = 42, R = 12, T = 12, B = 30;
    const maxC = E.cycles, maxV = E.budget;
    const X = (cy) => L + (cy / maxC) * (W - L - R), Y = (v) => H - B - (v / maxV) * (H - T - B);
    c.font = "11px " + css("--font"); c.fillStyle = C.muted; c.strokeStyle = C.line; c.lineWidth = 1;
    for (let v = 0; v <= maxV; v += maxV / 5) {
      c.beginPath(); c.moveTo(L, Y(v)); c.lineTo(W - R, Y(v)); c.stroke();
      c.fillText(v.toFixed(0), 8, Y(v) + 4);
    }
    for (let cy = 0; cy <= maxC; cy += 200) c.fillText(String(cy), X(cy) - 8, H - 10);
    c.fillText("cycle", W - R - 30, H - 10);
    c.setLineDash([5, 4]); c.strokeStyle = C.bad; c.beginPath(); c.moveTo(L, Y(maxV)); c.lineTo(W - R, Y(maxV)); c.stroke();
    c.setLineDash([]); c.fillStyle = C.bad; c.fillText("budget", W - R - 44, Y(maxV) + 14);
    const pts = [[0, 0], ...E.fuel, [maxC, E.fuel.length ? E.fuel[E.fuel.length - 1][1] : 0]];
    c.beginPath(); c.moveTo(X(0), Y(0));
    let last = 0;
    for (const [cy, v] of pts) { c.lineTo(X(cy), Y(last)); c.lineTo(X(cy), Y(v)); last = v; }
    c.lineTo(X(maxC), Y(0)); c.closePath(); c.fillStyle = "rgba(92,200,214,0.15)"; c.fill();
    c.beginPath(); last = 0; c.moveTo(X(0), Y(0));
    for (const [cy, v] of pts) { c.lineTo(X(cy), Y(last)); c.lineTo(X(cy), Y(v)); last = v; }
    c.strokeStyle = C.accent; c.lineWidth = 2; c.stroke();
  })();

  // decision bar
  (() => {
    const parts = [["Sent to OEM", E.committed, C.ok], ["Aborted by operator", E.rolled_back, C.warn],
                   ["Too large for one burn", E.too_large, C.warn], ["No fuel left", E.no_fuel, C.bad]]
      .filter((p) => p[1] > 0);
    document.getElementById("decisions").innerHTML = parts
      .map(([k, v, col]) => `<span title="${k}: ${v}" style="width:${(100 * v) / E.planned}%;background:${col}"></span>`).join("");
    document.getElementById("decision-legend").innerHTML = parts
      .map(([k, v, col]) => `<li><span class="dot" style="background:${col}"></span>${k}: <b>${fmt(v)}</b> (${((100 * v) / E.planned).toFixed(0)}%)</li>`).join("") +
      `<li class="small">Fuel runs out at cycle ${E.fuel.length ? E.fuel[E.fuel.length - 1][0] : "-"} of ${E.cycles}; later threats go to ground control.</li>`;
  })();

  // manoeuvre table
  (() => {
    const rank = { IMMEDIATE: 0, PRIORITY: 1, ROUTINE: 2 };
    const words = { COMMITTED: ["sent to OEM", "ok"], ROLLED_BACK: ["aborted", "warn"],
                    REJECTED_DV_CAP: ["too large", "warn"], REJECTED_BUDGET: ["no fuel", "bad"], STAGED: ["planned", "ok"] };
    const plane = { ALONG_TRACK: "along-track", RADIAL: "radial", CROSS_TRACK: "cross-track" };
    const rows = [...E.manoeuvres].sort((a, b) => rank[a.urgency] - rank[b.urgency] || a.burn_h - b.burn_h).slice(0, 12);
    document.getElementById("man-table").innerHTML =
      "<thead><tr><th>Threat</th><th>Urgency</th><th>Direction</th><th class='num'>Δv m/s</th><th class='num'>Burn in h</th><th class='num'>Miss after km</th><th>Decision</th></tr></thead><tbody>" +
      rows.map((m) => {
        const [w, cls] = words[m.status] || [m.status, "ok"];
        return `<tr><td>${m.alert}</td><td>${m.urgency.toLowerCase()}</td><td>${plane[m.plane] || m.plane}</td>` +
          `<td class="num">${m.dv.toFixed(3)}</td><td class="num">${m.burn_h.toFixed(1)}</td><td class="num">${m.sep_km.toFixed(1)}</td>` +
          `<td><span class="pill ${cls}">${w}</span></td></tr>`;
      }).join("") + "</tbody>";
  })();

  // model table
  (() => {
    const M = D.model_check;
    document.getElementById("model-note").textContent = M.alerts ? `${M.alerts} real alerts, ${M.positives} truly dangerous` : "";
    document.getElementById("model-table").innerHTML =
      "<thead><tr><th>Method</th><th class='num'>Accuracy</th><th class='num'>Sensitivity</th><th class='num'>Specificity</th><th class='num'>Precision</th></tr></thead><tbody>" +
      M.rows.map((r) => `<tr class="${r.name === "LSTM" ? "hl" : ""}"><td>${esc(r.name)}</td><td class="num">${r.accuracy.toFixed(3)}</td>` +
        `<td class="num">${r.sensitivity.toFixed(3)}</td><td class="num">${r.specificity.toFixed(3)}</td><td class="num">${r.precision.toFixed(3)}</td></tr>`).join("") +
      "</tbody>";
  })();

  // correlation heatmap + feature-set comparison
  (() => {
    const R = D.correlation, panel = document.getElementById("corr-panel");
    if (!R) { panel.hidden = true; return; }
    const short = (f) => f.replace("relative_", "rel_").replace("_distance", "_dist").replace("covariance", "cov");
    const label = (f) => esc(short(f)) + (R.log_scaled.includes(f) ? "*" : "");
    const isBase = (f) => R.baseline.includes(f);
    const colour = (r) => {  // diverging: blue (-1) .. panel (0) .. orange (+1)
      const t = Math.min(1, Math.abs(r)), base = [21, 28, 37], end = r < 0 ? [70, 120, 220] : [230, 120, 60];
      return `rgb(${base.map((b, i) => Math.round(b + (end[i] - b) * t)).join(",")})`;
    };
    document.getElementById("corr-note").textContent =
      `${fmt(R.cdms)} CDMs from ${fmt(R.events)} training events (test data not used)`;
    const F = R.fields;
    document.getElementById("heat").innerHTML =
      "<thead><tr><th></th>" + F.map((f) => `<th class="col"><span class="${isBase(f) ? "base" : ""}">${label(f)}</span></th>`).join("") +
      "</tr></thead><tbody>" + F.map((f, i) => `<tr><th class="row"><span class="${isBase(f) ? "base" : ""}">${label(f)}</span></th>` +
        R.matrix[i].map((v, j) => `<td class="c" style="background:${colour(v)}" title="${esc(F[i])} × ${esc(F[j])}: r = ${v.toFixed(3)}">` +
          `${i === j ? "" : v.toFixed(1).replace("0.", ".").replace("-.", "−.")}</td>`).join("") + "</tr>").join("") + "</tbody>";
    document.getElementById("fs-table").innerHTML =
      "<thead><tr><th>LSTM inputs</th><th class='num'>Macro-F1</th><th class='num'>Sens.</th><th class='num'>Spec.</th></tr></thead><tbody>" +
      R.comparison.map((r) => `<tr><td>${esc(r.name)}</td><td class="num">${r.official.macro_f1.toFixed(3)}</td>` +
        `<td class="num">${r.official.sens_high.toFixed(3)}</td><td class="num">${r.official.spec_high.toFixed(3)}</td></tr>`).join("") +
      "</tbody><caption class='small' style='caption-side:bottom;text-align:left;padding-top:6px'>Official ESA test file. " +
      "Sens./Spec. = High-or-Critical alerts caught / lower alerts left alone.</caption>";
  })();

  document.getElementById("footer").textContent =
    "Globe: real objects, positions from CelesTrak's latest orbital elements propagated with SGP4. CelesTrak's " +
    "miss distance was predicted from an earlier element set; public orbit data drifts by about a kilometre per " +
    "day, and satellites such as Starlink manoeuvre, so the two numbers differ. That gap is why OCAS re-screens " +
    "every new update. The ESA alerts are anonymised, so they appear in the engine charts, not on the globe.";

  select(0);
})();

// OCAS World View: every tracked object at once + the host satellite's trajectory.
// Orbits are propagated with Keplerian motion plus J2 drift (visual accuracy, not conjunction-grade).
(() => {
"use strict";
const D = Math.PI / 180, T = 2 * Math.PI, RE = 6378.137, MU = 398600.4418, J2 = 1.08263e-3, BUBBLE = 100;
const HOST = { name: "OCAS-1", alt: 550, inc: 53, raan: 40, argp: 0, M: 0 };   // edit to match the real mission orbit
const CL = [["Active satellites", "#4fe3f0", 1.3], ["Dead satellites", "#b58cff", 2.4], ["Rocket bodies", "#ffc14d", 1.8], ["Debris", "#ff6b4a", 1.3]];
const $ = (id) => document.getElementById(id);
const nOf = (a) => 86400 / (T * Math.sqrt(a * a * a / MU));      // revolutions per day

function demo() {  // clearly-labelled synthetic catalog, used until catalog.js exists
  let s = 7; const R = () => (s = (s * 16807) % 2147483647) / 2147483647, rows = [], now = Date.now();
  const add = (c, k, alt, inc, sp) => { for (let i = 0; i < k; i++) rows.push([c, inc + (R() - .5) * 1.2, R() * 360, R() * .004, R() * 360, R() * 360, nOf(RE + alt + (R() - .5) * sp), now]); };
  add(0,1800,550,53,30); add(0,900,570,70,30); add(0,700,540,97.6,30); add(0,500,1200,87.9,60); add(0,300,800,98.6,150); add(0,60,20200,55,400); add(0,40,35786,.1,300);
  add(1,300,780,98.5,200); add(1,250,850,74,150); add(1,200,1450,82,100); add(1,150,550,53,200);
  add(2,250,800,98,200); add(2,200,950,82,300); add(2,150,600,65,300);
  add(3,1800,790,98.6,120); add(3,1500,850,74,100); add(3,900,1450,82,80); add(3,700,550,53,150); add(3,600,720,99,100);
  return rows;
}
const live = !!(window.OCAS_CATALOG && window.OCAS_CATALOG.objs && window.OCAS_CATALOG.objs.length);
const rows = live ? window.OCAS_CATALOG.objs : demo();
$("src").textContent = live ? `Live catalog · ${window.OCAS_CATALOG.source} · built ${window.OCAS_CATALOG.generated}` : "DEMO DATA (synthetic). Run fetch_catalog.py to load the real catalog.";
if (!live) $("src").className = "demo";

function build(rs) {
  const N = rs.length, F = () => new Float64Array(N), S = { N, cls: new Uint8Array(N), ci: F(), si: F(), ra: F(), e: F(), w: F(), M: F(), n: F(), a: F(), ep: F(), dR: F(), dW: F() };
  rs.forEach((r, i) => {
    const [c, inc, ra, e, w, M, nrd, ep] = r, n = nrd * T / 86400, a = Math.cbrt(MU / (n * n)), p = a * (1 - e * e), k = 1.5 * J2 * (RE / p) ** 2 * n, ci = Math.cos(inc * D);
    S.cls[i] = c; S.ci[i] = ci; S.si[i] = Math.sin(inc * D); S.ra[i] = ra * D; S.e[i] = e; S.w[i] = w * D; S.M[i] = M * D; S.n[i] = n; S.a[i] = a; S.ep[i] = ep;
    S.dR[i] = -k * ci; S.dW[i] = 0.5 * k * (5 * ci * ci - 1);
  });
  return S;
}
function pos(S, i, t, X, o) {
  const dt = (t - S.ep[i]) / 1e3, e = S.e[i], M = (S.M[i] + S.n[i] * dt) % T;
  let E = M + e * Math.sin(M);
  for (let k = 0; k < 3; k++) E -= (E - e * Math.sin(E) - M) / (1 - e * Math.cos(E));
  const a = S.a[i], xo = a * (Math.cos(E) - e), yo = a * Math.sqrt(1 - e * e) * Math.sin(E), w = S.w[i] + S.dW[i] * dt, O = S.ra[i] + S.dR[i] * dt;
  const cw = Math.cos(w), sw = Math.sin(w), xp = xo * cw - yo * sw, yp = xo * sw + yo * cw, cO = Math.cos(O), sO = Math.sin(O);
  X[o] = xp * cO - yp * S.ci[i] * sO; X[o + 1] = xp * sO + yp * S.ci[i] * cO; X[o + 2] = yp * S.si[i];
}
const S = build(rows), X = new Float32Array(S.N * 3);
const H = build([[0, HOST.inc, HOST.raan, 0.0001, HOST.argp, HOST.M, nOf(RE + HOST.alt), Date.now()]]), XH = new Float64Array(3);
const byCls = [0, 1, 2, 3].map((c) => Uint32Array.from({ length: S.N }, (_, i) => i).filter((i) => S.cls[i] === c));
const show = [true, true, true, true];
const gmst = (t) => ((280.46061837 + 360.98564736629 * (t / 864e5 + 2440587.5 - 2451545)) % 360) * D;

// ---------------- HUD
$("legend").innerHTML = CL.map((c, k) => `<label class="chip"><input type="checkbox" data-k="${k}" checked><i style="--c:${c[1]}"></i>${c[0]}<b>${byCls[k].length.toLocaleString("en-US")}</b></label>`).join("") +
  `<div class="kv"><span>Total on screen</span><b>${S.N.toLocaleString("en-US")}</b></div>`;
$("legend").addEventListener("change", (ev) => { if (ev.target.dataset.k) show[+ev.target.dataset.k] = ev.target.checked; });
let speed = 60;
$("speeds").innerHTML = [["❚❚", 0], ["1×", 1], ["60×", 60], ["600×", 600], ["3000×", 3000]].map(([l, v]) => `<button type="button" data-v="${v}" aria-pressed="${v === speed}">${l}</button>`).join("");
$("speeds").addEventListener("click", (ev) => { const b = ev.target.closest("button"); if (!b) return; speed = +b.dataset.v; [...$("speeds").children].forEach((x) => x.setAttribute("aria-pressed", x === b)); });
$("per").textContent = (1440 / nOf(RE + HOST.alt)).toFixed(1) + " min";

// ---------------- view
const cv = $("c"), g = cv.getContext("2d"), view = { yaw: 0.6, pitch: 0.4, zoom: 1.2 };
let W = 0, Hh = 0, dpr = 1, simT = Date.now(), spin = true, near = { n: 0, d: Infinity, c: 0 }, threats = [];
function resize() { dpr = Math.min(2, devicePixelRatio || 1); W = cv.width = innerWidth * dpr; Hh = cv.height = innerHeight * dpr; }
addEventListener("resize", resize); resize();
let drag = null;
cv.addEventListener("pointerdown", (e) => { drag = [e.clientX, e.clientY]; spin = false; $("follow").checked = false; cv.setPointerCapture(e.pointerId); });
cv.addEventListener("pointermove", (e) => { if (!drag) return; view.yaw += (e.clientX - drag[0]) * .008; view.pitch = Math.max(-1.5, Math.min(1.5, view.pitch + (e.clientY - drag[1]) * .008)); drag = [e.clientX, e.clientY]; });
cv.addEventListener("pointerup", () => (drag = null));
cv.addEventListener("wheel", (e) => { e.preventDefault(); view.zoom = Math.max(.12, Math.min(8, view.zoom * (e.deltaY < 0 ? 1.12 : 1 / 1.12))); }, { passive: false });

let cy, sy, cp, sp, K, px, py, pd, CX;
function proj(x, y, z) {
  const x1 = cy * x - sy * y, y1 = sy * x + cy * y;
  pd = cp * y1 - sp * z; px = CX + x1 * K; py = Hh / 2 - (sp * y1 + cp * z) * K;
}
const behind = () => pd > 0 && Math.hypot(px - CX, py - Hh / 2) < RE * K;
const toEci = (lat, lon, th, r) => { const c = Math.cos(lat), l = lon + th; return [r * c * Math.cos(l), r * c * Math.sin(l), r * Math.sin(lat)]; };

function frame(now) {
  const dtw = Math.min(.1, (now - (frame.last || now)) / 1e3); frame.last = now;
  simT += dtw * speed * 1e3;
  if (now - (frame.phys || 0) > 50) { for (let i = 0; i < S.N; i++) pos(S, i, simT, X, i * 3); pos(H, 0, simT, XH, 0); frame.phys = now; }
  if (now - (frame.thr || 0) > 400) { scan(); frame.thr = now; }
  if ($("follow").checked) {  // swing the camera to look straight at OCAS-1
    const r = Math.hypot(XH[0], XH[1]), ty = Math.atan2(XH[0], XH[1]) + Math.PI, tp = Math.atan2(XH[2], r);
    let dy = ((ty - view.yaw + Math.PI * 3) % T) - Math.PI; view.yaw += dy * .08; view.pitch += (tp - view.pitch) * .08;
  } else if (spin) view.yaw += .0014;
  draw(); requestAnimationFrame(frame);
}
function scan() {
  threats = []; let best = Infinity, bc = 0;
  for (let i = 0; i < S.N; i++) {
    const dx = X[i * 3] - XH[0], dy = X[i * 3 + 1] - XH[1], dz = X[i * 3 + 2] - XH[2], d = Math.sqrt(dx * dx + dy * dy + dz * dz);
    if (d < best) { best = d; bc = S.cls[i]; }
    if (d < BUBBLE && threats.length < 80) threats.push(i);
  }
  near = { n: threats.length, d: best, c: bc };
  $("near").textContent = near.n + " object" + (near.n === 1 ? "" : "s"); $("near").className = near.n ? "hot" : "";
  $("nn").textContent = `${best < 10 ? best.toFixed(2) : Math.round(best).toLocaleString("en-US")} km · ${CL[bc][0].toLowerCase()}`;
  $("alt").textContent = (Math.hypot(XH[0], XH[1], XH[2]) - RE).toFixed(0) + " km";
  $("utc").textContent = new Date(simT).toISOString().slice(0, 19).replace("T", " ");
}
function draw() {
  cy = Math.cos(view.yaw); sy = Math.sin(view.yaw); cp = Math.cos(view.pitch); sp = Math.sin(view.pitch);
  CX = innerWidth > 760 ? (W - 306 * dpr) / 2 : W / 2;
  K = (Math.min(CX * 2, Hh) / 2 - 30 * dpr) / (RE + 2600) * view.zoom;
  g.clearRect(0, 0, W, Hh);
  const th = gmst(simT), R = RE * K, cx = CX, cyy = Hh / 2;
  let gr = g.createRadialGradient(cx, cyy, R * .96, cx, cyy, R * 1.2);
  gr.addColorStop(0, "rgba(79,200,255,.38)"); gr.addColorStop(1, "rgba(79,200,255,0)");
  g.fillStyle = gr; g.beginPath(); g.arc(cx, cyy, R * 1.2, 0, T); g.fill();
  gr = g.createRadialGradient(cx - R * .35, cyy - R * .35, R * .1, cx, cyy, R); gr.addColorStop(0, "#1d4a78"); gr.addColorStop(1, "#08182b");
  g.fillStyle = gr; g.beginPath(); g.arc(cx, cyy, R, 0, T); g.fill();
  // graticule + coastlines (front side only)
  g.lineWidth = dpr; g.strokeStyle = "rgba(150,200,240,.13)"; g.beginPath();
  for (let lat = -60; lat <= 60; lat += 30) ring(lat, th, true);
  for (let lon = 0; lon < 360; lon += 30) ring(lon, th, false);
  g.stroke();
  g.strokeStyle = "rgba(120,210,255,.8)"; g.lineWidth = 1.2 * dpr; g.beginPath();
  for (const r of window.OCAS_COAST || []) {
    let pen = false;
    for (let i = 0; i < r.length; i += 2) {
      const v = toEci(r[i + 1] * D, r[i] * D, th, RE); proj(v[0], v[1], v[2]);
      if (pd > 0) { pen = false; continue; }
      pen ? g.lineTo(px, py) : g.moveTo(px, py); pen = true;
    }
  }
  g.stroke();
  // objects
  for (let c = 0; c < 4; c++) {
    if (!show[c]) continue;
    g.fillStyle = CL[c][1]; const s = CL[c][2] * dpr, idx = byCls[c];
    for (let j = 0; j < idx.length; j++) { const i = idx[j]; proj(X[i * 3], X[i * 3 + 1], X[i * 3 + 2]); if (!behind()) g.fillRect(px - s / 2, py - s / 2, s, s); }
  }
  hostLayer(th);
}
function ring(v, th, isLat) {
  let pen = false;
  for (let u = 0; u <= 360; u += 4) {
    const w = isLat ? toEci(v * D, u * D, th, RE) : toEci((u - 180) / 2 * D, v * D, th, RE); proj(w[0], w[1], w[2]);
    if (pd > 0) { pen = false; continue; } pen ? g.lineTo(px, py) : g.moveTo(px, py); pen = true;
  }
}
function hostLayer(th) {
  const T0 = simT, P = [], tmp = new Float64Array(3), step = 60e3;
  for (let m = -50; m <= 98; m++) { pos(H, 0, T0 + m * step, tmp, 0); P.push([m, tmp[0], tmp[1], tmp[2]]); }
  const seg = (from, to, col, dash, width, ground) => {
    g.strokeStyle = col; g.lineWidth = width * dpr; g.setLineDash(dash.map((d) => d * dpr)); g.beginPath(); let pen = false;
    for (const [m, x, y, z] of P) {
      if (m < from || m > to) continue;
      let X3 = [x, y, z];
      if (ground) { const a = gmst(T0 + m * step), c = Math.cos(a), s = Math.sin(a), ex = c * x + s * y, ey = -s * x + c * y, r = Math.hypot(ex, ey, z), q = toEcefToEci(ex / r, ey / r, z / r, th); X3 = q; }
      proj(X3[0], X3[1], X3[2]); if (!ground && behind()) { pen = false; continue; } if (ground && pd > 0) { pen = false; continue; }
      pen ? g.lineTo(px, py) : g.moveTo(px, py); pen = true;
    }
    g.stroke(); g.setLineDash([]);
  };
  seg(-50, 0, "rgba(255,255,255,.35)", [], 1.6);
  seg(0, 98, "#ffffff", [7, 6], 1.8);
  seg(0, 98, "rgba(79,227,240,.9)", [2, 5], 2, true);
  proj(XH[0], XH[1], XH[2]); const hx = px, hy = py, hb = behind();
  for (const i of threats) { proj(X[i * 3], X[i * 3 + 1], X[i * 3 + 2]); g.strokeStyle = "rgba(255,107,125,.8)"; g.lineWidth = dpr; g.beginPath(); g.moveTo(hx, hy); g.lineTo(px, py); g.stroke();
    g.beginPath(); g.arc(px, py, 5 * dpr, 0, T); g.stroke(); }
  const pulse = 1 + .06 * Math.sin(performance.now() / 280), rb = BUBBLE * K * pulse;
  g.strokeStyle = near.n ? "rgba(255,107,125,.9)" : "rgba(94,224,160,.75)"; g.lineWidth = 1.4 * dpr; g.setLineDash([4 * dpr, 4 * dpr]); g.beginPath(); g.arc(hx, hy, Math.max(rb, 9 * dpr), 0, T); g.stroke(); g.setLineDash([]);
  g.globalAlpha = hb ? .35 : 1; g.shadowColor = "#fff"; g.shadowBlur = 18 * dpr; g.fillStyle = "#fff"; g.beginPath(); g.arc(hx, hy, 4.5 * dpr, 0, T); g.fill(); g.shadowBlur = 0;
  g.font = `${600} ${13 * dpr}px "Segoe UI",sans-serif`; g.fillStyle = "#fff"; g.fillText(`${HOST.name} · ${HOST.alt} km`, hx + 12 * dpr, hy - 10 * dpr); g.globalAlpha = 1;
}
const toEcefToEci = (ux, uy, uz, th) => { const c = Math.cos(th), s = Math.sin(th); return [RE * (c * ux - s * uy), RE * (s * ux + c * uy), RE * uz]; };
requestAnimationFrame(frame);
})();

// OCAS World View v2: live catalog + an auto-staged collision scenario.
// A debris threat is put on a crossing orbit with OCAS-1, the engine warns, burns (along-track +T, as the
// LEO regime does in the README) and the host trajectory changes. window.OCAS_DEBRIEF adds the cinematic layer.
(() => {
"use strict";
const FX = !!window.OCAS_DEBRIEF, FF = +new URLSearchParams(location.search).get("ff") || 1;
const D = Math.PI / 180, T = 2 * Math.PI, RE = 6378.137, MU = 398600.4418, J2 = 1.08263e-3, BUBBLE = 100, TARGET = 2;
const HOST = { name: "OCAS-1", alt: 550, inc: 53, raan: 40, argp: 0, M: 0 };   // edit to match the real mission orbit
const CL = [["Active satellites", "#4fe3f0", 1.3], ["Dead satellites", "#b58cff", 2.4], ["Rocket bodies", "#ffc14d", 1.8], ["Debris", "#ff6b4a", 1.3]];
if (FX) { CL[0][1] = "#6fe8ff"; CL[1][1] = "#ffd36b"; CL[2][1] = "#ff9f4a"; CL[3][1] = "#ff5a4a"; }
const $ = (id) => document.getElementById(id);
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2], cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const unit = (a) => { const n = Math.hypot(a[0], a[1], a[2]); return [a[0] / n, a[1] / n, a[2] / n]; };
const nOf = (a) => 86400 / (T * Math.sqrt(a * a * a / MU));
const fmt = (ms) => { const s = Math.max(0, Math.round(ms / 1e3)); return [s / 3600 | 0, (s / 60 | 0) % 60, s % 60].map((v) => String(v).padStart(2, "0")).join(":"); };
const ease = (x) => x * x * (3 - 2 * x), clamp = (x, a, b) => Math.max(a, Math.min(b, x));

function demo() {
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
const H = build([[0, HOST.inc, HOST.raan, 0.0001, HOST.argp, HOST.M, nOf(RE + HOST.alt), Date.now()]]), XH = new Float64Array(3), XT = new Float64Array(3);
const byCls = [0, 1, 2, 3].map((c) => Uint32Array.from({ length: S.N }, (_, i) => i).filter((i) => S.cls[i] === c));
const show = [true, true, true, true];
const gmst = (t) => ((280.46061837 + 360.98564736629 * (t / 864e5 + 2440587.5 - 2451545)) % 360) * D;

// ---------------- collision scenario
let sc = null;
const tmpA = new Float64Array(3), tmpB = new Float64Array(3);
function closest(hostFn, TH, tc) {   // closest approach near tc: coarse 2 s scan, then 5 ms refinement
  const gap = (t) => { hostFn(t, tmpA); pos(TH, 0, t, tmpB, 0); return Math.hypot(tmpA[0] - tmpB[0], tmpA[1] - tmpB[1], tmpA[2] - tmpB[2]); };
  let bt = tc, bd = 1e9;
  for (let s = -300; s <= 300; s += 2) { const d = gap(tc + s * 1e3); if (d < bd) { bd = d; bt = tc + s * 1e3; } }
  for (let s = -2000; s <= 2000; s += 5) { const d = gap(bt + s); if (d < bd) { bd = d; bt += s; bd = d; } }
  hostFn(bt, tmpA); pos(TH, 0, bt, tmpB, 0);
  return { d: bd, t: bt, v: [tmpB[0] - tmpA[0], tmpB[1] - tmpA[1], tmpB[2] - tmpA[2]] };
}
function burnOrbit(tb, dv) {          // along-track impulse on the (near-circular) host orbit at time tb
  const dt = (tb - H.ep[0]) / 1e3, a = H.a[0], M = H.M[0] + H.n[0] * dt, w = H.w[0] + H.dW[0] * dt, O = H.ra[0] + H.dR[0] * dt;
  const v = Math.sqrt(MU / a), v2 = v + dv / 1e3, a2 = 1 / (2 / a - v2 * v2 / MU), e2 = a * v2 * v2 / MU - 1;
  return build([[0, HOST.inc, (O / D) % 360, e2, ((w + M) / D) % 360, 0, nOf(a2), tb]]);   // burn point becomes perigee
}
function newScenario(t0) {
  const s = { t0, tw: t0 + 480e3, tb: t0 + 1080e3, tc: t0 + 6000e3, name: "DEB-" + (10000 + Math.floor(Math.random() * 89999)), burned: false, ph: 0 };
  pos(H, 0, s.tc, tmpA, 0); const r = Math.hypot(tmpA[0], tmpA[1], tmpA[2]), rh = [tmpA[0] / r, tmpA[1] / r, tmpA[2] / r];
  const O = H.ra[0] + H.dR[0] * (s.tc - H.ep[0]) / 1e3, hh = [H.si[0] * Math.sin(O), -H.si[0] * Math.cos(O), H.ci[0]], th = cross(hh, rh);
  const beta = (55 + Math.random() * 50) * D, tp = th.map((x, i) => Math.cos(beta) * x + Math.sin(beta) * hh[i]);
  const h2 = unit(cross(rh, tp)), Om = Math.atan2(h2[0], -h2[1]), N = [Math.cos(Om), Math.sin(Om), 0], u = Math.atan2(dot(rh, cross(h2, N)), dot(rh, N));
  s.TH = build([[3, Math.acos(h2[2]) / D, Om / D, 0, 0, u / D, nOf(r + 0.6), s.tc]]);   // crosses the host orbit, 0.6 km above it
  const nom = (t, o) => pos(H, 0, t, o, 0);
  s.c0 = closest(nom, s.TH, s.tc); s.dv = 1;
  for (let dv = 0.02; dv <= 1.0001; dv += 0.02) { const B = burnOrbit(s.tb, dv); if (closest((t, o) => pos(B, 0, t, o, 0), s.TH, s.tc).d >= TARGET) { s.dv = dv; break; } }
  s.H2 = burnOrbit(s.tb, s.dv); s.c1 = closest((t, o) => pos(s.H2, 0, t, o, 0), s.TH, s.tc);
  // encounter plane (perpendicular to the relative velocity at TCA): miss vectors as 2-D points
  pos(H, 0, s.tc - 500, tmpA, 0); pos(s.TH, 0, s.tc - 500, tmpB, 0); const a0 = [...tmpA], b0 = [...tmpB];
  pos(H, 0, s.tc + 500, tmpA, 0); pos(s.TH, 0, s.tc + 500, tmpB, 0);
  const uu = unit([(tmpB[0] - b0[0]) - (tmpA[0] - a0[0]), (tmpB[1] - b0[1]) - (tmpA[1] - a0[1]), (tmpB[2] - b0[2]) - (tmpA[2] - a0[2])]);
  let e1 = rh.map((x, i) => x - dot(rh, uu) * uu[i]); e1 = unit(e1); const e2 = cross(uu, e1);
  s.p0 = [dot(s.c0.v, e1), dot(s.c0.v, e2)]; s.p1 = [dot(s.c1.v, e1), dot(s.c1.v, e2)];
  return s;
}
const hostPos = (t, o) => (sc.burned && t >= sc.tb ? pos(sc.H2, 0, t, o, 0) : pos(H, 0, t, o, 0));
const phaseOf = (t) => (t < sc.tw ? 0 : t < sc.tb ? 1 : t < sc.tb + 90e3 ? 2 : t < sc.tc - 300e3 ? 3 : t < sc.tc + 30e3 ? 4 : 5);
const ZOOM = [1.2, 14, 90, 2.5, 130, 1.4], AUTO_SPEED = [60, 25, 8, 600, 20, 30];

// ---------------- HUD
$("legend").innerHTML = CL.map((c, k) => `<label class="chip"><input type="checkbox" data-k="${k}" checked><i style="--c:${c[1]}"></i>${c[0]}<b>${byCls[k].length.toLocaleString("en-US")}</b></label>`).join("") +
  `<div class="kv"><span>Total on screen</span><b>${S.N.toLocaleString("en-US")}</b></div>`;
$("legend").addEventListener("change", (ev) => { if (ev.target.dataset.k) show[+ev.target.dataset.k] = ev.target.checked; });
let speed = -1;
$("speeds").innerHTML = [["AUTO", -1], ["❚❚", 0], ["1×", 1], ["60×", 60], ["600×", 600]].map(([l, v]) => `<button type="button" data-v="${v}" aria-pressed="${v === speed}">${l}</button>`).join("");
$("speeds").addEventListener("click", (ev) => { const b = ev.target.closest("button"); if (!b) return; speed = +b.dataset.v; [...$("speeds").children].forEach((x) => x.setAttribute("aria-pressed", x === b)); });
$("per").textContent = (1440 / nOf(RE + HOST.alt)).toFixed(1) + " min";
const alertEl = $("alert");
function setAlert(key, txt, cls) {
  alertEl.className = "alert " + cls;
  if (alertEl.dataset.k === key) { if (!alertEl._typing) alertEl.textContent = txt; return; }   // same message: just refresh the countdown
  alertEl.dataset.k = key;
  if (!FX) { alertEl.textContent = txt; return; }
  let i = 0; clearInterval(alertEl._i); alertEl._typing = true;
  alertEl._i = setInterval(() => { alertEl.textContent = txt.slice(0, ++i) + (i < txt.length ? "▌" : ""); if (i >= txt.length) { clearInterval(alertEl._i); alertEl._typing = false; } }, 18);
}

// ---------------- view
const cv = $("c"), g = cv.getContext("2d"), enc = FX ? $("enc") : null, view = { yaw: 0.6, pitch: 0.4, zoom: FX ? 0.14 : 1.2 };
let W = 0, Hh = 0, dpr = 1, simT = Date.now(), spin = true, near = { n: 0, d: Infinity, c: 0 }, threats = [], flash = 0;
function resize() { dpr = Math.min(2, devicePixelRatio || 1); W = cv.width = innerWidth * dpr; Hh = cv.height = innerHeight * dpr; }
addEventListener("resize", resize); resize();
let drag = null;
cv.addEventListener("pointerdown", (e) => { drag = [e.clientX, e.clientY]; spin = false; $("follow").checked = false; $("auto").checked = false; cv.setPointerCapture(e.pointerId); });
cv.addEventListener("pointermove", (e) => { if (!drag) return; view.yaw += (e.clientX - drag[0]) * .008 / Math.sqrt(view.zoom); view.pitch = clamp(view.pitch + (e.clientY - drag[1]) * .008 / Math.sqrt(view.zoom), -1.5, 1.5); drag = [e.clientX, e.clientY]; });
cv.addEventListener("pointerup", () => (drag = null));
cv.addEventListener("wheel", (e) => { e.preventDefault(); $("auto").checked = false; view.zoom = clamp(view.zoom * (e.deltaY < 0 ? 1.15 : 1 / 1.15), .1, 400); }, { passive: false });

let cy, sy, cp, sp, K, px, py, pd, CX;
function proj(x, y, z) { const x1 = cy * x - sy * y, y1 = sy * x + cy * y; pd = cp * y1 - sp * z; px = CX + x1 * K; py = Hh / 2 - (sp * y1 + cp * z) * K; }
const behind = () => pd > 0 && Math.hypot(px - CX, py - Hh / 2) < RE * K;
const toEci = (lat, lon, th, r) => { const c = Math.cos(lat), l = lon + th; return [r * c * Math.cos(l), r * c * Math.sin(l), r * Math.sin(lat)]; };
const toEcefToEci = (ux, uy, uz, th) => { const c = Math.cos(th), s = Math.sin(th); return [RE * (c * ux - s * uy), RE * (s * ux + c * uy), RE * uz]; };
const stp = (t) => (Math.abs(t - simT) < 360e3 || Math.abs(t - sc.tc) < 360e3 ? 5e3 : 60e3);

sc = newScenario(simT);
function frame(now) {
  const dtw = Math.min(.1, (now - (frame.last || now)) / 1e3); frame.last = now;
  if (simT > sc.tc + 420e3) sc = newScenario(simT);
  const ph = phaseOf(simT); sc.burned = simT >= sc.tb;
  if (ph === 2 && sc.ph < 2) flash = now; sc.ph = ph;
  const spd = speed < 0 ? (ph === 4 && Math.abs(sc.tc - simT) < 20e3 ? 5 : AUTO_SPEED[ph]) * FF : speed;
  simT += dtw * spd * 1e3;
  if (now - (frame.phys || 0) > 50) { for (let i = 0; i < S.N; i++) pos(S, i, simT, X, i * 3); hostPos(simT, XH); pos(sc.TH, 0, simT, XT, 0); frame.phys = now; }
  if (now - (frame.thr || 0) > 300) { scan(ph); frame.thr = now; }
  if ($("auto").checked) view.zoom *= Math.pow(ZOOM[ph] / view.zoom, 1 - Math.exp(-dtw * (FX ? 1.6 : 2.2)));
  if ($("follow").checked) {
    const r = Math.hypot(XH[0], XH[1]), ty = Math.atan2(XH[0], XH[1]) + Math.PI, tp = Math.atan2(XH[2], r), k = 1 - Math.exp(-dtw * 9);
    view.yaw += (((ty - view.yaw + Math.PI * 3) % T) - Math.PI) * k; view.pitch += (tp - view.pitch) * k;
  } else if (spin) view.yaw += .0014;
  draw(now, ph); drawEnc(now); requestAnimationFrame(frame);
}
function scan(ph) {
  threats = []; let best = Infinity, bc = 0;
  for (let i = 0; i < S.N; i++) {
    const dx = X[i * 3] - XH[0], dy = X[i * 3 + 1] - XH[1], dz = X[i * 3 + 2] - XH[2], d = Math.sqrt(dx * dx + dy * dy + dz * dz);
    if (d < best) { best = d; bc = S.cls[i]; } if (d < BUBBLE && threats.length < 80) threats.push(i);
  }
  near = { n: threats.length, d: best, c: bc };
  $("near").textContent = near.n + " object" + (near.n === 1 ? "" : "s"); $("near").className = near.n ? "hot" : "";
  $("nn").textContent = `${best < 10 ? best.toFixed(2) : Math.round(best).toLocaleString("en-US")} km · ${CL[bc][0].toLowerCase()}`;
  $("alt").textContent = (Math.hypot(XH[0], XH[1], XH[2]) - RE).toFixed(0) + " km";
  $("utc").textContent = new Date(simT).toISOString().slice(0, 19).replace("T", " ");
  const miss = sc.burned ? sc.c1.d : sc.c0.d, tt = fmt(sc.tc - simT);
  $("st").textContent = ["SCANNING", "WARNING", "BURN", "NEW TRAJECTORY", "APPROACH", "CLEARED"][ph]; $("st").className = ph === 1 || ph === 4 && !sc.burned ? "hot" : ph ? "ok" : "";
  $("tca").textContent = simT < sc.tc ? "T-" + tt : "passed"; $("pm").textContent = ph ? miss.toFixed(2) + " km" : "—";
  $("dvv").textContent = sc.burned ? sc.dv.toFixed(3) + " m/s +T" : ph ? "planned " + sc.dv.toFixed(3) + " m/s" : "—";
  const id = sc.name;
  if (ph === 0) setAlert("a0", "ORBITAL SURVEILLANCE ACTIVE", "ok");
  else if (ph === 1) setAlert("a1", `CONJUNCTION WARNING · ${id} · TCA T-${tt} · MISS ${sc.c0.d.toFixed(2)} km`, "bad");
  else if (ph === 2) setAlert("a2", `EVASION BURN EXECUTED · Δv ${sc.dv.toFixed(3)} m/s ALONG-TRACK (+T)`, "warn");
  else if (ph < 5) setAlert("a3", `TRAJECTORY CHANGED · ${id} · TCA T-${tt} · MISS ${sc.c1.d.toFixed(2)} km`, "ok");
  else setAlert("a5", `THREAT CLEARED · ${id} PASSED AT ${sc.c1.d.toFixed(2)} km`, "ok");
}

function trace(fn, a, b, col, dash, w, ground, th) {
  g.strokeStyle = col; g.lineWidth = w * dpr; g.setLineDash(dash.map((d) => d * dpr)); g.beginPath(); let pen = false; const o = new Float64Array(3);
  for (let t = a; t <= b; t += stp(t)) {
    fn(t, o); let x = o[0], y = o[1], z = o[2];
    if (ground) { const q = gmst(t), c = Math.cos(q), s = Math.sin(q), ex = c * x + s * y, ey = -s * x + c * y, r = Math.hypot(ex, ey, z); [x, y, z] = toEcefToEci(ex / r, ey / r, z / r, th); }
    proj(x, y, z); if (pd > 0 && (ground || behind())) { pen = false; continue; }
    pen ? g.lineTo(px, py) : g.moveTo(px, py); pen = true;
  }
  g.stroke(); g.setLineDash([]);
}
function ring(v, th, isLat) {
  let pen = false;
  for (let u = 0; u <= 360; u += 4) {
    const w = isLat ? toEci(v * D, u * D, th, RE) : toEci((u - 180) / 2 * D, v * D, th, RE); proj(w[0], w[1], w[2]);
    if (pd > 0) { pen = false; continue; } pen ? g.lineTo(px, py) : g.moveTo(px, py); pen = true;
  }
}
function draw(now, ph) {
  cy = Math.cos(view.yaw); sy = Math.sin(view.yaw); cp = Math.cos(view.pitch); sp = Math.sin(view.pitch);
  CX = innerWidth > 760 ? (W - 306 * dpr) / 2 : W / 2; K = (Math.min(CX * 2, Hh) / 2 - 30 * dpr) / (RE + 2600) * view.zoom;
  g.clearRect(0, 0, W, Hh);
  const th = gmst(simT), R = RE * K, cx = CX, cyy = Hh / 2, z = view.zoom;
  if (z < 20) {
    let gr = g.createRadialGradient(cx, cyy, R * .96, cx, cyy, R * 1.2); gr.addColorStop(0, FX ? "rgba(255,170,70,.30)" : "rgba(79,200,255,.38)"); gr.addColorStop(1, "rgba(79,200,255,0)");
    g.fillStyle = gr; g.beginPath(); g.arc(cx, cyy, R * 1.2, 0, T); g.fill();
  }
  g.fillStyle = FX ? "#06121e" : "#0a1e36"; g.beginPath(); g.arc(cx, cyy, R, 0, T); g.fill();
  if (z < 40) { const gr = g.createRadialGradient(cx - R * .35, cyy - R * .35, R * .1, cx, cyy, R); gr.addColorStop(0, FX ? "#143a5a" : "#1d4a78"); gr.addColorStop(1, "#08182b"); g.fillStyle = gr; g.beginPath(); g.arc(cx, cyy, R, 0, T); g.fill(); }
  g.lineWidth = dpr; g.strokeStyle = "rgba(150,200,240,.13)"; g.beginPath();
  for (let lat = -60; lat <= 60; lat += 30) ring(lat, th, true);
  for (let lon = 0; lon < 360; lon += 30) ring(lon, th, false);
  g.stroke();
  g.globalAlpha = clamp(1.4 - z / 60, .25, 1); g.strokeStyle = FX ? "rgba(255,190,110,.85)" : "rgba(120,210,255,.8)"; g.lineWidth = 1.2 * dpr; g.beginPath();
  for (const r of window.OCAS_COAST || []) {
    let pen = false;
    for (let i = 0; i < r.length; i += 2) { const v = toEci(r[i + 1] * D, r[i] * D, th, RE); proj(v[0], v[1], v[2]); if (pd > 0) { pen = false; continue; } pen ? g.lineTo(px, py) : g.moveTo(px, py); pen = true; }
  }
  g.stroke(); g.globalAlpha = 1;
  if (z > 12) tacticalGrid(clamp((z - 12) / 40, 0, .9));
  for (let c = 0; c < 4; c++) {
    if (!show[c]) continue; g.fillStyle = CL[c][1]; const s = CL[c][2] * dpr * (z > 30 ? 2 : 1), idx = byCls[c];
    for (let j = 0; j < idx.length; j++) { const i = idx[j]; proj(X[i * 3], X[i * 3 + 1], X[i * 3 + 2]); if (!behind()) g.fillRect(px - s / 2, py - s / 2, s, s); }
  }
  scene(now, th, ph);
}
function tacticalGrid(a) {  // flat grid under the host for the close-up shots (spacing 1, 10 or 100 km)
  const target = 150 * dpr / K; let sp = 1; while (sp * 3 < target) sp *= 10; const px0 = sp * K;
  g.strokeStyle = FX ? `rgba(255,190,110,${.14 * a})` : `rgba(140,210,255,${.12 * a})`; g.lineWidth = dpr; g.beginPath();
  for (let x = CX % px0; x < W; x += px0) { g.moveTo(x, 0); g.lineTo(x, Hh); } for (let y = (Hh / 2) % px0; y < Hh; y += px0) { g.moveTo(0, y); g.lineTo(W, y); }
  g.stroke(); g.fillStyle = FX ? "#ffbe6e" : "#bfe8ff"; g.font = `${11 * dpr}px ui-monospace,Consolas,monospace`; g.globalAlpha = a;
  g.fillText(`GRID ${sp} km`, (FX ? 372 : 24) * dpr, Hh - 20 * dpr); g.globalAlpha = 1;
}
function label(txt, x, y, col, size) { g.font = `600 ${(size || 12) * dpr}px ui-monospace,Consolas,monospace`; g.fillStyle = col; g.fillText(txt, x, y); }
function scene(now, th, ph) {
  const burned = sc.burned, GREEN = FX ? "#9dff9a" : "#5ee0a0";
  trace(hostPos, simT - 50 * 6e4, simT, "rgba(255,255,255,.35)", [], 1.6);
  if (burned) trace((t, o) => pos(H, 0, t, o, 0), simT, simT + 98 * 6e4, "rgba(255,107,125,.55)", [3, 5], 1.2);
  trace(hostPos, simT, simT + 98 * 6e4, burned ? GREEN : "#ffffff", [7, 6], 1.9);
  trace(hostPos, simT, simT + 98 * 6e4, FX ? "rgba(255,190,110,.9)" : "rgba(79,227,240,.9)", [2, 5], 2, true, th);
  if (ph >= 1) trace((t, o) => pos(sc.TH, 0, t, o, 0), simT - 8 * 6e4, simT + 30 * 6e4, "rgba(255,90,90,.85)", [5, 5], 1.5);
  proj(XH[0], XH[1], XH[2]); const hx = px, hy = py, hb = behind();
  proj(XT[0], XT[1], XT[2]); const tx = px, ty = py, tb = behind(), gap = Math.hypot(XT[0] - XH[0], XT[1] - XH[1], XT[2] - XH[2]);
  const pulse = 1 + .06 * Math.sin(now / 280);
  if (ph >= 1) {  // conjunction point (TCA)
    pos(sc.TH, 0, sc.tc, tmpB, 0); proj(tmpB[0], tmpB[1], tmpB[2]); if (!behind() && simT < sc.tc) {
      const s = (9 + 3 * Math.sin(now / 160)) * dpr; g.strokeStyle = "#ff4d5e"; g.lineWidth = 2 * dpr; g.beginPath(); g.moveTo(px - s, py - s); g.lineTo(px + s, py + s); g.moveTo(px + s, py - s); g.lineTo(px - s, py + s); g.stroke();
      label("TCA T-" + fmt(sc.tc - simT), px + 14 * dpr, py - 12 * dpr, "#ff8b97", 12);
    }
    if (!tb) {
      const s = 8 * dpr; g.strokeStyle = "#ff4d5e"; g.lineWidth = 2 * dpr; g.beginPath(); g.moveTo(tx, ty - s); g.lineTo(tx + s, ty); g.lineTo(tx, ty + s); g.lineTo(tx - s, ty); g.closePath(); g.stroke();
      label(`${sc.name}`, tx + 14 * dpr, ty + 4 * dpr, "#ff8b97", 12);
      if (FX) { const b = 16 * dpr; g.lineWidth = 1.5 * dpr; g.beginPath(); for (const [sx, sy] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) { g.moveTo(tx + sx * b, ty + sy * (b - 7 * dpr)); g.lineTo(tx + sx * b, ty + sy * b); g.lineTo(tx + sx * (b - 7 * dpr), ty + sy * b); } g.stroke(); }
      if (gap < 1500) { g.strokeStyle = "rgba(255,107,125,.7)"; g.lineWidth = dpr; g.setLineDash([4 * dpr, 4 * dpr]); g.beginPath(); g.moveTo(hx, hy); g.lineTo(tx, ty); g.stroke(); g.setLineDash([]); label(`RANGE ${gap < 10 ? gap.toFixed(2) : Math.round(gap)} km`, (hx + tx) / 2 + 8 * dpr, (hy + ty) / 2 - 8 * dpr, "#ffd0d5", 12); }
    }
  }
  for (const i of threats) { proj(X[i * 3], X[i * 3 + 1], X[i * 3 + 2]); g.strokeStyle = "rgba(255,107,125,.8)"; g.lineWidth = dpr; g.beginPath(); g.moveTo(hx, hy); g.lineTo(px, py); g.stroke(); g.beginPath(); g.arc(px, py, 5 * dpr, 0, T); g.stroke(); }
  const rb = BUBBLE * K * pulse, alarm = near.n || (ph === 1 || ph === 4 && !burned);
  g.strokeStyle = alarm ? "rgba(255,107,125,.9)" : "rgba(94,224,160,.75)"; g.lineWidth = 1.4 * dpr; g.setLineDash([4 * dpr, 4 * dpr]); g.beginPath(); g.arc(hx, hy, clamp(rb, 9 * dpr, 4000), 0, T); g.stroke(); g.setLineDash([]);
  if (FX) for (let k = 0; k < 3; k++) { const f = ((now / 2400) + k / 3) % 1; g.strokeStyle = `rgba(255,190,110,${.5 * (1 - f)})`; g.lineWidth = dpr; g.beginPath(); g.arc(hx, hy, (14 + f * 70) * dpr, 0, T); g.stroke(); }
  const fa = (now - flash) / 1000;
  if (flash && fa < 3) { g.strokeStyle = `rgba(255,220,120,${1 - fa / 3})`; g.lineWidth = 3 * dpr; g.beginPath(); g.arc(hx, hy, (20 + fa * 90) * dpr, 0, T); g.stroke(); label(`BURN  Δv ${sc.dv.toFixed(3)} m/s  +T`, hx + 24 * dpr, hy + 34 * dpr, "#ffe08a", 13); }
  g.globalAlpha = hb ? .35 : 1; g.shadowColor = burned ? GREEN : "#fff"; g.shadowBlur = 18 * dpr; g.fillStyle = burned ? GREEN : "#fff"; g.beginPath(); g.arc(hx, hy, 4.5 * dpr, 0, T); g.fill(); g.shadowBlur = 0;
  label(`${HOST.name} · ${HOST.alt} km`, hx + 12 * dpr, hy - 10 * dpr, "#fff", 13); g.globalAlpha = 1;
  if (view.zoom > 12) { const km = [1, 2, 5, 10, 20, 50, 100, 200, 500].find((v) => v * K > 110 * dpr) || 500, w = km * K, y = Hh - 40 * dpr, x0 = CX - w / 2; g.strokeStyle = "#fff"; g.lineWidth = 2 * dpr; g.beginPath(); g.moveTo(x0, y - 5 * dpr); g.lineTo(x0, y); g.lineTo(x0 + w, y); g.lineTo(x0 + w, y - 5 * dpr); g.stroke(); label(km + " km", CX - 16 * dpr, y - 10 * dpr, "#fff", 12); }
}
function drawEnc(now) {   // encounter plane: where the debris misses us, before and after the burn
  if (!enc) return; const c = enc.getContext("2d"), s = 300, k = (s / 2 - 16) / 3.4; c.clearRect(0, 0, s, s); c.save(); c.translate(s / 2, s / 2);
  const amber = "#ffbe6e"; c.strokeStyle = "rgba(255,190,110,.28)"; c.lineWidth = 1; c.font = "10px ui-monospace,Consolas,monospace"; c.fillStyle = "rgba(255,190,110,.7)";
  for (const r of [1, 2, 3]) { c.beginPath(); c.arc(0, 0, r * k, 0, T); c.stroke(); c.fillText(r + " km", r * k * .71 + 2, -r * k * .71 - 2); }
  c.beginPath(); c.moveTo(-s / 2, 0); c.lineTo(s / 2, 0); c.moveTo(0, -s / 2); c.lineTo(0, s / 2); c.stroke();
  c.strokeStyle = "rgba(255,90,90,.8)"; c.setLineDash([5, 4]); c.beginPath(); c.arc(0, 0, 1 * k, 0, T); c.stroke(); c.setLineDash([]);
  c.strokeStyle = "#fff"; c.lineWidth = 2; c.beginPath(); c.moveTo(-7, 0); c.lineTo(7, 0); c.moveTo(0, -7); c.lineTo(0, 7); c.stroke();
  const ph = sc.ph;
  if (ph >= 1) {
    const q = ease(clamp((simT - sc.tb) / 150e3, 0, 1)), p = [sc.p0[0] + (sc.p1[0] - sc.p0[0]) * q, sc.p0[1] + (sc.p1[1] - sc.p0[1]) * q], x = p[0] * k, y = -p[1] * k;
    if (q > 0) { c.strokeStyle = "rgba(255,90,90,.6)"; c.setLineDash([3, 4]); c.beginPath(); c.arc(sc.p0[0] * k, -sc.p0[1] * k, 5, 0, T); c.stroke(); c.setLineDash([]); c.beginPath(); c.moveTo(sc.p0[0] * k, -sc.p0[1] * k); c.lineTo(x, y); c.stroke(); }
    const col = q > .98 ? "#9dff9a" : "#ff5a4a"; c.fillStyle = col; c.shadowColor = col; c.shadowBlur = 12; c.beginPath(); c.arc(x, y, 5 + Math.sin(now / 200), 0, T); c.fill(); c.shadowBlur = 0;
    c.fillStyle = col; c.font = "600 12px ui-monospace,Consolas,monospace"; c.fillText(Math.hypot(p[0], p[1]).toFixed(2) + " km", x + 10, y - 8);
  } else { c.fillStyle = "rgba(255,190,110,.7)"; c.fillText("NO CONJUNCTION", -46, 4); }
  c.restore(); $("encn").textContent = ph >= 1 ? `${sc.name} · keep-out 1 km · target ≥ ${TARGET} km` : "scanning";
}
window.__ocas = { toTCA: (s) => { simT = sc.tc - s * 1e3; } };   // test hook: jump to s seconds before closest approach
requestAnimationFrame(frame);
})();

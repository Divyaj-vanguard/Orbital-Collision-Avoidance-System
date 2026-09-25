/**
 * ============================================================================
 * OCAS: Autonomous Orbital Collision Avoidance System
 * 3D WebGL Orbit Visualizer (Three.js with Canvas Fallback)
 * Team ID: DSCPP-III-2026-T018 | Graphic Era University
 * ============================================================================
 */

class OrbitVisualizer3D {
  constructor(containerId) {
    this.container = document.getElementById(containerId);
    this.isThreeAvailable = typeof THREE !== 'undefined';
    this.satState = { r: [6786, 0, 120], v: [0, 7.66, 0.05] };
    this.debrisList = [];
    this.activeBurn = null;
    this.viewMode = 'orbit'; // 'orbit', 'follow', 'top'

    if (this.isThreeAvailable) {
      this.initThreeJS();
    } else {
      console.warn('[VISUALIZER] Three.js not found in global scope; falling back to high-res 2D Canvas engine.');
      this.initCanvasFallback();
    }
  }

  /* ==========================================================================
   * Three.js Full 3D WebGL Implementation
   * ========================================================================== */
  initThreeJS() {
    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(0x050811);

    const width = this.container.clientWidth || 800;
    const height = this.container.clientHeight || 500;

    this.camera = new THREE.PerspectiveCamera(45, width / height, 0.1, 50000);
    this.camera.position.set(0, -18000, 10000);

    this.renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    this.renderer.setSize(width, height);
    this.renderer.setPixelRatio(window.devicePixelRatio || 1);
    this.container.appendChild(this.renderer.domElement);

    if (typeof THREE.OrbitControls !== 'undefined') {
      this.controls = new THREE.OrbitControls(this.camera, this.renderer.domElement);
      this.controls.enableDamping = true;
      this.controls.dampingFactor = 0.05;
      this.controls.maxDistance = 40000;
      this.controls.minDistance = 7000;
    }

    // Lighting
    const ambientLight = new THREE.AmbientLight(0x223355, 1.2);
    this.scene.add(ambientLight);

    const sunLight = new THREE.DirectionalLight(0xffffff, 1.5);
    sunLight.position.set(15000, 10000, 8000);
    this.scene.add(sunLight);

    // 1. Earth Sphere (Radius ~6378 km scaled 1:1)
    const earthRadius = 6378;
    const earthGeo = new THREE.SphereGeometry(earthRadius, 48, 48);
    const earthMat = new THREE.MeshPhongMaterial({
      color: 0x1a365d,
      emissive: 0x07111e,
      specular: 0x2b6cb0,
      shininess: 15,
      wireframe: false
    });
    this.earth = new THREE.Mesh(earthGeo, earthMat);
    this.scene.add(this.earth);

    // Earth Equatorial & Meridian Grids
    const gridHelper = new THREE.PolarGridHelper(earthRadius * 1.02, 16, 8, 32, 0x00d4ff, 0x1e3a8a);
    gridHelper.rotation.x = Math.PI / 2;
    this.scene.add(gridHelper);

    // Atmosphere Glow
    const atmosGeo = new THREE.SphereGeometry(earthRadius * 1.03, 32, 32);
    const atmosMat = new THREE.MeshBasicMaterial({
      color: 0x00d4ff,
      transparent: true,
      opacity: 0.12,
      side: THREE.BackSide
    });
    this.scene.add(new THREE.Mesh(atmosGeo, atmosMat));

    // Starfield Background
    this.createStarfield();

    // 2. Primary Satellite Mesh & Orbit Path
    this.createPrimarySatellite();

    // Group for dynamically spawned debris & covariance ellipsoids
    this.debrisGroup = new THREE.Group();
    this.scene.add(this.debrisGroup);

    // Window resize handler
    window.addEventListener('resize', () => this.onWindowResize());

    // Start render loop
    this.animate();
  }

  createStarfield() {
    const starGeo = new THREE.BufferGeometry();
    const starCount = 1200;
    const starPos = new Float32Array(starCount * 3);

    for (let i = 0; i < starCount * 3; i += 3) {
      starPos[i] = (Math.random() - 0.5) * 80000;
      starPos[i + 1] = (Math.random() - 0.5) * 80000;
      starPos[i + 2] = (Math.random() - 0.5) * 80000;
    }

    starGeo.setAttribute('position', new THREE.BufferAttribute(starPos, 3));
    const starMat = new THREE.PointsMaterial({ color: 0x88a0c4, size: 40, sizeAttenuation: true });
    this.scene.add(new THREE.Points(starGeo, starMat));
  }

  createPrimarySatellite() {
    // Primary Orbit Path (Cyan glowing line)
    const points = [];
    const radius = 6786;
    for (let i = 0; i <= 128; i++) {
      const theta = (i / 128) * Math.PI * 2;
      points.push(new THREE.Vector3(Math.cos(theta) * radius, Math.sin(theta) * radius, Math.sin(theta * 2) * 200));
    }
    const orbitGeo = new THREE.BufferGeometry().setFromPoints(points);
    const orbitMat = new THREE.LineBasicMaterial({ color: 0x00d4ff, linewidth: 2, transparent: true, opacity: 0.85 });
    this.primaryOrbit = new THREE.Line(orbitGeo, orbitMat);
    this.scene.add(this.primaryOrbit);

    // Satellite Model
    this.satellite = new THREE.Group();

    // Main Bus (Cube)
    const busGeo = new THREE.BoxGeometry(160, 160, 220);
    const busMat = new THREE.MeshPhongMaterial({ color: 0xd4af37, shininess: 80 }); // Gold foil
    const bus = new THREE.Mesh(busGeo, busMat);
    this.satellite.add(bus);

    // Solar Arrays (Panels)
    const panelGeo = new THREE.BoxGeometry(700, 120, 10);
    const panelMat = new THREE.MeshPhongMaterial({ color: 0x1e3a8a, emissive: 0x0a1931 });
    const panels = new THREE.Mesh(panelGeo, panelMat);
    this.satellite.add(panels);

    // Velocity Vector Beacon
    const beaconGeo = new THREE.SphereGeometry(60, 16, 16);
    const beaconMat = new THREE.MeshBasicMaterial({ color: 0x00ffff });
    this.satBeacon = new THREE.Mesh(beaconGeo, beaconMat);
    this.satellite.add(this.satBeacon);

    // Thruster Plume Mesh
    const plumeGeo = new THREE.ConeGeometry(50, 180, 16);
    this.plumeMat = new THREE.MeshBasicMaterial({
      color: 0x00d4ff,
      transparent: true,
      opacity: 0.0
    });
    this.plumeMesh = new THREE.Mesh(plumeGeo, this.plumeMat);
    this.plumeMesh.position.set(0, -140, 0);
    this.plumeMesh.rotation.x = Math.PI;
    this.satellite.add(this.plumeMesh);

    this.scene.add(this.satellite);
  }

  updateSatellite(state) {
    this.satState = state;
    if (!this.satellite) return;

    // Convert km to scene units
    const [x, y, z] = state.r;
    this.satellite.position.set(x, y, z);

    // Orient satellite along velocity vector
    const [vx, vy, vz] = state.v;
    const vVec = new THREE.Vector3(vx, vy, vz).normalize();
    this.satellite.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), vVec);

    if (this.viewMode === 'follow' && this.controls) {
      this.controls.target.copy(this.satellite.position);
    }
  }

  updateThreats(threats) {
    this.debrisList = threats;
    if (!this.isThreeAvailable || !this.debrisGroup) return;

    // Clear old threat visuals
    while (this.debrisGroup.children.length > 0) {
      this.debrisGroup.remove(this.debrisGroup.children[0]);
    }

    threats.forEach((threat, idx) => {
      const isCritical = threat.risk_score >= 0.70;
      const colorHex = isCritical ? 0xef4444 : (threat.risk_score >= 0.30 ? 0xf59e0b : 0x10b981);

      // Relative position approximation based on miss distance & TCA
      const satPos = this.satellite ? this.satellite.position : new THREE.Vector3(6786, 0, 120);
      const angle = (idx / threats.length) * Math.PI * 2;
      const offsetDist = Math.max(300, threat.d_miss_m * 1.5);
      const debrisPos = new THREE.Vector3(
        satPos.x + Math.cos(angle) * offsetDist,
        satPos.y + Math.sin(angle) * offsetDist,
        satPos.z + (idx % 2 === 0 ? 200 : -200)
      );

      // Debris Sphere
      const dGeo = new THREE.SphereGeometry(isCritical ? 100 : 70, 16, 16);
      const dMat = new THREE.MeshBasicMaterial({ color: colorHex });
      const dMesh = new THREE.Mesh(dGeo, dMat);
      dMesh.position.copy(debrisPos);
      this.debrisGroup.add(dMesh);

      // Conjunction Vector Ray
      const lineGeo = new THREE.BufferGeometry().setFromPoints([satPos, debrisPos]);
      const lineMat = new THREE.LineDashedMaterial({
        color: colorHex,
        dashSize: 150,
        gapSize: 75,
        transparent: true,
        opacity: 0.75
      });
      const ray = new THREE.Line(lineGeo, lineMat);
      ray.computeLineDistances();
      this.debrisGroup.add(ray);

      // 3D Positional Covariance Ellipsoid
      const sigma = (threat.explain && threat.explain.sigma_eff_m) ? threat.explain.sigma_eff_m * 2.0 : 250;
      const elGeo = new THREE.SphereGeometry(sigma, 16, 16);
      const elMat = new THREE.MeshBasicMaterial({
        color: colorHex,
        wireframe: true,
        transparent: true,
        opacity: 0.35
      });
      const elMesh = new THREE.Mesh(elGeo, elMat);
      elMesh.position.copy(debrisPos);
      elMesh.scale.set(1.4, 1.0, 0.8);
      this.debrisGroup.add(elMesh);
    });
  }

  triggerBurnAnimation(engineType, durationS) {
    if (!this.plumeMat) return;

    if (engineType === 'CHEMICAL_BIPROP' || engineType === 0) {
      this.plumeMat.color.setHex(0xf97316); // Orange fire
    } else if (engineType === 'GRIDDED_ION' || engineType === 1) {
      this.plumeMat.color.setHex(0x00ffff); // Electric blue plasma
    } else {
      this.plumeMat.color.setHex(0xffffff); // Cold gas white
    }

    this.plumeMat.opacity = 0.9;
    setTimeout(() => {
      if (this.plumeMat) this.plumeMat.opacity = 0.0;
    }, Math.min(2500, Math.max(800, durationS * 100)));
  }

  setViewMode(mode) {
    this.viewMode = mode;
    if (!this.camera || !this.controls) return;

    if (mode === 'top') {
      this.camera.position.set(0, 0, 24000);
      this.camera.lookAt(0, 0, 0);
      this.controls.target.set(0, 0, 0);
    } else if (mode === 'orbit') {
      this.camera.position.set(0, -18000, 10000);
      this.controls.target.set(0, 0, 0);
    }
  }

  onWindowResize() {
    if (!this.renderer || !this.camera) return;
    const width = this.container.clientWidth;
    const height = this.container.clientHeight;
    this.camera.aspect = width / height;
    this.camera.updateProjectionMatrix();
    this.renderer.setSize(width, height);
  }

  animate() {
    requestAnimationFrame(() => this.animate());

    // Earth slow rotation
    if (this.earth) {
      this.earth.rotation.y += 0.0008;
    }

    // Satellite beacon pulse
    if (this.satBeacon) {
      const scale = 1.0 + 0.25 * Math.sin(Date.now() * 0.005);
      this.satBeacon.scale.set(scale, scale, scale);
    }

    if (this.controls) {
      this.controls.update();
    }

    if (this.renderer && this.scene && this.camera) {
      this.renderer.render(this.scene, this.camera);
    }
  }

  /* ==========================================================================
   * Canvas 2D / Isometric Fallback Engine
   * ========================================================================== */
  initCanvasFallback() {
    this.canvas = document.createElement('canvas');
    this.canvas.id = 'orbit-canvas';
    this.canvas.width = this.container.clientWidth || 800;
    this.canvas.height = this.container.clientHeight || 500;
    this.container.appendChild(this.canvas);
    this.ctx = this.canvas.getContext('2d');

    window.addEventListener('resize', () => {
      this.canvas.width = this.container.clientWidth;
      this.canvas.height = this.container.clientHeight;
    });

    const loop = () => {
      this.renderFallbackCanvas();
      requestAnimationFrame(loop);
    };
    requestAnimationFrame(loop);
  }

  renderFallbackCanvas() {
    const ctx = this.ctx;
    const w = this.canvas.width;
    const h = this.canvas.height;
    const cx = w / 2;
    const cy = h / 2;
    const scale = Math.min(w, h) / 18000;

    // Clear
    ctx.fillStyle = '#050811';
    ctx.fillRect(0, 0, w, h);

    // Stars
    ctx.fillStyle = '#475569';
    for (let i = 0; i < 60; i++) {
      const sx = (Math.sin(i * 99) * 0.5 + 0.5) * w;
      const sy = (Math.cos(i * 33) * 0.5 + 0.5) * h;
      ctx.fillRect(sx, sy, 1.5, 1.5);
    }

    // Earth
    const earthR = 6378 * scale;
    const grad = ctx.createRadialGradient(cx - earthR * 0.2, cy - earthR * 0.2, 10, cx, cy, earthR);
    grad.addColorStop(0, '#1e40af');
    grad.addColorStop(1, '#0b1329');
    ctx.fillStyle = grad;
    ctx.beginPath();
    ctx.arc(cx, cy, earthR, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = '#00d4ff';
    ctx.lineWidth = 1;
    ctx.stroke();

    // Primary Orbit Path
    const orbitR = 6786 * scale;
    ctx.strokeStyle = 'rgba(0, 212, 255, 0.4)';
    ctx.setLineDash([6, 6]);
    ctx.beginPath();
    ctx.arc(cx, cy, orbitR, 0, Math.PI * 2);
    ctx.stroke();
    ctx.setLineDash([]);

    // Primary Satellite
    const satAngle = Date.now() * 0.0003;
    const satX = cx + Math.cos(satAngle) * orbitR;
    const satY = cy + Math.sin(satAngle) * orbitR;

    ctx.fillStyle = '#00ffff';
    ctx.beginPath();
    ctx.arc(satX, satY, 6, 0, Math.PI * 2);
    ctx.fill();

    ctx.fillStyle = '#e6f1ff';
    ctx.font = '10px monospace';
    ctx.fillText('PRIMARY SAT (OCAS)', satX + 10, satY + 4);

    // Render Threats
    this.debrisList.forEach((t, i) => {
      const dAngle = satAngle + (i + 1) * 0.45;
      const dDist = orbitR + (t.d_miss_m > 5000 ? 50 : 25) * (i % 2 === 0 ? 1 : -1);
      const dx = cx + Math.cos(dAngle) * dDist;
      const dy = cy + Math.sin(dAngle) * dDist;

      const isCrit = t.risk_score >= 0.7;
      ctx.fillStyle = isCrit ? '#ef4444' : '#f59e0b';
      ctx.beginPath();
      ctx.arc(dx, dy, isCrit ? 5 : 3.5, 0, Math.PI * 2);
      ctx.fill();

      // Vector to satellite
      ctx.strokeStyle = isCrit ? 'rgba(239, 68, 68, 0.6)' : 'rgba(245, 158, 11, 0.4)';
      ctx.beginPath();
      ctx.moveTo(satX, satY);
      ctx.lineTo(dx, dy);
      ctx.stroke();
    });
  }
}

window.OrbitVisualizer3D = OrbitVisualizer3D;

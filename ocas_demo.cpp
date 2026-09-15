// ============================================================================
// OCAS - Autonomous Orbital Collision Avoidance System  (Terminal Prototype)
// ----------------------------------------------------------------------------
// B.Tech demo build: deterministic, fixed-memory, single-file.
// Build:  g++ -std=c++17 ocas_demo.cpp -o ocas_demo
// Run:    ./ocas_demo
// ============================================================================

#include <cmath>
#include <cstddef>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

// ----------------------------------------------------------------------------
// Constants & fixed capacities (no heap growth - flight-software style)
// ----------------------------------------------------------------------------
constexpr double      G0        = 9.80665;   // standard gravity [m/s^2]
constexpr std::size_t TEL_CAP   = 5;         // telemetry ring-buffer slots
constexpr std::size_t CDM_CAP   = 8;         // CDM FIFO capacity
constexpr std::size_t HEAP_CAP  = 16;        // priority queue capacity
constexpr std::size_t RB_CAP    = 8;         // rollback stack capacity
constexpr const char* LOG_PATH  = "collision_avoidance.log";

// ----------------------------------------------------------------------------
// Small helpers
// ----------------------------------------------------------------------------
static void hr(char c = '-') { std::cout << "  " << std::string(66, c) << "\n"; }

static std::string utcNow() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// ============================================================================
// [1] DETERMINISTIC MEMORY LAYER
// ============================================================================

struct Telemetry {
    double t_s;      
    double alt_km;   
    double vx, vy, vz; 
    double fuel_kg;  
};

class TelemetryRing {
    Telemetry buf_[TEL_CAP];
    std::size_t w_ = 0, n_ = 0;     
public:
    bool push(const Telemetry& s) {
        const bool wrapped = (n_ == TEL_CAP);
        buf_[w_] = s;
        w_ = (w_ + 1) % TEL_CAP;
        if (n_ < TEL_CAP) ++n_;
        return !wrapped;            
    }
    std::size_t size() const { return n_; }
    const Telemetry& at(std::size_t logical) const {          
        return buf_[(w_ + TEL_CAP - n_ + logical) % TEL_CAP];
    }
};

struct Cdm {
    int         id       = 0;
    std::string object;
    double      d_miss_m = 0.0;   
    double      v_rel    = 0.0;   
    double      t_tca    = 0.0;   
    double      sigma_m  = 0.0;   
    double      risk     = 0.0;   
};

// ---- Static FIFO Queue for incoming Conjunction Data Messages ----
class CdmFifo {
    Cdm buf_[CDM_CAP];
    std::size_t head_ = 0, n_ = 0;
public:
    bool push(const Cdm& c) {
        if (n_ == CDM_CAP) return false;               // drop instead of alloc
        buf_[(head_ + n_) % CDM_CAP] = c;
        ++n_;
        return true;
    }
    bool pop(Cdm& out) {
        if (n_ == 0) return false;
        out = buf_[head_];
        head_ = (head_ + 1) % CDM_CAP;
        --n_;
        return true;
    }
    std::size_t size() const { return n_; }
};

static double logisticRisk(const Cdm& c) {
    const double fD = 1.0 - c.d_miss_m / (c.d_miss_m + 3000.0); 
    const double fV = c.v_rel   / (c.v_rel   + 5.0);            
    const double fT = std::exp(-c.t_tca / 600.0);               
    const double fS = c.sigma_m / (c.sigma_m + 200.0);          
    const double z  = -4.0 + 4.0*fD + 2.5*fV + 3.0*fT + 1.5*fS;
    return 1.0 / (1.0 + std::exp(-z));
}

static const char* riskClass(double p) {
    if (p >= 0.90) return "CRITICAL";
    if (p >= 0.50) return "HIGH";
    if (p >= 0.20) return "MODERATE";
    return "LOW";
}

class MaxHeap {
    Cdm a_[HEAP_CAP];
    std::size_t n_ = 0;

    void siftUp(std::size_t i) {
        while (i > 0) {
            std::size_t p = (i - 1) / 2;
            if (a_[p].risk >= a_[i].risk) break;
            std::swap(a_[p], a_[i]);
            i = p;
        }
    }
    void siftDown(std::size_t i) {
        for (;;) {
            std::size_t l = 2*i + 1, r = l + 1, m = i;
            if (l < n_ && a_[l].risk > a_[m].risk) m = l;
            if (r < n_ && a_[r].risk > a_[m].risk) m = r;
            if (m == i) break;
            std::swap(a_[i], a_[m]);
            i = m;
        }
    }
public:
    bool push(const Cdm& c) {
        if (n_ == HEAP_CAP) return false;
        a_[n_] = c;
        siftUp(n_++);
        return true;
    }
    bool pop(Cdm& out) {
        if (n_ == 0) return false;
        out = a_[0];
        a_[0] = a_[--n_];
        siftDown(0);
        return true;
    }
    std::size_t size() const { return n_; }
};

class Thruster {
public:
    virtual ~Thruster() = default;
    virtual const char* name()    const = 0;
    virtual double      thrustN() const = 0;   
    virtual double      ispS()    const = 0;   
    virtual bool        primed()  const { return true; }      
    virtual std::string fault()   const { return "none"; }    
};

class ChemicalThruster : public Thruster {
    bool valveFault_ = true;  
public:
    const char* name()    const override { return "CHEMICAL_MAIN"; }
    double      thrustN() const override { return 400.0; }   
    double      ispS()    const override { return 300.0; }   
    bool        primed()  const override { return !valveFault_; }
    std::string fault()   const override {
        return "oxidizer valve latched shut (telemetry flag 0x2A)";
    }
};
class ElectricIonThruster : public Thruster {
public:
    const char* name()    const override { return "ELECTRIC_ION"; }
    double      thrustN() const override { return 0.09;  }   // 90 mN
    double      ispS()    const override { return 3000.0; }  // very high Isp
};
class ColdGasRcs : public Thruster {
public:
    const char* name()    const override { return "COLD_GAS_RCS"; }
    double      thrustN() const override { return 80.0; }    // micro-pulses
    double      ispS()    const override { return 70.0; }    // very low Isp
};

// ---- Spacecraft state (also the unit of rollback snapshots) ----
struct ShipState {
    std::string t;
    double alt_km  = 408.6;
    double vx = 7.66, vy = 0.0, vz = 0.0;   // km/s
    double mass_kg = 1200.0;                // wet mass
    double fuel_kg = 200.0;
};

struct BurnPlan {
    bool        feasible = false;
    double      dm_kg    = 0.0;
    double      t_burn_s = 0.0;
    std::string reason;
};

// ---- Tsiolkovsky gate: dm = m0 * (1 - e^(-dV / (Isp*g0))) ----
static BurnPlan evaluate(const Thruster& t, double dv, const ShipState& s,
                         double t_tca) {
    BurnPlan p;
    if (!t.primed()) {
        p.reason = "HEALTH FAULT -> " + t.fault();
        return p;
    }
    const double ve     = t.ispS() * G0;
    const double dv_max = ve * std::log(s.mass_kg / (s.mass_kg - s.fuel_kg));
    if (dv > dv_max) { p.reason = "delta-V exceeds propellant budget"; return p; }
    if (dv <= 0.0)   { p.reason = "maneuver delta-V out of thruster range"; return p; }

    const double dm   = s.mass_kg * (1.0 - std::exp(-dv / ve));
    if (dm > s.fuel_kg) { p.reason = "propellant required > fuel remaining"; return p; }

    const double mdot = t.thrustN() / ve;                 // kg/s
    const double tb   = dm / mdot;
    p.dm_kg = dm; p.t_burn_s = tb;                        // report even on reject
    if (tb >= t_tca) { p.reason = "t_burn >= t_TCA - too late"; return p; }

    p.feasible = true;
    p.reason = "feasible (dm and t_burn within limits)";
    return p;
}

// ============================================================================
// [5] PERSISTENCE & AUDITING
// ============================================================================
class RollbackStack {
    ShipState st_[RB_CAP];
    std::size_t n_ = 0;
public:
    bool push(const ShipState& s) { if (n_ == RB_CAP) return false; st_[n_++] = s; return true; }
    bool pop(ShipState& out)      { if (n_ == 0) return false; out = st_[--n_]; return true; }
    std::size_t size() const      { return n_; }
};

static void auditLog(const std::string& entry) {
    std::ofstream f(LOG_PATH, std::ios::app);      // append-only
    f << entry << '\n';
}

// ============================================================================
// DEMO DRIVER
// ============================================================================
int main() {
    std::cout << std::fixed << std::setprecision(2);
    hr('=');
    std::cout << "  OCAS v0.1  |  Autonomous Orbital Collision Avoidance System\n"
              << "  deterministic terminal prototype  |  " << utcNow() << "\n";
    hr('=');

    // ---- (1) Telemetry ingestion via circular queue --------------------
    std::cout << "\n[TELEM] Streaming spacecraft packets into ring buffer "
                 "(cap=" << TEL_CAP << ", fixed array)\n";
    TelemetryRing ring;
    ShipState ship;
    for (int i = 0; i < 8; ++i) {                  // push 8 frames, capacity 5
        Telemetry s{ i*15.0, 408.6 - i*0.012, 7.66, 0.0, 0.0, 200.0 - i*0.03 };
        bool fresh = ring.push(s);
        if (!fresh)
            std::cout << "         ring full -> evicted oldest frame (overwrite-oldest policy)\n";
    }
    std::cout << "         retained " << ring.size() << "/" << TEL_CAP
              << " frames (oldest -> newest):\n";
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Telemetry& s = ring.at(i);
        std::cout << "           t=" << std::setw(5) << s.t_s << " s  alt="
                  << std::setw(7) << std::setprecision(3) << s.alt_km
                  << " km  v=(" << s.vx << "," << s.vy << "," << s.vz
                  << ") km/s  fuel=" << std::setprecision(2) << s.fuel_kg << " kg\n";
    }
    std::cout << std::setprecision(2);

    // ---- (2) CDM ingestion via FIFO -------------------------------------
    hr();
    std::cout << "[INGEST] Uplink: 5 Conjunction Data Messages -> static FIFO (cap="
              << CDM_CAP << ")\n";
    const Cdm incoming[5] = {
        { 1, "STARLINK-4092 FRAG", 22000.0,  1.5, 5400.0, 120.0, 0.0 },
        { 2, "COSMOS-1408 DEBRIS",  1500.0,  4.0, 1800.0, 250.0, 0.0 },
        { 3, "ISS-SHED PANEL",       120.0, 13.0,  420.0, 180.0, 0.0 },
        { 4, "ORBCOMM SPARE",       8400.0,  3.2, 2700.0, 420.0, 0.0 },
        { 5, "FALCON9 UPPER STAGE",  380.0,  9.5, 1500.0,  90.0, 0.0 },
    };
    CdmFifo fifo;
    for (const auto& c : incoming) {
        fifo.push(c);
        std::cout << "         FIFO <- CDM#" << c.id << "  " << c.object << "\n";
    }

    // ---- (3) Risk scoring -----------------------------------------------
    hr();
    std::cout << "[EVAL] Logistic risk classifier (d_miss, v_rel, t_TCA, sigma):\n\n";
    std::cout << "   ID  OBJECT                  d_miss      v_rel    t_TCA   sigma   P(collision)  CLASS\n";
    std::cout << "   --  --------------------    ------      -----    -----   -----   ------------  --------\n";
    MaxHeap heap;
    Cdm c;
    while (fifo.pop(c)) {
        c.risk = logisticRisk(c);
        std::cout << "   " << c.id << "   " << std::left << std::setw(24) << c.object
                  << std::right << std::setw(10) << (int)c.d_miss_m << " m  "
                  << std::setw(5) << c.v_rel << " km/s  "
                  << std::setw(6) << (int)c.t_tca << " s  "
                  << std::setw(5) << (int)c.sigma_m << " m   "
                  << std::setprecision(4) << std::setw(9) << c.risk << "    "
                  << riskClass(c.risk) << std::setprecision(2) << "\n";
        heap.push(c);
    }

    // ---- (4) Max-heap reordering demonstration ---------------------------
    hr();
    std::cout << "[QUEUE] Max-Heap reorders threats by P(collision), not arrival order:\n";
    Cdm order[5]; int n = 0;
    while (heap.pop(order[n])) {
        std::cout << "         pop[" << n << "]  CDM#" << order[n].id << "  "
                  << std::left << std::setw(22) << order[n].object << std::right
                  << " Pc = " << std::setprecision(4) << order[n].risk
                  << "  (" << riskClass(order[n].risk) << ")\n" << std::setprecision(2);
        ++n;
    }

    // ---- (5) Dispatch top threat ----------------------------------------
    const Cdm& top = order[0];
    hr();
    std::cout << "[DISPATCH] Active threat: CDM#" << top.id << " " << top.object
              << "  (Pc=" << std::setprecision(4) << top.risk << ")\n" << std::setprecision(2);

    const double safe_miss = 1500.0;                                  // target miss [m]
    const double dv_req    = std::max(0.5, (safe_miss - top.d_miss_m) / top.t_tca * 2.5);
    std::cout << "         required delta-V  = " << std::setprecision(2) << dv_req
              << " m/s   (maneuver window: t_TCA = " << (int)top.t_tca << " s)\n";
    std::cout << "         ship: m0=" << (int)ship.mass_kg << " kg, fuel=" << (int)ship.fuel_kg
              << " kg -> Tsiolkovsky gate per engine:\n\n";

    ChemicalThruster   chemical;
    ElectricIonThruster ion;
    ColdGasRcs          rcs;
    Thruster* chain[3] = { &chemical, &ion, &rcs };      // fallback chain order

    const Thruster* chosen = nullptr;
    BurnPlan plan;
    for (Thruster* t : chain) {
        plan = evaluate(*t, dv_req, ship, top.t_tca);
        std::cout << "         [TRY] " << std::left << std::setw(15) << t->name()
                  << " thrust=" << std::setw(7) << t->thrustN() << " N  Isp="
                  << std::setw(7) << t->ispS() << " s  ->  "
                  << (plan.feasible ? "ACCEPT  " : "REJECT  ")
                  << plan.reason << "\n" << std::right;
        if (plan.feasible) { chosen = t; break; }
        std::cout << "               \\__ falling back to next engine in chain";
        if (plan.t_burn_s > 0.0)
            std::cout << "  (projected t_burn = "
                      << std::setprecision(0) << plan.t_burn_s << " s)";
        std::cout << "\n" << std::setprecision(2);
    }
    if (!chosen) {
        std::cout << "\n[ABORT] No viable thruster. Evading not possible; "
                     "issuing EMI shielding posture.\n";
        return 1;
    }

    // ---- (6) Execute burn + rollback snapshot ----------------------------
    hr();
    RollbackStack undo;
    undo.push(ship);                                   // snapshot BEFORE burn
    const double fuel0 = ship.fuel_kg, vx0 = ship.vx;
    ship.fuel_kg -= plan.dm_kg;
    ship.mass_kg -= plan.dm_kg;
    ship.vx      += dv_req / 1000.0;                   // along-track boost
    ship.alt_km  += dv_req * plan.t_burn_s / 2000.0;   // simplified raise

    std::cout << "[BURN] " << chosen->name() << " firing for "
              << std::setprecision(1) << plan.t_burn_s << " s\n";
    std::cout << "         dV applied        : " << std::setprecision(3) << dv_req
              << " m/s  (along-track)\n";
    std::cout << "         propellant (dm)   : " << std::setprecision(3) << plan.dm_kg
              << " kg     " << std::setprecision(1) << fuel0 << " -> "
              << ship.fuel_kg << " kg\n";
    std::cout << "         velocity change   : vx " << std::setprecision(3) << vx0
              << " -> " << ship.vx << " km/s\n";
    std::cout << std::setprecision(2)
              << "         new altitude      : " << ship.alt_km << " km\n"
              << "         rollback snapshot : stored (stack depth " << undo.size()
              << "/" << RB_CAP << ")\n";

    // ---- (7) Append-only audit log ---------------------------------------
    std::ostringstream entry;
    entry << utcNow()
          << " | CDM=" << top.object << " (#" << top.id << ")"
          << " | Pc=" << std::fixed << std::setprecision(4) << top.risk
          << " | THRUSTER=" << chosen->name()
          << " | dV=" << std::setprecision(2) << dv_req << " m/s"
          << " | dm=" << std::setprecision(3) << plan.dm_kg << " kg"
          << " | t_burn=" << std::setprecision(1) << plan.t_burn_s << " s"
          << " | FUEL " << std::setprecision(2) << fuel0 << "->" << ship.fuel_kg << " kg"
          << " | ROLLBACK_DEPTH=" << undo.size()
          << " | STATUS=EXECUTED";
    auditLog(entry.str());

    hr();
    std::cout << "[AUDIT] Appended to " << LOG_PATH << ":\n         " << entry.str() << "\n";
    std::cout << "         pending secondary threats: " << (n - 1)
              << " remain queued in heap (next: CDM#" << order[1].id
              << ", Pc=" << std::setprecision(4) << order[1].risk << ")\n";
    hr('=');
    std::cout << "  Demo complete. Log persisted to ./" << LOG_PATH << "\n";
    hr('=');
    return 0;
}

#include "ml/neural.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace ocas::ml {

namespace {

using Table = std::map<std::string, std::vector<double>>;

bool read_table(const std::string& path, const std::string& arch, Table& t, std::string& error) {
    std::ifstream in(path);
    if (!in) { error = "cannot open " + path; return false; }
    std::string line;
    bool arch_ok = false;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string name;
        ss >> name;
        if (name == "arch") {
            std::string a;
            ss >> a;
            arch_ok = (a == arch);
            continue;
        }
        std::vector<double> v;
        double x;
        while (ss >> x) {
            if (!std::isfinite(x)) { error = "non-finite value for " + name + " in " + path; return false; }
            v.push_back(x);
        }
        t[name] = std::move(v);
    }
    if (!arch_ok) { error = path + " is not an '" + arch + "' weight file"; return false; }
    return true;
}

template <std::size_t N>
bool take(const Table& t, const char* name, std::array<double, N>& out, std::string& error) {
    const auto it = t.find(name);
    if (it == t.end() || it->second.size() != N) {
        error = std::string("weight '") + name + "' missing or has wrong size (expected " + std::to_string(N) + ")";
        return false;
    }
    for (std::size_t i = 0; i < N; ++i) out[i] = it->second[i];
    return true;
}

bool take_scalar(const Table& t, const char* name, double& out, std::string& error) {
    std::array<double, 1> a{};
    if (!take(t, name, a, error)) return false;
    out = a[0];
    return true;
}

bool take_scaler(const Table& t, FeatureScaler& s, std::string& error) {
    if (!take(t, "feature_mean", s.mean, error) || !take(t, "feature_std", s.stddev, error)) return false;
    for (double sd : s.stddev) {
        if (!(sd > 0.0)) { error = "feature_std must be positive"; return false; }
    }
    return true;
}

template <std::size_t N>
void put(std::ofstream& f, const char* name, const std::array<double, N>& a) {
    f << name;
    for (double x : a) f << ' ' << x;
    f << '\n';
}

void put_scalar(std::ofstream& f, const char* name, double x) { f << name << ' ' << x << '\n'; }

std::ofstream open_out(const std::string& path, const std::string& comment, const char* arch) {
    std::ofstream f(path);
    f << std::setprecision(17);
    std::istringstream c(comment);
    std::string line;
    while (std::getline(c, line)) f << "# " << line << '\n';
    f << "arch " << arch << '\n';
    return f;
}

}  // namespace

bool load_lstm(const std::string& path, LstmParams& out, std::string& error) {
    Table t;
    LstmParams p{};
    if (!read_table(path, "lstm_4_16", t, error)) return false;
    if (!take_scaler(t, p.scaler, error) || !take(t, "W", p.W, error) || !take(t, "U", p.U, error) ||
        !take(t, "b", p.b, error) || !take(t, "w_out", p.w_out, error) || !take_scalar(t, "b_out", p.b_out, error) ||
        !take_scalar(t, "threshold", p.threshold, error))
        return false;
    out = p;
    return true;
}

bool save_lstm(const std::string& path, const LstmParams& p, const std::string& comment) {
    auto f = open_out(path, comment, "lstm_4_16");
    put(f, "feature_mean", p.scaler.mean);
    put(f, "feature_std", p.scaler.stddev);
    put_scalar(f, "threshold", p.threshold);
    put(f, "W", p.W);
    put(f, "U", p.U);
    put(f, "b", p.b);
    put(f, "w_out", p.w_out);
    put_scalar(f, "b_out", p.b_out);
    return static_cast<bool>(f);
}

}  // namespace ocas::ml

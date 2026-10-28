#include "neural.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <vector>

namespace ocas::ml {

namespace {
constexpr const char* kArch = "lstm_4_16";
}

// Text format: "# comment" lines, "arch lstm_4_16", then "<name> <values...>" per line.
bool load_lstm(const std::string& path, LstmParams& out, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    std::map<std::string, std::vector<double>> table;
    bool arch_ok = false;
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string name;
        ss >> name;
        if (name == "arch") {
            std::string a;
            ss >> a;
            arch_ok = a == kArch;
            continue;
        }
        std::vector<double>& v = table[name];
        for (double x; ss >> x;) v.push_back(x);
    }
    if (!arch_ok) {
        error = path + " is not an '" + kArch + "' weight file";
        return false;
    }
    auto take = [&](const char* name, double* dst, std::size_t n) {
        const auto it = table.find(name);
        if (it == table.end() || it->second.size() != n) {
            error = std::string("weight '") + name + "' missing or has wrong size (expected " + std::to_string(n) + ")";
            return false;
        }
        for (std::size_t i = 0; i < n; ++i) dst[i] = it->second[i];
        return true;
    };
    LstmParams p{};
    if (!take("feature_mean", p.scaler.mean.data(), kFeatures) ||
        !take("feature_std", p.scaler.stddev.data(), kFeatures) || !take("W", p.W.data(), p.W.size()) ||
        !take("U", p.U.data(), p.U.size()) || !take("b", p.b.data(), p.b.size()) ||
        !take("w_out", p.w_out.data(), p.w_out.size()) || !take("b_out", &p.b_out, 1) ||
        !take("threshold", &p.threshold, 1)) {
        return false;
    }
    for (double sd : p.scaler.stddev) {
        if (!(sd > 0.0)) {
            error = "feature_std must be positive";
            return false;
        }
    }
    out = p;
    return true;
}

bool save_lstm(const std::string& path, const LstmParams& p, const std::string& comment) {
    std::ofstream f(path);
    f << std::setprecision(17);
    std::istringstream c(comment);
    for (std::string line; std::getline(c, line);) f << "# " << line << '\n';
    f << "arch " << kArch << '\n';
    auto put = [&](const char* name, const double* a, std::size_t n) {
        f << name;
        for (std::size_t i = 0; i < n; ++i) f << ' ' << a[i];
        f << '\n';
    };
    put("feature_mean", p.scaler.mean.data(), kFeatures);
    put("feature_std", p.scaler.stddev.data(), kFeatures);
    put("threshold", &p.threshold, 1);
    put("W", p.W.data(), p.W.size());
    put("U", p.U.data(), p.U.size());
    put("b", p.b.data(), p.b.size());
    put("w_out", p.w_out.data(), p.w_out.size());
    put("b_out", &p.b_out, 1);
    return static_cast<bool>(f);
}

}  // namespace ocas::ml

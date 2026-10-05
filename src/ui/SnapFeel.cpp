#include "SnapFeel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bld::ui::snapfeel {

QString strengthId(Strength s) {
    switch (s) {
    case Strength::Off: return QStringLiteral("off");
    case Strength::Strong: return QStringLiteral("strong");
    case Strength::Gentle: break;
    }
    return QStringLiteral("gentle");
}

std::optional<Strength> strengthFromId(const QString& id) {
    if (id == QLatin1String("off")) return Strength::Off;
    if (id == QLatin1String("gentle")) return Strength::Gentle;
    if (id == QLatin1String("strong")) return Strength::Strong;
    return std::nullopt;
}

double strengthScale(Strength s) {
    switch (s) {
    case Strength::Off: return 0.0;
    case Strength::Strong: return 1.6;
    case Strength::Gentle: break;
    }
    return 1.0;
}

double reachStuds(double screenPxPerStud, Strength strength) {
    const double scale = strengthScale(strength);
    if (scale <= 0.0) return 0.0;
    const double raw = screenPxPerStud > 0.0 && std::isfinite(screenPxPerStud) ? kReachScreenPx / screenPxPerStud
                                                                               : kMaxReachStuds;
    return std::clamp(raw, kMinReachStuds, kMaxReachStuds) * scale;
}

namespace {

// a before b among level candidates: nearer the cursor, then nearer, then
// the lower keys.
bool better(const Candidate& a, const Candidate& b) {
    if (std::abs(a.mouseDist - b.mouseDist) > 1e-9) return a.mouseDist < b.mouseDist;
    if (std::abs(a.dist - b.dist) > 1e-9) return a.dist < b.dist;
    if (a.targetKey != b.targetKey) return a.targetKey < b.targetKey;
    return a.movingKey < b.movingKey;
}

// The best candidate within reach, the same whatever order they come in.
int bestNew(const std::vector<Candidate>& cands, double reach) {
    double nearest = std::numeric_limits<double>::infinity();
    for (const Candidate& c : cands)
        if (c.dist <= reach && c.dist < nearest) nearest = c.dist;
    if (!std::isfinite(nearest)) return -1;
    int best = -1;
    for (int i = 0; i < static_cast<int>(cands.size()); ++i) {
        const Candidate& c = cands[i];
        if (c.dist > reach || c.dist > nearest + kTieStuds) continue;
        if (best < 0 || better(c, cands[best])) best = i;
    }
    return best;
}

}  // namespace

int pick(const std::vector<Candidate>& cands, const std::optional<Lock>& lock, double reach, bool fast, bool bypass) {
    if (bypass || !(reach > 0.0)) return -1;
    const double hold = holdReach(reach);
    int held = -1;
    if (lock) {
        for (int i = 0; i < static_cast<int>(cands.size()); ++i) {
            const Candidate& c = cands[i];
            if (c.movingKey == lock->movingKey && c.targetKey == lock->targetKey && c.dist <= hold) {
                held = i;
                break;
            }
        }
    }
    const int best = bestNew(cands, reach);
    if (held >= 0) {
        if (fast || best < 0 || best == held) return held;
        const double h = cands[held].dist;
        const double b = cands[best].dist;
        const bool clearlyCloser = b <= h * kSwitchRatio && h - b >= kSwitchMinStuds;
        return clearlyCloser ? best : held;
    }
    if (fast) return -1;
    return best;
}

void SpeedMeter::sample(double x, double y, double tMs) {
    if (!samples_.empty() && tMs < samples_.back().t) samples_.clear();
    samples_.push_back({ x, y, tMs });
    while (static_cast<int>(samples_.size()) > kSpeedSamples + 1) samples_.pop_front();
}

double SpeedMeter::speed() const {
    if (samples_.size() < 2) return 0.0;
    const double newest = samples_.back().t;
    std::size_t first = samples_.size() - 1;
    while (first > 0 && newest - samples_[first - 1].t <= kSpeedWindowMs) --first;
    const double elapsed = newest - samples_[first].t;
    if (elapsed <= 0.0) return 0.0;
    double path = 0.0;
    for (std::size_t i = first + 1; i < samples_.size(); ++i)
        path += std::hypot(samples_[i].x - samples_[i - 1].x, samples_[i].y - samples_[i - 1].y);
    return path / elapsed * 1000.0;
}

int Session::step(const std::vector<Candidate>& candidates, double reach, bool bypass, bool final) {
    const bool fast = !final && meter.isFast();
    const int i = pick(candidates, lock, reach, fast, bypass);
    if (i >= 0) lock = Lock{ candidates[i].movingKey, candidates[i].targetKey };
    else lock.reset();
    return i;
}

}  // namespace bld::ui::snapfeel

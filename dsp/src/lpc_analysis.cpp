#include "cvspeak/lpc_analysis.h"

#include <cmath>

namespace cvspeak::lpc {

void autocorrelate(const float* x, int n, float* r, int order) {
    for (int lag = 0; lag <= order; ++lag) {
        float sum = 0.f;
        for (int i = 0; i + lag < n; ++i) sum += x[i] * x[i + lag];
        r[lag] = sum;
    }
}

float levinson(const float* r, int order, float* k) {
    float a[kMaxAnalysisOrder + 1] = {1.f};
    float tmp[kMaxAnalysisOrder + 1] = {};
    float err = r[0];
    if (err <= 0.f) {
        for (int i = 0; i < order; ++i) k[i] = 0.f;
        return 0.f;
    }
    for (int i = 1; i <= order; ++i) {
        float acc = r[i];
        for (int j = 1; j < i; ++j) acc += a[j] * r[i - j];
        float ki = -acc / err;
        if (ki > 0.9999f) ki = 0.9999f;
        if (ki < -0.9999f) ki = -0.9999f;
        k[i - 1] = ki;
        for (int j = 1; j < i; ++j) tmp[j] = a[j] + ki * a[i - j];
        for (int j = 1; j < i; ++j) a[j] = tmp[j];
        a[i] = ki;
        err *= 1.f - ki * ki;
    }
    return err;
}

namespace {

float normalizedCorrelation(const float* x, int n, int lag) {
    float c = 0.f, e1 = 0.f, e2 = 0.f;
    for (int i = 0; i + lag < n; ++i) {
        c += x[i] * x[i + lag];
        e1 += x[i] * x[i];
        e2 += x[i + lag] * x[i + lag];
    }
    const float denom = e1 * e2;
    return denom > 0.f ? c / std::sqrt(denom) : 0.f;
}

}  // namespace

float detectPitch(const float* x, int n, int minLag, int maxLag, float threshold, float* corrOut) {
    if (maxLag >= n) maxLag = n - 1;
    float best = 0.f;
    int bestLag = 0;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        const float c = normalizedCorrelation(x, n, lag);
        if (c > best) {
            best = c;
            bestLag = lag;
        }
    }

    for (int d = 4; d >= 2 && bestLag > 0; --d) {
        const int center = (bestLag + d / 2) / d;
        float subBest = 0.f;
        int subLag = 0;
        for (int lag = center - 1; lag <= center + 1; ++lag) {
            if (lag < minLag) continue;
            const float c = normalizedCorrelation(x, n, lag);
            if (c > subBest) {
                subBest = c;
                subLag = lag;
            }
        }
        if (subLag > 0 && subBest > 0.85f * best) {
            best = subBest;
            bestLag = subLag;
            break;
        }
    }

    if (corrOut) *corrOut = best;
    if (bestLag == 0 || best < threshold) return 0.f;

    float offset = 0.f;
    if (bestLag > minLag && bestLag < maxLag) {
        const float ym = normalizedCorrelation(x, n, bestLag - 1);
        const float yp = normalizedCorrelation(x, n, bestLag + 1);
        const float denom = ym - 2.f * best + yp;
        if (denom < 0.f) offset = 0.5f * (ym - yp) / denom;
    }
    return static_cast<float>(bestLag) + offset;
}

bool classifyVoiced(const VoicingFeatures& f) {
    if (f.corr > 0.5f) return true;
    if (f.corr > 0.2f && f.k1 < -0.4f) return true;
    if (f.corr > 0.33f && f.belowPeak <= 3 && f.zcr < 0.4f) return true;
    return false;
}

}  // namespace cvspeak::lpc

// A rational (P/Q) polyphase sample-rate converter, written for one job: running Beads'
// VINTAGE DIGITAL media at its true 32000 Hz inside a 44.1 kHz plugin. The module just
// clocks its codec at 32 kHz; a plugin has to convert.
//
// Design notes (each earned the hard way, see the test logs):
//   - h(t) = 2c sinc(2c t) w(|t|), c = 0.45 x min(1, P/Q) cycles/sample. A kernel
//     sinc(c t) has bandwidth c/2, not c -- the first cut measured a brick wall at 7 kHz.
//   - The window is a function of |t| (centered on the reconstruction point). A window
//     fixed to the sample grid makes the per-phase DC sums ramp 2:1 -- a swept tone
//     measured -1.8 dB at 1 kHz, -9 dB at 10 kHz.
//   - ONE global gain (mean phase sum). Per-phase DC normalization boosts the edge
//     phases' whole spectrum; global is flat to <0.1% per phase.
// Verified: frequency error < 0.0001 Hz over 4 s, ripple 0.6%, passband -0.0 dB to
// 10 kHz, -6 dB at the 14 kHz cutoff, stopband images below -78 dB.
//
// Streaming: Push() input blocks, Pull() converted frames. A block of n inputs yields
// n P/Q outputs give or take one -- the position is carried as an integer, so the count
// is exact over time and Pull() may return n-1 while the ratio rounds against you
// (callers hold the last frame; a start-up-only event).
#pragma once
#include <cmath>
#include <cstring>

namespace et {

class RationalSRC {
 public:
  static const int TAPS = 48, PHASES = 441, HIST = TAPS + 2, CAP = 512, HALF = TAPS / 2;

  void Init(int p, int q) {   // out_rate = in_rate * p / q
    p_ = p; q_ = q;
    const double fc = 0.45 * (p_ < q_ ? double(p_) / q_ : 1.0);
    double all = 0.0;
    for (int ph = 0; ph < PHASES; ++ph) {
      const double frac = ph / double(PHASES);
      for (int k = 0; k < TAPS; ++k) {
        double t = (k - HALF) - frac;              // tap k reads x[base - HALF + k]
        double a = fabs(t);
        double w = a < HALF ? 0.42 + 0.5 * cos(M_PI * a / HALF) + 0.08 * cos(2.0 * M_PI * a / HALF) : 0.0;
        double x = t * 2.0 * fc;                   // h(t) = 2c sinc(2c t) w(|t|)
        double sinc = fabs(x) < 1e-12 ? 2.0 * fc : 2.0 * fc * sin(M_PI * x) / (M_PI * x);
        h_[ph][k] = float(sinc * w);
        all += h_[ph][k];
      }
    }
    const double g = PHASES / all;                 // mean phase sum -> unity
    for (int ph = 0; ph < PHASES; ++ph)
      for (int k = 0; k < TAPS; ++k) h_[ph][k] = float(h_[ph][k] * g);
    Reset();
  }

  void Reset() {
    memset(hist_, 0, sizeof hist_);
    memset(ring_, 0, sizeof ring_);
    rlen_ = rpos_ = 0;
    pp_ = pushed_ = 0;
  }

  void Push(const float* lin, const float* rin, int n) {
    for (int i = 0; i < n; ++i) {
      memmove(&hist_[0][0], &hist_[0][1], sizeof(float) * (HIST - 1));
      memmove(&hist_[1][0], &hist_[1][1], sizeof(float) * (HIST - 1));
      hist_[0][HIST - 1] = lin[i];
      hist_[1][HIST - 1] = rin[i];
      ++pushed_;
      while (rlen_ < CAP) {
        const long long pq = pp_ * (long long)q_;
        const long long base = pq / p_;
        if (base + HALF > pushed_ - 1) break;   // newest tap x[base+HALF-1] not yet in
        const int rem = int(pq - base * p_);
        const int ph = int((long long)rem * PHASES / p_);
        int i0 = int(base - pushed_) + HIST - HALF;   // x[base-HALF] lives at HIST-slot i0

        float yl = 0.f, yr = 0.f;
        for (int k = 0; k < TAPS; ++k) {
          const float hl = h_[ph][k];
          yl += hl * hist_[0][i0 + k];
          yr += hl * hist_[1][i0 + k];
        }
        ring_[0][rlen_] = yl;
        ring_[1][rlen_] = yr;
        ++rlen_;
        ++pp_;
      }
    }
  }

  int Pull(float* lout, float* rout, int n) {
    int k = n < rlen_ - rpos_ ? n : rlen_ - rpos_;
    if (k > 0) {
      memcpy(lout, &ring_[0][rpos_], sizeof(float) * k);
      memcpy(rout, &ring_[1][rpos_], sizeof(float) * k);
      rpos_ += k;
    }
    if (rpos_ == rlen_) rpos_ = rlen_ = 0;
    else if (rpos_ >= 64) {   // compact: without this the write cursor creeps +n per block
      memmove(&ring_[0][0], &ring_[0][rpos_], sizeof(float) * (rlen_ - rpos_));
      memmove(&ring_[1][0], &ring_[1][rpos_], sizeof(float) * (rlen_ - rpos_));
      rlen_ -= rpos_;
      rpos_ = 0;
    }
    return k;
  }

 private:
  float h_[PHASES][TAPS];
  float hist_[2][HIST];
  float ring_[2][CAP];
  int rlen_ = 0, rpos_ = 0;
  long long pp_ = 0, pushed_ = 0;   // outputs produced, inputs consumed
  int p_ = 1, q_ = 1;
};

}  // namespace et

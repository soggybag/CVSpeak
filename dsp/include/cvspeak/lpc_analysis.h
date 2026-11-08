#pragma once
// LPC analysis: autocorrelation, Levinson-Durbin and pitch detection.
// Used offline by tools/lpc_encode, and later for the real-time LPC vocoder.
//
// Sign convention: A(z) = 1 + sum(a_i z^-i). The reflection coefficients
// returned by levinson() drive LatticeFilter (lpc_synth.h) directly.

namespace cvspeak::lpc {

constexpr int kMaxAnalysisOrder = 16;

// r[0..order] = autocorrelation of x[0..n).
void autocorrelate(const float* x, int n, float* r, int order);

// Levinson-Durbin recursion. Writes k[0..order) and returns the final
// prediction error energy (in the units of r[0]).
float levinson(const float* r, int order, float* k);

// Normalized autocorrelation pitch detector. Returns the period in samples
// (fractional, parabolic interpolation) or 0 when the best correlation is
// below `threshold`. Prefers the shortest lag that correlates nearly as well
// as the best one, which avoids octave-down errors. `corrOut` may be null.
float detectPitch(const float* x, int n, int minLag, int maxLag, float threshold, float* corrOut);

}  // namespace cvspeak::lpc

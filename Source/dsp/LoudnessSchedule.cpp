#include "LoudnessSchedule.h"

#include <vector>

namespace bsweep
{

namespace
{
    constexpr float kRefLowHz  = 15.0f;
    constexpr float kRefHighHz = 21000.0f;

    /** Second-order analogue high shelf, RBJ prototype:
        H(s) = A (A s^2 + (sqrt(A)/Q) s + 1) / (s^2 + (sqrt(A)/Q) s + A)
        DC gain 1, HF gain A^2. */
    float shelfMagnitudeSquared (float w, float A, float q) noexcept
    {
        const float sa = std::sqrt (A);
        // numerator:  A * (1 - A w^2  +  j w sqrt(A)/Q)
        const float nRe = A * (1.0f - A * w * w);
        const float nIm = A * (w * sa / q);
        // denominator:      (A - w^2) + j w sqrt(A)/Q
        const float dRe = A - w * w;
        const float dIm = w * sa / q;

        const float num = nRe * nRe + nIm * nIm;
        const float den = dRe * dRe + dIm * dIm;
        return num / std::max (den, 1.0e-20f);
    }

    float highpass2MagnitudeSquared (float w, float q) noexcept
    {
        const float a = 1.0f - w * w;
        const float b = w / q;
        return (w * w * w * w) / std::max (a * a + b * b, 1.0e-20f);
    }
}

float kWeightingMagnitudeSquared (float freqHz) noexcept
{
    constexpr float shelfHz = 1681.97f;
    constexpr float shelfQ  = 0.70710678f;
    constexpr float shelfDb = 3.999f;
    constexpr float hpHz    = 38.13f;
    constexpr float hpQ     = 0.5f;

    const float A = std::pow (10.0f, shelfDb / 40.0f);
    return shelfMagnitudeSquared (freqHz / shelfHz, A, shelfQ)
         * highpass2MagnitudeSquared (freqHz / hpHz, hpQ);
}

//==============================================================================
const LoudnessSchedule& LoudnessSchedule::instance()
{
    static const LoudnessSchedule shared;
    return shared;
}

LoudnessSchedule::LoudnessSchedule()
{
    // Log-spaced probe frequencies.  Because the reference spectrum is pink
    // (equal energy per octave) and the spacing is uniform in log f, every
    // probe carries the same pink weight and the integral degenerates to a
    // plain sum of the K-weighting.
    std::vector<float> freq (kNumFreq), weight (kNumFreq);
    double refEnergy = 0.0;

    for (int i = 0; i < kNumFreq; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (kNumFreq - 1);
        freq[static_cast<size_t> (i)]   = kRefLowHz * std::pow (kRefHighHz / kRefLowHz, t);
        weight[static_cast<size_t> (i)] = kWeightingMagnitudeSquared (freq[static_cast<size_t> (i)]);
        refEnergy += weight[static_cast<size_t> (i)];
    }

    const double invRef = 1.0 / refEnergy;

    // Response magnitudes are separable: |filter(f)|^2 depends on (mode, slope,
    // u, r) and |tilt(f)|^2 only on (u, c), so each axis is evaluated once and
    // the table cells are plain weighted sums.
    std::vector<float> tiltMag2 (static_cast<size_t> (kNumC * kNumFreq));
    std::vector<float> hpMag2   (static_cast<size_t> (kNumR * kNumFreq));

    // The tilt depends only on (cutoff, character) - not on the slope and not on
    // the filter topology - so it is computed once per cutoff position instead
    // of once per table cell.  That alone is the difference between a ~1.0 M
    // and a ~0.44 M filter-evaluation build.
    for (int iu = 0; iu < kNumU; ++iu)
    {
        const float u  = static_cast<float> (iu) / static_cast<float> (kNumU - 1);
        const float fc = positionToCutoff (u);

        for (int ic = 0; ic < kNumC; ++ic)
        {
            const float c    = static_cast<float> (ic) / static_cast<float> (kNumC - 1);
            const float tilt = tiltSlopeDbPerOctave (u, c);

            for (int i = 0; i < kNumFreq; ++i)
            {
                const auto h = responsemodel::tiltResponse (freq[static_cast<size_t> (i)], fc, tilt, 0.0f);
                tiltMag2[static_cast<size_t> (ic * kNumFreq + i)] = std::norm (h);
            }
        }

        for (int im = 0; im < kNumFilterModes; ++im)
        {
            for (int is = 0; is < kNumSlopes; ++is)
            {
                for (int ir = 0; ir < kNumR; ++ir)
                {
                    const float r = static_cast<float> (ir) / static_cast<float> (kNumR - 1);

                    for (int i = 0; i < kNumFreq; ++i)
                    {
                        const auto h = responsemodel::filterResponse (freq[static_cast<size_t> (i)],
                                                                      fc, is, im, r, 0.0f);
                        hpMag2[static_cast<size_t> (ir * kNumFreq + i)] = std::norm (h);
                    }
                }

                for (int ic = 0; ic < kNumC; ++ic)
                {
                    for (int ir = 0; ir < kNumR; ++ir)
                    {
                        double energy = 0.0;
                        for (int i = 0; i < kNumFreq; ++i)
                            energy += static_cast<double> (weight[static_cast<size_t> (i)])
                                    * static_cast<double> (hpMag2[static_cast<size_t> (ir * kNumFreq + i)])
                                    * static_cast<double> (tiltMag2[static_cast<size_t> (ic * kNumFreq + i)]);

                        table[im][is][iu][ic][ir] =
                            static_cast<float> (10.0 * std::log10 (std::max (energy * invRef, 1.0e-12)));
                    }
                }
            }
        }
    }
}

float LoudnessSchedule::measuredDb (float u, float character01, float resonance01,
                                    int slopeIndex, int filterMode) const noexcept
{
    const int is = clampValue (slopeIndex, 0, kNumSlopes - 1);
    const int im = clampValue (filterMode, 0, kNumFilterModes - 1);

    const float fu = clampValue (u,           0.0f, 1.0f) * (kNumU - 1);
    const float fc = clampValue (character01, 0.0f, 1.0f) * (kNumC - 1);
    const float fr = clampValue (resonance01, 0.0f, 1.0f) * (kNumR - 1);

    const int iu = std::min (static_cast<int> (fu), kNumU - 2);
    const int ic = std::min (static_cast<int> (fc), kNumC - 2);
    const int ir = std::min (static_cast<int> (fr), kNumR - 2);

    const float tu = fu - static_cast<float> (iu);
    const float tc = fc - static_cast<float> (ic);
    const float tr = fr - static_cast<float> (ir);

    // Bilinear across CHARACTER and RESONANCE (both move slowly and are not
    // usually automated), Catmull-Rom across the cutoff axis (which is).
    const auto plane = [&] (int uIndex)
    {
        const int uu = clampValue (uIndex, 0, kNumU - 1);
        const float a = lerp (table[im][is][uu][ic][ir],     table[im][is][uu][ic + 1][ir],     tc);
        const float b = lerp (table[im][is][uu][ic][ir + 1], table[im][is][uu][ic + 1][ir + 1], tc);
        return lerp (a, b, tr);
    };

    // Linearly extrapolated guard points at the two ends.  Clamping instead
    // (the obvious thing) forces the spline's end tangent to zero and leaves a
    // visible kink in the fade rate in the top few percent of the cutoff range.
    const auto guarded = [&] (int uIndex)
    {
        if (uIndex < 0)          return 2.0f * plane (0) - plane (1);
        if (uIndex > kNumU - 1)  return 2.0f * plane (kNumU - 1) - plane (kNumU - 2);
        return plane (uIndex);
    };

    return catmullRom (guarded (iu - 1), guarded (iu), guarded (iu + 1), guarded (iu + 2), tu);
}

float LoudnessSchedule::gainDb (float u, float character01, float resonance01,
                                int slopeIndex, int filterMode) const noexcept
{
    const float measured = measuredDb (u, character01, resonance01, slopeIndex, filterMode);
    const float target   = loudnessTargetDb (u, character01);

    // Never positive: AUTO is an attenuator, so it can never lift the noise
    // floor at extreme cutoff settings.
    return -smoothHinge (measured - target, kLoudnessSoftKneeDb);
}

} // namespace bsweep

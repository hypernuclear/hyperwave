#pragma once

#include <vector>

namespace hyperlight {

// Maps FFT bins onto the geometrically spaced display bands, and carries the
// per-band aesthetic tilt.
//
// Rebuilt whenever the sample rate or the FFT size changes, which is why it is
// an object rather than a free function -- the tables are the expensive part
// and they must not be recomputed sixty times a second.
class BandMapper
{
public:
    // `bandCount` display bands spanning [loHz, hiHz], against an FFT of
    // `fftSize` points taken at `sampleRate`.
    void rebuild(int bandCount, int fftSize, double sampleRate, float loHz, float hiHz);

    // `power` holds fftSize/2 + 1 normalised bin powers.
    // `outDb` receives bandCount values in dB, tilt already applied.
    void aggregate(const float *power, float *outDb) const;

    int bandCount() const { return static_cast<int>(m_bands.size()); }
    float centreHz(int band) const { return m_bands[static_cast<std::size_t>(band)].centreHz; }
    bool isValid() const { return !m_bands.empty(); }

private:
    struct Band
    {
        int binLo = 0;        // inclusive
        int binHi = -1;       // inclusive; binHi < binLo means "interpolate"
        float centreHz = 0.0f;
        float interpBin = 0.0f;   // fractional bin index for the narrow low bands
        float tiltDb = 0.0f;
    };

    std::vector<Band> m_bands;
    int m_binMin = 0;   // bins below this are treated as absent (DC, rumble)
    int m_binMax = 0;   // and above this (codec artefacts, dither)
};

} // namespace hyperlight

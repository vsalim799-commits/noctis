// Prints the core's propagation functions on a grid so that Tools/AudioFactory/qa.py can check
// that the Python port (audiofactory/propagation.py) matches the compiled C++ core bit-for-bit
// (to float precision). Built by qa.py against build/libnoctis_core.a; never part of the game.
#include "Noctis/World/SoundField.h"

#include <cstdio>

int main()
{
    using namespace noctis;
    const float freqs[] = {20.0f, 50.0f, 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 12000.0f, 16000.0f};
    const float temps[] = {-5.0f, 12.0f, 17.0f, 30.0f};
    const float hums[] = {0.1f, 0.3f, 0.75f, 0.95f};
    for (float t : temps)
    {
        for (float h : hums)
        {
            for (float f : freqs)
            {
                std::printf("abs %.1f %.3f %.1f %.9e\n", static_cast<double>(t), static_cast<double>(h), static_cast<double>(f),
                            static_cast<double>(SoundField::absorptionDbPerM(f, t, h)));
            }
        }
    }
    for (float f : freqs)
    {
        std::printf("fol %.1f %.9e\n", static_cast<double>(f), static_cast<double>(SoundField::foliageDbPerM(f)));
    }
    // Received level without vegetation / terrain / wind (the part the audio library bakes into distance variants).
    const float dists[] = {1.0f, 50.0f, 200.0f, 800.0f, 3000.0f};
    for (float d : dists)
    {
        for (float f : freqs)
        {
            SoundSource s;
            s.levelDb = 100.0f;
            s.f0Hz = f;
            s.position = Vec3{0.0f, 0.0f, 0.0f};
            PropagationEnv env;
            env.temperatureC = 12.0f;
            env.humidity = 0.75f;
            std::printf("rcv %.1f %.1f %.6f\n", static_cast<double>(d), static_cast<double>(f),
                        static_cast<double>(SoundField::receivedLevelDb(s, Vec3{d, 0.0f, 0.0f}, env)));
        }
    }
    return 0;
}

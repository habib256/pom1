#ifndef POM1_CPU_CLOCK_H
#define POM1_CPU_CLOCK_H

// Horloge 6502 : 1.022727 MHz (quartz NTSC 14.31818 MHz / 14).
inline constexpr int POM1_CPU_CLOCK_HZ = 1022727;

// Unité de cadence de l'émulateur : une trame NTSC de diffusion, 59,94 Hz
// (60 × 1000/1001), soit 1022727 / 59,94 ≈ 17062 cycles. C'est l'unité du menu
// de vitesse et du pacer (pom1CyclesPerSecond la normalise), PAS la trame d'une
// carte vidéo : le TMS9918 a la sienne (TMS9918::kCyclesPerFrame = 17067,
// 59,92 Hz — 262 lignes non entrelacées de 342 points), le GEN2 aussi (17030).
// Le nom reste 60HZ pour compat avec les call-sites historiques.
inline constexpr int POM1_CPU_CYCLES_PER_FRAME_1X_60HZ = (1001 * POM1_CPU_CLOCK_HZ + 30000) / 60000;
inline constexpr int POM1_CPU_CYCLES_PER_FRAME_2X_60HZ = (1001 * 2 * POM1_CPU_CLOCK_HZ + 30000) / 60000;

// Cadence murale d'une vitesse exprimée en « cycles par frame » (l'unité que
// stocke le menu de vitesse) : x1 EST le quartz, les autres vitesses en
// proportion. Le pacer calculait `cpf × 60` — or le cpf de x1 (17 062) compte
// une frame à 59,94 Hz (ci-dessus), si bien que x1 tournait à 1 023 720 Hz,
// 0,097 % trop vite : la barre d'état affichait « 1.024 MHz », et les trames
// GEN2 (17 030 cycles) sortaient à 60,11 Hz au lieu de 60,05 — un saut de trame
// toutes les ~9 s au lieu de ~18 s sur un écran à 60 Hz.
inline constexpr double pom1CyclesPerSecond(int cyclesPerFrame)
{
    return static_cast<double>(POM1_CPU_CLOCK_HZ) * cyclesPerFrame
         / POM1_CPU_CYCLES_PER_FRAME_1X_60HZ;
}
static_assert(pom1CyclesPerSecond(POM1_CPU_CYCLES_PER_FRAME_1X_60HZ) == POM1_CPU_CLOCK_HZ,
              "x1 must pace at exactly the crystal clock");

// ~1 ms en cycles machine (sondes modem / terminal).
inline constexpr int POM1_CPU_CYCLES_PER_MILLISECOND = (POM1_CPU_CLOCK_HZ + 500) / 1000;

#endif // POM1_CPU_CLOCK_H

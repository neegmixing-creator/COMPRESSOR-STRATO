#pragma once
#include "DspCommon.h"

namespace cmp
{
// STRENGTH = macro de INTENÇÃO. Uma única função converte o valor (0..1) em uma "intenção" global
// que todos os estágios do motor consultam (detector, curva estática, balística, Character).
// Nenhum estágio contém fórmulas próprias de STRENGTH.
//
//   0%   PRESERVAR     dinâmica e transientes intactos (GR = 0)
//   25%  CONTROLAR     picos e eventos relevantes
//   50%  ESTABILIZAR   consistência, sustain controlado (threshold/ratio do usuário valem como ajustados)
//   75%  DENSIFICAR    mais peso na energia sustentada, release adaptativo mais ativo
//   100% CARACTERIZAR  compressão intensa, forte acoplamento com o Character
struct StrengthIntent
{
    float compressionDepth   = 0;   // 0..1  profundidade da redução (não linear: ~0 perto de 0%, 1 em 50%)
    float detectorSensitivity = 0;  // 0..1  acima de 50%: detector mais sensível e mais "pico"
    float transientPreservation = 1;// 1..0.5 quanto do alívio de transiente do modo é mantido
    float attackAdaptation   = 1;   // multiplicador do attack do usuário (baseline, antes de modo/transiente)
    float releaseAdaptation  = 1;   // multiplicador do release do usuário
    float sustainWeight      = 0;   // 0..1  peso extra da energia sustentada no nível detectado (densificar)
    float densityResponse    = 0;   // 0..1  quanto a densidade de eventos encurta a cauda lenta (anti-pumping)
    float characterCoupling  = 0;   // 0..1  quanto do CHARACTER pode aparecer; 0 em STRENGTH 0
    // deslocamentos limitados (previsibilidade do THRESHOLD)
    float thresholdDeepenDb  = 0;   // 0..6 dB
    float ratioPush          = 0;   // 0..0.85  (proporcional ao ratio do usuário: 1:1 nunca comprime)
    float kneeFirmness       = 0;   // 0..0.5
};

inline float smoothStep (float a, float b, float x) noexcept
{
    const float t = std::clamp ((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline StrengthIntent computeStrengthIntent (float s) noexcept
{
    s = std::clamp (s, 0.0f, 1.0f);
    const float u     = std::min (s * 2.0f, 1.0f);                          // 0..1 até 50%
    const float boost = std::pow (std::max (0.0f, (s - 0.5f) * 2.0f), 1.5f); // 0..1 de 50% a 100%

    StrengthIntent i;
    i.compressionDepth      = u * u;
    i.detectorSensitivity   = boost;
    i.transientPreservation = 1.0f - 0.5f * smoothStep (0.3f, 1.0f, s);
    i.attackAdaptation      = 1.3f - 0.6f * s;                              // nunca "sempre rápido": 1.3x -> 0.7x
    i.releaseAdaptation     = 1.15f - 0.4f * s;
    i.sustainWeight         = smoothStep (0.4f, 0.9f, s);
    i.densityResponse       = smoothStep (0.25f, 0.8f, s);
    i.characterCoupling     = smoothStep (0.0f, 0.75f, s);
    i.thresholdDeepenDb     = 6.0f * boost;
    i.ratioPush             = 0.85f * boost;
    i.kneeFirmness          = 0.5f * boost;
    return i;
}
} // namespace cmp

#pragma once
#include "DspCommon.h"
#include "Detector.h"
#include "Saturation.h"
#include "Oversampler.h"
#include "Meter.h"
#include "StrengthIntent.h"

namespace cmp
{
enum class Mode { Clean = 0, Punch, Smooth, Aggressive, Bus };
enum class StereoMode { Stereo = 0, Mid, Side };

struct Params
{
    float strength = 0.5f;          // 0..1  cérebro do compressor (ver AdaptiveEngine em CompressorEngine.cpp)
    float thresholdDb = -18.0f;     // -60..0
    float ratio = 4.0f;             // 1..1000 (1000 ~ infinito)
    float attackMs = 10.0f;         // 0.05..100
    float releaseMs = 120.0f;       // 10..2000
    float kneeDb = 6.0f;            // 0..24
    float makeupDb = 0.0f;          // -12..24
    float mix = 1.0f;               // 0..1
    float inputDb = 0.0f;           // -24..24
    float outputDb = 0.0f;          // -24..24
    Mode  mode = Mode::Clean;
    bool  autoAttack = false, autoRelease = false, autoGain = false, bypass = false;
    bool  adaptive = true;          // false = macro legado (projetos antigos): STRENGTH 0.5 neutro, 1 = mais forte
    float lookaheadMs = 0.0f;       // 0, 0.5, 1, 2, 5
    float sidechainHpfHz = 0.0f;    // 0 = OFF
    float character = 0.0f;         // 0..1
    SatMode satMode = SatMode::Clean;
    int   oversampling = 1;         // 1, 2, 4, 8
    float stereoLink = 1.0f;        // 0..1
    StereoMode stereoMode = StereoMode::Stereo;
};

class CompressorEngine
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;                       // limpa estados e "encaixa" os suavizadores nos alvos
    void setParams (const Params& p) noexcept;   // barato; chamar a cada bloco
    int  getLatencySamples() const noexcept { return lookaheadSamples + os.getLatency(); }
    void process (float* const* channels, int numChannels, int numSamples) noexcept;
    MeterData& getMeterData() noexcept { return meters; }

    // Curva estática (dB): redução de ganho (positiva) para um nível de entrada, com soft knee.
    static float computeGainReductionDb (float levelDb, float thresholdDb, float invRatio, float kneeDb) noexcept;

private:
    struct Path
    {
        Detector det;
        DelayLine line;                          // áudio atrasado pelo lookahead
        float y1 = 0, y = 0, grSlow = 0, transient = 0;   // transient: crest suavizado (caminho legado)
        float r[3] = { 0, 0, 0 };                          // release em 3 estágios (caminho adaptativo)
        float gMed = 0, gLong = 0;                         // GR "carregada" por 60 ms / 300 ms: alimenta os estágios médio e lento
        float density = 0;                                 // frequência de eventos (média lenta do detector de transiente)
        double dcX = 0, dcY = 0;                 // bloqueador de DC da saturação Tube
    };

    void resetDsp() noexcept;
    void updateControl() noexcept;               // taxa de controle (a cada kCtl amostras)
    static constexpr int kCtl = 16;

    double sr = 48000.0;
    bool needSnap = true, wasBypassed = false, bypassTarget = false;
    int lookaheadSamples = 0;

    Path path[2];
    DelayLine rawLine[2];
    Oversampler os;
    Saturator sat;
    MeterData meters;

    Smoother smIn, smOut, smMakeup, smMix, smByp, smThr, smInvR, smKnee, smStrength, smChar;
    Smoother smAttack, smRelease;                // em ms; avançam a cada kCtl amostras
    bool adaptive = true;
    int modeIdx = 0, ctl = 0, agCtl = 0;
    // Derivados do STRENGTH e do programa (atualizados na taxa de controle)
    StrengthIntent intent;                       // única fonte de verdade do STRENGTH (adaptativo)
    float agGate = 0;                            // gate suave do Auto Gain
    bool agOpen = false;                         // histerese do gate
    float gScale = 1;                            // profundidade (também usada pelo caminho legado)
    float aS = 0, aT = 0, rc[3] = { 0, 0, 0 };
    float densCoef = 0, eAgCoef = 0, medCoef = 0;
    float eIn = 0, eOut = 0;                     // energias (janela ~400 ms) para o Auto Gain

    // Derivados de modo/parâmetros
    float attA = 0, attB = 0, relA = 0, relB = 0;   // A = sustentado, B = transiente
    bool autoAtt = false, autoRel = false, autoGain = false;
    float linkEff = 1.0f;
    Mode mode = Mode::Clean;
    SatMode satMode = SatMode::Clean;
    StereoMode stereoMode = StereoMode::Stereo;

    // Auto gain / transientes
    float gate = 0, avgGr = 0, autoDb = 0;
    float twCoef = 0, grSlowCoef = 0, gateCoef = 0, avgCoef = 0, autoCoef = 0;
    float tubeDcR = 0.999f;
};
} // namespace cmp

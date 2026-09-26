#pragma once

// Single source of truth for every NOVA parameter.
//
// The same table drives:
//   * the host-visible AudioProcessorValueTreeState layout (automation, state save/restore)
//   * the DSP ChainSettings snapshot read once per block on the audio thread
//   * AI tool validation (safe ranges, units, max step per AI action)
//   * the UI (names, units, formatting) and the JSON schema exported to the AI engineer
//
// Parameter IDs are part of the saved-state contract: never rename an ID once shipped.

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace nova
{

enum class Module : uint8_t
{
    Global, Level, EQ, ToneMatch, DynEQ, Comp, DeEss, Color, Space, Motion, Image, Limiter, Count
};

enum class ParamKind : uint8_t { Float, Bool, Choice };
enum class Unit : uint8_t { None, dB, Hz, ms, Percent, Ratio, Seconds, Q };

struct ParamSpec
{
    const char* id;
    const char* name;
    Module module;
    ParamKind kind;
    float minValue, maxValue, defaultValue;
    bool logScale;            // frequency / time style skew
    Unit unit;
    const char* choices;      // '|' separated for Choice params
    float maxAiStep;          // largest change allowed in one AI action (0 = unlimited)
    const char* description;
};

// X(Enum, id, name, Module, Kind, min, max, def, log, Unit, choices, maxAiStep, description)
#define NOVA_PARAM_LIST(X) \
    X(InTrim,        "in_trim",          "Input Trim",            Global,   Float, -24.f, 24.f, 0.f,   false, dB,      "", 12.f, "Gain applied before the NOVA chain") \
    X(OutGain,       "out_gain",         "Output Gain",           Global,   Float, -24.f, 24.f, 0.f,   false, dB,      "", 12.f, "Final output gain") \
    X(Mix,           "mix",              "Mix",                   Global,   Float, 0.f, 100.f, 100.f,  false, Percent, "", 0.f,  "Global dry/wet (latency compensated)") \
    X(Bypass,        "bypass",           "Bypass",                Global,   Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "Host bypass (latency compensated)") \
    X(MonitorA,      "ab_monitor_a",     "Monitor A (Original)",  Global,   Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "A/B: listen to the unprocessed original") \
    X(LoudMatch,     "ab_loudness_match","A/B Loudness Match",    Global,   Bool,  0.f, 1.f, 1.f,      false, None,    "", 0.f,  "Gain-match A to B so louder never wins") \
    X(Delta,         "delta",            "Delta Monitor",         Global,   Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "Listen only to what the chain adds/removes") \
    \
    X(LvlOn,         "lvl_on",           "Level Rider On",        Level,    Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "Enable automatic phrase level riding") \
    X(LvlTarget,     "lvl_target",       "Rider Target",          Level,    Float, -40.f, -6.f, -20.f, false, dB,      "", 12.f, "Target RMS level of active audio (dBFS)") \
    X(LvlRange,      "lvl_range",        "Rider Range",           Level,    Float, 0.f, 18.f, 6.f,     false, dB,      "", 9.f,  "Maximum boost/cut the rider may apply") \
    X(LvlSpeed,      "lvl_speed",        "Rider Speed",           Level,    Float, 20.f, 3000.f, 300.f,true,  ms,      "", 0.f,  "Rider response time") \
    X(LvlGate,       "lvl_gate",         "Rider Gate",            Level,    Float, -80.f, -20.f, -50.f,false, dB,      "", 20.f, "Below this level the rider holds (no boosting breaths/noise)") \
    X(LvlAmount,     "lvl_amount",       "Rider Amount",          Level,    Float, 0.f, 100.f, 100.f,  false, Percent, "", 0.f,  "Amount of computed riding applied") \
    \
    X(EqOn,          "eq_on",            "EQ On",                 EQ,       Bool,  0.f, 1.f, 1.f,      false, None,    "", 0.f,  "Enable the corrective/tonal EQ") \
    X(HpfOn,         "eq_hpf_on",        "High-Pass On",          EQ,       Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "Enable the high-pass filter") \
    X(HpfFreq,       "eq_hpf_freq",      "High-Pass Freq",        EQ,       Float, 20.f, 600.f, 80.f,  true,  Hz,      "", 0.f,  "High-pass cutoff") \
    X(HpfSlope,      "eq_hpf_slope",     "High-Pass Slope",       EQ,       Choice,0.f, 1.f, 1.f,      false, None,    "12 dB/oct|24 dB/oct", 0.f, "High-pass slope") \
    X(LpfOn,         "eq_lpf_on",        "Low-Pass On",           EQ,       Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "Enable the low-pass filter") \
    X(LpfFreq,       "eq_lpf_freq",      "Low-Pass Freq",         EQ,       Float, 2000.f, 22000.f, 18000.f, true, Hz, "", 0.f,  "Low-pass cutoff") \
    X(EqB1On,        "eq_b1_on",         "EQ Band 1 On",          EQ,       Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "") \
    X(EqB1Type,      "eq_b1_type",       "EQ Band 1 Type",        EQ,       Choice,0.f, 2.f, 1.f,      false, None,    "Bell|Low Shelf|High Shelf", 0.f, "") \
    X(EqB1Freq,      "eq_b1_freq",       "EQ Band 1 Freq",        EQ,       Float, 20.f, 20000.f, 100.f,  true, Hz,    "", 0.f,  "") \
    X(EqB1Gain,      "eq_b1_gain",       "EQ Band 1 Gain",        EQ,       Float, -18.f, 18.f, 0.f,   false, dB,      "", 6.f,  "") \
    X(EqB1Q,         "eq_b1_q",          "EQ Band 1 Q",           EQ,       Float, 0.1f, 10.f, 0.7f,   true,  Q,       "", 0.f,  "") \
    X(EqB2On,        "eq_b2_on",         "EQ Band 2 On",          EQ,       Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "") \
    X(EqB2Type,      "eq_b2_type",       "EQ Band 2 Type",        EQ,       Choice,0.f, 2.f, 0.f,      false, None,    "Bell|Low Shelf|High Shelf", 0.f, "") \
    X(EqB2Freq,      "eq_b2_freq",       "EQ Band 2 Freq",        EQ,       Float, 20.f, 20000.f, 300.f,  true, Hz,    "", 0.f,  "") \
    X(EqB2Gain,      "eq_b2_gain",       "EQ Band 2 Gain",        EQ,       Float, -18.f, 18.f, 0.f,   false, dB,      "", 6.f,  "") \
    X(EqB2Q,         "eq_b2_q",          "EQ Band 2 Q",           EQ,       Float, 0.1f, 10.f, 1.2f,   true,  Q,       "", 0.f,  "") \
    X(EqB3On,        "eq_b3_on",         "EQ Band 3 On",          EQ,       Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "") \
    X(EqB3Type,      "eq_b3_type",       "EQ Band 3 Type",        EQ,       Choice,0.f, 2.f, 0.f,      false, None,    "Bell|Low Shelf|High Shelf", 0.f, "") \
    X(EqB3Freq,      "eq_b3_freq",       "EQ Band 3 Freq",        EQ,       Float, 20.f, 20000.f, 1000.f, true, Hz,    "", 0.f,  "") \
    X(EqB3Gain,      "eq_b3_gain",       "EQ Band 3 Gain",        EQ,       Float, -18.f, 18.f, 0.f,   false, dB,      "", 6.f,  "") \
    X(EqB3Q,         "eq_b3_q",          "EQ Band 3 Q",           EQ,       Float, 0.1f, 10.f, 1.2f,   true,  Q,       "", 0.f,  "") \
    X(EqB4On,        "eq_b4_on",         "EQ Band 4 On",          EQ,       Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "") \
    X(EqB4Type,      "eq_b4_type",       "EQ Band 4 Type",        EQ,       Choice,0.f, 2.f, 0.f,      false, None,    "Bell|Low Shelf|High Shelf", 0.f, "") \
    X(EqB4Freq,      "eq_b4_freq",       "EQ Band 4 Freq",        EQ,       Float, 20.f, 20000.f, 3500.f, true, Hz,    "", 0.f,  "") \
    X(EqB4Gain,      "eq_b4_gain",       "EQ Band 4 Gain",        EQ,       Float, -18.f, 18.f, 0.f,   false, dB,      "", 6.f,  "") \
    X(EqB4Q,         "eq_b4_q",          "EQ Band 4 Q",           EQ,       Float, 0.1f, 10.f, 1.2f,   true,  Q,       "", 0.f,  "") \
    X(EqB5On,        "eq_b5_on",         "EQ Band 5 On",          EQ,       Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "") \
    X(EqB5Type,      "eq_b5_type",       "EQ Band 5 Type",        EQ,       Choice,0.f, 2.f, 2.f,      false, None,    "Bell|Low Shelf|High Shelf", 0.f, "") \
    X(EqB5Freq,      "eq_b5_freq",       "EQ Band 5 Freq",        EQ,       Float, 20.f, 20000.f, 10000.f,true, Hz,    "", 0.f,  "") \
    X(EqB5Gain,      "eq_b5_gain",       "EQ Band 5 Gain",        EQ,       Float, -18.f, 18.f, 0.f,   false, dB,      "", 6.f,  "") \
    X(EqB5Q,         "eq_b5_q",          "EQ Band 5 Q",           EQ,       Float, 0.1f, 10.f, 0.7f,   true,  Q,       "", 0.f,  "") \
    \
    X(TmOn,          "tm_on",            "Tone Match On",         ToneMatch,Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "Enable the reference tone-match curve") \
    X(TmAmount,      "tm_amount",        "Reference Influence",   ToneMatch,Float, 0.f, 100.f, 75.f,   false, Percent, "", 0.f,  "Scales every tone-match band (0 = off, 100 = full match)") \
    X(TmG1,          "tm_g1",            "Match 50 Hz",           ToneMatch,Float, -12.f, 12.f, 0.f,   false, dB,      "", 0.f,  "") \
    X(TmG2,          "tm_g2",            "Match 120 Hz",          ToneMatch,Float, -12.f, 12.f, 0.f,   false, dB,      "", 0.f,  "") \
    X(TmG3,          "tm_g3",            "Match 300 Hz",          ToneMatch,Float, -12.f, 12.f, 0.f,   false, dB,      "", 0.f,  "") \
    X(TmG4,          "tm_g4",            "Match 700 Hz",          ToneMatch,Float, -12.f, 12.f, 0.f,   false, dB,      "", 0.f,  "") \
    X(TmG5,          "tm_g5",            "Match 1.6 kHz",         ToneMatch,Float, -12.f, 12.f, 0.f,   false, dB,      "", 0.f,  "") \
    X(TmG6,          "tm_g6",            "Match 3.5 kHz",         ToneMatch,Float, -12.f, 12.f, 0.f,   false, dB,      "", 0.f,  "") \
    X(TmG7,          "tm_g7",            "Match 7 kHz",           ToneMatch,Float, -12.f, 12.f, 0.f,   false, dB,      "", 0.f,  "") \
    X(TmG8,          "tm_g8",            "Match 13 kHz",          ToneMatch,Float, -12.f, 12.f, 0.f,   false, dB,      "", 0.f,  "") \
    \
    X(DeqOn,         "deq_on",           "Dynamic EQ On",         DynEQ,    Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "Enable dynamic (event-only) EQ bands") \
    X(DeqB1On,       "deq_b1_on",        "Dyn Band 1 On",         DynEQ,    Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "") \
    X(DeqB1Freq,     "deq_b1_freq",      "Dyn Band 1 Freq",       DynEQ,    Float, 150.f, 14000.f, 3000.f, true, Hz,   "", 0.f,  "") \
    X(DeqB1Q,        "deq_b1_q",         "Dyn Band 1 Q",          DynEQ,    Float, 0.4f, 8.f, 2.f,     true,  Q,       "", 0.f,  "") \
    X(DeqB1Thresh,   "deq_b1_thresh",    "Dyn Band 1 Threshold",  DynEQ,    Float, -70.f, 0.f, -30.f,  false, dB,      "", 20.f, "") \
    X(DeqB1Range,    "deq_b1_range",     "Dyn Band 1 Max Cut",    DynEQ,    Float, 0.f, 18.f, 6.f,     false, dB,      "", 9.f,  "") \
    X(DeqB1Attack,   "deq_b1_attack",    "Dyn Band 1 Attack",     DynEQ,    Float, 0.5f, 50.f, 3.f,    true,  ms,      "", 0.f,  "") \
    X(DeqB1Release,  "deq_b1_release",   "Dyn Band 1 Release",    DynEQ,    Float, 10.f, 500.f, 80.f,  true,  ms,      "", 0.f,  "") \
    X(DeqB2On,       "deq_b2_on",        "Dyn Band 2 On",         DynEQ,    Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "") \
    X(DeqB2Freq,     "deq_b2_freq",      "Dyn Band 2 Freq",       DynEQ,    Float, 150.f, 14000.f, 400.f,  true, Hz,   "", 0.f,  "") \
    X(DeqB2Q,        "deq_b2_q",         "Dyn Band 2 Q",          DynEQ,    Float, 0.4f, 8.f, 1.5f,    true,  Q,       "", 0.f,  "") \
    X(DeqB2Thresh,   "deq_b2_thresh",    "Dyn Band 2 Threshold",  DynEQ,    Float, -70.f, 0.f, -30.f,  false, dB,      "", 20.f, "") \
    X(DeqB2Range,    "deq_b2_range",     "Dyn Band 2 Max Cut",    DynEQ,    Float, 0.f, 18.f, 6.f,     false, dB,      "", 9.f,  "") \
    X(DeqB2Attack,   "deq_b2_attack",    "Dyn Band 2 Attack",     DynEQ,    Float, 0.5f, 50.f, 5.f,    true,  ms,      "", 0.f,  "") \
    X(DeqB2Release,  "deq_b2_release",   "Dyn Band 2 Release",    DynEQ,    Float, 10.f, 500.f, 120.f, true,  ms,      "", 0.f,  "") \
    \
    X(CompOn,        "comp_on",          "Compressor On",         Comp,     Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "Enable the compressor") \
    X(CompThresh,    "comp_thresh",      "Comp Threshold",        Comp,     Float, -60.f, 0.f, -18.f,  false, dB,      "", 18.f, "") \
    X(CompRatio,     "comp_ratio",       "Comp Ratio",            Comp,     Float, 1.f, 20.f, 3.f,     true,  Ratio,   "", 0.f,  "") \
    X(CompAttack,    "comp_attack",      "Comp Attack",           Comp,     Float, 0.1f, 100.f, 10.f,  true,  ms,      "", 0.f,  "") \
    X(CompRelease,   "comp_release",     "Comp Release",          Comp,     Float, 10.f, 1500.f, 120.f,true,  ms,      "", 0.f,  "") \
    X(CompKnee,      "comp_knee",        "Comp Knee",             Comp,     Float, 0.f, 18.f, 6.f,     false, dB,      "", 0.f,  "") \
    X(CompMakeup,    "comp_makeup",      "Comp Makeup",           Comp,     Float, -12.f, 24.f, 0.f,   false, dB,      "", 9.f,  "") \
    X(CompMix,       "comp_mix",         "Comp Mix",              Comp,     Float, 0.f, 100.f, 100.f,  false, Percent, "", 0.f,  "Parallel compression blend") \
    X(CompDetector,  "comp_detector",    "Comp Detector",         Comp,     Choice,0.f, 1.f, 1.f,      false, None,    "Peak|RMS", 0.f, "") \
    \
    X(DessOn,        "dess_on",          "De-esser On",           DeEss,    Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "Enable the de-esser") \
    X(DessFreq,      "dess_freq",        "De-ess Frequency",      DeEss,    Float, 2000.f, 14000.f, 6500.f, true, Hz,  "", 0.f,  "") \
    X(DessThresh,    "dess_thresh",      "De-ess Threshold",      DeEss,    Float, -70.f, 0.f, -30.f,  false, dB,      "", 20.f, "") \
    X(DessRange,     "dess_range",       "De-ess Max Reduction",  DeEss,    Float, 0.f, 20.f, 6.f,     false, dB,      "", 10.f, "") \
    X(DessMode,      "dess_mode",        "De-ess Mode",           DeEss,    Choice,0.f, 1.f, 0.f,      false, None,    "Split Band|Wide Band", 0.f, "") \
    \
    X(ColOn,         "col_on",           "Color On",              Color,    Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "Enable saturation / coloration") \
    X(ColDrive,      "col_drive",        "Color Drive",           Color,    Float, 0.f, 24.f, 4.f,     false, dB,      "", 9.f,  "") \
    X(ColType,       "col_type",         "Color Character",       Color,    Choice,0.f, 2.f, 0.f,      false, None,    "Tape|Tube|Soft Clip", 0.f, "") \
    X(ColWarmth,     "col_warmth",       "Color Warmth",          Color,    Float, -100.f, 100.f, 0.f, false, Percent, "", 0.f,  "Post-saturation tilt: + warmer, - brighter") \
    X(ColMix,        "col_mix",          "Color Mix",             Color,    Float, 0.f, 100.f, 100.f,  false, Percent, "", 0.f,  "") \
    \
    X(SpcOn,         "spc_on",           "Space On",              Space,    Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "Enable reverb + delay") \
    X(SpcRevMix,     "spc_rev_mix",      "Reverb Level",          Space,    Float, 0.f, 100.f, 18.f,   false, Percent, "", 40.f, "") \
    X(SpcDecay,      "spc_decay",        "Reverb Decay",          Space,    Float, 0.2f, 10.f, 1.6f,   true,  Seconds, "", 0.f,  "") \
    X(SpcPreDelay,   "spc_predelay",     "Reverb Pre-delay",      Space,    Float, 0.f, 250.f, 20.f,   false, ms,      "", 0.f,  "") \
    X(SpcSize,       "spc_size",         "Reverb Size",           Space,    Float, 0.f, 100.f, 50.f,   false, Percent, "", 0.f,  "") \
    X(SpcDamping,    "spc_damping",      "Reverb Damping",        Space,    Float, 0.f, 100.f, 45.f,   false, Percent, "", 0.f,  "High-frequency damping in the tail") \
    X(SpcLowCut,     "spc_lowcut",       "Reverb Low Cut",        Space,    Float, 20.f, 1000.f, 200.f,true,  Hz,      "", 0.f,  "") \
    X(SpcRevWidth,   "spc_rev_width",    "Reverb Width",          Space,    Float, 0.f, 100.f, 100.f,  false, Percent, "", 0.f,  "") \
    X(SpcDuck,       "spc_duck",         "FX Ducking",            Space,    Float, 0.f, 100.f, 0.f,    false, Percent, "", 0.f,  "Duck reverb/delay while the dry signal is active") \
    X(SpcDlyMix,     "spc_dly_mix",      "Delay Level",           Space,    Float, 0.f, 100.f, 0.f,    false, Percent, "", 40.f, "") \
    X(SpcDlySync,    "spc_dly_sync",     "Delay Tempo Sync",      Space,    Bool,  0.f, 1.f, 1.f,      false, None,    "", 0.f,  "") \
    X(SpcDlyDiv,     "spc_dly_div",      "Delay Division",        Space,    Choice,0.f, 8.f, 1.f,      false, None,    "1/2|1/4|1/4 dotted|1/4 triplet|1/8|1/8 dotted|1/8 triplet|1/16|1/16 triplet", 0.f, "") \
    X(SpcDlyTime,    "spc_dly_time",     "Delay Time",            Space,    Float, 1.f, 2000.f, 250.f, true,  ms,      "", 0.f,  "Used when tempo sync is off") \
    X(SpcDlyFeedback,"spc_dly_feedback", "Delay Feedback",        Space,    Float, 0.f, 95.f, 30.f,    false, Percent, "", 0.f,  "") \
    X(SpcDlyPingPong,"spc_dly_pingpong", "Delay Ping-Pong",       Space,    Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "") \
    X(SpcDlyTone,    "spc_dly_tone",     "Delay Tone",            Space,    Float, 1000.f, 20000.f, 6000.f, true, Hz,  "", 0.f,  "Delay feedback high-cut") \
    \
    X(GateOn,        "fx_gate_on",       "Rhythm Gate On",        Motion,   Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "Tempo-synced rhythmic gate / stutter") \
    X(GateDiv,       "fx_gate_div",      "Gate Division",         Motion,   Choice,0.f, 5.f, 3.f,      false, None,    "1/4|1/8|1/8 triplet|1/16|1/16 triplet|1/32", 0.f, "") \
    X(GatePattern,   "fx_gate_pattern",  "Gate Pattern",          Motion,   Choice,0.f, 4.f, 0.f,      false, None,    "Straight|Offbeat|Stutter 3-3-2|Broken|Half-time", 0.f, "") \
    X(GateDepth,     "fx_gate_depth",    "Gate Depth",            Motion,   Float, 0.f, 100.f, 100.f,  false, Percent, "", 0.f,  "") \
    X(GateSmooth,    "fx_gate_smooth",   "Gate Smoothing",        Motion,   Float, 0.5f, 30.f, 3.f,    true,  ms,      "", 0.f,  "") \
    \
    X(ImgWidth,      "img_width",        "Stereo Width",          Image,    Float, 0.f, 200.f, 100.f,  false, Percent, "", 60.f, "Mid/side width (100 = unchanged)") \
    X(ImgMonoBassOn, "img_monobass_on",  "Mono Bass On",          Image,    Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "") \
    X(ImgMonoBass,   "img_monobass_freq","Mono Bass Below",       Image,    Float, 40.f, 300.f, 120.f, true,  Hz,      "", 0.f,  "") \
    \
    X(LimOn,         "lim_on",           "Limiter On",            Limiter,  Bool,  0.f, 1.f, 0.f,      false, None,    "", 0.f,  "Enable the output limiter") \
    X(LimGain,       "lim_gain",         "Limiter Drive",         Limiter,  Float, 0.f, 24.f, 0.f,     false, dB,      "", 8.f,  "") \
    X(LimCeiling,    "lim_ceiling",      "Limiter Ceiling",       Limiter,  Float, -12.f, 0.f, -1.f,   false, dB,      "", 0.f,  "") \
    X(LimRelease,    "lim_release",      "Limiter Release",       Limiter,  Float, 1.f, 1000.f, 80.f,  true,  ms,      "", 0.f,  "")

namespace P
{
    #define NOVA_X_ENUM(e, ...) e,
    enum Index : int { NOVA_PARAM_LIST(NOVA_X_ENUM) Count };
    #undef NOVA_X_ENUM
}

extern const std::array<ParamSpec, P::Count> kParams;

constexpr int kNumEqBands = 5;
constexpr int kNumToneMatchBands = 8;
constexpr int kNumDynBands = 2;
constexpr std::array<float, kNumToneMatchBands> kToneMatchFreqs { 50.f, 120.f, 300.f, 700.f, 1600.f, 3500.f, 7000.f, 13000.f };

// Per-band parameter helpers (bands are laid out contiguously in the table)
inline int eqBandParam (int band, int offsetFromOn) { return P::EqB1On + band * 5 + offsetFromOn; }   // 0 on,1 type,2 freq,3 gain,4 q
inline int dynBandParam (int band, int offsetFromOn) { return P::DeqB1On + band * 7 + offsetFromOn; } // 0 on,1 freq,2 q,3 thr,4 range,5 att,6 rel

std::optional<int> findParamIndex (std::string_view id);
const char* moduleName (Module m);          // "level", "eq", ...
std::optional<Module> moduleFromName (std::string_view name);
const char* unitSuffix (Unit u);
int numChoices (const ParamSpec& spec);
std::string_view choiceName (const ParamSpec& spec, int index);

float clampToSpec (const ParamSpec& spec, float value);
float normalise (const ParamSpec& spec, float value);     // 0..1 (respects log scale)
float denormalise (const ParamSpec& spec, float norm01);

// A plain snapshot of every parameter. Read once per block from the APVTS atomics on the
// audio thread (no allocation), or built freely by the AI / offline renderer.
struct ChainSettings
{
    std::array<float, P::Count> v {};

    ChainSettings();                                   // defaults
    float operator[] (int i) const noexcept { return v[(size_t) i]; }
    float& operator[] (int i) noexcept { return v[(size_t) i]; }
    bool on (int i) const noexcept { return v[(size_t) i] > 0.5f; }
    int choice (int i) const noexcept { return (int) (v[(size_t) i] + 0.5f); }
};

// Processing order of the reorderable insert modules. Packed into a uint64 so the audio
// thread can read it atomically.
enum class ChainSlot : uint8_t { Level = 0, EQ, ToneMatch, DynEQ, Comp, DeEss, Color, Motion, NumSlots };
constexpr int kNumChainSlots = (int) ChainSlot::NumSlots;
using ChainOrder = std::array<ChainSlot, kNumChainSlots>;
ChainOrder defaultChainOrder();
uint64_t packChainOrder (const ChainOrder& order);
ChainOrder unpackChainOrder (uint64_t packed);
bool isValidChainOrder (const ChainOrder& order);
const char* chainSlotName (ChainSlot s);
std::optional<ChainSlot> chainSlotFromName (std::string_view name);

} // namespace nova

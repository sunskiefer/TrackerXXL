/* TrackerXXL voice library -- LOCAL REPLACEMENT for LXRXXL ZYNTH's src/param_map.h (GENERATED upstream by
 * tools/gen_params.py from the plugin's parameter list). Only the part sound.c needs is kept: the SoundDef type, the
 * option name lists of the sound parameters and the SOUND_DEFS table (each sound parameter's key, name, default,
 * number of options). The text below the line is copied verbatim from ZYNTH 9441e6a, src/param_map.h. */
#pragma once
#include <stdint.h>
#include "bl.h"
#define UNUSED __attribute__((unused))
/* ---- verbatim from upstream ---- */
typedef struct { const char *key, *name; float def; uint8_t nopts; const char *const *opts; } SoundDef;
static const char *const OPTS_SP_WAVE[] UNUSED = {"SQR", "SAW", "TRI"};
static const char *const OPTS_SP_FMODE[] UNUSED = {"BP", "LP"};
static const char *const OPTS_SP_VOICE[] UNUSED = {"ANALOG", "TRIO", "FM6", "FM4", "PHASE", "WAVE", "CHIP", "MACRO", "WHEEL", "VOICE", "PHYS", "GRAIN", "NOISE", "LXR TONE", "LXR METAL", "BAZZ", "DRONE", "TEXTURE", "FX"};
static const char *const OPTS_SP_DRIVE_POS[] UNUSED = {"PRE", "POST"};
static const char *const OPTS_SP_CURVE[] UNUSED = {"SOFT", "HARD", "FOLD"};
static const char *const OPTS_SP_FMODEL[] UNUSED = {"POLIVOKS", "DIODE"};
static const char *const OPTS_SP_SW_MODE[] UNUSED = {"BBD", "UNISON"};
static const char *const OPTS_SP_MOD_WAVE[] UNUSED = {"TRI", "SAW", "SQR", "S&H", "NOISE"};
static const char *const OPTS_SP_MOD_DEST[] UNUSED = {"PITCH", "CUTOFF", "DRIVE", "WAVE"};
static const char *const OPTS_SP_MOD_SYNC[] UNUSED = {"FREE", "TEMPO"};
static const SoundDef SOUND_DEFS[] UNUSED = {
    {"drive", "DRIVE", 0.1f, 0, 0},
    {"detune", "DETUNE", 0.0f, 0, 0},
    {"vca_env", "DECAY", 0.0f, 0, 0},
    {"vcf_env", "VCF ENV", 0.35f, 0, 0},
    {"resonance", "RESONANCE", 0.3f, 0, 0},
    {"cutoff", "CUTOFF", 0.6f, 0, 0},
    {"fm_lvl", "FM LVL", 0.0f, 0, 0},
    {"sub_lvl", "SUB LVL", 0.0f, 0, 0},
    {"noise_lvl", "NOISE LVL", 0.0f, 0, 0},
    {"lfo_rate", "MOD RATE", 0.4f, 0, 0},
    {"lfo_lvl", "MOD DEPTH", 0.0f, 0, 0},
    {"env_cv", "ENV/CV LVL", 0.0f, 0, 0},
    {"wave", "WAVE", 1.0f, 3, OPTS_SP_WAVE},
    {"fmode", "FILTER", 1.0f, 2, OPTS_SP_FMODE},
    {"voice", "VOICE", 0.0f, 19, OPTS_SP_VOICE},
    {"v1", "VOICE CTRL 1", 0.0f, 0, 0},
    {"v2", "VOICE CTRL 2", 0.0f, 0, 0},
    {"v3", "VOICE CTRL 3", 0.0f, 0, 0},
    {"v4", "VOICE CTRL 4", 0.0f, 0, 0},
    {"v5", "VOICE CTRL 5", 0.0f, 0, 0},
    {"v6", "VOICE CTRL 6", 0.0f, 0, 0},
    {"v7", "VOICE CTRL 7", 0.0f, 0, 0},
    {"v8", "VOICE CTRL 8", 0.0f, 0, 0},
    {"v9", "VOICE CTRL 9", 0.0f, 0, 0},
    {"v10", "VOICE CTRL 10", 0.0f, 0, 0},
    {"v11", "VOICE CTRL 11", 0.0f, 0, 0},
    {"v12", "VOICE CTRL 12", 0.0f, 0, 0},
    {"v13", "VOICE CTRL 13", 0.0f, 0, 0},
    {"v14", "VOICE CTRL 14", 0.0f, 0, 0},
    {"v15", "VOICE CTRL 15", 0.0f, 0, 0},
    {"v16", "VOICE CTRL 16", 0.0f, 0, 0},
    {"v17", "VOICE CTRL 17", 0.0f, 0, 0},
    {"v18", "VOICE CTRL 18", 0.0f, 0, 0},
    {"v19", "VOICE CTRL 19", 0.0f, 0, 0},
    {"v20", "VOICE CTRL 20", 0.0f, 0, 0},
    {"fm_ratio", "FM RATIO", 0.25f, 0, 0},
    {"drive_pos", "DRIVE POS", 0.0f, 2, OPTS_SP_DRIVE_POS},
    {"curve", "CURVE", 0.0f, 3, OPTS_SP_CURVE},
    {"acc_amt", "ACCENT", 0.5f, 0, 0},
    {"acc_sweep", "SWEEP", 0.35f, 0, 0},
    {"slide_time", "SLIDE TIME", 0.3f, 0, 0},
    {"vcf_atk", "VCF ATK", 0.0f, 0, 0},
    {"vcf_dec", "VCF DEC", 0.45f, 0, 0},
    {"vca_atk", "ATTACK", 0.0f, 0, 0},
    {"pitch_dec", "PITCH DEC", 0.3f, 0, 0},
    {"level", "LEVEL", 0.8f, 0, 0},
    {"pan", "PAN", 0.5f, 0, 0},
    {"fmodel", "FILTER MODEL", 0.0f, 2, OPTS_SP_FMODEL},
    {"keytrack", "KEY TRACK", 0.5f, 0, 0},
    {"sw_mode", "SWARM MODE", 0.0f, 2, OPTS_SP_SW_MODE},
    {"sw_rate", "SWARM RATE", 0.3f, 0, 0},
    {"sw_mix", "SWARM MIX", 0.6f, 0, 0},
    {"mod_wave", "MOD WAVE", 0.0f, 5, OPTS_SP_MOD_WAVE},
    {"mod_dest", "MOD DEST", 1.0f, 4, OPTS_SP_MOD_DEST},
    {"mod_sync", "MOD SYNC", 0.0f, 2, OPTS_SP_MOD_SYNC},
    {"vca_rel", "RELEASE", 0.2f, 0, 0},
    {"sustain", "SUSTAIN", 1.0f, 0, 0},
    {"vcf_sus", "VCF SUSTAIN", 0.0f, 0, 0},
    {"vcf_rel", "VCF RELEASE", 0.3f, 0, 0},
};

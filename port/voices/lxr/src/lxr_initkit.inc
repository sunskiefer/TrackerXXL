/* TrackerXXL voice library -- EXTRACT (verbatim: from the "the init kit" comment block down to just before lxr_initPattern) from LXRXXL 0.5.10 src/lxr.c (lxr02xxl b449268):
 * voice_common() and lxr_initKit(), the init kit "built from the owner's manual's own drum-synthesis recipes (ch. 11)".
 * Nothing else of lxr.c (the instance, sequencer glue, render loop) is used by the voice library. */

/* ---------------------------------------------------------------- the init kit
 * "Clear kit" reverts to the init sound (7.1). The 0.37 firmware has no init kit of its own, so this one is built from
 * the owner's manual's own drum-synthesis recipes (ch. 11): a kick, a tom, the 808 cowbell (to the manual's numbers),
 * the snare, the clap and closed/open hats. */
static void voice_common(uint8_t *b, int v)
{
    static const uint8_t pans[6] = {PAR_PAN1, PAR_PAN2, PAR_PAN3, PAR_PAN4, PAR_PAN5, PAR_PAN6};
    b[PAR_FINE1 + 2 * v] = 63;
    b[PAR_VOL1 + v] = 100;
    b[pans[v]] = 63;
    b[PAR_VOICE_DECIMATION1 + v] = 127;
    b[PAR_AUDIO_OUT1 + v] = 0;
    b[PAR_FILTER_FREQ_1 + v] = 127;
    b[PAR_FILTER_TYPE_1 + v] = 0;
    b[PAR_VOLUME_MOD_ON_OFF1 + v] = 1;
    b[PAR_VOICE_LFO1 + v] = (uint8_t)(v + 1);
    b[PAR_TRANS1_FREQ + v] = 60;
    b[PAR_VOL_SLOPE1 + v] = 25;
}

void lxr_initKit(Kit *k)
{
    uint8_t *b = k->b;
    int v;
    memset(k, 0, sizeof *k);
    memcpy(k->name, "Init    ", NAME_LEN);
    for (v = 0; v < 6; v++) voice_common(b, v);
    b[PAR_VOICE_DECIMATION_ALL] = 127;
    /* drum 1: kick (11.1): sine around 30-35, no attack, decay 20-30, slope ~25, pitch envelope alike */
    b[PAR_OSC_WAVE_DRUM1] = 0; b[PAR_COARSE1] = 32;
    b[PAR_VELOA1] = 0; b[PAR_VELOD1] = 28; b[PAR_VOL_SLOPE1] = 25;
    b[PAR_MOD_EG1] = 26; b[PAR_PITCH_SLOPE1] = 25; b[PAR_MODAMNT1] = 60;
    b[PAR_TRANS1_WAVE] = 0; b[PAR_TRANS1_VOL] = 30;
    b[PAR_FM_FREQ1] = 63; b[PAR_MOD_WAVE_DRUM1] = 0;
    /* drum 2: tom */
    b[PAR_OSC_WAVE_DRUM2] = 0; b[PAR_COARSE2] = 45;
    b[PAR_VELOA2] = 0; b[PAR_VELOD2] = 38; b[PAR_VOL_SLOPE2] = 30;
    b[PAR_MOD_EG2] = 34; b[PAR_PITCH_SLOPE2] = 30; b[PAR_MODAMNT2] = 30;
    b[PAR_TRANS2_WAVE] = 0; b[PAR_TRANS2_VOL] = 20;
    b[PAR_FM_FREQ2] = 63;
    /* drum 3: the 808 cowbell, to the manual's numbers (11.6) */
    b[PAR_MIX_MOD_3] = 1; b[PAR_FMAMNT3] = 63; b[PAR_MOD_WAVE_DRUM3] = 3; b[PAR_FM_FREQ3] = 78;
    b[PAR_OSC_WAVE_DRUM3] = 3; b[PAR_COARSE3] = 71;
    b[PAR_VELOA3] = 0; b[PAR_VOL_SLOPE3] = 3; b[PAR_VELOD3] = 35;
    b[PAR_FILTER_TYPE_3] = 2; b[PAR_RESO_3] = 70; b[PAR_FILTER_FREQ_3] = 92;
    /* snare (11.2): noise louder than the body, high-passed noise, a short exponential tail */
    b[PAR_OSC_WAVE_SNARE] = 0; b[PAR_COARSE4] = 52; b[PAR_NOISE_FREQ1] = 127; b[PAR_MIX1] = 100;
    b[PAR_VELOA4] = 0; b[PAR_VELOD4] = 45; b[PAR_VOL_SLOPE4] = 8;
    b[PAR_MOD_EG4] = 20; b[PAR_PITCH_SLOPE4] = 20; b[PAR_MODAMNT4] = 15;
    b[PAR_FILTER_TYPE_4] = 1; b[PAR_FILTER_FREQ_4] = 80; b[PAR_RESO_4] = 40;
    b[PAR_TRANS4_WAVE] = 0; b[PAR_TRANS4_VOL] = 10;
    /* clap / cymbal voice (11.3): noise at full frequency, 3-4 repeats, attack below 10, slope below 10, BP 60-80 */
    b[PAR_WAVE1_CYM] = 4; b[PAR_COARSE5] = 127;
    b[PAR_VELOA5] = 6; b[PAR_VELOD5] = 32; b[PAR_REPEAT5] = 3; b[PAR_VOL_SLOPE5] = 6;
    b[PAR_FILTER_TYPE_5] = 2; b[PAR_FILTER_FREQ_5] = 70; b[PAR_RESO_5] = 100;
    b[PAR_WAVE2_CYM] = 0; b[PAR_WAVE3_CYM] = 0;
    /* hi-hats (11.4): metallic FM, short decays (open longer than closed), slope below 10, high-pass around 120 */
    b[PAR_WAVE1_HH] = 3; b[PAR_COARSE6] = 100;
    b[PAR_MOD_OSC_F1] = 110; b[PAR_MOD_OSC_F2] = 92; b[PAR_MOD_OSC_GAIN1] = 100; b[PAR_MOD_OSC_GAIN2] = 80;
    b[PAR_WAVE2_HH] = 3; b[PAR_WAVE3_HH] = 3;
    b[PAR_VELOA6] = 0; b[PAR_VELOD6_CLOSED] = 12; b[PAR_VELOD6_OPEN] = 45; b[PAR_VOL_SLOPE6] = 5;
    b[PAR_FILTER_TYPE_6] = 1; b[PAR_FILTER_FREQ_6] = 120; b[PAR_RESO_6] = 10;
    /* the LXR-02's own: pulse widths at 50%, the FX unit off with a delay set up */
    for (v = 0; v < 6; v++) b[LXR_NUM_SOUND_PARAMS + EXT_PWM1 + v] = 64;
    b[LXR_NUM_SOUND_PARAMS + EXT_FX_TYPE] = 0;
    b[LXR_NUM_SOUND_PARAMS + EXT_FX_OUT] = 0;
    b[LXR_NUM_SOUND_PARAMS + EXT_FX_DW] = 64;
    b[LXR_NUM_SOUND_PARAMS + EXT_FX_P1] = 90;
    b[LXR_NUM_SOUND_PARAMS + EXT_FX_P2] = 1;
    b[LXR_NUM_SOUND_PARAMS + EXT_FX_P3] = 90;
    b[LXR_NUM_SOUND_PARAMS + EXT_FX_P4] = 60;
    b[LXR_NUM_SOUND_PARAMS + EXT_FX_RATIO] = 2;
}


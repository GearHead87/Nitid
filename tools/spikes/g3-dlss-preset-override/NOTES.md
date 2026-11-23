# G3: Does the newest-DLSS-model override work?

- **Result:** ⏳ run by the user, result not reported yet
- **Date:** 2026-11-23
- **Driver / Proton:** 620.73.04 / Proton Experimental (0.9.3)
- **Game:** Cyberpunk 2077 or RESIDENT EVIL requiem
- **How to run:** G1 options plus
  `DXVK_NVAPI_DRS_NGX_DLSS_SR_OVERRIDE=on DXVK_NVAPI_DRS_NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION=render_preset_latest`
- **Pass if:** the indicator shows a newer preset than the game's own.

## Report (fill in)

- Preset without override:
- Preset with override:

Afterwards clear the saved indicator once with `DXVK_NVAPI_SET_NGX_DEBUG_OPTIONS=DLSSIndicator=0,DLSSGIndicator=0`.

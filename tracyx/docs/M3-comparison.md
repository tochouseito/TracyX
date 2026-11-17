# M3 session comparison

The TracyX panel compares the open session (After) with a directory selected as Before. It reports sample counts, mean, P50, P95 and P99 for consecutive FrameMark intervals, displayed PresentMon display intervals, and normalized GPU command-list durations. Dropped presents are excluded from the distribution and reported as a warning. Percentiles use nearest rank. The GPU metric measures individual command-list events, not total frame GPU time.

The Agent reads `TRACYX_BUILD_SHA`, `TRACYX_ENVIRONMENT`, `TRACYX_GPU`, `TRACYX_DRIVER`, `TRACYX_SCENE`, and `TRACYX_MODE` when collection begins. These optional values are stored in `session.json`; older sessions remain readable. A comparison warns when required condition metadata is missing or different, when samples are insufficient or unavailable, when events were lost or could not be decoded, when presents were dropped, and when the recorded clock residual exceeds 1 ms. A differing Build SHA is expected for a Before/After comparison.

For a fair run, use the same scene, GPU, driver, settings and capture mode, and warm up both runs before recording. The percentages are descriptive measurements, not proof of a performance cause.

On 2026-11-17, the Viewer loaded two actual probe sessions (`System-1` as Before and a 600-frame PIX Timing run as After). The comparison panel displayed 599 frame intervals per session, 1,165/1,163 displayed Present intervals, and 3,600 GPU work samples per session. It reported Frame P95 +1.3%, Present P95 -0.1%, and GPU work P95 +48.1%, along with warnings for missing condition metadata and dropped presents. These sessions were chosen to exercise the UI; their differing capture conditions make the percentages unsuitable as a performance comparison.

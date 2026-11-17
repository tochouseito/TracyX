# M3 session comparison

The TracyX panel compares the open session (After) with a directory selected as Before. It reports sample counts, mean, P50, P95 and P99 for consecutive FrameMark intervals, PresentMon display intervals, and normalized GPU work durations. Percentiles use nearest rank. The GPU work metric measures individual work events, not total frame GPU time.

The Agent reads `TRACYX_BUILD_SHA`, `TRACYX_ENVIRONMENT`, `TRACYX_GPU`, `TRACYX_DRIVER`, `TRACYX_SCENE`, and `TRACYX_MODE` when collection begins. These optional values are stored in `manifest.json`; older sessions remain readable. A comparison warns when required condition metadata is missing or different, when samples are insufficient or unavailable, when events were lost or could not be decoded, when presents were dropped, and when the recorded clock residual exceeds 1 ms. A differing Build SHA is expected for a Before/After comparison.

For a fair run, use the same scene, GPU, driver, settings and capture mode, and warm up both runs before recording. The percentages are descriptive measurements, not proof of a performance cause.

# M3 optional diagnostics

The Agent enables the DirectStorage ETW provider when available. Correlated read requests and queue submissions appear in separate DirectStorage queue lanes. A read span measures enqueue to completion, including time in the DirectStorage queue; it is not a disk-only latency measurement. If provider activation fails, the Agent logs the Win32 status and keeps the D3D12 capture running. A session with no DirectStorage events may simply have performed no DirectStorage I/O.

The D3D12 API-object callback records committed, placed and reserved resource creations and resource destruction. The library assigns monotonically increasing object IDs for the capture, so reuse of an underlying API handle does not merge two lifetimes. Resource lanes are keyed by these IDs. Captures that start after resource creation can have a destroy without a matching create. The first 48 visible lanes are rendered in the current timeline view.

In a 600-frame D3D12 Present Probe run, the Agent saved 8 resource-create events, 8 resource-destroy events, and 13 residency events. The probe did not issue DirectStorage requests, so the DirectStorage lane has not been exercised with live I/O.

For DRED, the target application calls `tracyx::CaptureDred(device, sessionDirectory)` in the device-removal path. It writes a small `dred.txt` sidecar with the QPC timestamp, HRESULT, page-fault virtual address (if reported), and auto-breadcrumb node count (if enabled). The Agent imports it as a runtime-error event when collection stops. DRED settings must be enabled before device creation to obtain breadcrumb/page-fault data. An external ETW Agent cannot call DRED on a device in another process. Use a fresh session directory for each capture.

The DRED event links to the timeline through its timestamp; it does not claim a specific resource caused removal. Page-fault VA and breadcrumb count are evidence for subsequent PIX or application investigation, not a complete DRED dump.

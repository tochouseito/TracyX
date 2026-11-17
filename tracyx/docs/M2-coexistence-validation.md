# M2 同時動作検証記録（Issue #14）

2026-11-17、Windows 11、Visual Studio 18 Debug、PIX 2603.25、PresentMon SDK Service 2.6.0、WinPixEventRuntime 1.0.240308001 で実測した。2 つの可視 Window／Swap Chain を持つ `TracyXProbePresentD3D12` を対象とした。Probe は各実行で 600 Frame、Tracy Capture は約 6 秒。Agent は対象 PID を追跡し、対象終了後に `session.json` を保存した。

## Mode と結果

| Mode | 起動構成 | FrameMark アンカー | 外部イベント | PresentMon Frame | ETW loss / Agent drop / Bridge 欠番 / decode error | PIX |
| --- | --- | ---: | ---: | ---: | --- | --- |
| Light | Probe + Tracy Capture | 600 | なし | なし | 対象外 | 要求なし |
| System | Probe + Agent + Tracy Capture | 600 | 12,581 | 1,168 | すべて 0 | 要求なし |
| Deep | Probe + Agent + Tracy Capture + GPU Capture | 600 | 14,059 | 1,194 | すべて 0 | Frame 300 開始の `.wpix` を生成 |
| Deep、PIX SDK なし | Probe + Agent + Tracy Capture | 600 | 12,581 | 1,168 | すべて 0 | 利用不可理由を `pix-captures.jsonl` に記録 |

Light は Agent を起動せず Tracy と Bridge のみ、System は Agent を追加、Deep はさらに PIX GPU Capture を要求する構成。モードは独立した実行構成であり、実行中に GPU Capturer を後から有効化する操作は含まない。PIX SDK なしの Deep 要求でも Probe、Agent、Tracy Capture は終了コード 0 で完了した。SDK 不在は `WinPixEventRuntime SDK unavailable; configure TRACYX_WINPIX_ROOT and rebuild` として記録し、収集を続けた。

Deep の PIX Capture は QPC 区間で Frame 300～310 の 11 アンカーと重なり、同区間に PresentMon イベント 22 件があった。Viewer は Capture の開始 Frame を明示的に、残りを QPC の候補リンクとして表示する。候補リンクは因果関係の証明ではなく、Capture と同じ時刻範囲の Frame を探す手掛かりである。Viewer で 317 アンカーと 7,102 外部イベントが Tracy 記録区間に整列し、6,357 件は記録区間外として表示された。ETW の session buffer loss は Provider 個別の loss を意味しない。

## 競合、再接続、終了

同じ PID とプロセス開始時刻を追う Agent を二重起動すると、以前は live named pipe の `CreateNamedPipeW` が Win32 231 で競合した。Agent 起動時に対象別の `Global\\TracyX.Agent.<PID>.<start-time>` mutex を確保するよう変更した。再実行では片方の Agent が終了コード 0、もう片方が終了コード 3 と `Agent already collecting PID ...` を返した。先に起動した Agent は収集を続け、ETW loss／Agent drop は 0 だった。別 PID の Agent はこの mutex によって制限されない。

900 Frame の実行中に live named pipe を 2 回接続し直した。2 回とも protocol version 4 と 128 イベントを受信した。対象終了時に Agent も正常終了し、18,880 外部イベント、1,767 PresentMon Frame、ETW loss／Agent drop／Bridge 欠番 0 を保存した。複数 Window の Present は Swap Chain ID ごとに別レーンに表示された。収集開始前の Present があるため、2 Swap Chain × 600 Frame と PresentMon Frame 数の差を loss と解釈しない。

## 権限と残る確認

ETW Session の開始には Windows の権限が必要であり、権限不足は Agent の `StartTraceW`／`EnableTraceEx2` エラーで判別する。PIX Timing Capture は管理者権限が必要で、非管理者の失敗理由の記録と管理者実行での成功を確認した。管理者での 600 Frame 実行では Probe、Agent、Tracy Capture が終了コード 0、外部イベント 12,621 件、PresentMon Frame 1,167 件、ETW loss／Agent drop／Bridge 欠番／D3D12・DXGK decode error はすべて 0 だった。Frame 300 の Timing `.wpix` は PIX 2603.25 の Timeline で開けた。Deep 実行の DxTimingCaptureLibrary は Pipeline State Compilation Event の API Object ID がない旨の診断を 1 件 stderr に出したが、記録された decode error と loss は 0 だった。この診断の影響は追加調査が必要。

本記録は短い Probe 実行の結果であり、長時間・高負荷のオーバーヘッドを保証しない。TracyX Viewer の「Open in PIX」から Timing `.wpix` を起動でき、Frame 300～310 の候補リンクと PIX Timeline を確認した。GPU Capture と Timing Capture は別々の対象実行で検証した。

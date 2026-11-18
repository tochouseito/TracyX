# M4 Windows x64 回帰記録

2026-11-18、Windows 11 x64／Visual Studio 18 2026 で実施。固定版 DxTimingCaptureLibrary、PresentMon 2.6.0 SDK Service、WinPixEventRuntime 1.0.240308001 を使用した。数値はこのマシンの実測であり、他の環境での性能保証ではない。

## 完了した確認

| 対象 | 操作と結果 |
| --- | --- |
| Core／保存形式 | Debug／Release `TracyXCoreTests` 各 1/1 成功。世代別イベントファイル、旧 `events.jsonl` の読込、イベント欠損・不正パス・不正 Live Batch の拒否、公開失敗時の旧セッション保持を確認。実際の 75,634 件の保存セッションも `LoadSession` で読めた。 |
| Agent／対象の分離 | D3D12 Probe を途中で強制終了しても Agent は 1434 件を保存して終了コード 0。ETW 開始権限がない実行では `StartTraceW failed: 5` を表示して Agent のみ終了し、対象 Probe は継続。 |
| 二重起動／不正入力 | 同じ PID の 2 個目の Agent は理由を表示して終了コード 3。最初の Agent と対象は終了コード 0。数字以外の PID、存在しない PID、不正なイベント上限を拒否。 |
| ファイル境界 | `events_file` と `.tracy` はセッション内の通常ファイル名だけを許す。PIX index の `../outside.wpix` は書込・読込とも拒否する Core テストが成功。Viewer の Existing PIX 添付は選択ファイルをセッション内へコピーして相対名を記録する。 |
| 実セッション／Live 再接続 | 3600 Frame の D3D12 Probe、Agent、Tracy Capture を同時実行。3 回の Pipe 接続・切断・再接続で magic／version が一致。対象・Agent・Capture はすべて終了コード 0。 |
| Agent 再起動 | 3600 Frame の対象の実行中に 1 個目の Agent を強制停止し、残った ETW セッションを停止して 2 個目を起動。対象と 2 個目の Agent は終了コード 0、保存イベント 68,834 件、ETW loss／Agent drop 0。 |
| データ整合性 | 上記セッションのイベント 75,634 件と JSONL 行数が一致し、イベント ID は厳密昇順。Frame anchor 3600 件、PresentMon frame 7168 件、Queue ID 5 種、Swap Chain ID 2 種。ETW loss、Agent drop、D3D12／DXGK decode error はすべて 0。 |
| Tracy と表示メトリクス | 同時 Capture で `tracy.tracy` を保存。Agent は `.tracy` とイベントファイルを参照する `session.json` を保存。PresentMon 指標のイベントが同じセッションに入ることを確認。 |
| ZIP 配布 | Debug ZIP に Agent、Viewer、Capture、任意 Probe、SDK、導入手順、第三者ライセンスを収録。170 ファイルの SHA-256 再計算で不一致 0。展開した ZIP の Probe＋Agent＋Capture で実セッションを保存。 |
| Release ZIP | Agent、Probe、Capture、Viewer を Release でビルド。ZIP は 171 ファイル、SHA-256 不一致 0。ZIP 内の実行ファイルで 900 Frame を収集し、対象・Agent・Capture は終了コード 0。Live Pipe は 3 回再接続でき、18,909 件の保存・再読込に成功。ETW loss／Agent drop は 0。 |
| Viewer オフライン表示 | 上記 Release セッションの `tracy.tracy` を Viewer で直接開き、`session.json` と 18,909 件の外部イベントを自動読込。DX キュー 5 本と Present の 2 Swap Chain レーンを表示し、Tracy／ETW 時刻の整列を確認。Present イベントの Inspector に表示間隔 16.652 ms、GPU busy 0.374 ms、表示遅延 298.863 ms、Tracy Frame との時間重複が表示された。 |
| Viewer Live 接続 | Release ZIP の Probe と Agent を実行中、同じ ZIP の Viewer を `-a 127.0.0.1` で接続。Frame 数が 1,277 まで増加し、DX パネルには外部イベント 25,375 件、整列済み 23,938 件が表示された。その後、検証用 Probe を終了して Agent が 25,838 件、ETW loss／Agent drop 0 で保存した。 |
| Tracy／DX／PIX Timing 統合 | 既存の 600 Frame Timing セッションを Release Viewer で開き、12,621 件の外部イベント、5 本の DX キュー、2 本の Present レーンを表示。PIX Capture は Frame 300 の候補リンクとして ready と表示され、「Open in PIX」で同じ `.wpix` を PIX 2603.25 の Timeline に開けた。 |
| DirectStorage／FrameMark 統合 | 追加した `TracyXProbeDirectStorage` を固定 DirectStorage SDK 1.3.0 と Debug でビルドし、Release ZIP の Agent／Capture と同時実行。17,040 byte の実読込 600 件、Submit 600 件、Frame anchor 601 件、イベント 3,644 件を M4 形式で保存した。Tracy Capture は 603 Frame。対象・Agent・Capture は終了コード 0、ETW loss／Agent drop／Bridge 欠番／decode error は 0。事前に Microsoft `HelloDirectStorage`（commit `303a7d239e27c615281d6304ed13f01dc35061ef`）を一時的に計測用に拡張して同じ経路も確認した。Viewer は `.tracy` と M4 形式の `events_file` を自動読込し、外部イベント 2,997 件の時刻を整列、DirectStorage queue 2 のレーンを表示。Storage Submit の Inspector では Frame 202 との時間重複と時刻不確かさ 603 ns を確認した。 |
| 制御した device removal | 同じ `TracyXProbeDirectStorage` 実行で 600 回の読込完了後に `ID3D12Device5::RemoveDevice` を呼び、対象内の `CaptureDred` が `dred.txt` を保存。`GetDeviceRemovedReason` は `0x887A0005`。Agent は DRED イベント 1 件を含む 3,644 件を保存した。page-fault VA と breadcrumb node 数はともに 0 であり、GPU fault の証拠とは解釈しない。 |
| 高負荷 PIX GPU Capture | Release の PIX 計測を有効にして 3600 Frame の D3D12 Probe、Agent、Tracy Capture を同時実行。Frame 1800 の `.wpix` を保存し、PIX index は開始・完了とも成功、Begin／End の HRESULT は 0。84,144 件の外部イベント、Frame anchor 3600 件、PresentMon 7194 Frame、Tracy 3012 Frame を保存。ETW loss／Agent drop／Bridge 欠番／decode error／PresentMon drop は 0。 |
| 一般 C++ アプリ | Release ZIP の `TracyXProbeCpp.exe` と `tracy-capture.exe -s 1` で 256,335 byte の `.tracy` を保存し、両者は終了コード 0。メモリプールの未解放 0、失敗割当 1 件。 |
| パッケージ失敗 | Vendor ライセンスのない入力で生成を中断し、不完全なディレクトリと ZIP が残らないことを確認。 |
| メモリアダプタ | ZIP の C++ Probe で named pool と arena reset を実行し、終了時の未解放は 0。失敗割当 1 件を記録。Debug 単発の中央値は無計測 184.5 ns/op、計測あり 2191.6 ns/op。 |

## DirectStorage／DRED の再現

Microsoft.Direct3D.DirectStorage 1.3.0 NuGet パッケージの `native` ディレクトリを用意し、下記のオプションで検証用 Probe をビルドする。既定のビルドと配布 ZIP にはこの Probe を含めない。対象の実行中に、同じセッションディレクトリへ Release ZIP の Agent と Tracy Capture を接続する。

```powershell
$sdk = 'C:\path\to\Microsoft.Direct3D.DirectStorage.1.3.0\native'
cmake -S tracyx -B build/tracyx-m4-ds -G 'Visual Studio 18 2026' -A x64 -DTRACYX_BUILD_DIRECTSTORAGE_PROBE=ON "-DTRACYX_DIRECTSTORAGE_ROOT=$sdk"
cmake --build build/tracyx-m4-ds --config Debug --target TracyXProbeDirectStorage
$session = Join-Path $env:TEMP 'TracyX-M4-DirectStorage'
$bin = 'C:\path\to\TracyX-1.0.0-win-x64\bin'
$probe = Start-Process build/tracyx-m4-ds/Debug/TracyXProbeDirectStorage.exe -ArgumentList @("`"$session`"", '600', 'remove') -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 350
$agent = Start-Process (Join-Path $bin 'TracyXAgent.exe') -ArgumentList @($probe.Id, "`"$session`"") -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 350
& (Join-Path $bin 'tracy-capture.exe') -o (Join-Path $session 'tracy.tracy') -a 127.0.0.1 -s 15
$probe.WaitForExit()
$agent.WaitForExit()
```

`remove` を省くと device removal を起こさずに DirectStorage／FrameMark だけを確認できる。`session.json` の `events_file`、`frame-anchors.jsonl`、`dred.txt`、`tracy.tracy` を同じディレクトリに保管し、Viewer で `.tracy` を開く。

## 制限と次の確認

- Viewer のオフライン読込と Present Inspector は UI で確認した。Live 接続と Frame／外部イベントの更新も確認したが、Probe の起動パスを変えると Windows ファイアウォールの許可画面が再表示され、Live Inspector の操作は完了していない。セキュリティ画面は自動操作していない。
- DirectStorage 実 I/O、Tracy FrameMark、Agent 保存、Viewer レーン表示、および制御した device removal から DRED 取込までを同じ検証用アプリで確認した。高負荷 PIX GPU Capture は別の 3600 Frame セッションで確認した。自然発生の GPU fault／TDR と高負荷 PIX Capture を同時に起こす検証は未実施。PIX Timing の Viewer リンクから PIX Timeline への移動も確認した。
- Live Pipe の同一ログオン接続は確認した。異なる Windows ログオンセッションからの拒否は実機で未確認。ACL はログオン SID と SYSTEM のみ、リモートクライアントは拒否する設定。
- Release ZIP のビルド・SHA-256 検証はクリーンな GitHub Windows runner でも成功した。新しい Windows 環境での Visual C++ Runtime、PresentMon Service、PIX のセットアップと、公開 GitHub Release への添付ジョブは未確認。公開 Release の添付経路はタグ `tracyx-v*` に限定して用意した。
- コミット `ca8516f1` の GitHub CI は Windows パッケージ、Windows Agent／Viewer、macOS、Linux、Android、Emscripten、MinGW、AppImage、マニュアルを含む全チェックに成功。Release 添付ジョブはタグ付き公開時のみ実行するため、このブランチでは対象外。
- メモリアダプタの値は Debug 単発であり、Viewer 接続中や高負荷時の上限を示さない。M3 の [安定性・負荷検証](M3-stability-validation.md)に 600 Frame 3 回と 3600 Frame の測定がある。

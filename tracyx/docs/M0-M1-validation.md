# M0 / M1 検証記録（2026-11-16）

## 実行環境と固定版

- Windows 11 Pro 10.0.26200、Visual Studio 18.10、x64 Debug。
- Tracy `aa5240ca19dd4978784b76f769f067cdda647615`、DxTimingCaptureLibrary `72bd2b18553493aa9850e434caf71bfa18952aac`。
- D3D12 Probe は Direct / Compute / Copy の 3 Queue、3 フレーム同時進行、FrameMark、Tracy CPU/GPU Zone、WinPixEventRuntime マーカーを使用。
- PresentMon 2.6.0 SDK Service を公式 MSI から導入。PIX アプリ本体は未導入。Runtime マーカーの有効化と PIX アプリによる Capture 検証は別の条件として扱う。

## 検証コマンド

ビルド手順は [README](../README.md) に記載。Probe と Agent / Capture は別の PowerShell で起動する。Probe の出力から PID を取得し、対象が終了する前に Agent を起動する。

```powershell
build/tracyx/Debug/TracyXProbeD3D12.exe 300 C:\temp\TracyXSession
build/tracyx/Debug/TracyXAgent.exe <PID> C:\temp\TracyXSession
build/tracyx/Debug/TracyXProbePresentMon.exe <PID>
build/capture/Debug/tracy-capture.exe -o C:\temp\TracyXSession\tracy.tracy -a 127.0.0.1 -s 4
build/profiler/Debug/tracy-profiler.exe C:\temp\TracyXSession\tracy.tracy
```

`ctest --test-dir build/tracyx -C Debug --output-on-failure` は 1/1 成功。TracyX Core、Agent、Probe、Tracy Capture、Viewer の Debug ビルドは成功。

## 観測値

| 条件 | 結果 |
| --- | --- |
| Capture State 追加前の D3D12 Probe 300 Frame | 900 Queue submit を Probe が発行、Bridge drop 0。Agent は 2,112 イベント、D3D12 ETW 8,299 records、DXGK ETW 236,763 records、ETW buffer loss 0、デコード失敗 0、Agent drop 0、Bridge 欠番 0。QueueSubmit の復元は失敗。 |
| Provider Capture State を追加した Agent 単独 | 300 Frame で外部イベント 5,834 件、QueueSubmit 873 件、GPU Command List 2,910 件、ETW buffer loss / Agent drop / デコード失敗 0。開始前の提出は捕捉されない。 |
| PresentMon SDK Service と Agent が同じ PID を追跡 | SDK の Open / Track / Stop / Close はすべて成功。300 Frame、QueueSubmit 900 件、GPU Command List 3,000 件、外部イベント 6,012 件、ETW buffer loss / Agent drop / Bridge 欠番 / デコード失敗 0。 |
| Tracy + PresentMon SDK + Agent + PIX Runtime の最終同時実行 | 400 Frame／1,200 Queue submit。Tracy は 4.47 秒・131 Frame・130 Zone を記録。Agent は外部イベント 8,012 件、QueueSubmit 1,200 件、GPU Command List 4,000 件、D3D12 ETW 11,298 records、DXGK ETW 390,616 records。ETW buffer loss / Agent drop / Bridge 欠番 / デコード失敗 0。Viewer で Queue 集約レーン、選択 Command List の詳細レーン、Inspector を表示。 |
| Tracy + Agent 同時収集 600 Frame | 4.51 秒の `.tracy` に 132 Frame、外部イベント 4,212 件。130 アンカーで時刻合わせ、774 イベントを配置、2,838 イベントはアンカー範囲外。全区間直線近似の最大残差は約 8.7 ms。 |
| WinPixEventRuntime マーカー + Tracy + Agent 180 Frame | 540 Queue submit、外部イベント 1,272 件、ETW buffer loss / Agent drop / Bridge 欠番いずれも 0。Tracy 記録は 103 Frame。 |
| Agent 上限 32 イベント | 保存 32 イベント、Agent drop 820。欠損値はセッションに記録。 |
| Live Viewer | Bridge アンカー 145 件を受信し、133 件で時刻合わせ、654 外部イベントを配置。Viewer 側 drop 0。 |
| 保存セッションの Viewer 読込 | Inspector で Source / Clock / Raw・Tracy 時刻 / 不確かさ / 識別子 / Frame 候補を表示。イベントから Tracy 範囲への Zoom を確認。 |

## 判定と残る制約

- GitHub の正式 Fork を維持し、Viewer の追加 UI を `profiler/src/tracyx/` に集める方針を継続する。
- RAW QPC と FrameMark アンカーから時刻を合わせる。外挿せず、アンカー範囲外を未同期として数える。Frame への時間重なりは候補、Bridge の Frame ID は確定リンクとして区別する。
- DxTimingCaptureLibrary の公式 Basic サンプルと同じ Provider Capture State を追加すると、対象の起動後に収集を開始しても GPU Queue / CommandList の大部分を復元できた。一部 PSO の API Object ID は得られず、Agent 起動前の提出は記録されない。
- ETW の `EventsLost` はセッション全体の値で、Provider 別に帰属できない。Provider 別の受信 records とデコード失敗を別々に記録する。
- [PresentMon SDK Service](https://github.com/GameTechDev/PresentMon/blob/main/README-Service.md) の同時追跡を実測した。Probe は SDK 接続と対象追跡の確認用で、表示メトリクスの取得・時刻対応は未検証。PIX は WinPixEventRuntime のマーカーまで検証し、アプリ本体による Capture の起動・読込は検証範囲から外した。
- 複数 Frame 系列の識別子・保存・Viewer 切替は実装した。複数 Swap Chain の実アプリでの計測と長時間・高負荷下の Viewer 応答性は今後の検証対象。

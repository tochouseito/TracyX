# TracyX 統合プロファイラ

Tracy の Client–Viewer 通信と `.tracy` 形式は変更しない。TracyX は別プロセスで DX ETW を収集し、QPC を介して FrameMark と対応させる。Viewer は `session.json` と `events.jsonl` を `.tracy` の隣から読み取り、DX イベントを追加の時間レーンに表示する。

## 固定版と依存関係

| 対象 | 固定版 | 用途 |
| --- | --- | --- |
| Tracy upstream | `aa5240ca19dd4978784b76f769f067cdda647615`（v0.15.0） | Client、Capture、Viewer |
| [DxTimingCaptureLibrary](https://github.com/microsoft/DxTimingCaptureLibrary) | `72bd2b18553493aa9850e434caf71bfa18952aac` | ETW の DX イベント復元。MIT License |
| D3D12 Agility SDK | NuGet `Microsoft.Direct3D.D3D12` `1.619.5` | DxTimingCaptureLibrary のビルド |
| WinPixEventRuntime | NuGet `1.0.240308001`（任意） | Probe の PIX マーカーと Capture API。MIT License |
| Microsoft PIX | Windows にインストール済みの版（Capture 実行時） | GPU／Timing Capturer と `.wpix` の表示 |
| PresentMon SDK Service | [公式 v2.6.0](https://github.com/GameTechDev/PresentMon/releases/tag/v2.6.0)（任意） | Present の Frame Query。SDK Service／API は同梱しない |

DxTimingCaptureLibrary は TracyX に同梱しない。ライブラリを別ディレクトリに Clone し、上記コミットを Checkout して `DxTimingCaptureLibrary.sln` の `Debug|x64` を NuGet restore 付きでビルドする。Viewer は Tracy の既存 CMake 依存パッケージを使用する。Tracy と DxTimingCaptureLibrary のライセンス・第三者通知はそれぞれのルートにあるファイルを参照する。

## Windows Debug ビルド

Visual Studio 18、Windows SDK、CMake を使用する例。

```powershell
git clone https://github.com/microsoft/DxTimingCaptureLibrary.git C:\dev\DxTimingCaptureLibrary
git -C C:\dev\DxTimingCaptureLibrary checkout 72bd2b18553493aa9850e434caf71bfa18952aac
msbuild C:\dev\DxTimingCaptureLibrary\DxTimingCaptureLibrary.sln /restore /p:Configuration=Debug /p:Platform=x64

cmake -S tracyx -B build/tracyx -G "Visual Studio 18 2026" -A x64 -DTRACYX_DX_TIMING_ROOT=C:/dev/DxTimingCaptureLibrary
cmake --build build/tracyx --config Debug
ctest --test-dir build/tracyx -C Debug --output-on-failure

cmake -S capture -B build/capture -G "Visual Studio 18 2026" -A x64
cmake --build build/capture --config Debug
cmake -S profiler -B build/profiler -G "Visual Studio 18 2026" -A x64
cmake --build build/profiler --config Debug
```

`TracyXAgent` は `TRACYX_DX_TIMING_ROOT` を指定した場合のみビルドする。DxTimingCaptureLibrary の NuGet restore 後、Agility SDK Include が既定の NuGet キャッシュ以外にある場合は `TRACYX_D3D12_AGILITY_INCLUDE` を設定する。
PIX NuGet が既定のキャッシュにある場合、Probe に PIX マーカーと DLL を自動追加する。別の場所なら `TRACYX_WINPIX_ROOT` を指定する。
PIX Capture の対象は `TracyXProbePresentD3D12.exe 40 <session-directory> gpu` の第 3 引数で有効にする。`timing` も選べるが管理者権限が必要。第 4 引数で Capture 開始 Frame を指定できる。Tracy の記録開始後に PIX Capture を含めるには、例えば `600 <session-directory> gpu 300` を使う。GPU Capturer はデバイス作成前にロードするので、実行中プロセスへ後から切り替える場合は再起動が必要。Capture の手順と検証結果は [PIX Capture 検証記録](docs/M2-pix-capture.md) を参照。
PresentMon SDK Service をインストールしている場合、`-DTRACYX_PRESENTMON_SDK_ROOT="C:/Program Files/Intel/PresentMon/SDK"` を CMake configure に追加する。`TracyXAgent` に PresentMon Frame Query を組み込み、`TracyXProbePresentMon` もビルドする。SDK がない構成でも Agent と Tracy は動作し、Viewer に PresentMon の利用不可理由を表示する。`TracyXProbePresentD3D12.exe 360 <session-directory>` は可視ウィンドウに 2 つの Swap Chain を表示する。非表示で起動すると表示結果の計測値が変わるため、可視状態で検証する。

## 計測手順

1. `TracyXProbeD3D12.exe 600 C:\temp\TracyXSession` を起動する。Direct、Compute、Copy の 3 Queue、3 フレーム in-flight の D3D12 サンプル。`FrameMark` と前後の QPC は上限付きメモリ Queue から専用 Thread が `frame-anchors.jsonl` に書く。PIX NuGet がある場合は CPU／GPU PIX マーカーも発行する。一般 C++ の Tracy 単体動作は `TracyXProbeCpp.exe` で確認できる。
2. 対象 PID に対して `TracyXAgent.exe <PID> C:\temp\TracyXSession` を起動する。ETW の Provider を有効化し、RAW QPC を指定する。ETW セッションの開始権限がない場合は `StartTraceW` / `EnableTraceEx2` のエラーを表示して終了する。収集上限の検証には第 3 引数で最大イベント数を指定できる。
3. 対象が動作中に `tracy-capture.exe -o C:\temp\TracyXSession\tracy.tracy -a 127.0.0.1 -s 4` で Tracy を記録する。Agent は対象終了後に `session.json` と、その `events_file` が指す `events-*.jsonl` を保存する。
4. `tracy-profiler.exe C:\temp\TracyXSession\tracy.tracy` で開く。同じディレクトリの統合セッションを自動読込する。別ディレクトリを表示する場合は「TracyX DX」の Session directory に指定する。

Live 表示では Viewer を `tracy-profiler.exe -a 127.0.0.1` で起動する。Agent はローカル専用の `\\.\pipe\TracyX.<PID>` で上限 128 件のバッチを送る。TracyX DX は接続先 PID を使ってパイプを開き、受信済みアンカーの範囲だけ同期する。Agent が落ちても対象プロセスは独立して動作する。Viewer は ETW loss、Agent の Queue drop、Bridge の欠番、パイプ backlog、Viewer 側 drop、D3D12／DXGK の受信件数とデコード失敗、PresentMon の状態・取得数・Dropped 数を表示する。ETW の buffer loss は Provider 別に帰属できないためセッション全体の値として示す。

Agent の ETW Timestamp は `PROCESS_TRACE_MODE_RAW_TIMESTAMP` と QPC ベースの ETW セッションで受け取り、DxTimingCaptureLibrary が復元したイベント時刻は絶対ナノ秒で記録する。時刻を黙って推定せず、複数の FrameMark アンカーが得られない場合や PID が一致しない場合は DX 時刻合わせを無効にする。Capture が開始前のフレームを欠いていても間隔から開始位置を探索する。Viewer はアンカー区間外のイベントを未同期として数え、ETW loss と Agent drop を表示する。ETW buffer loss と D3D12／DXGK のデコード失敗は別の値であり、合算しない。

## セッション形式

- `tracy.tracy`: 純正 Tracy 記録。外部イベントを混入しない。
- `session.json`: 形式版 `1`、PID とプロセス開始時刻、QPC 周波数・原点、ETW buffer loss、D3D12／DXGK の受信件数とデコード失敗、Queue drop、Bridge 欠番、イベント件数、参照する Tracy ファイル。
- `events-*.jsonl`: イベントごとの版、Source、Clock domain、原時刻、相対時刻、誤差、Process／Thread／Device／Queue／CommandList／Resource ID、Frame Link、Label。`session.json` の `events_file` が参照する。従来の `events.jsonl` も読み込める。
- PresentMon イベントは Swap Chain ID、表示結果、表示間隔・GPU Busy・Display Latency を追加する。時間値はミリ秒から整数ナノ秒に変換し、欠損値は 0 と区別する。表示されるフレームには Present から Display までの区間も記録する。古い形式版 `1` のセッションは新しい項目なしで読み込める。
- `frame-anchors.jsonl`: Probe が FrameMark 前後の QPC、系列 ID、連番を書いた Bridge 入力。複数系列は Viewer の Frame series メニューで切り替える。
- `pix-captures.jsonl`: PIX Capture の種類、開始 Frame ID、開始／終了 QPC、HRESULT、保存ファイル、失敗理由。`.wpix` 本体は同じセッション内に置く。Viewer の「Existing .wpix」で選んだファイルはセッション内にコピーして関連付ける。Viewer の「Open in PIX」は Windows の `.wpix` 関連付けを使用する。

未知版、欠損した必須ファイル、不正なイベント行、件数不一致、参照先 `.tracy` の欠損を読込時にエラーとして扱う。保存中に失敗した場合は新しい `session.json` を公開せず、以前の有効なセッションを保持する。

## 実測と未完了

2026-11-16 に Windows / Visual Studio 18 で D3D12 Probe、RAW QPC Agent、Tracy Capture を同時実行した。600 Frame 実行中の 4.51 秒の Tracy 記録に 132 Frame、外部イベント 4,212 件、ETW loss 0、Agent drop 0 を得た。Viewer では 130 件のアンカーで時刻対応し、アンカー範囲内の 774 件を描画、区間外の 2,838 件を未同期として表示した。全区間の直線近似残差は最大約 8.7 ms。イベント位置は隣接アンカーで局所補間する。その後、DxTimingCaptureLibrary の公式 Basic サンプルに合わせて ETW Provider の Capture State と Resource Keyword を追加し、Agent 単独でも QueueSubmit と GPU Command List を復元できることを確認した。

追加で PIX マーカー、Tracy、RAW QPC Agent の同時実行を確認した。180 Frame／540 Queue submit に対して外部イベント 1,272 件、ETW loss 0、Agent drop 0、Bridge 欠番 0、Tracy 記録 103 Frame を得た。Viewer で Event Inspector と DX イベントからの Zoom を確認した。上限 32 件の Agent Queue 検証では 820 drop をマニフェストと Viewer 向けの健全性値に反映できた。Live 接続では Bridge アンカー 145 件を受信し、133 件で時刻対応、654 件のイベントを配置した。

Tracy の Frame hover から DX イベントを強調し、DX Event Inspector から Tracy の表示範囲へ Zoom できる。ETW と Tracy の時間対応では、全区間の直線近似残差を表示し、イベント位置は隣接アンカーで局所補間している。

PresentMon 2.6.0 SDK Service を導入し、`TracyXProbePresentMon` と Agent で同じ D3D12 PID を同時追跡した。SDK の Open／Track／Stop／Close はすべて成功。300 Frame／900 Queue submit に対して外部イベント 6,012 件、QueueSubmit 900 件、GPU Command List 3,000 件、ETW buffer loss／Agent drop／Bridge 欠番／デコード失敗はいずれも 0 だった。Agent 単独では Capture State の追加後に 300 Frame で QueueSubmit 873 件、GPU Command List 2,910 件を得た。収集開始前の数フレームは欠ける。

Issue #12 の PresentMon 実測は [M2 検証記録](docs/M2-presentmon-validation.md)、Issue #13 の PIX Capture 実測は [PIX Capture 検証記録](docs/M2-pix-capture.md)、Issue #14 の同時動作は [共存検証記録](docs/M2-coexistence-validation.md) を参照。長時間・高負荷下の Viewer 応答性は未検証。

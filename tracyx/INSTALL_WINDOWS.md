# TracyX v1.0 Windows x64 導入・計測手順

## 対応環境と配布物

Windows 11 x64、D3D12 対応 GPU、ビルドに対応する Visual C++ 再頒布可能パッケージを想定する。ソースからのビルドには Visual Studio 18 2026 C++ ツール、Windows SDK、CMake 3.25 以降、Git が必要。配布 ZIP は `bin/TracyXAgent.exe`、`bin/tracy-profiler.exe`、`bin/tracy-capture.exe`、任意の Probe と DLL、`LICENSE`、`THIRD_PARTY_NOTICES.md`、依存ライセンス、SHA-256 一覧を含む。ZIP の展開先から実行し、インストーラや環境変数の変更は不要。

`TracyXAgent` は DX ETW を使用するため、ETW セッションを開始できる権限で実行する。権限がない場合は `StartTraceW` または `EnableTraceEx2` の Win32 エラーで終了する。Live Pipe は対象アプリと Agent の同じ Windows ログオンセッション内だけに公開される。Agent は対象 PID とプロセス開始時刻を固定し、同じ対象への二重起動を拒否する。Viewer は対象と同じログオンセッションで実行する。

## 追加コンポーネント

- Present の表示間隔、GPU Busy、表示遅延には [PresentMon 2.6.0 SDK Service](https://github.com/GameTechDev/PresentMon/releases/tag/v2.6.0) が必要。サービスをインストールして起動する。Agent に `PresentMonAPI2Loader.dll` が同梱されていない構成では PresentMon 計測は利用できない。
- PIX GPU／Timing Capture には [Microsoft PIX](https://devblogs.microsoft.com/pix/download/) と、対象アプリに組み込む WinPixEventRuntime が必要。Timing Capture は管理者権限で実行する。GPU と Timing は別の対象実行で記録する。
- CueEngine 固有の計測は v1.0 の対象外。一般 C++ アプリは Tracy Client と必要に応じて `TracyMemoryPool` を組み込む。

## 最初の計測

以下は ZIP の `bin` を作業ディレクトリとして、PowerShell で実行する例。セッションごとに新しいディレクトリを使う。Probe の代わりに `TRACY_ENABLE` 付き Tracy Client と Frame Bridge を組み込んだ自分のアプリを指定できる。

```powershell
$session = Join-Path $env:TEMP 'TracyX-FirstRun'
New-Item -ItemType Directory -Force -Path $session | Out-Null
$target = Start-Process .\TracyXProbePresentD3D12.exe -ArgumentList @(600, $session) -PassThru
Start-Process .\TracyXAgent.exe -ArgumentList @($target.Id, $session) -Wait
```

対象が起動している間に別の PowerShell から Tracy を保存する。

```powershell
.\tracy-capture.exe -o "$session\tracy.tracy" -a 127.0.0.1 -s 4
.\tracy-profiler.exe "$session\tracy.tracy"
```

Probe が先に終了する場合は Frame 数を増やす。Viewer の「TracyX DX」は `.tracy` と同じディレクトリの `session.json` を自動で読む。`events_file` が示す `events-*.jsonl` と `.tracy` を一緒に保管する。従来の `events.jsonl` も読み込める。Live 閲覧は対象と Agent の起動中に `tracy-profiler.exe -a 127.0.0.1` を実行する。Agent を Ctrl+C で停止しても対象は継続し、セッションが保存される。

## モードと失敗時の扱い

| Mode | 構成 | 必要なもの |
| --- | --- | --- |
| Light | Tracy Client と Frame Bridge | 通常の Tracy 接続 |
| System | Light + Agent の DX ETW／PresentMon | ETW 権限。PresentMon 指標には SDK Service |
| Deep | System + 対象アプリの PIX Capture | PIX、WinPixEventRuntime、Capture に応じた権限 |

PIX／PresentMon の依存がない場合は利用不可理由を記録し、可能な計測を続ける。Agent の対象 PID が無効、別のログオンセッション、または既に収集中なら Agent は理由を stderr に表示して終了する。Viewer が Live Pipe に接続できない場合はアクセス拒否またはプロトコル不一致を表示する。保存失敗では Agent は非ゼロで終了し、対象アプリは停止しない。`session.json` は最後に置き換えるため、以前の有効なセッションを上書きする途中で失敗しても、以前のイベントファイルを参照し続ける。容量上限時は古いイベントを退避し、ETW loss と Agent drop を区別して表示する。

## ソースからの Release ビルドと ZIP 作成

Tracy の採用点は `aa5240ca19dd4978784b76f769f067cdda647615`。DxTimingCaptureLibrary は別ディレクトリに Clone し、固定コミット `72bd2b18553493aa9850e434caf71bfa18952aac` を使用する。NuGet の D3D12 Agility SDK `1.619.5`、任意の WinPixEventRuntime `1.0.240308001` と PresentMon SDK `2.6.0` を準備する。依存の取得先とデバッグ手順は [README](README.md) を参照。

```powershell
msbuild C:\dev\DxTimingCaptureLibrary\DxTimingCaptureLibrary.sln /restore /p:Configuration=Release /p:Platform=x64
cmake -S tracyx -B build/tracyx-release -G 'Visual Studio 18 2026' -A x64 -DTRACYX_DX_TIMING_ROOT=C:/dev/DxTimingCaptureLibrary -DTRACYX_PRESENTMON_SDK_ROOT='C:/Program Files/Intel/PresentMon/SDK'
cmake --build build/tracyx-release --config Release --parallel 2
cmake -S capture -B build/capture-release -G 'Visual Studio 18 2026' -A x64
cmake --build build/capture-release --config Release --target tracy-capture --parallel 2
cmake -S profiler -B build/profiler-release -G 'Visual Studio 18 2026' -A x64
cmake --build build/profiler-release --config Release --target tracy-profiler --parallel 2
pwsh -NoProfile -File tracyx/scripts/package_windows.ps1 -Configuration Release -AgentBuildDir build/tracyx-release -ViewerBuildDir build/profiler-release -CaptureBuildDir build/capture-release -DxTimingRoot C:/dev/DxTimingCaptureLibrary -VendorCache build/profiler-release/.cpm-cache -WinPixRoot "$env:USERPROFILE/.nuget/packages/winpixeventruntime/1.0.240308001" -PresentMonLicense 'C:/Program Files/Intel/PresentMon/LICENSE.txt'
```

`package_windows.ps1` は同名の出力先が存在すると停止する。固定依存のライセンスが欠けた ZIP は作らない。`SHA256SUMS.txt` で展開ファイルを確認できる。配布前には [M4 回帰記録](docs/M4-regression.md) の対応環境、未検証項目、負荷数値を確認する。

## セッション共有時の注意

`.tracy`、イベント JSONL、PIX `.wpix` には、プロセス名・PID・スレッド名・ソースファイルパス・メモリアドレス・GPU 名・Scene 名などが入る場合がある。外部共有前に組織内のルールに従って確認する。パスだけを削ると Frame 対応や PIX の参照が壊れる場合があるため、共有用に複製したセッションで確認する。

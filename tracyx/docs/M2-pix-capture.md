# M2 PIX Capture 検証記録（Issue #13）

## 実行方法

`TracyXProbePresentD3D12.exe 40 <session-directory> gpu` を可視のデスクトップで実行する。引数を省略すると Capture は実行しない。`timing` を指定した場合は管理者権限が必要。PIX アプリ本体と NuGet `WinPixEventRuntime` が必要であり、NuGet の場所は CMake の `TRACYX_WINPIX_ROOT` で指定できる。

GPU Capturer DLL は D3D12 デバイス作成前にロードする。起動済みアプリで後から GPU Capture を有効化する場合は再起動する。GPU Capture は最初の可視ウィンドウを対象にし、既定では Frame 10 から最大 10 Frame を `PIXBeginCapture`／`PIXEndCapture` で記録する。第 4 引数で開始 Frame を変更できる。Timing Capture は別の実行として起動し、GPU Capturer と同じプロセスで併用しない。保存先ディレクトリを先に作成し、PID と QPC を含む一意の `.wpix` 名を使う。

結果は `pix-captures.jsonl` に追記する。各行には Capture 種類、Frame ID、QPC 区間、開始／終了 HRESULT、開始・完了状態、`.wpix` パス、失敗理由がある。Capture が使えない場合も理由を記録し、通常の FrameMark／Present 計測は続行する。Viewer の「PIX Captures」で一覧を表示し、「Refresh PIX index」で更新する。既存の `.wpix` は絶対パスを「Existing .wpix」に入力して「Attach」で追加できる。「Open in PIX」は Windows のファイル関連付けを使用し、ファイル欠損と関連付け失敗を表示する。

## Windows 実測

2026-11-17、Microsoft PIX 2603.25 と WinPixEventRuntime 1.0.240308001 を使用。40 Frame／2 Swap Chain の GPU プローブは終了コード 0、Bridge drop 0。`PIXBeginCapture` と `PIXEndCapture` は HRESULT `0x00000000`、Frame 10 からの `.wpix` を生成した。`pixtool open-capture <file>` は終了コード 0 で GPU Playback engine を起動し、ファイルを開いた。PIX GUI でも同じファイルを開き、Present と Queue の Wait／Signal を含むイベント一覧を確認した。非管理者の Timing プローブは権限不足を検出し、管理者として再起動する必要がある旨を索引に記録した。

同日、管理者権限で 600 Frame の Timing プローブを実行した。Probe、Agent、Tracy Capture はすべて終了コード 0。Frame 300 で `PIXBeginCapture`／`PIXEndCapture` が HRESULT `0x00000000` を返し、約 14.5 MB の `.wpix` を生成した。`pix-captures.jsonl` は開始・完了を記録し、Viewer は Frame 300～310 の 11 候補リンクを表示した。Viewer の「Open in PIX」から Microsoft PIX 2603.25 がこのファイルを開き、Timeline に Tracy DX11 と Queue のイベントを表示した。これにより `.wpix` の生成、索引、Viewer からの起動、PIX での読み込みを確認した。

GPU Capture のプローブは Present のみを実行するため、GPU Draw／Dispatch を含む記録の検証には実アプリでの計測が必要。Timing Capture の高負荷・長時間実行は今回の 600 Frame 検証に含まない。

## API 根拠

- [Microsoft Learn: Programmatic capture](https://learn.microsoft.com/en-us/windows/pix/get-started/programmatic-capture)
- [PIX Blog: Programmatic capture](https://devblogs.microsoft.com/pix/programmatic-capture/)

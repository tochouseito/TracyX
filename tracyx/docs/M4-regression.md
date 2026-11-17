# M4 Windows x64 回帰記録

2026-11-18、Windows 11 x64／Visual Studio 18 2026 で実施。固定版 DxTimingCaptureLibrary、PresentMon 2.6.0 SDK Service、WinPixEventRuntime 1.0.240308001 を使用した。数値はこのマシンの実測であり、他の環境での性能保証ではない。

## 完了した確認

| 対象 | 操作と結果 |
| --- | --- |
| Core／保存形式 | Debug／Release `TracyXCoreTests` 各 1/1 成功。世代別イベントファイル、旧 `events.jsonl` の読込、イベント欠損・不正パス・不正 Live Batch の拒否、公開失敗時の旧セッション保持を確認。実際の 75,634 件の保存セッションも `LoadSession` で読めた。 |
| Agent／対象の分離 | D3D12 Probe を途中で強制終了しても Agent は 1434 件を保存して終了コード 0。ETW 開始権限がない実行では `StartTraceW failed: 5` を表示して Agent のみ終了し、対象 Probe は継続。 |
| 二重起動／不正入力 | 同じ PID の 2 個目の Agent は理由を表示して終了コード 3。最初の Agent と対象は終了コード 0。数字以外の PID、存在しない PID、不正なイベント上限を拒否。 |
| 実セッション／Live 再接続 | 3600 Frame の D3D12 Probe、Agent、Tracy Capture を同時実行。3 回の Pipe 接続・切断・再接続で magic／version が一致。対象・Agent・Capture はすべて終了コード 0。 |
| Agent 再起動 | 3600 Frame の対象の実行中に 1 個目の Agent を強制停止し、残った ETW セッションを停止して 2 個目を起動。対象と 2 個目の Agent は終了コード 0、保存イベント 68,834 件、ETW loss／Agent drop 0。 |
| データ整合性 | 上記セッションのイベント 75,634 件と JSONL 行数が一致し、イベント ID は厳密昇順。Frame anchor 3600 件、PresentMon frame 7168 件、Queue ID 5 種、Swap Chain ID 2 種。ETW loss、Agent drop、D3D12／DXGK decode error はすべて 0。 |
| Tracy と表示メトリクス | 同時 Capture で `tracy.tracy` を保存。Agent は `.tracy` とイベントファイルを参照する `session.json` を保存。PresentMon 指標のイベントが同じセッションに入ることを確認。 |
| ZIP 配布 | Debug ZIP に Agent、Viewer、Capture、任意 Probe、SDK、導入手順、第三者ライセンスを収録。170 ファイルの SHA-256 再計算で不一致 0。展開した ZIP の Probe＋Agent＋Capture で実セッションを保存。 |
| Release ZIP | Agent、Probe、Capture、Viewer を Release でビルド。ZIP は 171 ファイル、SHA-256 不一致 0。ZIP 内の実行ファイルで 900 Frame を収集し、対象・Agent・Capture は終了コード 0。Live Pipe は 3 回再接続でき、18,909 件の保存・再読込に成功。ETW loss／Agent drop は 0。 |
| 一般 C++ アプリ | Release ZIP の `TracyXProbeCpp.exe` と `tracy-capture.exe -s 1` で 256,335 byte の `.tracy` を保存し、両者は終了コード 0。メモリプールの未解放 0、失敗割当 1 件。 |
| パッケージ失敗 | Vendor ライセンスのない入力で生成を中断し、不完全なディレクトリと ZIP が残らないことを確認。 |
| メモリアダプタ | ZIP の C++ Probe で named pool と arena reset を実行し、終了時の未解放は 0。失敗割当 1 件を記録。Debug 単発の中央値は無計測 184.5 ns/op、計測あり 2191.6 ns/op。 |

## 制限と次の確認

- Viewer の新しいバイナリは Debug でビルドできた。初回起動で Windows ファイアウォールの許可画面が出たため、UI 操作によるオフライン読込と Live Inspector の確認は保留した。Core の読込経路はテスト済み。
- DirectStorage の実 I/O → Agent 保存、DRED sidecar の構文・取込、PIX GPU／Timing と Viewer の各表示は M3 の個別検証に記録した。Tracy FrameMark を持つ DirectStorage 実アプリ、実際の device removal、高負荷 PIX Capture を組み合わせた端から端までの実行は未確認。
- Live Pipe の同一ログオン接続は確認した。異なる Windows ログオンセッションからの拒否は実機で未確認。ACL はログオン SID と SYSTEM のみ、リモートクライアントは拒否する設定。
- Release ZIP のビルド・SHA-256 検証はクリーンな GitHub Windows runner でも成功した。新しい Windows 環境での Visual C++ Runtime、PresentMon Service、PIX のセットアップと、公開 GitHub Release への添付ジョブは未確認。公開 Release の添付経路はタグ `tracyx-v*` に限定して用意した。
- メモリアダプタの値は Debug 単発であり、Viewer 接続中や高負荷時の上限を示さない。M3 の [安定性・負荷検証](M3-stability-validation.md)に 600 Frame 3 回と 3600 Frame の測定がある。

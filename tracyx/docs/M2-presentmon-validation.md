# M2 PresentMon 表示メトリクス検証

2026-11-17、Windows の PresentMon SDK Service 2.6.0、Debug ビルドで確認した。SDK の Frame Query は Swap Chain Address と Present Start QPC を整数で返し、Between Display Change、GPU Busy、Display Latency、Until Displayed をミリ秒の `double` で返す。Agent は QPC を Frame Bridge と同じ周波数で換算し、時間値を整数ナノ秒で保存する。Dropped Frames は真偽値で、表示されなかった Present には表示完了時刻を作らない。

`TracyXProbePresentD3D12.exe 360 <directory>` を通常の可視ウィンドウで起動し、同じ PID に `TracyXAgent.exe <PID> <directory>` を接続した。2 Swap Chain の 720 回の Present 呼び出しに対して SDK Frame Query から 685 件を取得した。Swap Chain 別は 343／342 件。685 件すべてで表示間隔、GPU Busy、Display Latency と QPC 時刻を取得し、Present→Display の区間を記録した。Agent の ETW buffer loss、Queue drop、Bridge 欠番、デコード失敗はいずれも 0。Agent が開始する前の Present などは収集対象外であり、720 件との差をすべて欠落とは解釈しない。

Viewer は Present を Swap Chain ごとに分離し、Displayed／Dropped／Unknown を色分けする。Frame との対応は同じ PID と開始時刻を確認してから FrameMark の QPC アンカー区間で求める。Present→Display 区間が複数 Frame と重なれば複数の候補リンクを表示する。アンカー外や換算できない時刻は未同期件数として表示する。SDK が未設定または失敗した場合も DX ETW と Tracy の記録は続く。

非表示ウィンドウでの試行では表示された Present が約 100 件に留まり、別クライアントと同時に追跡した試行では Dropped Frames が多く報告された。表示状態とクライアント構成に依存するため、性能比較には同じ条件の可視ウィンドウで再計測する。長時間・高負荷下の SDK 取得率と Viewer 応答性は未検証。

SDK を指定しない別の Debug 構成もビルド・Core テストを通した。120 フレームの実行では Agent が外部イベント 2,293 件を保存し、ETW buffer loss／Queue drop 0、PresentMon 状態 `unavailable` と理由 `PresentMon SDK was not configured` を記録した。

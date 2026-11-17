# M3 安定性・負荷検証（Issue #19）

2026-11-17、Windows 11／Visual Studio 18 Debug、D3D12 Present Probe と Agent で検証した。`tracyx/scripts/measure_overhead.ps1` は Probe の経過時間、対象と Agent の CPU／ピーク working set、セッション保存量、Agent の終了コードと欠損数を CSV に出力する。表示更新は VSync に依存するため、下の数値はこのマシンでの参考値であり、厳密なオーバーヘッド率ではない。

## 600 Frame、各 3 回

| Mode | 1 Frame 経過時間の中央値 | 対象 CPU 時間の中央値 | 対象ピーク working set の中央値 | 保存量の平均 | ETW loss／退避／Bridge 欠番 |
| --- | ---: | ---: | ---: | ---: | --- |
| Light | 18.30 ms | 1.67 s | 180.70 MiB | 0.05 MiB | Agent なし |
| System | 18.45 ms | 1.94 s | 182.67 MiB | 5.66 MiB | 全回 0 |
| Deep | 19.32 ms | 2.45 s | 249.86 MiB | 6.32 MiB | 全回 0 |

System／Deep は全回で対象と Agent が終了コード 0。Deep の GPU `.wpix` は 3 回とも保存された。別の 600 Frame 1 回ずつの計測では、Agent CPU は System 0.89 s／Deep 1.08 s、ピーク working set は 33.05／33.86 MiB だった。Deep は PIX GPU Capture を含み、対象プロセスのメモリ増加を Agent だけに帰属できない。測定順は Light→System→Deep、ウォームアップと順序のランダム化は実施していない。

## 容量上限

`max-events=32` で 120 Frame を System／Deep で実行した。対象／Agent はいずれも終了コード 0、ETW loss 0。System は最新 30 イベント（event ID 2501–2530）を保存し、古い 2,500 イベントの退避を `queue_drops` に記録した。Deep は最新 31 イベント、退避 2,872 件。Agent は上限に達してもイベント採取を継続し、最大容量の 1/8 ずつ古いイベントを退避する。`queue_drops` はこの退避も含むため、ETW 自体の loss と区別して読む。

## 3600 Frame 継続実行

System、`max-events=1000` で 3600 Frame／約 61 秒を実行した。対象と Agent は終了コード 0、ETW loss・Bridge 欠番・D3D12／DXGK decode error は 0。イベントは通算 75,633 件になり、保存には最新の 883 件（event ID 74751–75633）が残り、退避 74,750 件を記録した。Agent ピーク working set は 40.48 MiB、CPU 7.20 s、保存量 0.70 MiB。対象の 1 Frame 経過時間は平均 17.01 ms だった。収集上限到達後も対象プロセスは最後まで動作した。

保存先パスを既存ファイルにして C++ Probe を対象に故障注入した。対象は終了コード 0、Agent は `SaveSession failed: Cannot create a file when that file already exists.` を出して終了コード 1 だった。対象プロセスに失敗は伝播しなかった。

別の 3600 Frame Probe に Agent を接続し、対象が動作中に Agent のコンソールへ Ctrl+C を送った。Agent は `Saved 44052 events, 0 trace losses, 0 queue drops.` を出し、`session.json` と `events.jsonl` を保存した。対象はその後も 3600 Frame／7200 Present を完了し、Bridge drop 0。保存セッションは ETW loss／Agent drop／Bridge 欠番／D3D12・DXGK decode error 0 だった。この操作では Agent の停止と対象プロセスの独立性を確認した。長時間実行中の Viewer UI 再接続は未検証。900 Frame 実行中の named pipe 再接続は [M2 の検証](M2-coexistence-validation.md)に記録した。

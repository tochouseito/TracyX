# M3 汎用 C++ メモリ計測（Issue #16）

`tracyx/TracyMemory.hpp` の `TracyMemoryPool` は Tracy の名前付き `TracyAllocN`／`TracyFreeN` と `TracyMemoryDiscard` を使う。`Alloc(pointer, bytes)`、`Free(pointer)`、`Realloc(old, replacement, bytes)`、`ResetArena()` を呼ぶ。Pool の名前は他の Pool と重複させない。内部の mutex により同じ Pool のイベント順序と使用量を一致させる。`Snapshot()` は記録した未解放件数・bytes・失敗件数を返す。

`Realloc` は成功後に旧 pointer の Free と新 pointer の Alloc を同じロック内で発行する。新 pointer が null の失敗は旧割当を保持する。`newBytes == 0` による解放は扱わず、明示的に `Free` を呼ぶ。Arena reset はその名前の全割当を Tracy から破棄し、内部状態も 0 に戻す。Arena の backing allocation と suballocation を同時に記録すると合計が二重計上されるため、どちらか一方を計測対象に選ぶ。Probe は suballocation のみを記録する。

予約量、使用量、空き量、断片化率、確保失敗数は allocator が知る値を `PublishMetrics` へ任意に渡して Tracy plot に表示する。これらは OS／GPU の値を自動推定しない。Tracy の Memory View に記録される割当と plot の値は異なる範囲を表し得るため、集計時に足し合わせない。

`TracyXProbeCpp` の Debug 実行で 64→96 bytes の realloc、失敗時の旧割当保持、Arena の 80→0 bytes reset、4 スレッド×100 回の確保・解放を確認した。未解放 0、失敗 1 で終了した。Adapter を通さない `malloc`、第三者ライブラリ、カーネル／GPU の確保は追跡されない。Pool は pointer ごとの表と mutex を保持するため、計測対象の割当数に比例する追加メモリとロック負荷がある。モード別の定量負荷は Issue #19 で測定する。

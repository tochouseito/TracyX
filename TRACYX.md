# TracyX

TracyX は [Tracy](https://github.com/wolfpld/tracy) の正式な GitHub Fork に、Windows / D3D12 の ETW 診断表示を追加するプロジェクトです。M0 / M1 の実装と再現手順は [tracyx/README.md](tracyx/README.md) にあります。

## 上流との関係

- 上流の採用点: `aa5240ca19dd4978784b76f769f067cdda647615`（Tracy v0.15.0）。
- Client、Capture、通信プロトコル、`.tracy` 形式を変更していません。Viewer への接点は `profiler/src/tracyx/` と少数の `TracyView` / CMake 変更に集めています。
- 上流更新時は `git remote add upstream https://github.com/wolfpld/tracy.git`（初回のみ）、`git fetch upstream`、`git diff aa5240ca..upstream/master -- profiler public capture` で変更を確認してから、別 Branch で取り込みます。Viewer の接点と TracyX の Debug ビルド・テストを再検証します。

## ライセンスと外部依存

Tracy 本体は [ルートの LICENSE](LICENSE) にある BSD 3-Clause です。DxTimingCaptureLibrary と WinPixEventRuntime は外部依存で、TracyX のソースに同梱していません。それぞれの MIT ライセンスと配布条件を確認して取得します。DxTimingCaptureLibrary の固定コミット、Agility SDK / PIX Runtime の版と取得手順は [実装の README](tracyx/README.md) に記載しています。PresentMon SDK Service と PIX アプリ本体も同梱していません。

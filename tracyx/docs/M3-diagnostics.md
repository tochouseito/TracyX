# M3 フレーム診断（Issue #15）

Viewer の Tracy フレームを Hover した後、「Analyze last hovered frame」を押すと、同じ Frame series の次アンカーまでを対象に診断する。PSO compilation、Residency、Queue submit から最初の GPU work までの待ちがフレーム時間と重なる場合、元イベント ID、重なり時間、時刻不確かさを表示する。「Inspect evidence」と「Inspect GPU work」で元イベントへ戻れる。

表示するのは**時間上の候補**であり、フレーム遅延の原因を断定しない。ETW loss、Agent drop、Bridge 欠番、decode error、1 ms 超の clock residual があれば品質低下を表示する。連続したアンカーがない場合は診断しない。PSO と GPU work は DxTimingCaptureLibrary が復元したイベントを使用する。Residency イベントが収集されない構成では項目は表示されない。

Queue 待ちは同じ execution ID の submit と最初の GPU work の間隔で近似する。ハードウェア Queue の待ち理由やスケジューラの因果関係を示す値ではない。元イベントの時刻がアンカー区間外の場合、診断候補として扱わない。

2026-11-17、実測した 600 Frame の Tracy／PIX Timing セッションを Viewer で開き、Frame 172 を診断した。観測 Frame 間隔は 16.803 ms。Queue submit と最初の GPU work が重なる候補を 2 件表示し、重なりは 0.447／0.348 ms だった。「Inspect evidence」から Queue submit の Event 3700 を Event Inspector に開き、queue 95、command list 345、Frame 172 への時間重なりリンクと時刻不確かさを確認した。候補の表示と元イベントへの移動を実セッションで検証した。

# ColorTile Solver

カラータイルのスクリーンショットから盤面を読み取り、全消しするためのクリック手順を探す Windows 用アプリです。画像認識と探索を C++17 で実装し、GUI とコマンドラインから利用できます。

**開発中です。このリポジトリではソースコードを管理しており、実行ファイルの配布・リリースはまだ行っていません。**

![画面例](examples/app_preview.png)

## できること

- PNG・JPEG・BMP などの画像から、タイルの位置と色を認識。
- 画像のドラッグ＆ドロップ、クリップボードからの貼り付け（`Ctrl+V`）。
- 認識した色の手動修正と、格子の位置・間隔の調整。
- 制限時間内の探索、または最短性を確認するまでの探索。
- 手順の前後移動・自動再生、CSV への書き出し。
- 盤面の `.ctb` 保存と読み込み、CLI での JSON 出力。

自動再生はアプリ内の盤面表示を進める機能です。ゲーム画面への自動クリック機能はありません。

## ビルド

Windows 10／11 の 64 ビット環境と、C++17 に対応した **64 ビットの LLVM-MinGW または MinGW-w64** を使用します。GCC を使用する場合は C++ の `std::thread`・`std::mutex` を利用できる POSIX スレッドモデル版を選んでください。古い Win32 スレッドモデル版ではビルドできません。Windows 版のビルドは MSVC に対応していません。

### PowerShell スクリプトでビルド

コンパイラと `windres` を `PATH` に追加し、リポジトリ直下で実行します。

```powershell
.\build_windows.ps1
.\ColorTileSolver.exe
```

GUI と CLI の実行ファイルをリポジトリ直下へ生成します。アプリの実行に Python は不要です。

### CMake でビルド

CMake 3.20 以上と MinGW-w64 の `g++`、`windres`、`mingw32-make` を `PATH` に追加して実行します。

```powershell
cmake -S . -B .build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build .build --parallel
.\.build\ColorTileSolver.exe
```

GUI で使うサンプル画像はビルド時に実行ファイルの隣へコピーされます。生成された実行ファイルやビルドフォルダは Git の管理対象外です。

Linux では CLI と共通処理のテストをビルドできます（GUI は Windows 専用）。

```bash
cmake -S . -B .build -DCMAKE_BUILD_TYPE=Release
cmake --build .build --parallel
./.build/ColorTileSolverCLI examples/sample.ctb --seconds 10 --threads 4 --output result.json
```

## 基本操作

1. ビルドした `ColorTileSolver.exe` を起動します。
2. 「画像を開く」でスクリーンショットを選びます。「サンプル」でも動作を試せます。
3. 「元画像を表示」で、認識した格子と画像の位置を確認します。
4. 必要に応じて「盤面を修正」や「位置調整」で認識結果を直します。
5. 探索モードと時間を設定し、「最短手順を探索」を押します。
6. 手順一覧、前後のボタン、自動再生でクリック位置を確認します。

認識が不確かなマスには橙色の枠が表示されます。盤面を修正すると、それまでの探索結果は破棄されます。

標準の盤面は 23 列 × 15 行です。手動設定では各辺 1～32 マス、タイルは最大 256 枚に対応します。盤面の端が空白だけの場合や、画像の一部が隠れている場合は認識位置を確認してください。

## 探索結果の読み方

「時間を指定して探索」は、指定した時間内に見つかった全消し手順のうち、最も少ない手数を保持します。既定は 10 秒で、0.1～86,400 秒を指定できます。「最短確定まで探索」は時間制限を設けず、より短い全消し手順が存在しないことを確認するまで探索します。途中で停止した場合も、その時点の手順を保持します。

| 表示 | 意味 |
|---|---|
| 最短確定 | 全消しでき、これより少ないクリック数の手順がないと証明済み。 |
| 最短性未確認／全消し暫定解 | 全消しできるが、さらに短い手順が存在する可能性あり。 |
| 時間内に全消し未発見 | 時間内に手順が見つからなかった状態。全消し不能の証明ではありません。 |
| 全消し不能 | 認識した盤面について、全消し手順がないと判定。認識結果も確認してください。 |

最短の基準はクリック回数です。マウス移動距離や実ゲームの操作時間は最適化しません。「下界」は少なくとも必要になるクリック数で、見つかった全消し手数と一致すると最短性を確定できます。

大きな盤面では、全消し手順が見つかっても最短性の証明に時間がかかります。制限時間内に全消しや最短性の証明を必ず達成できる保証はありません。並列探索では同じ時間設定でも結果が変わる場合があります。

## コマンドライン版

```powershell
.\.build\ColorTileSolverCLI.exe .\examples\sample.ctb --seconds 10 --threads 4 --output result.json
```

PowerShell スクリプトでビルドした場合は `.build\` を省き、`.\ColorTileSolverCLI.exe` を使用します。`--seconds 0` は時間無制限です。

JSON の `status` が `optimal` の場合のみ最短性が証明済みです。`feasible` は全消し手順があるものの、最短性は未証明です。行・列は 1 から数えます。GUI の CSV 出力に含まれる座標は読み込んだ画像内の座標です。

`.ctb` は先頭に幅と高さ、続いてマスの色番号を並べたテキスト形式です。`0` は空白、`1`～`10` は順に灰・青・水色・緑・黄・茶・橙・赤・桃・紫です。具体例は [sample.ctb](examples/sample.ctb) を参照してください。

## テスト

ビルド後、共通処理のテストを実行できます。

```powershell
ctest --test-dir .build --output-on-failure
```

探索結果を独立した参照探索と比較するテスト、3×3 盤面の全組合せなどを調べるテスト、画像認識の入力検証を実行します。

スクリーンショットの拡大縮小・余白・JPEG 圧縮などの認識テストには、追加で Python と Pillow が必要です。アプリの実行には不要です。

```powershell
python -m pip install Pillow
python tests/test_recognition.py --driver .build/test_recognition.exe
```

Linux ではドライバーのパスを `.build/test_recognition` に置き換えてください。

Windows GUI の起動・停止・手順移動・探索中の終了も、次のテストで確認できます。実行中はアプリのウィンドウが開きます。

```powershell
.\.build\test_gui_windows.exe "$PWD\.build\ColorTileSolver.exe" "$PWD\examples\sample.png"
```

過去の検証条件と結果は [検証レポート](docs/TEST_REPORT.md) を参照してください。実行環境や所要時間の記録は、その検証時点のものです。

## フォルダ構成

| パス | 内容 |
|---|---|
| `src/` | 探索・画像認識・CLI・Windows GUI とリソース。 |
| `tests/` | 参照探索との比較、画像認識、Windows GUI のテスト。 |
| `examples/` | サンプル画像、盤面データ（`.ctb`・JSON）と画面例。 |
| `docs/` | 検証レポート。 |
| `licenses/` | コンパイラ・実行時ライブラリのライセンス。 |
| `CMakeLists.txt` | アプリとテストのビルド定義。 |
| `build_windows.ps1` | Windows 用の簡易ビルドスクリプト。 |

実行ファイル、ライブラリ、アーカイブ、ビルド中間生成物はコミットしません。画像とアイコンはアプリのリソースおよびテスト入力として管理します。

## ライセンス・参考

独自実装のソースコードは [MIT License](LICENSE.txt) です。コンパイラの実行時ライブラリとサンプル画像に関する注意事項は [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) を参照してください。サンプル画像のゲーム素材をソースコードと同じライセンスで再許諾するものではありません。

ルールは [ゲーム菜園の説明](https://www.gamesaien.com/game/color_tiles/guide/) を参考にしています。検討時に [xkikeg/colortile](https://github.com/xkikeg/colortile) を参照していますが、このアプリのコードは独立して実装しています。
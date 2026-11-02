# NX2 Port Studio

[English](README.md) · [Español](README.es.md) · [Français](README.fr.md) · **日本語** · [简体中文](README.zh-Hans.md)

私はApple Silicon向けのネイティブSwitch 2ツールを開発しています。**0.3.0**では、特定のコミットに固定したnxnativeを使うMacアプリ書き出しを追加しました。私の実験的NVN2バックエンドとは別の経路です。

私はタイトル、更新バージョン、全モジュールのビルドIDを検証し、**Donkey Kong Bananza（458752）**と**Splatoon Raiders（65536）**の該当ビルドだけを受け付けます。上流の動作報告をNX2自身の動作確認とは扱いません。Mario Kart Worldは引き続き予定段階で、iPhone/iPad用IPAは出力しません。

`base/`と`updates/`を含む復号済みダンプを選択し、**Inspect dump → Prepare Mac export tools → Create Mac app**の順に進みます。更新のRomFSは差分ではなく完全な状態が必要です。保存先の初期値はApplicationsで、既存アプリは保護します。初回コンパイルには1時間ほどかかる場合があります。処理後にはその処理だけの一時ファイルを削除し、非公開ログと成功時の記録を残します。署名付きアプリの生成は、ゲームプレイの正常動作を保証するものではありません。

私はダンプ検証と開発用ワークスペースも維持しています。[書き出しガイド](docs/nx2/NATIVE_EXPORT.md)、[英語の全手順](README.md)、[固定したソース](upstreams.lock.json)、[クレジット](CREDITS.md)を参照してください。元のライセンスを保持し、ゲーム、Nintendoの素材、ファームウェア、キー、アカウント、セーブデータは公開しません。

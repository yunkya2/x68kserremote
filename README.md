# x68kserremote - X68000(Z) Serial Remote Drive Driver

## 概要

X680x0 とシリアルポートで接続した Windows PC のファイルシステムをリモートドライブとしてアクセスするドライバです。
X68k エミュレータの WinDRV や HFS (ホストファイルシステム)と同様の機能をシリアル接続先の Windows に対して実現します。

主に [X68000Z](https://www.zuiki.co.jp/x68000z/) の UART 端子で Windows とファイルのやり取りを行う使い方を想定していますが、シリアル接続が繋がってさえいればエミュレータや実機での利用も可能なはずです
(開発は [XM6 TypeG](http://retropc.net/pi/xm6/index.html) + [com0com](https://ja.osdn.net/projects/sfnet_com0com/) で行っています)。

## 使用方法

1. X68000 と Windows PC をシリアルケーブルなどで接続します。
    * X68000Z の場合は付属の UART ケーブルとシリアル- USB 変換アダプタなどを使用します
    * X68k エミュレータの場合はヌルモデムエミュレータ [com0com](https://ja.osdn.net/projects/sfnet_com0com/) で仮想 COM ポートの組を作って、片方の COM ポートをエミュレータの設定で X68k の RS-232C ポートに割り当て、もう片方の COM ポートを後述のサーバに指定します
2. `x68kremote.exe` を Windows のコマンドプロンプトや PowerShell から起動しておきます。
    ```
    x68kremote.exe [-D][-s <ボーレート>] <COMポート名> [<ルートディレクトリ>]
    ```
    * `-D` を指定するとデバッグ出力が on になります。
    * `-s <ボーレート>` でシリアルポートの通信速度を指定します。省略した場合は `38400` となります。
      * X68000 側と同じ速度に設定してください。
    * `<COMポート名>` には Windows に接続したシリアルポートの名前を指定します(`COM3`など)。
    * `<ルートディレクトリ>` には X68k 側から参照する際にルートディレクトリとなるディレクトリ名を指定します。省略した場合はカレントディレクトリとなります。
3. `SERREMOTE.SYS` を X68000 の起動ディスクにコピーして CONFIG.SYS に以下の記述を追加します。
    ```
    DEVICE = <ディレクトリ名>\SERREMOTE.SYS [/s<ボーレート>] [/r<登録モード>] [/t<タイムアウト>]
    ```
    * `/s<ボーレート>` でシリアルポートの通信速度を指定します。省略した場合は `/s38400` となります。
      * Windows 側と同じ速度に設定してください。
      * 通信速度を38,400bpsに設定する場合は、CONFIG.SYS で `SERREMOTE.SYS` より前に `RSDRV.SYS` を登録してください。
    * `/r<登録モード>` で起動時にドライバを登録するかどうかのモードを指定します。省略した場合は `/r0` となります。
      * `/r0` では起動時に Windows 側サービスの存在をチェックせず、常にデバイスドライバとして登録します。
        Windows 側サービスを起動せずにリモートドライブをアクセスすると「ディスクが入っていません」エラーになりますが、後からサービスを起動して再実行すればアクセス可能になります。
      * `/r1` では起動時に Windows 側サービスが動作していることをチェックします。
        X68000 側からのコマンド送信に応答しなかった場合はデバイスドライバの組み込みを行いません。
    * `/t<タイムアウト>` で Windows 側サービスの応答を待つタイムアウト値を指定します。省略した場合は `/t5` となります。
    * リリースアーカイブ内の `serremote.xdf` は `SERREMOTE.SYS` の入ったフロッピーディスクイメージファイルです。ドライバを X68000Z に持ち込む場合などに利用できます。

Windows 側を起動すると以下のようなメッセージを表示して、X68k 側の接続を待ちます。
```
X68000 Serial Remote Driver Service (version xxxx)
```
続いて X68000 側を起動すると、以下のようなメッセージが表示されます。
```
X68000 Serial Remote Drive Driver (version xxxx)
ドライブX:でRS-232Cに接続したリモートドライブが利用可能です (xxxxxbps)
```
表示されたドライブから、シリアル接続先の Windows ファイルシステムにアクセスできるようになります。

## ビルド環境

* X68k 側ドライバ(SERREMOTE.SYS)のビルドには [elf2x68k](https://github.com/yunkya2/elf2x68k) を使用します
* Windows 側サーバ(x68kremote.exe)のビルドには [MSYS2](https://www.msys2.org/) を使用します
    * MSYS2 MinGW x64 環境でビルドすることで、単体の Windows コンソールアプリとして実行できるようになります
    * MSYS2 MSYS 環境でもビルドは可能ですが、実行時に MSYS2 の DLL が必要になります
    * ビルド時に `WINNT` が define されていたら Windows APIを、define されていなければ POSIX API を使用します。他の POSIX API 環境 (Ubuntu や WSL など) でも動作するかも知れませんが、未確認です。
* macOS(Homebrew 環境) でのビルドをサポートしました(@hyano さんありがとうございます)。\
  `service` ディレクトリ内で make を行うことで macOS 側サーバをビルドできます。

## 制約事項

* リモートドライブ上ではファイルアトリビュートの隠しファイルやシステム属性、書き込み禁止属性などは無視されます
* Human68k の DSKFRE が 2GB 以上のディスクサイズを想定していないため、ドライブの残容量表示は不正確です

## 謝辞

Human68k のリモートドライブの実装は以下を参考にしています。開発者の皆様に感謝します。

* [ぷにぐらま～ずまにゅある](https://github.com/kg68k/puni) by 立花@桑島技研 氏
  * [filesystem.txt](https://github.com/kg68k/puni/blob/main/filesystem.txt)
* [XEiJ (X68000 Emulator in Java)](https://stdkmd.net/xeij/) by Makoto Kamada 氏
  * ソースコード [HFS.java](https://stdkmd.net/xeij/source/HFS.htm)
* [XM6 TypeG](http://retropc.net/pi/xm6/index.html) by PI. 氏 & GIMONS 氏
  * XM6 version 2.06 ソースコード

## ライセンス

本プログラムは MIT ライセンスとします。

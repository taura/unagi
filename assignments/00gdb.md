
練習 0-1
====================

unagi_serverの中身を追跡するためにデバッグオプション付きでコンパイルせよ.
以下,

```
$ コマンド
```

のような表記は「コマンド」部分をコマンドラインに入力せよという意味.
$ はシェルプロンプトであり, 入力の一部ではない.

また, cdコマンドでフォルダ間を移動する必要がある場合, すべて現在位置がunagiのトップレベルフォルダ(git cloneしてできたフォルダのルート; 直下にsrc, docなどがあるフォルダ)であると仮定している.

```
$ cd src/server

## エディタで Makefileを開き, CFLAGS += -O3 の行を無効に
## (先頭に # を入れてコメントアウト)して CFLAGS += -O0 -g
## の行を有効にせよ

$ make -B
gcc -o unagi_utility.o -Wall -Wextra -pthread -O0 -g  -c unagi_utility.c
gcc -o document_repository.o -Wall -Wextra -pthread -O0 -g  -c document_repository.c
gcc -o unagi_server.o -Wall -Wextra -pthread -O0 -g  -c unagi_server.c
gcc -o unagi_server unagi_utility.o document_repository.o unagi_server.o -pthread

## -O0 -g などがコンパイラに渡されているか確認せよ
```

注: Mac OSの人は, Makefile内で
```
CC := gcc
```
の代わりに
```
CC := clang
```
とすると(きっと)動く


練習 0-2
====================

unagi_server をgdbデバッガで立ち上げよ

注: Mac OSな人は, gdbの代わりに lldb を使う
(コマンドの詳細が違うかもしれない.
可能な範囲で補足しますが適宜試行錯誤してください).

以下はコマンドラインからgdbを立ち上げる方法

```
$ gdb ./unagi_server
GNU gdb (Ubuntu 8.1-0ubuntu3) 8.1.0.20180409-git
Copyright (C) 2018 Free Software Foundation, Inc.
License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.  Type "show copying"
and "show warranty" for details.
This GDB was configured as "x86_64-linux-gnu".
Type "show configuration" for configuration details.
For bug reporting instructions, please see:
<http://www.gnu.org/software/gdb/bugs/>.
Find the GDB manual and other documentation resources online at:
<http://www.gnu.org/software/gdb/documentation/>.
For help, type "help".
Type "apropos word" to search for commands related to "word"...
Reading symbols from ./unagi_server...done.
(gdb)
```

```
Reading symbols from ./unagi_server...done.
```

という行を確認せよ.こうなっておらず

```
Reading symbols from ./unagi_server...(no debugging symbols found)...done.
```

のようになってしまったら, デバッグ用の情報(デバッグシンボル)がついていないということ. make したときにきちんと, -g オプションが渡っているかを確認せよ.

デバッガの終了は q(uit). 

```
(gdb) q
$
```

以降はコマンドラインからではなく,
Emacs の中で gdb を実行する例を示す. やり方は,

(Emacsでunagi_server.cを開いた状態で)
```
M-x gud-gdb   (M-x の入力方法: Altを押しながらx, ESCを押してからx, Ctrl-[ を押してからxのいずれでも)
```

とすると下部に

```
Run gud-gdb (like this): gdb --fullname unagi_server
```
のように聞かれる (unagi_server の部分は違うかもしれない. その場合はunagi_serverにする).

gdb --fullname unagi_server の部分は, コマンドラインでgdbを実行するときの好きなコマンドを入れれば良い. 唯一の注意は --fullname を付けるということだけ. これをつけないとEmacsがソースコードの位置を取得して自動的に表示する機能が働かない.


gdbを実行しているだけなので,
gdbに入力するコマンド自体はEmacs内でやる場合でもコマンドラインからやる場合でも同じ.
Emacsで実行すると現在実行中のソース行などが表示されて便利.
というよりもこれなしにデバッガで追跡する気はしないというところ.

Emacs以外にも, このような機能を持つエディタや統合環境は多数ある
(vscode, Eclipseなど). 好きな環境を使えば良いが, くれぐれも
コマンドラインから不便なままやり続けないことを進める.

練習 0-3
====================

unagi_server をgdbデバッガで追跡せよ. 以下は (gdb) 以降が入力部分. 


入力
```
(gdb) b main
```
出力
```
Breakpoint 1 at 0x3a6b: file unagi_server.c, line 930.
```
入力
```
(gdb) run
```
出力
```
Starting program: /home/tau/public_html/lecture/operating_systems/gen/unagi/tau/unagi/src/server/unagi_server 
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/lib/x86_64-linux-gnu/libthread_db.so.1".

Breakpoint 1, main (argc=1, argv=0x7fffffffe268) at unagi_server.c:930
```

このときEmacsであれば自動的にソース(unagi_server.c)が開いてmain関数の先頭にカーソルが移動する.

以降, n(ext)コマンドと s(tep)コマンドをデバッガに入力して少しずつ実行する.

なお途中でやり直したくなったら, run コマンドを実行すれば良い.


 * まず

```
(gdb) n
```

を何回か実行して, 実行位置が

```
> run_server(opt);
```

に来るまで(run_server関数が呼び出される直前)まで実行.

なお, 同じコマンドを続けて実行するときは RETURN (Enter) キーを叩けば良い(直前の入力が繰り返される)

 * 次に

```
(gdb) s
```

を一度実行して run_server関数の中に突入.

 * n(ext)とs(tep)はどちらも「一行」実行するコマンドだが, s(ext)は関数呼び出しが会ったらその中に突入する. nは関数の中には入らずにあくまでその行全体が修了するまで(次の行に行くまで)実行しようとする.

 * nとsを組み合わせて, 以下の順に関数の中に入っていって, サーバが接続を受付けるところまで到達せよ.

  * main -> run_server -> server_accept_connection -> accept

 * acceptを呼び出している行で n を実行すると, クライアントからの接続が来るまで待つ(ブロックする)ため, acceptがリターンせず, (gdb)のプロンプトも表示されない(正しい動作)

 * この状態で別の端末からクライアントを立ち上げ,

```
$ cd src/client
$ ./unagi_client.py 53243 put すもも すもももももももものうち
```

のように文書を登録する. 53243 はサーバが途中で以下のように表示したポート番号(xxxxxの部分).

```
server listening on port xxxxx
```

 * クライアントを実行するとサーバの accept 関数が戻るはずである. すると再びデバッガで追跡できる. n と s を組み合わせて以降の動き, 仕組みを追跡する

 * n を何回か実行して,

```
server_process_connection(so, sv);
```

の行に到達する. ここで s を実行して中身を追う

```
request_t req = server_recv_message(so);
```

の行で, sで中に入れば実際にメッセージをネットワークから受け取っているところを観察できる. メッセージの詳細なフォーマットなどを知りたければこの関数の中身を見る.

server_recv_message(so)からリターンしたあとさらに実行していくと,

```
connection_handle_put(req, so, sv)
```

という行に達する. ここで s で中身を追跡.

重要なのは,

```
  document_t doc = { req.put.label, req.put.label_len, 
                     req.put.data, req.put.data_len, };
```
と
```
  ssize_t c = document_repo_add(sv->repo, doc);
```

の二つの文. 前者は, ひとつの文書(ラベルと中身)を表すデータ構造.

後者は, 文書を, repository に加える関数. sv->repo というのが, このサーバが受け取った文書を格納しているメインデータ構造(document_repository; 文書レポジトリ)である. 文書の追加も検索も, sv->repo に対して, 追加, 検索用の関数を呼び出すことで達成される.

後者を s で追跡. その中身は現在は以下のような単純なもの. 

```
  return document_array_pushback(repo->da, d);
```

文書のレポジトリの正体は(現在は)単に, すべての文書を配列に入れているだけである. document_array_pushbackの中身は本当に, 配列の末尾に文書を追加するだけなのだが, 配列が満杯になったときは割り当て直す, ということをする. 詳細はここで s を実行して中身を追跡すればわかる.

document_repo_add が終了すると, 要求を送ってきたクライアントに返事を返す. putの場合はOKという印と数字をひとつ返すだけ(数字は, 追加された文書が何番目の文書かを示す数0, 1, 2, ... である. 深い意味はない). 

document_repo_add が終了したところで, 実際に蓄積されている文書のデータの中身を見ておくと見通しが良いだろう. それには p(rint) コマンドを使う.

```
(gdb) p sv->repo
$1 = {{da = {{sz = 16, n = 1, a = 0x55555575b560}}}}
```

この結果を見てsv->repo が1要素の配列 {{ ... }} で, その要素は da
というフィールドを持つ構造体 {da = ...}, で, そのdaは再び
1要素の配列{{ ... }}で, その要素は sz, n, aという要素を持つ構造体
であるとわかる. なおこれはもちろん unagi_server.c のソースコードの,
server_t のデータ定義を起点にして追っていけば確認できることである.

a が実際にドキュメントを格納している配列(ポインタ)のようなのでその中身を見たければ以下.

```
(gdb) p sv->repo->da->a[0]
$7 = {label = 0x55555575b510 "すもも", label_len = 9, 
  data = 0x55555575b530 "すもももももももものうち", data_len = 36}
```

すると1文書の中身を見ることが出来, ラベルは「すもも」, 中身は「すもももももももものうち」という意図したものが見える. label_len, data_len はそれぞれのバイト数. UTF-8という文字の符号化方式では, 日本語の文字はたいがい1文字3バイトで表されているので, 「すもも」に9バイトは頷ける結果である.

練習 0-4
====================

練習 0-3 の要領で unagi_server を追跡し, getおよびgetc を受信したときの動作を理解せよ.






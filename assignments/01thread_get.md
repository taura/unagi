
unagi_serverのスレッド用いたgetの並列化
====================

(必須) 課題 1-1
====================
unagi_server.c を unagi_server_1.c という名前にコピーし, そのファイルを変更して, 複数の接続を並列に処理するようにせよ. 

ただし課題 1-1の時点では以下を仮定して良い.

 * put要求, quit要求は他の処理が行われていないときにしかやってこない
 * put要求が処理されている間, 他の要求はやってこない

これは, 授業についてきている人向けに一言で言えば, putがデータを書き換える処理によって生ずる競合状態の問題を一旦棚上げにしてよいということである.

概要説明
--------------------

やるべきことは, サーバーのメインループであるrun_server関数

```
static void run_server(cmdline_options_t opt) {
  server_t * sv = start_server(opt);
  if (!sv) return;
  while (sv->server_continues) {
    int so = server_accept_connection(sv);
    if (so == -1) {
      sv->server_continues = 0;
    } else {
      server_process_connection(so, sv);
    }
  }
  stop_server(sv);
}
```

の中で, 1つの接続(ソケット)を処理する関数である

```
      server_process_connection(so, sv);
```

を, 「スレッドを作って, 作られたスレッドがserver_process_connection(so, sv)を呼ぶ」という風に書き換えるだけである(ただしいくつか詳細に関する注意が以下にある).

このような実装でも, get, getc, dump, dumpcなどの, データを読み出すだけの処理(もちろん名前だけでデータを読み出しているだけ, と勝手に決めつけてはいけないのだが現在はそうなっている)であれば, 安全に並列処理できるし, 複数のスレッドを使うことの高速化の恩恵を受けられる.

詳細補足: pthread_createの呼び出し方
-----------------------

 * 目標は, server_process_connection(so, sv) を, スレッドを作ってそのスレッドに処理させること. そのための基本APIは pthread_create.
```
$ man pthread_create
```
などとしてマニュアルで使い方を確認せよ
 * pthread_createの基本は, pthread_create(&tid, 0, f, a) と呼び出すと f(a)を実行するスレッドを作るということである. &tidや0の意味はマニュアルを参照.
 * server_process_connection(so, sv)を新しいスレッドを作ってそれに実行させたいのだが, 若干面倒なのはpthread_create に渡せる関数の引数が1つ (void *型の引数が一つ)と決まっていることである. 今回のように二つの引数を受け取る関数(server_process_connection)を直接渡すことが出来ない. 以下はそのための常套手段.
 1. 渡したい要素を持つ構造体(struct)を定義 (例: server_process_connection_arg_t)
 1. スレッドに実行させたい関数(今の場合server_process_connection)を, そのままpthread_createには渡せない(引数の数がひとつではない)ので, 上記の構造体へのポインタを受取り, その先から要素を取り出してserver_process_connectionを呼び出すような関数(ラッパー関数)を作る(例: server_process_connection_thread_fun)
 1. pthread_createを呼び出す方は, その構造体を割り当て(malloc), その構造体に要素を格納し, pthread_createにはその構造体へのポインタ(server_process_connection_arg_t *)を渡す.

以上の擬似コード(この詳細化はまさしく演習)

 1. 構造体を定義(引数を詰め込む構造体)
```
typedef struct {
  int so;
  server_t * sv;
} server_process_connection_arg_t;
```
 1. ラッパー関数(構造体から引数を取り出して呼ぶ)
```
void * server_process_connection_thread_fun(void * args_) {
  server_process_connection_arg_t * args = args_;
  int so = ...
  server_t * sv = ...
  my_free(args_);
  server_process_connection(so, sv);
  return 0;
}
```
 1. スレッドを作るところ

```
  pthread_t tid;
  server_process_connection_arg_t * args
    = malloc_or_err(sizeof(server_process_connection_arg_t));
  args->so = so;
  args->sv = sv;
  err = pthread_create(&tid, attr, server_process_connection_thread_fun, args);
  if (err) {
    api_err("pthread_create");
    return 0;
  }
  return 1;
}

```

ビルドのためのMakefileの書き換え
-----------------------

 * src/server/Makefile を開き, コメントを見ながら, このMakefileでunagi_clientとunagi_client_1 がビルドされるようにせよ.
```
$ make
gcc -o unagi_utility.o -Wall -Wextra -pthread -O3 -c unagi_utility.c
gcc -o document_repository.o -Wall -Wextra -pthread -O3 -c document_repository.c
gcc -o unagi_server.o -Wall -Wextra -pthread -O3 -c unagi_server.c
gcc -o unagi_server unagi_utility.o document_repository.o unagi_server.o -pthread  
gcc -o unagi_server_1.o -Wall -Wextra -pthread -O3 -c unagi_server_1.c
gcc -o unagi_server_1 unagi_utility.o document_repository.o unagi_server_1.o -pthread  
```

実験 1-2 結果の確認 (**11/6更新**)
====================

src/client/random フォルダに, unagi_client.py をたくさん実行するmakefile (ファイル名は random_test.mk)があるのでそれを使う.

 * 手順1 (メッセージの準備)

putに使うメッセージとgetに使うメッセージを予め生成しておく.
random_test.mk中の

```
n_puts:=50
```
や
```
n_gets:=80
```
が, それぞれの個数. 最初は小さい値で実験したいという場合はこれらの値を適宜調整せよ.
 
```
$ make -f random_test.mk prepare n_puts=50 n_gets=80 -j 20
mkdir -p msgs/created
../unagi_client.py 10000 make_put_random 1 1 1000000 msgs/put_msg_1
../unagi_client.py 10000 make_put_random 2 2 1000000 msgs/put_msg_2
../unagi_client.py 10000 make_put_random 3 3 1000000 msgs/put_msg_3
   ...
```

これを実行すると, msgs/ というフォルダが作られ, メッセージが格納される.
約1000000 バイト(1MB)のファイル(msgs/put_msg_*)が50個と,
約100バイト程度のファイル(msgs/get_msg_*)が80個できる.
ここで指定した n_puts=50 と n_gets=80 は以下でも同じ値を指定する.
ここではサーバとのやり取りはせず, メッセージをファイルに格納するだけ.
-j 20 は20個までプロセスを同時に立ち上げるというオプション(つまり20
個くらいのファイルを並行に生成している).

 * 手順2

```
$ cd src/server
$ ./unagi_server_1 -l ""
server listening on port 53455
```
としてサーバを立ち上げる.

-l "" は「ログファイルを出力しない」というオプション
ログはたちまち巨大になる上, ファイルへの書き込みのせいで性能が律速されるので, 性能評価をしているときには使わない(もちろん本物のサーバでは実運用だからといってログを出さないわけには行かないが, 現状では何もかも出力しているのでログは必要以上に巨大になっている).

 * 手順3 (文書のput)

```
$ make -f random_test.mk port=53455 n_puts=50 n_gets=80 put
../unagi_client.py 53455 send_file msgs/put_msg_1 > status/p_1
../unagi_client.py 53455 send_file msgs/put_msg_2 > status/p_2
../unagi_client.py 53455 send_file msgs/put_msg_3 > status/p_3
../unagi_client.py 53455 send_file msgs/put_msg_4 > status/p_4
   ...
```

ここで実際にサーバとの通信が行われ, ランダムな文字列がサーバに送り込まれる.

 * 手順4 (文書のget)

```
$ time make -f random_test.mk port=53455 n_puts=50 n_gets=80 get
../unagi_client.py 53455 send_file msgs/get_msg_1 > status/g_1
../unagi_client.py 53455 send_file msgs/get_msg_2 > status/g_2
../unagi_client.py 53455 send_file msgs/get_msg_3 > status/g_3
   ...
```

答えの確かめ方: getを実行するとその出力が, status/g_XXX というファイルに格納されている. 
最初のn_puts=50個 status/{g_1,g_2,...,g_50} は, 1行目が
```
OK 1
```
となっているべき. 一方(n_puts+1)=51個目以降は, 1行目が
```
OK 0
```
となっているべき.

OK X は検索した文字列がX個所見つかった(hitした)という意味.

文字列はランダムに生成しているが, get_msg_X は put_msg_X の一部を検索するように作られているため, 対応するput_msg_Xが存在する(つまり最初のn_puts=50個の)getは必ずhitする. それ以降は偶然hitする可能性はあるものの, 100文字ランダムに文字を生成して偶然hitする可能性はまずないと思って良い.

大雑把に全体を調べる方法は
```
$ grep 'OK 1' status/g_* | wc
$ grep 'OK 0' status/g_* | wc
```

最初のコマンドでn_puts=50個の行が見つかり, 次のコマンドで(n_gets - n_puts = 30)個の行が見つかればまあ, あっていると思って良いだろう.

初期状態に戻す方法
====================

 * サーバを終了させる (unagi_client.py で quit コマンドを送るか, ctrl-c でサーバを強制終了しても良い)
 * status/ フォルダをまるごと消去する

でよい. なお, msgs/ フォルダには送信するメッセージが格納されているので, 実験をリセットしたい場合も有効である(基本的に消したり再生成したりする必要はない). 送るメッセージの数を増やす場合は 
```
$ make -f random_test.mk prepare
```
をやり直す必要がある.

実験 1-3 性能測定
====================

```
$ make -f random_test.mk port=60243 get
```

は, getcを一つずつ逐次的に実行していくもので, サーバの性能を測るにはこれではダメで, クライアントが一斉にサーバに押し寄せる(多数のクライアントが並列にサーバに接続する)状況を作らないといけない. makeは優秀なツールで -j というオプションひとつでそれができる.

```
$ make -f random_test.mk port=60243 n_puts=... n_gets=... get -j 100
```

とするだけで, (最大で) 100個までのプロセス (unagi_client.py)が立ち上がるようになる.

```
$ time make -f random_test.mk port=60243 n_puts=... n_gets=... get -j 100
```

とするとかかった合計時間が表示される.

これで, スレッドを使う場合と使わない場合の性能を比較せよ. 性能は, 秒あたりに処理したリクエスト数で測る. 性能測定がやりやすいように(それなりの時間がかかるように)n_puts, n_getsの値を適宜調整せよ.

実験 1-4 CPUやメモリの使用率・量の観察
====================

getが処理されている間, CPUの使われ方やメモリの使われ方を観察せよ. コマンドラインのツールやGUIなどいくつか試してみよ

 * Linux, Mac, またはBash on Windowsなどであればtopコマンド
 * Linuxの「システムモニタ」
 * Windowsの「リソースモニタ」
 * など...

これらを用いて,

 * CPUのコアがどのくらい使われているか
 * メモリがどのくらい使われているか

などを観察せよ.

残念ながらまだ終わりじゃない
-----------------------

これでコードとしてはだいたい完成なのだが一点, 不完全な点がある. それは, スレッドを生んだら最終的にはそれに対して pthread_join を呼び出してやらないといけないということである. 授業では pthread_join は スレッドの終了を待つためのAPIだと述べた. それはもちろんそのとおりなのだが, 今回のようにスレッドの終了を確認する必要がとくにない場合, 呼び出さなくてよいのかと言うと残念ながらそうではない. 必ず呼び出さないといけない.

呼び出さないと何が起きるかというと, メモリのリークがおきる.

なぜか? pthread_joinが呼ばれるまでは, 最低限, スレッドの終了ステータスや返り値を格納したデータ構造を(今後pthread_joinが呼ばれるかもしれないのでその時のために)保持しておかないといけないからである. pthread_joinはスレッドの終了を待つという直接的な意味の他に, 「もうこのスレッドに関する情報は全てこちらで看取ったので, もうその情報を全部無き物にして良いですよ」宣言という意味がある. 

さてそこでこのプログラムを, 終了したスレッドに対してpthread_joinを呼ぶように変更しよう, ということになるのが実はそれはあまり易しくない. 何故易しくないかに深入りするとそれだけで長くなってしまうので一度考えてみよ. ここでは, pthread_joinを呼ばなくてもメモリリークを起こさない方法を述べる.

それが, detach (切り離し)という操作である.
```
$ man pthread_detach 
```
として調べよ.

要点を言うと, pthread_join を呼ぶ代わりにpthread_detachを呼べば, もうそのスレッドにpthread_joinを呼ぶことは出来ない(従って終了ステータスなどを得ることも出来ない)代わりに, そのスレッドが終了するとそのスレッド用のすべてのメモリが解放される, というものである. なので, 新しいスレッドを生成した直後にpthread_detachを呼んでやれば良い.

課題 (必須) 1-5
====================

unagi_server_1.c を修正し, 作った子スレッドを直ちにdetachするようにせよ.

子スレッドをdetachする場合としない場合で, メモリの使用量の違いを観察せよ. 必要ならばrandom_test.mkを修正して, 当初の設定よりも大量のgetc動作をするようにせよ(2000回とか).

課題 (推奨) 1-6
====================

unagi_server.c にコマンドライン引数を処理している部分がある(parse_args). これを変更して, -t というオプションを追加し, 同じプログラムでスレッドを使う・使わないを切り替えられるよにせよ.

-t は, 
 * -t が与えられないか, または -t 0 ならば子スレッドを使わない(全てを逐次で処理する)
 * -t 1 ならば子スレッドを使う(接続毎に子スレッドを作る)
という動作をするものとする. これをやるのに必要な変更点は

 * cmdline_options_t という型に要素 (例: int use_thread) を追加
 * default_opts() 関数の中でデフォルトの動作 use_thread = 0 を設定
 * parse_args で getopt(argc, argv, "l:p:q:h") の呼び出しに, "t:"を追加 (getopt(argc, argv, "l:p:q:t:h"))
```
$ man -s 3 getopt
```
でgetoptの使い方を調べよ
 * その中のswitch文に, -t オプションを取り扱う case 't': を追加. 他の case を参考にすれば何をしたらよいかわかるだろう.
 * unagi_usage 関数に, 正しくヘルプを表示

提出物
====================

 * ソースコード(追加, 変更全てをcommit, push)
 * 以下を含む内容のテキストファイル submit/01submit.md (add, commit, push)
  1. 達成した課題の番号
  1. スレッドなし・ありの場合の性能比較をまとめたもの
  1. 動作確認と性能測定を行った際の実行ログ. スレッド化した場合としていない場合
 * サンプル 01submit_example.md
  1. この程度のものでよいという例として示します
  1. 書式を厳密に規定するという意図ではありません
  1. 適宜考えたことや疑問に思ったことがあったら書いて下さい
  

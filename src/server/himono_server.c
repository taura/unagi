/**
 * @file himono_server.c
 * @brief himono server メインファイル
 * @author 田浦
 * @date Dec. 11, 2018
 */

/* 
   注意: 演習を始める際にこのファイルを直接いじらないこと.
   各演習の指示に従い, 指定されたファイル名で新しいファイルを
   (恐らくこのファイルをコピーしてから)作り, 提出する.

   このファイルは演習が進むにつれて更新されるかもしれない.
 */

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>          /* See NOTES */
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#include "unagi_utility.h"
#include "document_repository_himono.h"

/** 
    @brief サーバのコマンドラインオプションを表すデータ構造
*/
typedef struct {
  int port;     /**< 接続を受け付けるポート番号(bind) */
  int qlen;     /**< 接続要求のキュー長(listen) */
  char * log;   /**< ログファイルの名前 */
  char * data_dir; /**< 保存ディレクトリ */
  int load_data;   /**< ディレクトリからデータをロードするか */
  int thread;   /**< スレッドを使うか */
  int error;    /**< コマンドライン処理でエラーが出たら1にする */
  int help;    /**< コマンドライン処理で'-h'が出たら1にする */
} cmdline_options_t;

/**
   @brief サーバを表すデータ構造
 */
typedef struct {
  cmdline_options_t opt;
  FILE * log_wp;           /**< ログファイルへ書き込むためのFILE*構造体 */
  int server_sock;         /**< 接続を受け付けるソケット(socket) */
  int server_continues;    /**< サーバが処理を続ける間1  */
  int term_fd[2];          /**< スレッドの終了通知用パイプ  */
  int nthreads;            /**< 走行中スレッド */
  document_repo_t repo[1]; /**< ドキュメントレポジトリ */
} server_t;

/**
   @brief サーバを起動する

   @details クライアントからの接続を受け付けるソケットを作る; ログファイルを開く; 空のドキュメントレポジトリを作る.
  */
static server_t * start_server(cmdline_options_t opt) {
  /* ソケットを作る */
  int ss = socket(AF_INET, SOCK_STREAM, 0);
  if (ss == -1) {
    api_err("socket");
    return 0;
  }
  struct sockaddr_in addr[1];
  /* ポート番号を割り当てる(bind) */
  addr->sin_family = AF_INET;
  addr->sin_port = htons(opt.port);
  addr->sin_addr.s_addr = INADDR_ANY;
  unsigned int addrlen = sizeof(addr);
  if (bind(ss, (struct sockaddr *)addr, addrlen) == -1) {
    api_err("bind");
    return 0;
  }
  /* 割り当てられたポート番号を得る */
  if (getsockname(ss, (struct sockaddr *)addr, &addrlen) == -1) {
    api_err("getsockname");
    return 0;
  }
  /* 接続可能にする. 接続要求を貯めるキューの長さを設定する(listen) */
  if (listen(ss, opt.qlen) == -1) {
    api_err("listen");
    return 0;
  }
  /* ログファイルを開く */
  FILE * log_wp = 0;
  if (strlen(opt.log)) {
    log_wp = fopen(opt.log, "wb");
    if (!log_wp) {
      api_err("fopen");
      close(ss);
      return 0;
    }
  }

  /* スレッド終了通知 */
  int term_fd[2] = { 0, 0 };
  if (pipe(term_fd) == -1) {
    api_err("pipe");
    close(ss);
    return 0;                   /* NG */
  }
  
  /* サーバデータ構造の割当て */
  server_t * sv = malloc_or_err(sizeof(server_t));
  if (!sv) return 0;
  sv->opt = opt;
  sv->server_sock = ss;
  sv->server_continues = 1;
  sv->log_wp = log_wp;
  sv->term_fd[0] = term_fd[0];
  sv->term_fd[1] = term_fd[1];
  sv->nthreads = 0;

  if (opt.load_data) {
    /* ファイルからロード */
    if (!document_repo_load(sv->repo, opt.data_dir)) {
      return 0;
    }
  } else {
    /* 空のドキュメントレポジトリを作る */
    document_repo_init(sv->repo);
  }
  fprintf(stderr, "server listening on port %d\n", ntohs(addr->sin_port));
  if (sv->log_wp) {
    fprintf(sv->log_wp, "server pid %d\n", getpid());
    fprintf(sv->log_wp, "server listening on port %d\n", ntohs(addr->sin_port));
    fflush(sv->log_wp);
  }
  return sv;
}

/**
   @brief サーバを停止. メモリを開放
  */
static void stop_server(server_t * sv) {
  if (sv->repo) {
    document_repo_destroy(sv->repo);
  }
  if (sv->log_wp) {
    fclose(sv->log_wp);
  }
  close(sv->server_sock);
  my_free(sv);
}

/**
   @brief クライアントからのリクエストの種類
  */

typedef enum {
  request_kind_put,             /**< put (ドキュメント追加) */
  request_kind_get,             /**< get (文字列検索)  */
  request_kind_getc,             /**< getc (文字列出現数)  */
  request_kind_dump,             /**< dump (全ドキュメントダンプ) */
  request_kind_dumpc,             /**< dumpc (全ドキュメント数) */
  request_kind_save,             /**< save */
  request_kind_discon,            /**< discon (接続終了) */
  request_kind_quit,            /**< quit (サーバ終了) */
  request_kind_invalid,         /**< 無効なリクエスト  */
} request_kind_t;

const int max_inst_len = 20;    /**< 命令(put, get, quit, ...)の最大長  */
const int max_num_len = 20;     /**< 数字の最大桁数  */

/**
   @brief クライアントからのリクエストを表すデータ構造
  */
typedef struct {
  request_kind_t kind;          /**< 種類(put, get, quit, ...)  */
  union {
    struct {
      char * label;             /**< putされるドキュメントのラベル(タイトル, ファイル名など任意の文字列 etc.) */
      size_t label_len;         /**< labelの長さ(バイト数) */
      char * data;              /**< putされるドキュメントの中身 */
      size_t data_len;          /**< dataの長さ(バイト数) */
    } put;
    struct {
      char * query;             /**< 検索文字列 */
      size_t query_len;         /**< queryの長さ(バイト数) */
    } get;
  };
} request_t;

/**
   @brief ソケットにbufから指定されたnバイト全て送る.
   @return 実際に送信されたバイト数

   @details 指定されたnバイト全て送りきるまでsendする. 
   途中でエラーが発生したら終了. 送れたバイト数を返す.
   返り値がnでなければ, エラーにより, 意図したデータ
   が送信できなかったということ
   */
static ssize_t send_bytes(int so, char * buf, size_t n) {
  size_t sent = 0;
  while (sent < n) {
    ssize_t r = send(so, buf + sent, n - sent, 0);
    if (r == -1) {
      api_err("send");
      return -1;
    }
    assert(r > 0);
    sent += r;
  }
  return sent;
}

/**
   @brief 数字を送信
  */
static int send_num(int so,   /**< ソケット */
                    size_t x, /**< 送信する数 */
                    int ws) { /**< 数の後ろに送る空白文字(通常' 'または'\n') */
  const int max_width = 20;
  char s[max_width];
  memset(s, 0, max_width);
  sprintf(s, "%ld%c", x, ws);
  ssize_t n = strlen(s);
  ssize_t sent = send_bytes(so, s, n);
  if (sent == n) {
    return 1;                     /* OK */
  } else {
    return 0;                   /* NG */
  }
}

/**
   @brief 数字を送信
  */
static int send_ok_and_num(int so,   /**< ソケット */
                           size_t x, /**< 送信する数 */
                           int ws) { /**< 数の後ろに送る空白文字(通常' 'または'\n') */
  const int max_width = 20;
  char s[max_width];
  memset(s, 0, max_width);
  sprintf(s, "OK %ld%c", x, ws);
  ssize_t n = strlen(s);
  ssize_t sent = send_bytes(so, s, n);
  if (sent == n) {
    return 1;                     /* OK */
  } else {
    return 0;                   /* NG */
  }
}

/**
   @brief 返事 "NG + 理由\n" を送信
  */
static int send_ng(int so, char * msg) {
  char * rep = "NG ";
  ssize_t rep_sz = strlen(rep);
  ssize_t msg_sz = strlen(msg);
  char * newline = "\n";
  ssize_t newline_sz = strlen(newline);
  ssize_t sent = send_bytes(so, rep, rep_sz);
  if (sent != rep_sz) return 0; /* NG */
  sent = send_bytes(so, msg, msg_sz);
  if (sent != msg_sz) return 0; /* NG */
  sent = send_bytes(so, newline, newline_sz);
  if (sent != newline_sz) return 0; /* NG */
  return 1;                         /* OK */
}

/**
   @brief ソケットからbufにnバイト受信する.
   @return 実際に受信されたバイト数

   @details 指定されたバイト数(n)受信するまでrecvする. 
   途中でエラーが発生したり, データ(接続)が終了したら終了. 
   受け取れたバイト数を返す. 返り値がnでなかったらそれは,
   エラーまたは相手の接続断により, 意図したデータが受信
   できていないということ.
*/
/* receive exactly n bytes or until EOF.
   return -1 if we encountered an error.
   return the number of bytes read otherwise */
static ssize_t recv_bytes(int so, size_t n, char * buf) {
  size_t received = 0;
  while (received < n) {
    ssize_t r = recv(so, buf + received, n - received, 0);
    if (r == -1) {
      api_err("recv");
      return -1;
    }
    if (r == 0) {
      fprintf(stderr,
              "premature end of stream. expected %ld bytes"
              " but ended with %ld bytes\n", n, received);
      return received;
    }
    received += r;
  }
  return received;
}

/** 
    @brief 空白文字(' ', '\\t', '\\n'など. man isspaceを参照)が現れるまで, 最大でnバイト, データを読む. 

    @details 空白文字(' ', '\\t', '\\n'など. man isspaceを参照)が現れるかnバイト読まれるまでデータを読む. そうなる前にエラーになったり, 接続が終了したら終了.
    エラーが生じたら-1, そうでなければ実際に読まれたバイト数(空白文字含め)を返す. 返り値をmとしたとき, m > 0 かつ isspace(buf[m - 1]) が成り立っていれば意図通りのデータが読まれている.
 */
static ssize_t recv_until_ws(int so, size_t n, char * buf) {
  size_t received = 0;
  while (received < n) {
    ssize_t r = recv(so, buf + received, 1, 0);
    if (r == -1) {
      api_err("recv");
      return -1;
    }
    if (r == 0) {
      if (received) {
        fprintf(stderr,
                "premature end of stream. expected %ld bytes"
                " but ended with %ld bytes\n", n, received);
      }
      return received;
    }
    received++;
    if (isspace(buf[received - 1])) break;
  }
  if (!isspace(buf[received - 1])) {
    fprintf(stderr, "error: a line too long (> %ld bytes) [%s...]\n", n, buf);
  }
  return received;
}

/**
   @brief ソケットから数字の列を読み込みそれを数字として返す. 

   @details 例えば, '3', '4', '5', 空白文字 が順に読み込まれたら, 
   空白文字 まで読み込んだところで 345 を返す.

 */
static ssize_t recv_num(int so) {
  char num[max_num_len + 1];
  memset(num, 0, max_num_len + 1);
  ssize_t num_len = recv_until_ws(so, max_num_len, num);
  if (num_len <= 0) return -1;
  if (!isspace(num[num_len - 1])) return -1;
  char * end = 0;
  long int sz = strtol(num, &end, 10);
  if (end == num) {
    api_err("invalid number");
    fprintf(stderr, "  [%s]\n", num);
    return -1;
  } else {
    if (end != num + num_len - 1) {
      fprintf(stderr, "warning: junk after message size [%s] (ignored)\n", num);
    }
    return sz;
  }
}

/**
   @brief quit メッセージを受信
 */
static request_t server_recv_message_quit(int so) {
  (void)so;
  request_t req;
  req.kind = request_kind_quit;
  return req;
}

/**
   @brief discon メッセージを受信
 */
static request_t server_recv_message_discon(int so) {
  (void)so;
  request_t req;
  req.kind = request_kind_discon;
  return req;
}

/**
   @brief dump メッセージを受信
 */
static request_t server_recv_message_dump(int so) {
  (void)so;
  request_t req;
  req.kind = request_kind_dump;
  return req;
}

/**
   @brief dumpc メッセージを受信
 */
static request_t server_recv_message_dumpc(int so) {
  (void)so;
  request_t req;
  req.kind = request_kind_dumpc;
  return req;
}

/**
   @brief put メッセージを受信

   @details putメッセージの形式 (put 空白 まですでに読み込み済み) 

     put 空白 LABEL_LEN LABEL 空白 DATA_LEN DATA

     LABEL_LEN, DATA_LENはそれぞれLABEL, DATAの長さ(バイト数)

 */
static request_t server_recv_message_put(int so) {
  request_t req;
  req.kind = request_kind_invalid;

  /* LABEL_LEN + LABEL を受信 */
  ssize_t label_len = recv_num(so);
  if (label_len == -1) return req;
  char * label = malloc_or_err(label_len + 1);
  if (!label) return req;
  ssize_t r = recv_bytes(so, label_len, label);
  if (r != label_len) return req;
  label[label_len] = 0;

  /* LABEL 後の空白を受信 */
  char ws[1];
  r = recv_bytes(so, 1, ws);
  if (r != 1) return req;
  if (!isspace(ws[0])) {
    fprintf(stderr,
            "expected a whitespace but received %c after label (%s)\n",
            ws[0], label);
    return req;
  }

  /* DATA_LEN + DATA を受信 */
  ssize_t data_len = recv_num(so);
  if (data_len == -1) {
    my_free(label);
    return req;
  }
  char * data = malloc_or_err(data_len + 1);
  if (!data) {
    my_free(label);
    return req;
  }
  r = recv_bytes(so, data_len, data);
  if (r != data_len) {
    my_free(label);
    return req;
  }
  data[data_len] = 0;
  
  req.kind = request_kind_put;
  req.put.label_len = label_len;
  req.put.label = label;
  req.put.data_len = data_len;
  req.put.data = data;
  return req;
}

/**
   @brief getc メッセージを受信

   @details getc メッセージの形式 (getc 空白 まですでに読み込み済み) 

   getc 空白 QUERY_LEN QUERY

   QUERY_LENはQUERYの長さ(バイト数)

 */
static request_t server_recv_message_getc(int so) {
  request_t req;
  req.kind = request_kind_invalid;

  /* QUERY_LEN + QUERYを受信 */
  ssize_t query_len = recv_num(so);
  if (query_len == -1) return req;
  /* allocate the buffer for the payload */
  char * query = malloc_or_err(query_len + 1);
  if (!query) return req;
  /* receive the payload */
  ssize_t r = recv_bytes(so, query_len, query);
  if (r != query_len) return req;
  query[query_len] = 0;

  req.kind = request_kind_getc;
  req.get.query_len = query_len;
  req.get.query = query;
  return req;
}

/**
   @brief get メッセージを受信

   @details get メッセージの形式 (get 空白 まですでに読み込み済み) 

   get 空白 QUERY_LEN QUERY

   QUERY_LENはQUERYの長さ(バイト数)

 */
static request_t server_recv_message_get(int so) {
  request_t req;
  req.kind = request_kind_invalid;

  /* QUERY_LEN + QUERYを受信 */
  ssize_t query_len = recv_num(so);
  if (query_len == -1) return req;
  /* allocate the buffer for the payload */
  char * query = malloc_or_err(query_len + 1);
  if (!query) return req;
  /* receive the payload */
  ssize_t r = recv_bytes(so, query_len, query);
  if (r != query_len) return req;
  query[query_len] = 0;

  req.kind = request_kind_get;
  req.get.query_len = query_len;
  req.get.query = query;
  return req;
}

static request_t server_recv_message_save(int so) {
  (void)so;
  request_t req;
  req.kind = request_kind_save;
  return req;
}

/** 
    @brief ソケットからリクエストメッセージをひとつ受信する.

    @details 現在は以下の3種類 (空白 は ' ' でも '\\n' でもよい. isspaceが1を返す任意の文字)
   
   (1) quit 空白

   (2) put 空白 LABEL_LEN 空白 LABEL 空白 DATA_LEN DATA

   (3) get 空白 QUERY_LEN 空白 QUERY

 */

static request_t server_recv_message(int so) {
  request_t req;
  req.kind = request_kind_invalid;
  char inst[max_inst_len + 1];
  /* clear all bytes in inst */
  memset(inst, 0, max_inst_len + 1);
  /* get a line */
  ssize_t inst_len = recv_until_ws(so, max_inst_len, inst);
  /* error? -> return invalid */
  if (inst_len <= 0) return req;
  /* could not get a line */
  if (!isspace(inst[inst_len - 1])) return req;
  inst[inst_len - 1] = 0;
  /* parse the instruction (quit, get or put) */
  if (strcasecmp(inst, "quit") == 0) {
    return server_recv_message_quit(so);
  } else if (strcasecmp(inst, "discon") == 0) {
    return server_recv_message_discon(so);
  } else if (strcasecmp(inst, "dumpc") == 0) {
    return server_recv_message_dumpc(so);
  } else if (strcasecmp(inst, "dump") == 0) {
    return server_recv_message_dump(so);
  } else if (strcasecmp(inst, "put") == 0) {
    return server_recv_message_put(so);
  } else if (strcasecmp(inst, "getc") == 0) {
    return server_recv_message_getc(so);
  } else if (strcasecmp(inst, "get") == 0) {
    return server_recv_message_get(so);
  } else if (strcasecmp(inst, "save") == 0) {
    return server_recv_message_save(so);
  } else {
    fprintf(stderr, "invalid command [%s]\n", inst);
  }
  return req;
}

/**
   @brief putメッセージを処理
   @return 1 (成功) または 0 (失敗)
  */
static int connection_handle_put(request_t req, int so, server_t * sv) {
  if (sv->log_wp) {
    fprintf(sv->log_wp, "put label[%ld]=[%s] data[%ld]=[...]\n",
            req.put.label_len, req.put.label,
            req.put.data_len); // , req.put.data
    fflush(sv->log_wp);
  }
  document_t doc = { req.put.label, 0, req.put.label_len, 
                     req.put.data, 0, req.put.data_len };
  ssize_t c = document_repo_add(sv->repo, doc);
  if (c == -1) {
    return send_ng(so, "could not put the requested document");
  } else {
    return send_ok_and_num(so, c, '\n');
  }
}

/** 検索結果のスニペットに含める, 出現部分に先立つバイト数 */
static const size_t snippet_prefix_len = 12;
/** 検索結果のスニペットに含める, 出現部分に続くバイト数 */
static const size_t snippet_suffix_len = 12;

/**
   @brief getメッセージを処理
   @return 1 (成功) または 0 (失敗)
  */
static int connection_handle_getc(request_t req, int so, server_t * sv) {
  char * q = req.get.query;
  size_t qlen = req.get.query_len;
  if (sv->log_wp) {
    fprintf(sv->log_wp, "getc query[%ld]=[%s]\n", qlen, q);
    fflush(sv->log_wp);
  }
  /* 検索を実行 */
  size_t c = document_repo_queryc(sv->repo, q, qlen);
  my_free(q);
  return send_ok_and_num(so, c, '\n');
}

/**
   @brief getメッセージを処理
   @return 1 (成功) または 0 (失敗)
  */
static int connection_handle_get(request_t req, int so, server_t * sv) {
  char * q = req.get.query;
  size_t qlen = req.get.query_len;
  if (sv->log_wp) {
    fprintf(sv->log_wp, "get query[%ld]=[%s]\n", qlen, q);
    fflush(sv->log_wp);
  }
  /* 検索を実行 */
  size_t c = document_repo_queryc(sv->repo, q, qlen);
  if (!send_ok_and_num(so, c, '\n')) return 0;
  
  query_result_t qr[1] = { document_repo_query(sv->repo, q, qlen) };
  /* 結果(出現位置)を順に取り出して返事を送信. 形式:

     (LABEL_LEN LABEL <改行> SNIPPET_LEN SNIPPET <改行>)* 0

     LABEL_LEN, SNIPPET_LENはそれぞれLABEL, SNIPPETの長さ(バイト数)
     
 */
  size_t cx = 0;
  char * labels_base = qr->repo->labels->a;
  char * data_base   = qr->repo->data->a;
  while (1) {
    occurrence_t occ = query_result_next(qr);
    if (occ.offset == -1) break;
    cx++;
    /* 出現位置 */
    ssize_t o = occ.offset;
    /* 出現位置を含む周辺(スニペット)を返す.
       例えば O バイト目に現れたら 
       (O - snippet_prefix_len) バイト目から
       (O + 検索文字列長 + snippet_suffix_len - 1) バイト目
       までを返す.
       ただしドキュメントの先頭や終了を飛び越えないように注意 */
    /* スニペット先頭. ただし < 0 になったら 0 */
    ssize_t start = o - snippet_prefix_len;
    if (start < 0) start = 0;
    /* スニペット終わり. ただし >= ドキュメント長 になったらドキュメント長  */
    ssize_t end = o + qlen + snippet_suffix_len;
    if (end > (ssize_t)occ.doc.data_len) end = occ.doc.data_len;
    /* ラベル長 ラベル を送信 */
    if (!send_num(so, occ.doc.label_len, ' ')) return 0;
    if (!send_bytes(so, labels_base + occ.doc.label_o, occ.doc.label_len))
      return 0;
    if (!send_bytes(so, " ", 1)) return 0;
    /* 出現位置を送信 */
    if (!send_num(so, occ.offset, ' ')) return 0;
    /* スニペット長 スニペット を送信 */
    if (!send_num(so, end - start, ' ')) return 0;
    if (!send_bytes(so, data_base + occ.doc.data_o + start, end - start))
      return 0;
    if (!send_bytes(so, "\n", 1)) return 0;
  }
  if (cx != c) {
    fprintf(stderr, "occurrence count did not match (before: %ld after: %ld)\n",
            c, cx);
  }
  my_free(q);
  return send_num(so, 0, '\n');
}

/**
   @brief dumpメッセージを処理
   @return 0
  */
static int connection_handle_dump(request_t req, int so, server_t * sv) {
  (void)req;
  if (sv->log_wp) {
    fprintf(sv->log_wp, "dump\n");
    fflush(sv->log_wp);
  }
  /* 検索を実行 */
  size_t c = document_repo_n_docs(sv->repo);
  if (!send_ok_and_num(so, c, '\n')) return 0;
  
  dump_result_t dr[1] = { document_repo_dump(sv->repo) };
  /* 結果(出現位置)を順に取り出して返事を送信. 形式:

     (LABEL_LEN LABEL <改行> SNIPPET_LEN SNIPPET <改行>)* 0

     LABEL_LEN, SNIPPET_LENはそれぞれLABEL, SNIPPETの長さ(バイト数)
     
 */
  size_t cx = 0;
  char * labels_base = dr->repo->labels->a;
  char * data_base   = dr->repo->data->a;
  while (1) {
    document_t doc = dump_result_next(dr);
    if (doc.label_o == -1) break;
    cx++;
    if (!send_num(so, doc.label_len, ' ')) return 0;
    if (!send_bytes(so, labels_base + doc.label_o, doc.label_len)) return 0;
    if (!send_bytes(so, " ", 1)) return 0;
    if (!send_num(so, doc.data_len, ' ')) return 0;
    if (!send_bytes(so, data_base + doc.data_o, doc.data_len)) return 0;
    if (!send_bytes(so, "\n", 1)) return 0;
  }
  if (cx != c) {
    fprintf(stderr, "occurrence count did not match (before: %ld after: %ld)\n",
            c, cx);
  }
  return send_num(so, 0, '\n');
}

/**
   @brief dumpcメッセージを処理
   @return 0
  */
static int connection_handle_dumpc(request_t req, int so, server_t * sv) {
  (void)req;
  if (sv->log_wp) {
    fprintf(sv->log_wp, "dumpc\n");
    fflush(sv->log_wp);
  }
  /* 検索を実行 */
  size_t c = document_repo_n_docs(sv->repo);
  return send_ok_and_num(so, c, '\n');
}

/**
   @brief saveメッセージを処理
   @return 0
  */
static int connection_handle_save(request_t req, int so, server_t * sv) {
  (void)req;
  if (sv->log_wp) {
    fprintf(sv->log_wp, "save\n");
    fflush(sv->log_wp);
  }
  /* 検索を実行 */
  size_t c = document_repo_save(sv->repo, sv->opt.data_dir);
  return send_ok_and_num(so, c, '\n');
}

/**
   @brief quitメッセージを処理
   @return 0
  */
static int connection_handle_quit(request_t req, int so, server_t * sv) {
  (void)req;
  (void)so;
  if (sv->log_wp) {
    fprintf(sv->log_wp, "quit\n");
    fflush(sv->log_wp);
  }
  sv->server_continues = 0;
  return 0;
}

/**
   @brief disconメッセージを処理
   @return 0
  */
static int connection_handle_discon(request_t req, int so, server_t * sv) {
  (void)req;
  (void)so;
  if (sv->log_wp) {
    fprintf(sv->log_wp, "discon\n");
    fflush(sv->log_wp);
  }
  return 0;
}

/**
   @brief 無効メッセージを処理
   @return 0
  */
static int connection_handle_invalid(request_t req, int so, server_t * sv) {
  (void)req;
  (void)so;
  (void)sv;
  return 0;                     /* NG. abandon this connection */
}

/**
   @brief 1クライアントからの接続を処理
   @return 1 (サーバーは処理を継続可能), 
   0 (重大エラーまたはquitの受信によりサーバは処理を終了)

   @details 1クライアントと接続されたソケット(so)からメッセージ受信,
   その処理, を繰り返す.
  */
static int server_process_connection(int so, server_t * sv) {
  int connection_continues = 1;
  while (connection_continues) {
    request_t req = server_recv_message(so);
    switch (req.kind) {
    case request_kind_put:
      connection_continues = connection_handle_put(req, so, sv);
      break;
    case request_kind_getc:
      connection_continues = connection_handle_getc(req, so, sv);
      break;
    case request_kind_get:
      connection_continues = connection_handle_get(req, so, sv);
      break;
    case request_kind_dump:
      connection_continues = connection_handle_dump(req, so, sv);
      break;
    case request_kind_dumpc:
      connection_continues = connection_handle_dumpc(req, so, sv);
      break;
    case request_kind_save:
      connection_continues = connection_handle_save(req, so, sv);
      break;
    case request_kind_discon:
      connection_continues = connection_handle_discon(req, so, sv);
      break;
    case request_kind_quit:
      connection_continues = connection_handle_quit(req, so, sv);
      break;
    case request_kind_invalid:
      connection_continues = connection_handle_invalid(req, so, sv);
      break;
    default:
      internal_err("unknown request kind");
      connection_continues = 0;
      break;
    }
  }
  close(so);
  return 1;
}

/**
   @brief クライアントからの接続を待つ
   @return クライアントと接続されたソケット

   @details クライアントから接続要求が来るのを待ち, 
   クライアントと接続されたソケットを返す
  */
static int server_accept_connection(server_t * sv) {
  struct sockaddr_in addr[1];
  socklen_t addrlen = sizeof(addr);
  int ss = sv->server_sock;
  int so = accept(ss, (struct sockaddr *)addr, &addrlen);
  if (so == -1) {
    api_err("accept");
  } else {
    struct sockaddr_in caddr[1];
    socklen_t caddrlen = sizeof(caddr);
    if (getsockname(ss, (struct sockaddr *)caddr, &caddrlen) == -1) {
      api_err("getpeername");
      close(so);
    } else {
      if (sv->log_wp) {
        fprintf(sv->log_wp, "accepted connection from %s\n",
                inet_ntoa(caddr->sin_addr));
        fflush(sv->log_wp);
      }
    }
  }
  return so;
}

/**
   @brief server_process_connectionの引数
  */
typedef struct {
  int so;
  server_t * sv;
} server_process_connection_arg_t;

/**
 */
void * server_process_connection_thread_fun(void * args_) {
  server_process_connection_arg_t * args = args_;
  int so = args->so;
  server_t * sv = args->sv;
  int term_wfd = sv->term_fd[1];
  my_free(args_);

  int r = server_process_connection(so, sv);
  /* notify the main thread of my termination */
  pthread_t tid = pthread_self();
  ssize_t tid_sz = sizeof(pthread_t);
  ssize_t w = write(term_wfd, &tid, tid_sz);
  if (w == tid_sz) {
    if (sv->log_wp) {
      fprintf(sv->log_wp, "thread %lu finished\n", tid);
      fflush(sv->log_wp);
    }
  } else {
    api_err("write");
    exit(1);
  }
  return (void *)(intptr_t)r;
}

typedef enum {
  server_event_kind_err,
  server_event_kind_accept_connection,
  server_event_kind_join_thread,
} server_event_kind_t;

int server_join_thread(server_t * sv) {
  pthread_t tid = 0;
  ssize_t tid_sz = sizeof(tid);
  int term_rfd = sv->term_fd[0];
  ssize_t r = read(term_rfd, &tid, tid_sz);
  if (r != tid_sz) {
    api_err("read");
    return 0;                   /* NG */
  }
  void * retval = 0;
  int e = pthread_join(tid, &retval);
  if (e == 0) {
    sv->nthreads--;
    if (sv->log_wp) {
      fprintf(sv->log_wp, "reaped thread %lu\n", tid);
      fflush(sv->log_wp);
    }
    return 1;
  } else {
    api_err("pthread_join");
    return 0;
  }
}

/**

  */
int server_process_connection_thread(int so, server_t * sv) {
  pthread_t tid = 0;
  server_process_connection_arg_t * args
    = malloc(sizeof(server_process_connection_arg_t));
  args->so = so;
  args->sv = sv;
  
  if (pthread_create(&tid, 0, server_process_connection_thread_fun, args) == 0) {
    sv->nthreads++;
    return 1;                   /* OK */
  } else {
    api_err("pthread_create");
    return 0;
  }
}

/**
   @brief クライアントからの接続を待つ
   @return クライアントと接続されたソケット

   @details クライアントから接続要求が来るのを待ち, 
   クライアントと接続されたソケットを返す
  */
server_event_kind_t server_wait_for_event(server_t * sv) {
  int ss = sv->server_sock;
  int term_rfd = sv->term_fd[0];
  int max_fd = term_rfd;

  if (sv->server_continues) {
    if (max_fd < ss) max_fd = ss;
  }
  int nfds = max_fd + 1;

  /* check if any outstanding thread has finished */
  fd_set rfds[1], wfds[1], efds[2];
  FD_ZERO(rfds);
  FD_ZERO(wfds);
  FD_ZERO(efds);
  if (sv->server_continues) {
    FD_SET(ss, rfds);
  }
  FD_SET(term_rfd, rfds);
  int n_ready = select(nfds, rfds, wfds, efds, 0);
  if (n_ready == -1) {
    api_err("select");
    return server_event_kind_err;
  } else if (n_ready == 0) {
    internal_err("select returns 0");
    return server_event_kind_err;
  } else if (FD_ISSET(term_rfd, rfds)) {
    return server_event_kind_join_thread;
  } else if (sv->server_continues && FD_ISSET(ss, rfds)) {
    return server_event_kind_accept_connection;
  } else {
    internal_err("neither term_rfd nor server socket are ready");
    return server_event_kind_err;
  }
}

/**
   @brief サーバを実行する

   @details サーバを生成し, 接続用のオープン, 接続待ち, その処理まで,
   全てを行う
  */
static void run_server(cmdline_options_t opt) {
  server_t * sv = start_server(opt);
  if (!sv) return;
  while (sv->server_continues || sv->nthreads > 0) {
    server_event_kind_t ev = server_wait_for_event(sv);
    if (ev == server_event_kind_err) {
      sv->server_continues = 0;
    } else if (ev == server_event_kind_accept_connection) {
      int so = server_accept_connection(sv);
      if (so == -1) break;
      if (opt.thread) {
        if (!server_process_connection_thread(so, sv)) break;
      } else {
        if (!server_process_connection(so, sv)) break;
      }
    } else if (ev == server_event_kind_join_thread) {
      server_join_thread(sv);
    } else {
      fprintf(stderr, "invalid server_event_kind %d\n", ev);
      internal_err("invalid server_event_kind");
    }
  }
  stop_server(sv);
}

/** @brief デフォルトのポート番号(0 : OSに選ばせる)  */
#define options_default_port 0
/** @brief デフォルトの接続待ちキュー */
#define options_default_qlen 1000
/** @brief デフォルトのログファイル名 */
#define options_default_log "unagi.log"
/** @brief デフォルトの保存ディレクトリ名 */
#define options_default_data_dir "unagi_data"
#define options_default_load_data 0
/** @brief デフォルトでスレッドを使うか */
#define options_default_thread 0

/**
   @brief デフォルトのコマンドラインオプションを作る

   @sa parse_args
   @sa cmdline_options_t
  */
static cmdline_options_t default_opts() {
  cmdline_options_t opt;
  opt.port = options_default_port;
  opt.qlen = options_default_qlen;
  opt.log = strdup(options_default_log);
  opt.data_dir = strdup(options_default_data_dir);
  opt.load_data = 0;
  opt.thread = options_default_thread;
  opt.error = 0;
  opt.help = 0;
  return opt;
}

/**
   @brief cmdline_options_t のメモリを開放する
*/
static void cmdline_options_destroy(cmdline_options_t opt) {
  my_free(opt.log);
}

/**
   @brief 使用法を表示
  */
static void unagi_usage(const char * prog) {
  fprintf(stderr,
          "usage:\n"
          "\n"
          "%s [options ...]\n"
          "\n"
          "options:\n"
          "  -p PORT : the port number the server listens to [%d]\n"
          "  -q QLEN : the length of the listen queue [%d]\n"
          "  -l LOG_FILE : log file. not generated if the empty string \"\" is given [%s]\n"
          "  -t 0/1 : use thread or not [%d]\n"
          ,
          prog,
          options_default_port,
          options_default_qlen,
          options_default_log,
          options_default_thread);
}


/**
   @brief コマンドラインを処理

   @sa cmdline_options_t
  */
static cmdline_options_t parse_args(int argc, char ** argv) {
  char * prog = argv[0];
  cmdline_options_t opt = default_opts();
  while (1) {
    int c = getopt(argc, argv, "d:l:p:q:t:Lh");
    if (c == -1) break;
    switch (c) {
    case 'd':
      my_free(opt.data_dir);
      opt.data_dir = strdup(optarg);
      break;
    case 'L':
      opt.load_data = 1;
      break;
    case 'l':
      my_free(opt.log);
      opt.log = strdup(optarg);
      break;
    case 'p':
      opt.port = atoi(optarg);
      break;
    case 'q':
      opt.qlen = atoi(optarg);
      break;
    case 't':
      opt.thread = atoi(optarg);
      break;
    case 'h':
      opt.help = 1;
      break;
    default: /* '?' */
      unagi_usage(prog);
      opt.error = 1;
      return opt;
    }
  }
  return opt;
}

/**
   @brief メイン関数

   @details コマンドラインを処理. サーバを立ち上げ. 
   停止(quit)命令を受け取るまで処理を
  */
int main(int argc, char ** argv) {
  cmdline_options_t opt = parse_args(argc, argv);
  if (opt.help) {
    unagi_usage(argv[0]);
    return EXIT_SUCCESS;
  }
  if (opt.error) return EXIT_FAILURE;
  run_server(opt);
  cmdline_options_destroy(opt);
  return EXIT_SUCCESS;
}

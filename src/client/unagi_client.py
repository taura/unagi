#!/usr/bin/python3
import sys,socket,random,time,string

#
# @brief ソケットから接続が切れるまでデータを受取り表示
# @param (so) ソケット
#
def recv_until_eos(so):
    while 1:
        rep = so.recv(10000)
        if not rep: break
        print(rep.decode("utf-8", "ignore"), end="")
    so.close()

#
# @brief 接続し, メッセージを送り, 接続が切れるまでデータを受取り表示
# @param (ip) 接続先IPアドレス
# @param (port) 接続先ポート
# @param (msg) 送るメッセージ
#
def send_msg_and_wait(ip, port, msg):
    so = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    so.connect((ip, port))
    so.sendall(msg)
    so.shutdown(socket.SHUT_WR)
    recv_until_eos(so)
    
#
# @brief ランダムなデータの生成
# @param (seed) 乱数の種
# @param (skip) 読み飛ばす乱数の数
# @param (n) 生成する文字数
# @param (alphabet) 文字種類
# @details 例: mk_random_data(1234, 100, 500, "ab") は乱数の種1234
# から100個の乱数を読み飛ばし, 'a', 'b'からなる500文字の文字列を生成
#
def mk_random_data(seed, skip, n, alphabet):
    rg = random.Random()
    rg.seed(seed)
    s = "".join([ rg.choice(alphabet) for i in range(skip + n) ])
    ws = string.whitespace
    for i in range(skip, 0, -1):
        if i > 0 and s[i] not in ws and s[i - 1] in ws:
            return s[i:]
    else:
        return s

#
# @brief 文書をputするためのメッセージ(wire data)を生成
# @param (label) 文書のラベル
# @param (data) 文書のデータ
#
def mk_put_msg(label, data):
    label = bytes(label, "utf8")
    data = bytes(data, "utf8")
    msg = b"put\n%d\n%s\n%d\n%s" % (len(label), label, len(data), data)
    return msg

#
# @brief 文字列を検索(get)するためのメッセージ(wire data)を生成
# @param (query) 検索文字列
#
def mk_get_msg(query):
    query = bytes(query, "utf8")
    msg = b"get\n%d\n%s" % (len(query), query)
    return msg

#
# @brief 文字列の出現数を問い合わせる(getc)するためのメッセージ(wire data)を生成
# @param (query) 検索文字列
#
def mk_getc_msg(query):
    query = bytes(query, "utf8")
    msg = b"getc\n%d\n%s" % (len(query), query)
    return msg

#
# @brief ランダムな文字列をputするためのwire dataをファイルに格納
# @param (label) 文書のラベル
# @param (seed) 乱数の種
# @param (skip) 読み飛ばす乱数の数
# @param (n) 生成する文字数
# @param (alphabet) 文字種類
# @param (filename) wire dataを格納するファイル名
# @details 例: make_put_random_data(1234, 100, 500, "ab", "p.dat")
# は乱数の種1234から100個の乱数を読み飛ばし, 'a', 'b'からなる500文字
# の文字列を生成し, p.datに格納
#
def make_put_random(label, seed, skip, n, alphabet, filename):
    data = mk_random_data(seed, skip, n, alphabet)
    msg = mk_put_msg(label, data)
    wp = open(filename, "wb")
    wp.write(msg)
    wp.close()

#
# @brief ランダムな文字列を検索するためのwire dataをファイルに格納
# @param (seed) 乱数の種
# @param (skip) 読み飛ばす乱数の数
# @param (n) 生成する文字数
# @param (alphabet) 文字種類
# @param (filename) wire dataを格納するファイル名
#
def make_get_random(seed, skip, n, alphabet, filename):
    query = mk_random_data(seed, skip, n, alphabet)
    msg = mk_get_msg(query)
    wp = open(filename, "wb")
    wp.write(msg)
    wp.close()

#
# @brief ランダムな文字列の出現数を問い合わせるためのwire dataをファイルに格納
# @param (seed) 乱数の種
# @param (skip) 読み飛ばす乱数の数
# @param (n) 生成する文字数
# @param (alphabet) 文字種類
# @param (filename) wire dataを格納するファイル名
#
def make_getc_random(seed, skip, n, alphabet, filename):
    query = mk_random_data(seed, skip, n, alphabet)
    msg = mk_getc_msg(query)
    wp = open(filename, "wb")
    wp.write(msg)
    wp.close()


#
# @brief 文書をput
# @param (ip) 接続先IPアドレス
# @param (port) 接続先ポート
# @param (label) 文書のラベル
# @param (data) 文書のデータ
#
def send_put(ip, port, label, data):
    msg = mk_put_msg(label, data)
    send_msg_and_wait(ip, port, msg)

#
# @brief ランダムな文字列をput
# @param (ip) 接続先IPアドレス
# @param (port) 接続先ポート
# @param (label) 文書のラベル
# @param (seed) 乱数の種
# @param (skip) 読み飛ばす乱数の数
# @param (n) 生成する文字数
# @param (alphabet) 文字種類
# @param (filename) wire dataを格納するファイル名
# @details 例: make_put_random_data(1234, 100, 500, "ab", "p.dat")
# は乱数の種1234から100個の乱数を読み飛ばし, 'a', 'b'からなる500文字
# の文字列を生成し, p.datに格納
#
def send_put_random(ip, port, label, seed, skip, n, alphabet):
    data = mk_random_data(seed, skip, n, alphabet)
    msg = mk_put_msg(label, data)
    send_msg_and_wait(ip, port, msg)

#
# @brief 文字列を検索
# @param (ip) 接続先IPアドレス
# @param (port) 接続先ポート
# @param (query) 検索文字列
#
def send_get(ip, port, query):
    msg = mk_get_msg(query)
    send_msg_and_wait(ip, port, msg)

#
# @brief 文字列の出現数を問い合わせ
# @param (ip) 接続先IPアドレス
# @param (port) 接続先ポート
# @param (query) 検索文字列
#
def send_getc(ip, port, query):
    msg = mk_getc_msg(query)
    send_msg_and_wait(ip, port, msg)

#
# @brief ランダムな文字列を検索
# @param (ip) 接続先IPアドレス
# @param (port) 接続先ポート
# @param (seed) 乱数の種
# @param (skip) 読み飛ばす乱数の数
# @param (n) 生成する文字数
# @param (alphabet) 文字種類
#
def send_get_random(ip, port, seed, skip, n, alphabet):
    query = mk_random_data(seed, skip, n, alphabet)
    msg = mk_get_msg(query)
    send_msg_and_wait(ip, port, msg)

#
# @brief ランダムな文字列の出現数を問い合わせ
# @param (ip) 接続先IPアドレス
# @param (port) 接続先ポート
# @param (seed) 乱数の種
# @param (skip) 読み飛ばす乱数の数
# @param (n) 生成する文字数
# @param (alphabet) 文字種類
#
def send_getc_random(ip, port, seed, skip, n, alphabet):
    query = mk_random_data(seed, skip, n, alphabet)
    msg = mk_getc_msg(query)
    send_msg_and_wait(ip, port, msg)

#
# @brief サーバをquit
# @param (ip) 接続先IPアドレス
# @param (port) 接続先ポート
#
def send_quit(ip, port):
    msg = b"quit\n"
    send_msg_and_wait(ip, port, msg)

#
# @brief 全文書を取り寄せ
# @param (ip) 接続先IPアドレス
# @param (port) 接続先ポート
#
def send_dump(ip, port):
    msg = b"dump\n"
    send_msg_and_wait(ip, port, msg)

#
# @brief 全文書数を取り寄せ
# @param (ip) 接続先IPアドレス
# @param (port) 接続先ポート
#
def send_dumpc(ip, port):
    msg = b"dumpc\n"
    send_msg_and_wait(ip, port, msg)

#
# @brief 全文書数を取り寄せ
# @param (ip) 接続先IPアドレス
# @param (port) 接続先ポート
#
def send_save(ip, port):
    msg = b"save\n"
    send_msg_and_wait(ip, port, msg)

#
# @brief ファイルの中身をwire dataとして送信
# @param (ip) 接続先IPアドレス
# @param (port) 接続先ポート
# @param (filename) wire dataが入ったファイル
#
def send_file(ip, port, filename):
    fp = open(filename, "rb")
    msg = fp.read()
    fp.close()
    send_msg_and_wait(ip, port, msg)

#
# @brief コマンドライン(sys.argv)で指定されたリクエストを発行
# @details コマンドラインの形式は
#   ./unagi_client.py ポート番号 コマンド コマンド固有の引数
#   コマンド: put, get, getc, put_random, get_random, get_random, prepare_random
def run_cmd():
    ip = "localhost"
    port = int(sys.argv[1])
    cmd = sys.argv[2]
    args = sys.argv[3:]
    letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ" + " " * 18
    if cmd == "put":
        send_put(ip, port, args[0], args[1])
    elif cmd == "get":
        send_get(ip, port, args[0])
    elif cmd == "getc":
        send_getc(ip, port, args[0])
    elif cmd == "put_random":
        # label, seed, n, alphabet
        send_put_random(ip, port,
                        args[0], int(args[1]), 0, int(args[2]), letters)
    elif cmd == "get_random":
        # seed, skip, n, alphabet
        send_get_random(ip, port,
                        int(args[0]), int(args[1]), int(args[2]), letters)
    elif cmd == "getc_random":
        # seed, skip, n, alphabet
        send_getc_random(ip, port,
                         int(args[0]), int(args[1]), int(args[2]), letters)
    elif cmd == "make_put_random":
        # label, seed, n, alphabet, filename
        make_put_random(args[0], int(args[1]), 0, int(args[2]), letters, args[3])
    elif cmd == "make_get_random":
        # seed, skip, n, alphabet, filename
        make_get_random(int(args[0]), int(args[1]), int(args[2]), letters, args[3])
    elif cmd == "make_getc_random":
        # seed, skip, n, alphabet, filename
        make_getc_random(int(args[0]), int(args[1]), int(args[2]), letters, args[3])
    elif cmd == "send_file":
        send_file(ip, port, args[0])
    elif cmd == "dump":
        send_dump(ip, port)
    elif cmd == "dumpc":
        send_dumpc(ip, port)
    elif cmd == "save":
        send_save(ip, port)
    elif cmd == "quit":
        send_quit(ip, port)
    else:
        assert(cmd in [ "put", "get", "getc",
                        "put_random", "get_random", "getc_random",
                        "make_put_random", "send_file",
                        "dump", "dumpc", "save", "quit" ]), cmd

def usage():
    print("""usage:

  %(prog)s PORT COMMAND args ...

    COMMAND: put, get, getc, dump, dumpc, quit, put_random, get_random, getc_random, make_put_random, send_file

    (1)  %(prog)s PORT put LABEL DATA
    (2)  %(prog)s PORT get QUERY
    (3)  %(prog)s PORT getc QUERY
    (4)  %(prog)s PORT dump
    (5)  %(prog)s PORT dumpc
    (6)  %(prog)s PORT quit
    (7)  %(prog)s PORT put_random LABEL RANDOM_SEED NUM_CHARS
    (8)  %(prog)s PORT get_random RANDOM_SEED SKIP_CHARS NUM_CHARS
    (9)  %(prog)s PORT get_random RANDOM_SEED SKIP_CHARS NUM_CHARS
    (10) %(prog)s PORT make_put_random LABEL RANDOM_SEED NUM_CHARS FILENAME
    (11) %(prog)s PORT send_file FILENAME

    """ % { "prog" : sys.argv[0] })
        
def main():
    if len(sys.argv) <= 2:
        usage()
    else:
        t0 = time.time()
        run_cmd()
        t1 = time.time()
        print("command took %.9f sec" % (t1 - t0))

if __name__ == "__main__":
    main()


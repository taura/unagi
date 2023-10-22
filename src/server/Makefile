# 
# Makefile
#

# Cコンパイラ
CC := gcc
#CC := clang

# コンパイラオプション
CFLAGS := -Wall -Wextra -pthread
#CFLAGS += -O0 -g 
CFLAGS += -O3

# リンクオプション
LDFLAGS := -pthread
LIBS :=

#
# ソースファイルのリスト
# 課題提出のために新たなファイルを作ったら以下に追加
# (すると, makeが勝手にコンパイルしてくれる)
SRCS := unagi_utility.c document_repository.c
SRCS += unagi_server.c
# SRCS += unagi_server_1.c

# *.c --> *.o
OBJS := $(patsubst %.c,%.o,$(SRCS))

#
# 作る実行可能プログラムの追加
# 以下の EXES += に追加する(複数行可).
# 例えば unagi_server_1 という実行可能プログラムを作りたくなったら
# EXES += unagi_server_1
# を追加
#  ...
# その上で
# unagi_server_1 : unagi_utility.o document_repository.o unagi_server_1.o
#	$(CC) -o $@ $+ $(LDFLAGS) $(LIBS)
# のようなエントリを追加.

EXES := unagi_server
EXES += himono_server

all : $(EXES)

unagi_server : unagi_utility.o document_repository.o unagi_server.o
	$(CC) -o $@ $+ $(LDFLAGS) $(LIBS) 

himono_server : unagi_utility.o document_repository_himono.o himono_server.o
	$(CC) -o $@ $+ $(LDFLAGS) $(LIBS) 

# ルールの追加例: 
# unagi_server_1 : unagi_utility.o document_repository.o unagi_server_1.o
# 	$(CC) -o $@ $+ $(LDFLAGS) $(LIBS) 

#
# .c を .o にする一般的ルール
#
$(OBJS) : %.o : %.c
	$(CC) -o $@ $(CFLAGS) -c $<

clean :
	rm -f *.o unagi_server

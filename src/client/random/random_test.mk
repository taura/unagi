#
# random_test.mk
# 使い方
#   make -f random_test.mk port=nnnnn put

# ポート番号 (コマンドラインで port=nnnnn として変更)
port?=10000
# documentひとつの長さ
#doc_len:=1000
doc_len:=1000000
# document数
#n_puts:=10
n_puts:=2000

skip_len:=100
query_len:=100
n_gets:=3000
#n_gets:=500

##########################################
# 以下は変更の必要なし
##########################################

put_msg_indexes:=$(shell seq 1 $(n_puts))
put_msgs:=$(addprefix msgs/put_msg_,$(put_msg_indexes))

get_msg_indexes:=$(shell seq 1 $(n_gets))
get_msgs:=$(addprefix msgs/get_msg_,$(get_msg_indexes))

put_indexes:=$(shell seq 1 $(n_puts))
send_puts:=$(addprefix status/put_,$(put_indexes))

get_indexes:=$(shell seq 1 $(n_gets))
send_gets:=$(addprefix status/get_,$(get_indexes))

put_get_indexes:=$(shell (seq 1 $(n_puts) ; seq 1 $(n_gets)) | sort -n | uniq)
send_put_gets:=$(addprefix status/put_get_,$(put_get_indexes))

make_put=../unagi_client.py $(port) make_put_random $* $* $(doc_len) $@
make_get=../unagi_client.py $(port) make_getc_random $* $(skip_len) $(query_len) $@
do_put=../unagi_client.py $(port) send_file msgs/put_msg_$*
do_get=../unagi_client.py $(port) send_file msgs/get_msg_$*

help :
	@echo "make -f random_test.mk prepare/put/get/put_get"

all : prepare

prepare : $(put_msgs) $(get_msgs)
put     : $(send_puts)
get     : $(send_gets)
put_get : $(send_put_gets)

$(put_msgs) : msgs/put_msg_% : msgs/created
	$(make_put)

$(get_msgs) : msgs/get_msg_% : msgs/created
	$(make_get)

$(send_puts) : status/put_% : status/created
	$(do_put) > status/p_$*

$(send_gets) : status/get_% : status/created
	$(do_get) > status/g_$*

$(send_put_gets) : status/put_get_% : status/created
	if [ -e msgs/put_msg_$* ] ; then $(do_put) > status/p_$* ; fi
	if [ -e msgs/get_msg_$* ] ; then $(do_get) > status/g_$* ; fi

quit :
	../unagi_client.py $(port) quit

save :
	../unagi_client.py $(port) save

status/created :
	mkdir -p $@

msgs/created :
	mkdir -p $@

.DELETE_ON_ERROR :

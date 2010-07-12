XDR_TEST_DIR:=src/xdr/test
TEST_PROGRAMS+=$(XDR_TEST_DIR)/xdr_t
$(XDR_TEST_DIR)/xdr_t: $(XDR_TEST_DIR)/xdr_t.o lib/libreplicator.a
	@echo '    [LD] '$@
	$(Q)$(CC) -o $@ $(XDR_TEST_DIR)/xdr_t.o -Llib -lreplicator -lrt
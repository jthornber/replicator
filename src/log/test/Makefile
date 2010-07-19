LOG_TEST_DIR=src/log/test
TEST_PROGRAMS+=\
	$(LOG_TEST_DIR)/log_t

$(LOG_TEST_DIR)/log_t: $(LOG_TEST_DIR)/log_t.o lib/libreplicator.a
	@echo '    [LD] '$@
	$(Q)$(CC) -o $@ $(LOG_TEST_DIR)/log_t.o -Llib -lreplicator -lrt
SNAP_TEST_DIR:=src/snapshots/test
TEST_PROGRAMS+=$(SNAPSHOTS_TEST_DIR)/block_manager_t
$(SNAP_TEST_DIR)/block_manager_t: $(SNAP_TEST_DIR)/block_manager_t.o lib/libreplicator.a
	@echo '    [LD] '$@
	$(Q)$(CC) -o $@ $(SNAP_TEST_DIR)/block_manager_t.o -Llib -lreplicator -lrt
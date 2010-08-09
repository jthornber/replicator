SNAP_TEST_DIR:=src/snapshots/test
TEST_PROGRAMS+=\
	$(SNAP_TEST_DIR)/block_manager_t \
	$(SNAP_TEST_DIR)/space_map_t \
	$(SNAP_TEST_DIR)/btree_t \
	$(SNAP_TEST_DIR)/pstore_t

$(SNAP_TEST_DIR)/block_manager_t: $(SNAP_TEST_DIR)/block_manager_t.o lib/libreplicator.a
	@echo '    [LD] '$@
	$(Q)$(CC) -o $@ $(SNAP_TEST_DIR)/block_manager_t.o -Llib -lreplicator -lrt

$(SNAP_TEST_DIR)/space_map_t: $(SNAP_TEST_DIR)/space_map_t.o lib/libreplicator.a
	@echo '    [LD] '$@
	$(Q)$(CC) -o $@ $(SNAP_TEST_DIR)/space_map_t.o -Llib -lreplicator -lrt

$(SNAP_TEST_DIR)/btree_t: $(SNAP_TEST_DIR)/btree_t.o lib/libreplicator.a
	@echo '    [LD] '$@
	$(Q)$(CC) -o $@ $(SNAP_TEST_DIR)/btree_t.o -Llib -lreplicator -lrt

$(SNAP_TEST_DIR)/pstore_t: $(SNAP_TEST_DIR)/pstore_t.o lib/libreplicator.a
	@echo '    [LD] '$@
	$(Q)$(CC) -o $@ $(SNAP_TEST_DIR)/pstore_t.o -Llib -lreplicator -lrt

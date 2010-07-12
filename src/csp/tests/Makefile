CSP_TEST=src/csp/tests
TEST_PROGRAMS+=\
	$(CSP_TEST)/process_t \
	$(CSP_TEST)/process1_t \
	$(CSP_TEST)/io1_t \
	$(CSP_TEST)/sleep_t

$(CSP_TEST)/process_t: $(CSP_TEST)/process_t.c lib/libreplicator.a
	@echo '    [CC] '$@
	$(Q)$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< -Llib -lreplicator -lrt

$(CSP_TEST)/process1_t: $(CSP_TEST)/process1_t.c lib/libreplicator.a
	@echo '    [CC] '$@
	$(Q)$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< -Llib -lreplicator -lrt

$(CSP_TEST)/io1_t: $(CSP_TEST)/io1_t.c lib/libreplicator.a
	@echo '    [CC] '$@
	$(Q)$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< -Llib -lreplicator -lrt

$(CSP_TEST)/sleep_t: $(CSP_TEST)/sleep_t.c lib/libreplicator.a
	@echo '    [CC] '$@
	$(Q)$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< -Llib -lreplicator -lrt

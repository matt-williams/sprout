# included mk file for the rlite module

RLITE_DIR := ${MODULE_DIR}/rlite
RLITE_CONFIGURE := ${RLITE_DIR}/configure
RLITE_MAKEFILE := ${RLITE_DIR}/Makefile

${RLITE_CONFIGURE}:
	cd ${RLITE_DIR} && ./buildconf

${RLITE_MAKEFILE}: ${RLITE_CONFIGURE}
	cd ${RLITE_DIR} && ./configure --prefix ${ROOT}

rlite: ${RLITE_MAKEFILE}
	${MAKE} -C ${RLITE_DIR} usr
	${MAKE} -C ${RLITE_DIR} usr_install

rlite_test:
	true

rlite_clean: ${RLITE_MAKEFILE}
	${MAKE} -C ${RLITE_DIR} clean

rlite_distclean: rlite_clean
	rm -f ${RLITE_MAKEFILE}

.PHONY: rlite rlite_test rlite_clean rlite_distclean

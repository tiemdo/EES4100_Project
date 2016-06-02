include $(top_srcdir)/common/common.am

bin_PROGRAMS = project_app



BACNET_DEPENDENT_SRC = project_app.c



project_app_SOURCES = project_app.c
project_app_CFLAGS = $(AM_CFLAGS) $(BACNET_CFLAGS) $(MODBUS_CFLAGS)
project_app_LDADD = $(AM_LIBS) $(BACNET_LIBS) $(MODBUS_LIBS)

BUILT_SOURCES = bacnet_namespace.h .bacnet_dependent_src_stamp

EXTRA_DIST = bacnet_api_names

.bacnet_dependent_src_stamp: bacnet_api_names $(BACNET_DEPENDENT_SRC)
	cat $< | $(top_srcdir)/scripts/check_namespace.sh $< $?
	touch $@

bacnet_namespace.h: bacnet_api_names
	cat $< | $(top_srcdir)/scripts/build_namespace.sh > $@

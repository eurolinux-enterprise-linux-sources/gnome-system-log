SUBDIRS = tests

INCLUDES = \
	-DG_LOG_DOMAIN=\"gnome-system-log\"			\
	-DGNOMELOCALEDIR=\""$(prefix)/$(DATADIRNAME)/locale"\"	\
	-DDATADIR=\""$(datadir)"\"				\
	-DLOGVIEWINSTALLPREFIX=\""$(prefix)/\""			\
	-I$(top_srcdir)/libgd					\
	$(NULL)

bin_PROGRAMS = gnome-system-log

resource_files = $(shell glib-compile-resources --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/logview.gresource.xml)
logview-resources.c: logview.gresource.xml $(resource_files)
	$(AM_V_GEN)glib-compile-resources --target=$@ --sourcedir=$(srcdir) --generate-source --c-name logview $(srcdir)/logview.gresource.xml
logview-resources.h: logview.gresource.xml $(resource_files)
	$(AM_V_GEN)glib-compile-resources --target=$@ --sourcedir=$(srcdir) --generate-header --c-name logview $(srcdir)/logview.gresource.xml

BUILT_SOURCES = 		\
	logview-resources.c	\
	logview-resources.h

gnome_system_log_SOURCES = 	\
	logview-app.c		\
	logview-app.h		\
	logview-main.c		\
	logview-about.h		\
	logview-manager.c	\
	logview-manager.h	\
	logview-utils.c		\
	logview-utils.h		\
	logview-loglist.c	\
	logview-loglist.h	\
	logview-window.c	\
	logview-window.h	\
	logview-log.h		\
	logview-log.c		\
	logview-findbar.h	\
	logview-findbar.c	\
	logview-prefs.c		\
	logview-prefs.h		\
	logview-filter.h       \
	logview-filter.c       \
	logview-filter-manager.h       \
	logview-filter-manager.c  \
	$(BUILT_SOURCES)

gnome_system_log_CFLAGS =	\
	$(LOGVIEW_CFLAGS)		\
	$(NULL)

gnome_system_log_LDADD =	\
	$(LOGVIEW_LIBS)		\
	$(Z_LIBS)		\
	$(top_builddir)/libgd/libgd.la \
	-lm

EXTRA_DIST =			\
	$(resource_files)	\
	logview.gresource.xml

CLEANFILES = 				\
	$(BUILT_SOURCES)

dist-hook:
	cd $(distdir) ; rm -f $(CLEANFILES)


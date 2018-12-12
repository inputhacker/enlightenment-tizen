MDIR = $(libdir)/enlightenment/modules
MOD_LDFLAGS = -module -avoid-version
MOD_CPPFLAGS = -I. -DE_LOGGING=1 \
-I$(top_srcdir) \
-I$(top_srcdir)/src/bin \
-I$(top_srcdir)/src/bin/video \
-I$(top_builddir)/src/bin \
-I$(top_builddir)/src/bin/video \
-I$(top_srcdir)/src/modules \
@e_cflags@ \
@WAYLAND_CFLAGS@ \
-DE_BINDIR=\"$(bindir)\"

MOD_LIBS = @e_libs@ @dlopen_libs@

include src/modules/Makefile_bufferqueue.mk

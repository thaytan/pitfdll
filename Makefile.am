SUBDIRS = gst-libs ext m4
EXTRA_DIST = pitfdll.spec pitfdll.spec.in \
	depcomp \
	configure.ac autogen.sh \
	AUTHORS COPYING NEWS README ChangeLog

AUTOMAKE_OPTIONS = dist-bzip2

release: dist
	make $(PACKAGE)-$(VERSION).tar.gz.md5
	make $(PACKAGE)-$(VERSION).tar.bz2.md5

%.md5: %
	md5sum $< > $@

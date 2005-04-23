SUBDIRS = gst-libs ext

EXTRA_DIST = \
	pitfdll.spec pitfdll.spec.in \
	depcomp \
	INSTALL AUTHORS COPYING NEWS README ChangeLog

AUTOMAKE_OPTIONS = dist-bzip2

release: dist
	make $(PACKAGE)-$(VERSION).tar.gz.md5
	make $(PACKAGE)-$(VERSION).tar.bz2.md5

%.md5: %
	md5sum $< > $@

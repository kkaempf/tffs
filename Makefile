export DESTDIR

build:
	scons

clean:
	scons --clean
	
install:
	scons $(if $(PREFIX),PREFIX=$(PREFIX)) \
		$(if $(SBINDIR),SBINDIR=$(SBINDIR)) \
		$(if $(MANPATH),MANPATH=$(MANPATH)) \
		install

distclean: clean
	@rm -Rfv *~ */*~ .sconsign* */.sconsign* .sconf_temp config.log

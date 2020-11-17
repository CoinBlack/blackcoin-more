package=dbus
$(package)_version=1.12.20
$(package)_download_path=https://dbus.freedesktop.org/releases/dbus
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=F77620140ECB4CDC67F37FB444F8A6BEA70B5B6461F12F1CBE2CEC60FA7DE5FE
$(package)_dependencies=expat

define $(package)_set_vars
  $(package)_config_opts=--disable-tests --disable-doxygen-docs --disable-xml-docs --disable-static --without-x
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE) -C dbus libdbus-1.la
endef

define $(package)_stage_cmds
  $(MAKE) -C dbus DESTDIR=$($(package)_staging_dir) install-libLTLIBRARIES install-dbusincludeHEADERS install-nodist_dbusarchincludeHEADERS && \
  $(MAKE) DESTDIR=$($(package)_staging_dir) install-pkgconfigDATA
endef

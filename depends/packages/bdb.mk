package=bdb
$(package)_version=6.2.32.NC
$(package)_download_path=http://download.oracle.com/berkeley-db
$(package)_file_name=db-$($(package)_version).tar.gz
$(package)_sha256_hash=d86cf1283c519d42dd112b4501ecb2db11ae765b37a1bdad8f8cb06b0ffc69b8
$(package)_build_subdir=build_unix

define $(package)_set_vars
$(package)_config_opts=--disable-shared --enable-cxx --disable-replication
$(package)_config_opts_mingw32=--enable-mingw
$(package)_config_opts_mingw64=--enable-mingw
$(package)_config_opts_linux=--with-pic
$(package)_cxxflags=-std=c++11
$(package)_cppflags_mingw32=-DUNICODE -D_UNICODE
endef

define $(package)_preprocess_cmds
  sed -i.old 's/__atomic_compare_exchange/__atomic_compare_exchange_db/' dbinc/atomic.h && \
  sed -i.old 's/atomic_init/atomic_init_db/' dbinc/atomic.h mp/mp_region.c mp/mp_mvcc.c mp/mp_fget.c mutex/mut_method.c mutex/mut_tas.c && \
  cp -f $(BASEDIR)/config.guess $(BASEDIR)/config.sub dist
endef

define $(package)_config_cmds
  ../dist/$($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE) libdb_cxx-6.2.a libdb-6.2.a
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install_lib install_include
endef

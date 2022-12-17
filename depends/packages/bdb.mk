package=bdb
$(package)_version=6.2.38
$(package)_download_path=https://strawberryperl.com/package/kmx/libs_src/
$(package)_file_name=db-$($(package)_version).tar.gz
$(package)_sha256_hash=99ccd944ffcccc88c0f404b4f3d8cb10747e1e3dfe9ec566f518725f986ca2fd
$(package)_build_subdir=build_unix

define $(package)_set_vars
$(package)_config_opts=--disable-shared --enable-cxx --disable-replication --enable-option-checking
$(package)_config_opts_mingw32=--enable-mingw
$(package)_config_opts_linux=--with-pic
$(package)_config_opts_freebsd=--with-pic
$(package)_config_opts_netbsd=--with-pic
$(package)_config_opts_openbsd=--with-pic
$(package)_config_opts_android=--with-pic
$(package)_cflags+=-Wno-error=implicit-function-declaration
$(package)_cxxflags+=-std=c++17
$(package)_cppflags_mingw32=-DUNICODE -D_UNICODE
endef

define $(package)_preprocess_cmds
  sed -i.old 's/WinIoCtl.h/winioctl.h/g' src/dbinc/win_db.h && \
  sed -i.old 's/atomic_init/atomic_init_db/' src/dbinc/atomic.h src/mp/mp_region.c src/mp/mp_mvcc.c src/mp/mp_fget.c src/mutex/mut_method.c src/mutex/mut_tas.c && \
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

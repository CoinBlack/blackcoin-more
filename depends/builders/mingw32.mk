build_mingw32_SHA256SUM = sha256sum.exe
build_mingw32_DOWNLOAD = wget.exe --timeout=$(DOWNLOAD_CONNECT_TIMEOUT) --tries=$(DOWNLOAD_RETRIES) -nv -O
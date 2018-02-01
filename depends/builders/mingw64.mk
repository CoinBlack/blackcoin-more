build_mingw64_SHA256SUM = sha256sum.exe
build_mingw64_DOWNLOAD = wget.exe --timeout=$(DOWNLOAD_CONNECT_TIMEOUT) --tries=$(DOWNLOAD_RETRIES) -nv -O
#!/bin/bash

BASE_DIR=$(dirname $(realpath $0 ))
moreBuilder=${BASE_DIR}/moreBuilder
rm -fr ${moreBuilder}/*

export SYSTYPE=x86_64  
export DockerHub=blackcoinnl  
export HUBLAB=github  
export GITNAME=CoinBlack  
export BRANCH=${GIT_CURRENT_BRANCH}
sed -i "s|BRANCH=master|BRANCH=${BRANCH}|" ${BASE_DIR}/Dockerfile.minbase
export TZ=Etc/UTC
X11=no

echo "${GITHUB_ENV} = GITHUB_ENV"
echo "DockerHub Account: ${DockerHub}"
echo "Git Account: ${GITNAME}"
echo ${BRANCH}
echo ${SYSTYPE}
echo ${TZ}

# tag names
base="${DockerHub}/blackcoin-more-base-${SYSTYPE}:${BRANCH}"
minimal="${DockerHub}/blackcoin-more-minimal-${SYSTYPE}:${BRANCH}"
ubuntu="${DockerHub}/blackcoin-more-ubuntu-${SYSTYPE}:${BRANCH}"

# build
# ubase (base using ubuntu)
# ubuntu (package with full ubuntu distro)
if ! [[ ${X11} =~ [no|n] ]]; then
	docker build -t ${base} - --network=host < ${BASE_DIR}/Dockerfile.ubase
	docker build -t ${ubuntu} - --network=host < ${BASE_DIR}/Dockerfile.ubuntu
	docker image push ${ubuntu}
else
	docker build -t ${base} - --network=host < ${BASE_DIR}/Dockerfile.minbase
fi

# minimal (only package binaries and scripts)
docker run -itd  --network=host --name base ${base} bash
docker cp base:/parts ${moreBuilder}
cd ${moreBuilder}
tar -c . | docker import - ${minimal} &&  docker image push ${minimal}
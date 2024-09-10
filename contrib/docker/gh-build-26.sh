#!/bin/bash

BASE_DIR=$(dirname $(realpath $0 ))
moreBuilder=${BASE_DIR}/moreBuilder
rm -fr ${moreBuilder}/*

export SYSTYPE=x86_64
export DockerHub=blackcoinnl  
export HUBLAB=github
export GITNAME=CoinBlack
export BRANCH=${GIT_CURRENT_BRANCH}
sed -i "s/master/${BRANCH}/" ${BASE_DIR}/Dockerfile.ubase
sed -i "s/master/${BRANCH}/" ${BASE_DIR}/Dockerfile.ubuntu
export TZ=Etc/UTC

echo "${GITHUB_ENV} = GITHUB_ENV"
echo "DockerHub Account: ${DockerHub}"
echo "Git Account: ${GITNAME}"
echo ${BRANCH}
echo ${SYSTYPE}
echo ${TZ}

# tag names
base="${DockerHub}/blackcoin-more-base-${SYSTYPE}:${BRANCH}"
minimal="${DockerHub}/blackcoin-more-26.2.0-minimal-${SYSTYPE}:${BRANCH}"
ubuntu="${DockerHub}/blackcoin-more-26.2.0-ubuntu-${SYSTYPE}:${BRANCH}"

# build
# ubase (base using ubuntu)
# ubuntu (package with full ubuntu distro)
docker build -t ${base} --network=host -f ${BASE_DIR}/Dockerfile.ubase ${BASE_DIR}
if [ $? -ne 0 ]; then
    echo "Error building base image" && exit 1
fi
docker build -t ${ubuntu} --network=host -f ${BASE_DIR}/Dockerfile.ubuntu ${BASE_DIR}
if [ $? -ne 0 ]; then
    echo "Error building ubuntu image" && exit 1
fi
docker image push ${ubuntu}
if [ $? -ne 0 ]; then
    echo "Error pushing image" && exit 1
fi


# minimal (only package binaries and scripts)
docker run -itd  --network=host --name base ${base} bash
docker cp base:/parts ${moreBuilder}
cd ${moreBuilder}
tar -c . | docker import - ${minimal} &&  docker image push ${minimal}
if [ $? -ne 0 ]; then
    echo "Error creating and pushing minimal image" && exit 1
fi

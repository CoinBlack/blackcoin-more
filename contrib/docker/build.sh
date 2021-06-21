#!/bin/bash

BASE_DIR=$(dirname $(realpath $0 ))
moreBuilder=${BASE_DIR}/moreBuilder
SYSTYPE=`lscpu | head -1 | tr -s ' ' | cut -d ' ' -f2`

# DockerHub Account

defaultDockerHub=blackcoinnl
read -p "What is your DockerHub Account Name? (default: ${defaultDockerHub}): " DockerHub
DockerHub=${DockerHub:-${defaultDockerHub}}
if [ ${DockerHub} != ${defaultDockerHub} ]; then
	sed -i "s/defaultDockerHub=blackcoinnl/defaultDockerHub=${DockerHub}/" $0
fi

# Git Account

defaultHubLab=github
read -p "Github or Gitlab? (default: ${defaultHubLab}): " HubLab
HubLab=${HubLab:-${defaultHubLab}}
if [ ${HubLab} != ${defaultHubLab} ]; then
	sed -i "s|defaultHubLab=github|defaultGit=${HubLab}|" $0
	sed -i "s|github|${HubLab}|" ${BASE_DIR}/Dockerfile.ubase
fi

defaultGit=CoinBlack
read -p "Git account to use? (default: ${defaultGit}): " Git
Git=${Git:-${defaultGit}}
if [ ${Git} != ${defaultGit} ]; then
	sed -i "s|defaultGit=CoinBlack|defaultGit=${Git}|" $0
	sed -i "s|CoinBlack|${Git}|" ${BASE_DIR}/Dockerfile.ubase
fi

# Git Branch

defaultBranch=v2.13.2.8
read -p "What branch/version? (default: ${defaultBranch}): " BRANCH
BRANCH=${BRANCH:-${defaultBranch}}
if [ ${BRANCH} != ${defaultBranch} ]; then
	sed -i "s|defaultBranch=v2.13.2.8|defaultBranch=${BRANCH}|" $0
	sed -i "s|ENV BRANCH=v2.13.2.8|ENV BRANCH=${BRANCH}|" ${BASE_DIR}/Dockerfile.ubase
	sed -i "s|ENV BRANCH=v2.13.2.8|ENV BRANCH=${BRANCH}|" ${BASE_DIR}/Dockerfile.ubuntu
fi

# x11 Desktop QT?
defaultX11=no
read -p "Are you going to need X11docker for QT (visual) client? (default: ${defaultX11}): " X11
X11=${X11:-${defaultX11}}
if [ ${X11} != ${defaultX11} ]; then
	sed -i "s/defaultX11=no/defaultX11=${X11}/" $0
fi

# change branch for multi-stage build
defaultUbase=blackcoinnl/blackcoin-more-ubase-x86_64:v2.13.2.8
ubase="${DockerHub}/blackcoin-more-ubase-${SYSTYPE}:${BRANCH}"
if [ ${defaultUbase} != ${ubase} ]; then
	sed -i "s|FROM danielclough/blackcoin-more-ubase-aarch64:${BRANCH} as build|FROM ${ubase} as build|" ${BASE_DIR}/Dockerfile.ubuntu
	sed -i "s|defaultUbase=blackcoinnl/blackcoin-more-ubase-x86_64:v2.13.2.8|defaultUbase=${ubase}|" $0
fi

# timezone
defaultTimezone=America/Los_Angeles
read -p "What is your timezone? (default: ${defaultTimezone}): " timezone
timezone=${timezone:-${defaultTimezone}}
if [ ${timezone} != ${defaultTimezone} ]; then
	sed -i "s|defaultTimezone=America/Los_Angeles|defaultTimezone=${timezone}|" $0
	sed -i "s|defaultTimezone=America/Los_Angeles|${timezone}|" ${BASE_DIR}/Dockerfile.ubase
	sed -i "s|defaultTimezone=America/Los_Angeles|${timezone}|" ${BASE_DIR}/Dockerfile.ubuntu
fi


echo "DockerHub Account: ${DockerHub}"
echo "Git Account: ${Git}"
echo ${BRANCH}
echo ${SYSTYPE}
echo ${timezone}

# tag names
ubase="${DockerHub}/blackcoin-more-ubase-${SYSTYPE}:${BRANCH}"
minimal="${DockerHub}/blackcoin-more-minimal-${SYSTYPE}:${BRANCH}"
ubuntu="${DockerHub}/blackcoin-more-ubuntu-${SYSTYPE}:${BRANCH}"

# build
# ubase (base using ubuntu)
docker build -t ${ubase} - --network=host < ${BASE_DIR}/Dockerfile.ubase
# ubuntu (package with full ubuntu distro)
if [ ${X11} != ${defaultX11} ]; then
	docker build -t ${ubuntu} - --network=host < ${BASE_DIR}/Dockerfile.ubuntu
	docker image push ${ubuntu}
fi
# minimal (only package binaries and scripts)
docker run -itd  --network=host --name ubase ${ubase} bash
docker cp ubase:/parts ${moreBuilder}
cd ${moreBuilder}
tar -C parts -c . | docker import - ${minimal}
docker image push ${minimal}
docker container stop ubase
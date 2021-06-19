#!/bin/bash

BASE_DIR=$(dirname $(realpath $0 ))
moreBuilder=${BASE_DIR}/moreBuilder
SYSTYPE=`lscpu | head -1 | tr -s ' ' | cut -d ' ' -f2`

# DockerHub Account

defaultDockerHub=danielclough
read -p "What is your DockerHub Account Name? (default ${defaultDockerHub}): " DockerHub
DockerHub=${DockerHub:-${defaultDockerHub}}
if [ ${DockerHub} != ${defaultDockerHub} ]; then
	sed -i "s/defaultDockerHub=danielclough/defaultDockerHub=${DockerHub}/" $0
fi

# Git Account

defaultHubLab=github
read -p "Github or Gitlab? (default ${defaultHubLab}): " HubLab
HubLab=${HubLab:-${defaultHubLab}}
if [ ${HubLab} != ${defaultHubLab} ]; then
	sed -i "s|defaultHubLab=github|defaultGit=${HubLab}|" $0
	sed -i "s|github|${HubLab}|" ${BASE_DIR}/Dockerfile.ubase
fi

defaultGit=CoinBlack
read -p "Git account to use? (default ${defaultGit}): " Git
Git=${Git:-${defaultGit}}
if [ ${Git} != ${defaultGit} ]; then
	sed -i "s|defaultGit=CoinBlack|defaultGit=${Git}|" $0
	sed -i "s|CoinBlack|${Git}|" ${BASE_DIR}/Dockerfile.ubase
fi

# Git Branch

defaultBranch=v2.13.2.8
read -p "What branch/version? (default ${defaultBranch}): " BRANCH
BRANCH=${BRANCH:-${defaultBranch}}
if [ ${BRANCH} != ${defaultBranch} ]; then
	sed -i "s|defaultBranch=v2.13.2.8|defaultBranch=${BRANCH}|" $0
	sed -i "s|ENV BRANCH=v2.13.2.8|ENV BRANCH=${BRANCH}|" ${BASE_DIR}/Dockerfile.ubase
	sed -i "s|ENV BRANCH=v2.13.2.8|ENV BRANCH=${BRANCH}|" ${BASE_DIR}/Dockerfile.ubuntu
fi

# change branch for multi-stage build
defaultUbase=danielclough/blackcoin-more-ubase-aarch64:v2.13.2.8
ubase="${DockerHub}/blackcoin-more-ubase-${SYSTYPE}:${BRANCH}"
if [ ${defaultUbase} != ${ubase} ]; then
	sed -i "s|FROM danielclough/blackcoin-more-ubase-aarch64:${BRANCH} as build|FROM ${ubase} as build|" ${BASE_DIR}/Dockerfile.ubuntu
	sed -i "s|defaultUbase=danielclough/blackcoin-more-ubase-aarch64:v2.13.2.8|defaultUbase=${ubase}|" $0
fi

# timezone
defaultTimezone=America/Los_Angeles
read -p "What is your timezone? (default ${defaultTimezone}): " timezone
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
# ubase
docker build -t ${ubase} - --network=host < ${BASE_DIR}/Dockerfile.ubase
docker run -itd  --network=host --name ubase ${ubase} bash
# minimal (only package binaries and scripts)
docker cp ubase:/parts ${moreBuilder}
cd ${moreBuilder}
tar -C parts -c . | docker import - ${minimal}
# ubuntu (package with full ubuntu distro)
docker build -t ${ubuntu} - --network=host < ${BASE_DIR}/Dockerfile.ubuntu


# cleanup
docker container rm -f ubase
docker image rm -f ${ubase}

# push to docker hub
docker image push ${minimal}
docker image push ${ubuntu}
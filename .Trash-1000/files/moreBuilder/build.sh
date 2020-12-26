#!/bin/bash

BASE_DIR=$(dirname $(realpath $0 ))
moreBuilder=${BASE_DIR}/moreBuilder
SYSTYPE=`lscpu | head -1 | tr -s ' ' | cut -d ' ' -f2`

# DockerHub Account

defaultDockerHub=blackcoinnl
read -p "What is your DockerHub Account Name? (default $defaultDockerHub): " DockerHub
DockerHub=${DockerHub:-${defaultDockerHub}}
if [ $DockerHub != $defaultDockerHub ]; then
	sed -i "s/defaultDockerHub=blackcoinnl/defaultDockerHub=$DockerHub/" $0
fi

# Git Account

defaultHubLab=github
read -p "Github or Gitlab? (default $defaultHubLab): " HubLab
HubLab=${HubLab:-${defaultHubLab}}
if [ $HubLab != $defaultHubLab ]; then
	sed -i "s|defaultHubLab=github|defaultGit=$HubLab|" $0
	sed -i "s|github|$HubLab|" ${BASE_DIR}/Dockerfile.ubase
fi

defaultGit=CoinBlack
read -p "What is your Git account? (default $defaultGit): " Git
Git=${Git:-${defaultGit}}
if [ $Git != $defaultGit ]; then
	sed -i "s|defaultGit=CoinBlack|defaultGit=$Git|" $0
	sed -i "s|CoinBlack|$Git|" ${BASE_DIR}/Dockerfile.ubase
fi

# Git Branch

read -p "What branch/version? (default $defaultBranch): " BRANCH
defaultBranch=master
BRANCH=${BRANCH:-${defaultBranch}}
if [ $BRANCH != $defaultBranch ]; then
	sed -i "s|defaultBranch=master|defaultBranch=$BRANCH|" $0
	sed -i "s|ENV BRANCH=v2.13.2.7|ENV BRANCH=$BRANCH|" ${BASE_DIR}/Dockerfile.ubase
fi

# change branch for multi-stage build
defaultUbase=blackcoinnl/blackmore-ubase-x86_64:master
ubase="${DockerHub}/blackmore-ubase-$SYSTYPE:$BRANCH"
if [ $defaultUbase != $ubase ]; then
	sed -i "s|FROM $defaultUbase as build|FROM $ubase as build|" ${BASE_DIR}/Dockerfile.ubuntu
	sed -i "s|defaultUbase=blackcoinnl/blackmore-ubase-x86_64:master|defaultUbase=$ubase|" $0
fi

# timezone
defaultTimezone=America/Los_Angeles
read -p "What is your timezone? (default $defaultTimezone): " timezone
timezone=${timezone:-${defaultTimezone}}
if [ $timezone != $defaultTimezone ]; then
	sed -i "s|defaultTimezone=America/Los_Angeles|defaultTimezone=$timezone|" $0
	sed -i "s|defaultTimezone=America/Los_Angeles|$timezone|" ${BASE_DIR}/Dockerfile.ubase
	sed -i "s|defaultTimezone=America/Los_Angeles|$timezone|" ${BASE_DIR}/Dockerfile.ubuntu
fi


echo "DockerHub Account: ${DockerHub}"
echo "Git Account: $Git"
echo $BRANCH
echo $SYSTYPE
echo $timezone




minimal="${DockerHub}/blackcoin-more-minimal-$SYSTYPE:$BRANCH"
ubuntu="${DockerHub}/blackcoin-more-ubuntu-$SYSTYPE:$BRANCH"
ubase="${DockerHub}/blackmore-ubase-$SYSTYPE:$BRANCH"

# build

docker build -t $ubase - --network=host < ${BASE_DIR}/Dockerfile.ubase

docker run -itd  --network=host --name ubase $ubase bash

docker cp ubase:/parts $moreBuilder
cd $moreBuilder
tar -C parts -c . | docker import - $minimal

docker container rm -f ubase

# push to docker hub

docker image push $minimal
docker image push $ubuntu
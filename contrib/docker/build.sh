#!/bin/bash
[[ -z `echo $PWD | grep jenkins_home` ]] && BASE_DIR=$(dirname $(realpath $0 )) || BASE_DIR="${WORKSPACE}/contrib/docker"
Alist="\n
\t \t \t \t aarch64-linux-gnu \t (eg. Raspi4 with Ubuntu) \n
\t \t \t \t arm-linux-gnueabihf \t (eg. Other Raspi) \n
\t \t \t \t x86_64-linux-gnu \t (aka. Linux AMD64) \n
\t \t \t \t all \t (only works in option mode) \n
"

usage="Usage: \n 
\t option mode: \t \`build.sh -o <architecture> <DockerHub> <HubLab> <GitAccount> <BRANCH> <TIMEZONE>\` \n
\t interactive: \t \`build.sh -i\` \n 
\t \t \t help: \t \`build.sh -h\` \n 
\n Architectures: ${Alist} \n
"

h1="<architecture>  \t Choose your architecture. ${Alist}"
h2="<DockerHub> \t \t Enter your Docker Hub Account name."
h3="<HubLab> \t \t \t Enter \"github\" or \"gitlab\"."
h4="<GitAccount> \t Enter the git account name. (eg. \"CoinBlack\" on GitHub, or \"blackcoin\" on GitLab)"
h5="<BRANCH> \t \t \t Enter the BRANCH name. (eg. master)"
h6="<TIMEZONE> \t \t Enter your timezone. (https://en.wikipedia.org/wiki/List_of_tz_database_time_zones)"

help=" \n
\t The defaults in interactive mode, written in for option mode: \n
build.sh -o x86_64-linux-gnu blackcoinnl github CoinBlack v2.13.2.7 America/Los_Angeles \n
\n $h1 \n
\n $h2 \n
\n $h3 \n
\n $h4 \n
\n $h5 \n
\n $h6 \n
"

case $1 in

-h)
	echo -e ${help}
;;

-i|-o)

# Questions for Interactive Mode

defaultARCH=${2:-x86_64-linux-gnu}
defaultDockerHub=${3:-blackcoinnl}
defaultHubLab=${4:-github}
defaultRepo=${5:-CoinBlack}
defaultBranch=${6:-v2.13.2.7}
defaultTimezone=${7:-America/Los_Angeles}
if [[ $1 == -i ]]; then
	echo -e ${Alist}
	read -p "For what architecture would you like to build? ($defaultARCH): " architecture
	read -p "What is your DockerHub Account Name? ($defaultDockerHub): " DockerHub
	read -p "Github or Gitlab? ($defaultHubLab): " HubLab
	read -p "What is your GitAccount name? ($defaultRepo): " GitAccount
	read -p "What BRANCH/version? ($defaultBranch): " BRANCH
	read -p "What is your TIMEZONE? (${defaultTimezone}): " TIMEZONE
	echo "Architecture: ${architecture}"
	echo "DockerHub Account: ${DockerHub}"
	echo "Git Account: ${HubLab}.com/${GitAccount} ${BRANCH}"
	echo "Timezone: ${TIMEZONE}"
else
	echo -e "
	Option Mode! \n
	Architecture: $2
	DockerHub Account: $3
	Git Account: https://${4}.com/${5} ${6}
	Timezone: ${7}
	"
fi

# Architecture


architecture=${architecture:-${defaultARCH}}
if [[ ${architecture} == all ]]; then 
	bash ${BASE_DIR}/build.sh -o aarch64-linux-gnu $3 $4 $5 $6 $7
	bash ${BASE_DIR}/build.sh -o arm-linux-gnueabihf $3 $4 $5 $6 $7
	bash ${BASE_DIR}/build.sh -o x86_64-linux-gnu $3 $4 $5 $6 $7
	exit 0
else
	[[ ${architecture} != ${defaultARCH} ]] &&	sed -i "s/-x86_64-linux-gnu/-${architecture}/" $0

	# Download Dependencies

	if [[ -f ${BASE_DIR}/depends-${architecture}.tar.xz && -f ${BASE_DIR}/${architecture}/${architecture}/bin/fc-cache ]]; then
		echo "Dependencies exist! Checking sha256sum!";

		case $architecture in
			aarch64-linux-gnu)
				checkSUM=`echo "f6f1b099d876db90396d0d56eb5b3f366f14c90e077524e2b10bfdaaa1aa5805 ${BASE_DIR}/depends-${architecture}.tar.xz" | sha256sum --check | awk '{print $2}' 2> /dev/null`
				[[ "${checkSUM}" == "OK" ]] && 	echo "The sha256sum is OK!" || exit 1					
			;;
			arm-linux-gnueabihf)
				checkSUM=`echo "339c1159adcccecb45155b316f1f5772009b92acb8cfed29464dd7f09775fb79 ${BASE_DIR}/depends-${architecture}.tar.xz" | sha256sum --check | awk '{print $2}' 2> /dev/null`
				[[ "${checkSUM}" == "OK" ]] && 	echo "The sha256sum is OK!" || exit 1					
			;;
			x86_64-linux-gnu)
				checkSUM=`echo "eed063b26f4c4e0fa35dc085fe09bafd4251cffa76cdabb26bf43077da03b84e ${BASE_DIR}/depends-${architecture}.tar.xz" | sha256sum --check | awk '{print $2}' 2> /dev/null`
				[[ "${checkSUM}" == "OK" ]] && 	echo "The sha256sum is OK!" || exit 1					
			;;
		esac
	else
		[[ -z `which wget` ]] && apt-get install wget 
		/usr/bin/wget -qO ${BASE_DIR}/depends-${architecture}.tar.xz https://admin.blackcoin.nl/static/depends-${architecture}.tar.xz 

		case $architecture in
			aarch64-linux-gnu)
				platform="linux/arm64, linux/arm/v8"
				checkSUM=`echo "f6f1b099d876db90396d0d56eb5b3f366f14c90e077524e2b10bfdaaa1aa5805 ${BASE_DIR}/depends-${architecture}.tar.xz" | sha256sum --check | awk '{print $2}' 2> /dev/null`
				[[ "${checkSUM}" == "OK" ]] && 	echo "The sha256sum is OK! You will need sudo to untar the dependencies." && sudo tar xJf ${BASE_DIR}/depends-${architecture}.tar.xz -C ${BASE_DIR}/\
					|| exit 1
			;;
			arm-linux-gnueabihf)
				platform="linux/arm/v7"
				checkSUM=`echo "339c1159adcccecb45155b316f1f5772009b92acb8cfed29464dd7f09775fb79 ${BASE_DIR}/depends-${architecture}.tar.xz" | sha256sum --check | awk '{print $2}' 2> /dev/null`
				[[ "${checkSUM}" == "OK" ]] && 	echo "The sha256sum is OK! You will need sudo to untar the dependencies." && sudo tar xJf ${BASE_DIR}/depends-${architecture}.tar.xz -C ${BASE_DIR}/\
					|| exit 1
			;;
			x86_64-linux-gnu)
				platform="linux/amd64"
				checkSUM=`echo "eed063b26f4c4e0fa35dc085fe09bafd4251cffa76cdabb26bf43077da03b84e ${BASE_DIR}/depends-${architecture}.tar.xz" | sha256sum --check | awk '{print $2}' 2> /dev/null`
				[[ "${checkSUM}" == "OK" ]] && 	echo "The sha256sum is OK! You will need sudo to untar the dependencies." && sudo tar xJf ${BASE_DIR}/depends-${architecture}.tar.xz -C ${BASE_DIR}/\
					|| exit 1
			;;
		esac
		echo -e "
		Dependencies SHA256SUM GOOD!
		"
	fi


	# DockerHub Account

	DockerHub=${DockerHub:-${defaultDockerHub}}
	[[ $DockerHub != ${defaultDockerHub} ]] &&	sed -i "s/blackcoinnl/${DockerHub}/" $0

	# Git Account

	HubLab=${HubLab:-${defaultHubLab}}
	[[ $HubLab != $defaultHubLab ]] && sed -i "s|github|${HubLab}|" $0

	GitAccount=${GitAccount:-${defaultRepo}}
	[[ ${GitAccount} != ${defaultRepo} ]] && sed -i "s|CoinBlack|$GitAccount|" $0

	# BRANCH

	BRANCH=${BRANCH:-${defaultBranch}}
	[[ ${BRANCH} != ${defaultBranch} ]] && sed -i "s|v2.13.2.7|${BRANCH}|" $0

	# TIMEZONE

	TIMEZONE=${TIMEZONE:-${defaultTimezone}}
	[[ ${TIMEZONE} != ${defaultTimezone} ]] && sed -i "s|America/Los_Angeles|${TIMEZONE}|" $0

	# build ubase
	ubase="ubase-${architecture}"
	Dockerfile="${BASE_DIR}/${architecture}/Dockerfile.${ubase}"
	docker build --network=host ${BASE_DIR}/${architecture} -t ${ubase} --build-arg BRANCH=${BRANCH} --build-arg TIMEZONE=${TZ} -f ${Dockerfile}

	# build ubuntu
	ubuntu="ubuntu-${architecture}"
	Dockerfile="${BASE_DIR}/${architecture}/Dockerfile.${ubuntu}"
	docker build --network=host -t ${ubuntu}:${BRANCH} - < ${Dockerfile}
	[[ ${BRANCH} != master ]] || docker image tag ${ubuntu}:${BRANCH} ${DockerHub}/blackcoin-more-ubuntu-${architecture}:latest && \
		docker image tag ${ubuntu}:${BRANCH} ${DockerHub}/blackcoin-more-ubuntu-${architecture}:${BRANCH}

	# build minimal
	minimal="minimal-${architecture}"
	rm -fr ${BASE_DIR}/${architecture}/parts
	docker run -itd --network=host --name ${ubase} ${ubase} bash
	docker cp ${ubase}:/${architecture}/parts ${BASE_DIR}/${architecture}/parts
	cd ${BASE_DIR}/${architecture}
	tar -C parts -c . | docker import - ${minimal}:${BRANCH}
	docker container rm -f ${ubase}
	[[ ${BRANCH} != master ]] || docker image tag ${minimal}:${BRANCH} ${DockerHub}/blackcoin-more-minimal-${architecture}:latest && \
		docker image tag ${minimal}:${BRANCH} ${DockerHub}/blackcoin-more-minimal-${architecture}:${BRANCH}

fi
;;
*)
echo -e ${usage}
;;
esac
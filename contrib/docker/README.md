## To Build

#### Log in to [Docker Hub](https://hub.docker.com).

`docker login`

#### Build 

`moreBuilder/build.sh`

## To Run

Replace `danielclough` with your Dockerhub account name to use your own.


### Do you need a *complete OS* or just the *artifacts*?
#### Full OS
Blackcoin-More-Ubuntu (over a GB)
```
docker pull danielclough/blackcoin-more-ubuntu-<ARCH>:<VERSION>
docker run -itd  -v /home/$USER/.blackmore:/.blackmore --network=host --name=blackmore danielclough/blackcoin-more-ubuntu-<ARCH>:<VERSION> bash
docker exec -itd blackmore blackmored -daemon
docker exec -it blackmore blackmore-cli help
```
#### Install x11docker ([link]](https://github.com/mviereck/x11docker#shortest-way-for-first-installation))
### QT
	x11docker uses three options ([read more](https://github.com/mviereck/x11docker)):
		`--desktop` indicates the image contains a complete OS 
		`--clipboard` to allow copy/paste into and out of the container and host 
		`--home` creates a default directory in `~/.local/share/x11docker/<IMAGENAME>`, **OR** specify a directory instead.

	`sudo x11docker --desktop --clipboard --home=/home/$USER/x11blackmore danielclough/blackcoin-more-ubuntu-<ARCH>:v2.13.2.8 blackmore-qt`
#### Minimal 
Blackcoin-More-Minimal (hundreds of MB)

```
docker pull danielclough/blackcoin-more-minimal-<ARCH>:<VERSION>
docker run -itd  -v /home/$USER/.blackmore:/.blackmore --network=host --name=blackmore danielclough/blackcoin-more-minimal-<ARCH>:<VERSION> blackmored
docker exec -it blackmore blackmore-cli help
```
pipeline {
    agent { label 'master' }
    stages {
        stage('build') {
            steps {
              sh "contrib/docker/build.sh -o aarch64-linux-gnu blackcoinnl github CoinBlack " + env.GIT_LOCAL_BRANCH + " Etc/UTC"
            }
        }
    }
}

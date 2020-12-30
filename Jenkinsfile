pipeline {
    agent { label 'master' }
    stages {
        stage('build') {
        	when { changeset pattern: "src/*", caseSensitive: true }
            steps {
              sh "contrib/docker/build.sh -o x86_64-linux-gnu blackcoinnl github CoinBlack" + env.GIT_LOCAL_BRANCH + " Etc/UTC"
            }
        }
    }
}

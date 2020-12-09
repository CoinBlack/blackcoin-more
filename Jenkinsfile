pipeline {
  agent {
    dockerfile {
      filename 'contrib/docker/moreBuilder/Dockerfile.ubase'
    }

  }
  stages {
    stage('Build') {
      steps {
        sh 'docker exec -it blackmore blackmore-cli help'
      }
    }

  }
}
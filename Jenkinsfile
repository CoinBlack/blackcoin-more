pipeline {
  agent {
    dockerfile {
      filename 'contrib/docker/moreBuilder/Dockerfile.ubase'
	args '--net=host'
    }

  }
  stages {
    stage('Build') {
      steps {
        sh 'blackmored -daemon'
      }
    }

  }
}

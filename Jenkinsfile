pipeline {
  agent {
    dockerfile {
      filename 'contrib/docker/moreBuilder/Dockerfile.ubase'
	args '--network=host'
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

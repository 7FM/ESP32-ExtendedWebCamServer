pipeline {
  agent any

  stages {

    stage('Build') {
        steps {
            echo 'Start Building...'
            sh 'make'
            archiveArtifacts artifacts: '**/build/**/CameraWebServer.bin', fingerprint: true
        }
    }

  }

}

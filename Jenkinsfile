// Same checks as GitHub Actions, run inside the build image. Set DATA_FILE to
// an ITCH file on the agent to also run the benchmark.
pipeline {
    agent {
        dockerfile {
            filename 'dev.Dockerfile'
            dir 'ops'
            args '--privileged'
        }
    }
    parameters {
        string(name: 'DATA_FILE', defaultValue: '', description: 'ITCH file for the benchmark stage; empty skips it')
        string(name: 'RUNS', defaultValue: '5', description: 'Benchmark runs per config')
    }
    options {
        disableConcurrentBuilds()
        timeout(time: 60, unit: 'MINUTES')
        buildDiscarder(logRotator(numToKeepStr: '20'))
    }
    stages {
        stage('Build and fixture checks') {
            steps {
                sh 'sh tests/check_fixture.sh'
            }
        }
        stage('Benchmark') {
            when { expression { params.DATA_FILE?.trim() } }
            steps {
                sh 'RUNS=$RUNS CONFIGS="read:std mmap:std read:flat mmap:flat" bench/run.sh "$DATA_FILE" jenkins'
                sh 'BOOK=flat bench/perf.sh "$DATA_FILE" jenkins || echo "perf stat unavailable on this agent"'
            }
            post {
                always { archiveArtifacts artifacts: 'bench/results/jenkins/**', allowEmptyArchive: true }
            }
        }
    }
}

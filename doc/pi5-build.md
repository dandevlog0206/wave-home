# Raspberry Pi 5 Build Guide

이 문서는 Raspberry Pi 5에서 `wave-home`를 직접 빌드하는 절차를 정리한 가이드입니다.  
기준 환경은 Raspberry Pi OS 64-bit(Bookworm 계열)이며, 최종 산출물은 다음 두 가지입니다.

- 웹 사이트 정적 파일: `site/`
- 서버 바이너리: `bin/wave-server`

## 1. GitHub에서 소스 받기

아직 저장소를 업로드하기 전 단계라면 이 문서를 먼저 넣어두고, 이후 GitHub URL만 실제 값으로 바꾸면 됩니다.

### 방법 A. `git clone`

```bash
git clone <YOUR_GITHUB_REPOSITORY_URL>
cd wave-home
```

예시:

```bash
git clone https://github.com/<your-account>/wave-home.git
cd wave-home
```

### 방법 B. GitHub ZIP 다운로드

1. GitHub 저장소 페이지로 이동합니다.
2. `Code` 버튼을 누릅니다.
3. `Download ZIP`을 선택합니다.
4. Pi 5에서 압축을 풉니다.

```bash
unzip wave-home-main.zip
cd wave-home-main
```

## 2. 시스템 패키지 설치

먼저 기본 빌드 도구와 C++/Node.js 관련 패키지를 설치합니다.

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  pkg-config \
  git \
  curl \
  unzip \
  zip \
  python3 \
  python3-pip \
  libssl-dev \
  uuid-dev \
  zlib1g-dev
```

## 3. Node.js 설치

React/Vite 프론트 빌드를 위해 Node.js가 필요합니다.  
Node 20 LTS 이상을 권장합니다.

```bash
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt install -y nodejs
```

설치 확인:

```bash
node -v
npm -v
```

## 4. 전체 빌드

프로젝트 루트에서 아래 스크립트를 실행하면:

- 프론트엔드를 빌드해서 `site/`에 넣고
- C++ 서버를 빌드해서 `bin/wave-server`를 만듭니다.

```bash
./script/build_pi5.sh
```

실행 권한이 없다면:

```bash
chmod +x ./script/build_pi5.sh ./script/build_site.sh ./script/build_server.sh
./script/build_pi5.sh
```

## 5. 개별 빌드

웹 사이트만 다시 빌드:

```bash
./script/build_site.sh
```

서버만 다시 빌드:

```bash
./script/build_server.sh
```

## 6. 실행

빌드가 끝나면 서버를 실행합니다.

```bash
./bin/wave-server
```

기본 포트는 `8500`입니다.  
브라우저에서 다음 주소로 접속합니다.

```text
http://<raspberry-pi-ip>:8500
```

## 7. Homebridge 설정 파일 지정

Homebridge 설정 파일 경로를 직접 지정하려면:

```bash
./bin/wave-server --homebridge-config /var/lib/homebridge/config.json
```

정적 사이트 경로를 명시하고 싶다면:

```bash
./bin/wave-server --site-root ./site
```

## 8. 문제 해결

### `npm` 또는 `node`를 찾을 수 없는 경우

Node.js 설치가 완료되지 않은 상태입니다. 위의 Node.js 설치 단계를 다시 진행합니다.

### `Permission denied`가 뜨는 경우

스크립트 실행 권한을 부여합니다.

```bash
chmod +x ./script/build_pi5.sh ./script/build_site.sh ./script/build_server.sh
```

### 웹 UI가 예전 버전으로 보이는 경우

정적 파일은 `react/dist/`가 아니라 `site/`에 최종 빌드됩니다.  
다시 빌드하려면:

```bash
./script/build_site.sh
```

### CMake 재설정이 필요해 보이는 경우

빌드 디렉터리를 지우고 다시 구성합니다.

```bash
rm -rf build
./script/build_pi5.sh
```

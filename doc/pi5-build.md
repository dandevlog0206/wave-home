# Raspberry Pi 5 Build Guide

이 문서는 Raspberry Pi 5에서 `wave-home`를 직접 빌드하는 절차를 정리한 가이드입니다.  
기준 환경은 Raspberry Pi OS 64-bit(Bookworm 계열)이며, 최종 산출물은 다음 두 가지입니다.

- 웹 사이트 정적 파일: `site/`
- 서버 바이너리: `bin/wave-server`

## 1. GitHub에서 소스 받기

이 저장소는 `thirdparty/` 아래에 Git 서브모듈을 포함합니다.  
따라서 가능하면 ZIP 다운로드보다 `git clone --recurse-submodules` 방식을 권장합니다.

### 방법 A. `git clone --recurse-submodules`

```bash
git clone --recurse-submodules https://github.com/dandevlog0206/wave-home.git
cd wave-home
```

예시:

```bash
git clone --recurse-submodules https://github.com/dandevlog0206/wave-home.git
cd wave-home
```

이미 일반 `git clone`으로 받은 경우에는 프로젝트 루트에서 서브모듈을 추가로 받아옵니다.

```bash
git submodule update --init --recursive
```

현재 빌드에 필요한 주요 서브모듈은 다음과 같습니다.

- `thirdparty/drogon`
- `thirdparty/drogon/trantor`
- `thirdparty/asio`
- `thirdparty/ncnn`
- `thirdparty/json`

### 방법 B. GitHub ZIP 다운로드

GitHub의 `Download ZIP`은 일반적으로 서브모듈 내용을 포함하지 않습니다.  
따라서 ZIP만 받아서는 바로 빌드되지 않을 수 있습니다.

1. GitHub 저장소 페이지로 이동합니다.
2. `Code` 버튼을 누릅니다.
3. `Download ZIP`을 선택합니다.
4. Pi 5에서 압축을 풉니다.

```bash
unzip wave-home-main.zip
cd wave-home-main
```

이 방식으로 받았다면 추가로 아래 중 하나가 필요합니다.

- 서브모듈이 포함된 별도 배포 압축 사용
- 같은 버전의 서브모듈 디렉터리를 직접 채우기
- 가장 간단하게는 다시 `git clone --recurse-submodules`로 받기

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

이제 서버는 기본적으로 `config/` 아래 두 파일을 사용합니다.

- `config/appliances.json`
- `config/server_state.json`

패키지된 형태로 실행할 때도 `bin/wave-server` 기준 `../config`를 자동으로 찾습니다.

## 7. 경로 지정

설정 디렉터리를 직접 지정하려면:

```bash
./bin/wave-server --config-root ./config
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

### 서브모듈 관련 파일이 없다고 나오는 경우

예를 들어 `thirdparty/drogon/trantor` 또는 `thirdparty/asio` 관련 오류가 나면
서브모듈이 내려받아지지 않은 상태일 가능성이 큽니다.

프로젝트 루트에서 다시 실행합니다.

```bash
git submodule update --init --recursive
```

이미 ZIP으로 받았다면 이 방법이 어려울 수 있으므로, 저장소를 다시
`git clone --recurse-submodules`로 받는 편이 가장 확실합니다.

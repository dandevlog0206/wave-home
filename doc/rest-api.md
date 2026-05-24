# Wave Home REST API

웹 대시보드(`site/App.jsx`) 각 화면을 실제 데이터로 동작시키기 위한 REST API 설계입니다.

- **전송**: HTTP/JSON only (WebSocket 미사용)
- **갱신 방식**: 클라이언트 폴링 (권장 **0.5s ~ 1s**)
- **베이스 URL**: `http://<host>:<port>/api/v1`
- **기본 포트**: `8500` (`wave-server --port`로 변경 가능)

정적 UI는 기존처럼 Drogon이 `site/dist/`를 서빙하고, API는 `/api/v1/*` 경로로 추가합니다.

---

## 공통 규칙

### 요청 헤더

```http
Accept: application/json
Content-Type: application/json
```

### 응답 형식

성공 시 본문은 리소스 JSON. 실패 시:

```json
{
  "error": {
    "code": "DEVICE_NOT_FOUND",
    "message": "device id 'tv-living' does not exist"
  }
}
```

| HTTP | 용도 |
|------|------|
| 200 | 조회·수정 성공 |
| 201 | 생성 성공 |
| 204 | 삭제·비활성 성공 (본문 없음) |
| 400 | 잘못된 요청 |
| 404 | 리소스 없음 |
| 409 | 충돌 (동일 제스처 중복 바인딩 등) |
| 503 | 레이더/홈브릿지/MQTT 일시 불가 |

### 시간 필드

- 서버: ISO 8601 UTC (`triggeredAt`, `lastPacketAt`)
- UI 표시용 상대 시간(`방금 전`, `3분 전`)은 **프론트에서 계산** 권장

### 제스처 클래스 ID

| id | 의미 (현재 UI/모델 기준) |
|----|-------------------------|
| 0 | 제스처 없음 (no-op) |
| 1~5 | 제스처 세트 1 (5종) |
| 6~13 | 제스처 세트 2 (8종, 모델 확장 시) |
| — | 세트 3: 미등록 |

`gestureName`은 표시용 문자열, 바인딩·인식 결과에는 **`gestureClassId`** 를 canonical key로 사용합니다.

### 폴링 가이드 (WebSocket 대체)

| 화면 | 권장 주기 | 호출 API |
|------|-----------|----------|
| Main | **1s** | `GET /dashboard/summary` |
| 제스처 히스토리 | **1s** | `GET /gestures/history?since=...` |
| 제스처 목록 (세트 목록) | **5s** 또는 진입 시 1회 + 변경 후 재조회 | `GET /gesture-sets`, `GET /bindings` |
| 제스처 목록 (세트 상세) | **5s** | `GET /gesture-sets/{id}`, `GET /bindings` |
| IoT 상태 | **1s** | `GET /devices`, `GET /bindings` (활성 세트 연동 시) |

레이더 패킷·제스처 추론은 서버 내부에서 고주기 처리하고, UI는 위 API만 폴링합니다.

#### 폴링 예시 (Main)

```javascript
const API = '/api/v1';

async function pollSummary() {
  const res = await fetch(`${API}/dashboard/summary`);
  const data = await res.json();
  // metric-grid 4칸에 매핑
  return data;
}

// React: useEffect + setInterval(pollSummary, 1000)
```

---

## 1. Main (`activeView === 'main'`)

### UI가 필요로 하는 데이터

| Metric 카드 | 현재 UI | API 필드 |
|-------------|---------|----------|
| Radar 상태 | `정상` / 실시간 감지 | `radar.status`, `radar.detail` |
| 오늘 인식 | `N회` | `todayRecognitionCount` |
| 연결된 IoT | `활성/전체` | `iot.connectedActive`, `iot.total` |
| 활성 제스처 | 세트 이름·개수 | `activeGestureSet.name`, `activeGestureSet.gestureCount` |

### `GET /api/v1/dashboard/summary`

**폴링: 1s**

**Response 200**

```json
{
  "radar": {
    "status": "ok",
    "detail": "실시간 감지 중",
    "connected": true,
    "lastPacketAt": "2026-05-21T15:30:12.345Z",
    "frameRateHz": 20.0,
    "targetCount": 1
  },
  "todayRecognitionCount": 5,
  "iot": {
    "connectedActive": 0,
    "total": 4
  },
  "activeGestureSet": {
    "id": "set-1",
    "name": "제스처 세트 1",
    "gestureCount": 5
  }
}
```

`radar.status` 값: `ok` | `degraded` | `offline`  
`iot.connectedActive`: 온라인이면서 **제스처 바인딩이 1개 이상**인 기기 수 (UI `connectedDeviceCount`와 동일 규칙)

### (선택) `GET /api/v1/radar/status`

Main만 쓸 때는 summary로 충분. 레이더 상세(타깃 수, 마지막 프레임 등)만 자주 볼 때 **0.5s** 폴링.

**Response 200**

```json
{
  "connected": true,
  "status": "ok",
  "detail": "실시간 감지 중",
  "lastPacketAt": "2026-05-21T15:30:12.345Z",
  "frameRateHz": 20.0,
  "targetCount": 1,
  "device": {
    "ip": "192.168.0.42",
    "model": "RETINA-xxx"
  }
}
```

---

## 2. 제스처 히스토리 (`activeView === 'history'`)

### UI가 필요로 하는 데이터

`HistoryList` 항목:

| UI 필드 | API 필드 |
|---------|----------|
| `id` | `id` |
| `gesture` | `gestureName` |
| `device` | `deviceName` |
| `action` | `actionLabel` |
| `time` | `triggeredAt` → 클라이언트에서 상대 시간 변환 |
| `confidence` | `confidence` (0–100) |

### `GET /api/v1/gestures/history`

**폴링: 1s** (증분 조회 권장)

**Query**

| 파라미터 | 기본 | 설명 |
|----------|------|------|
| `limit` | `50` | 최대 건수 |
| `since` | — | ISO 8601. 이 시각 **이후** 이벤트만 (폴링 시 마지막 `triggeredAt` 전달) |
| `offset` | `0` | 페이지네이션 |

**Response 200**

```json
{
  "items": [
    {
      "id": 1042,
      "gestureClassId": 2,
      "gestureName": "만세 후 오른쪽으로 스윙",
      "deviceId": "curtain-living",
      "deviceName": "커튼",
      "actionLabel": "열림",
      "triggeredAt": "2026-05-21T15:29:01.000Z",
      "confidence": 94,
      "source": "gesture_binding"
    }
  ],
  "hasMore": false,
  "serverTime": "2026-05-21T15:30:00.000Z"
}
```

`source`: `gesture_binding` | `ir_binding` | `manual` (향후 IR·수동 실행 구분)

#### 폴링 예시 (증분)

```javascript
let lastSince = null;

async function pollHistory() {
  const qs = new URLSearchParams({ limit: '50' });
  if (lastSince) qs.set('since', lastSince);
  const data = await fetch(`/api/v1/gestures/history?${qs}`).then((r) => r.json());

  if (data.items.length) {
    lastSince = data.items[0].triggeredAt; // 또는 목록 merge 후 max(triggeredAt)
  }
  return data.items;
}
```

첫 로드는 `since` 없이 전체(또는 오늘 분) 조회, 이후 1s마다 `since`로 신규만 append.

---

## 3. 제스처 목록 (`activeView === 'gestures'`)

두 단계 UI:

1. **세트 그리드** — 세트 카드, 활성 세트 표시, `활성화` / `목록 보기`
2. **세트 상세** — 제스처 카드, `활성`/`비활성` pill (IoT 바인딩 여부)

### 3.1 세트 목록

### `GET /api/v1/gesture-sets`

**폴링: 진입 시 1회 + 활성화/저장 후 재조회 (또는 5s)**

**Response 200**

```json
{
  "activeSetId": "set-1",
  "items": [
    {
      "id": "set-1",
      "name": "제스처 세트 1",
      "description": "기본 홈 제어에 적합한 핵심 제스처입니다.",
      "gestureClassIds": [1, 2, 3, 4, 5],
      "gestureCount": 5
    },
    {
      "id": "set-2",
      "name": "제스처 세트 2",
      "description": "밝기, 온도, 보안 제어에 어울리는 제스처입니다.",
      "gestureClassIds": [6, 7, 8, 9, 10, 11, 12, 13],
      "gestureCount": 8
    },
    {
      "id": "set-3",
      "name": "제스처 세트 3",
      "description": "아직 등록된 제스처가 없습니다.",
      "gestureClassIds": [],
      "gestureCount": 0
    }
  ]
}
```

### `PUT /api/v1/gesture-sets/active`

UI: `activateGestureSet(setId)` — 활성 세트 변경 시 **모든 IoT 바인딩 초기화** (현재 프론트 동작과 동일).

**Request**

```json
{
  "setId": "set-2"
}
```

**Response 200**

```json
{
  "activeSetId": "set-2",
  "bindingsCleared": true
}
```

### 3.2 세트 상세 (제스처 카드)

### `GET /api/v1/gesture-sets/{setId}`

**Response 200**

```json
{
  "id": "set-1",
  "name": "제스처 세트 1",
  "description": "기본 홈 제어에 적합한 핵심 제스처입니다.",
  "gestures": [
    {
      "gestureClassId": 1,
      "name": "만세 후 왼쪽으로 스윙",
      "imageUrl": "/assets/set1_gesture1-xxx.png",
      "videoUrl": "",
      "status": "active"
    },
    {
      "gestureClassId": 2,
      "name": "만세 후 오른쪽으로 스윙",
      "imageUrl": "/assets/set1_gesture2-xxx.png",
      "videoUrl": "",
      "status": "inactive"
    }
  ]
}
```

`status`: `active` | `inactive` — **현재 활성 세트**에 포함된 클래스이면서, 어떤 IoT control에든 바인딩되어 있으면 `active` (UI `gestureList`의 `활성`/`비활성`).

서버 계산 규칙:

```
status = activeSetId === setId
         AND gestureClassId가 /bindings 에 1건 이상 존재
         ? "active" : "inactive"
```

### `GET /api/v1/gestures/catalog`

이미지·이름 메타만 필요할 때 (빌드 산출물 URL 매핑).

**Response 200**

```json
{
  "gestures": [
    {
      "gestureClassId": 1,
      "name": "만세 후 왼쪽으로 스윙",
      "imageUrl": "/assets/set1_gesture1-xxx.png",
      "videoUrl": ""
    }
  ]
}
```

---

## 4. IoT 상태 (`activeView === 'devices'`)

### UI가 필요로 하는 데이터

- **기기 목록**: `id`, `name`, `room`, `state`, `connection`, `controls[]`
- **선택 기기 제어**: control별 제스처 `<select>` (`비활성` + 활성 세트 내 제스처)
- **활성 세트 배너**: `activeGestureSet.name`
- **기기 dot 색**: `online` + 바인딩 있음 → effective online, 없으면 `inactive`
- **전체 비활성**: 선택 기기의 모든 control 바인딩 제거

### `GET /api/v1/devices`

**폴링: 1s**

**Response 200**

```json
{
  "items": [
    {
      "id": "light-living",
      "name": "조명",
      "room": "거실",
      "state": "켜짐",
      "connection": "online",
      "controls": [
        { "id": "power", "label": "전원 on/off" }
      ],
      "hasActiveBindings": false
    },
    {
      "id": "tv-living",
      "name": "TV",
      "room": "거실",
      "state": "켜짐",
      "connection": "online",
      "controls": [
        { "id": "power", "label": "전원 on/off" },
        { "id": "volume_up", "label": "볼륨 up" },
        { "id": "volume_down", "label": "볼륨 down" },
        { "id": "channel_up", "label": "채널 up" },
        { "id": "channel_down", "label": "채널 down" }
      ],
      "hasActiveBindings": true
    }
  ],
  "activeGestureSetId": "set-1"
}
```

`connection`: `online` | `offline` (Homebridge/MQTT 기준)  
`hasActiveBindings`: UI `getDeviceControlState` — 바인딩이 있으면 목록 dot를 `online`처럼 표시

### `GET /api/v1/devices/{deviceId}`

기기 단건 + 최신 상태. 폴링 1s 시 목록 API만으로도 충분하면 생략 가능.

---

## 5. 제스처 ↔ IoT 바인딩 (제스처 목록 + IoT 화면 공통)

프론트 `controlGestures` 상태를 서버에 persist할 때 사용.

### `GET /api/v1/bindings`

**폴링: 1s** (IoT 화면), 제스처 상세에서 status 표시 시 **5s** 또는 bindings 변경 후

**Query**

| 파라미터 | 설명 |
|----------|------|
| `setId` | 생략 시 현재 활성 세트 |

**Response 200**

```json
{
  "activeSetId": "set-1",
  "bindings": [
    {
      "deviceId": "light-living",
      "controlId": "power",
      "controlLabel": "전원 on/off",
      "gestureClassId": 3,
      "gestureName": "만세 후 앞으로 숙이기"
    }
  ]
}
```

### `PUT /api/v1/bindings`

UI: `<select>` 변경 (`updateControlGesture`)

**Request**

```json
{
  "deviceId": "tv-living",
  "controlId": "volume_up",
  "gestureClassId": 4
}
```

`gestureClassId: null` 또는 필드 생략 → 비활성 (`비활성` 옵션).

**Response 200** — 갱신된 binding 1건

**409** — 같은 활성 세트에서 동일 `gestureClassId`가 다른 control에 이미 할당됨 (UI `disabled={isUsedByAnotherControl}`)

### `DELETE /api/v1/bindings`

UI: `deactivateSelectedDevice` — 기기 단위 일괄 해제

**Query**: `deviceId=light-living`  
**Response**: `204`

### `DELETE /api/v1/bindings/all`

UI: `deactivateAllControls` (활성 세트 전환 시 내부 호출과 동일)

**Response**: `204`

---

## 6. 화면별 API 호출 요약

### Main

```
매 1s:
  GET /api/v1/dashboard/summary
```

### 제스처 히스토리

```
매 1s:
  GET /api/v1/gestures/history?limit=50&since=<lastTriggeredAt>
```

### 제스처 목록 — 세트 그리드

```
진입 시 + 세트 활성화 후:
  GET /api/v1/gesture-sets
  PUT /api/v1/gesture-sets/active   (활성화 버튼)

선택적 폴링 5s:
  GET /api/v1/bindings
```

### 제스처 목록 — 세트 상세

```
진입 시 + 5s:
  GET /api/v1/gesture-sets/{setId}
  GET /api/v1/bindings?setId={setId}
```

### IoT 상태

```
매 1s:
  GET /api/v1/devices
  GET /api/v1/bindings

사용자 조작 시:
  PUT /api/v1/bindings
  DELETE /api/v1/bindings?deviceId=...
```

---

## 7. 백엔드 내부 연동 (구현 참고)

| API 영역 | 서버 내부 소스 |
|----------|----------------|
| `radar.*` | `retina` 패킷 수신 (asio), 연결/프레임율/타깃 수 |
| `gestures/history` | ncnn 추론 결과 + 바인딩 매칭 후 이벤트 로그 append |
| `gesture-sets`, `catalog` | 설정 파일 또는 DB, 클래스 ID ↔ 이름 ↔ 이미지 URL |
| `devices` | Homebridge/MQTT 메타 (초기에는 더미 JSON 가능) |
| `bindings` | 영속 저장 (JSON/SQLite), 활성 세트 scoped |
| 제스처 인식 시 실행 | binding 조회 → MQTT publish / IR 송신 |

인식 파이프라인(서버 내부, REST 아님):

```
레이다 패킷 → 전처리 → ncnn(classId) → [classId==0 skip]
  → bindings 조회 → device control 실행 → history 이벤트 기록
```

---

## 8. 향후 확장 (readme 예정 기능)

UI에 아직 없지만 같은 REST 스타일로 추가할 엔드포인트입니다.

### IoT 검색·등록 (더미 → 실제)

| 메서드 | 경로 | 설명 |
|--------|------|------|
| `POST` | `/api/v1/devices/discover` | 네트워크/Homebridge 스캔 시작 |
| `GET` | `/api/v1/devices/discover` | 스캔 결과 폴링 (**1s**) |
| `POST` | `/api/v1/devices` | 기기 등록 |

### IR 리모트 + 제스처 바인딩

| 메서드 | 경로 | 설명 |
|--------|------|------|
| `GET` | `/api/v1/ir/remotes` | 등록된 리모트 목록 |
| `POST` | `/api/v1/ir/remotes` | 리모트 등록 |
| `POST` | `/api/v1/ir/remotes/{id}/signals` | 신호 학습(녹화) |
| `GET` | `/api/v1/ir/bindings` | 제스처 ↔ IR 신호 매핑 |
| `PUT` | `/api/v1/ir/bindings` | 매핑 저장 |

히스토리 `source: "ir_binding"` 으로 구분.

### 수동 제어 (디버그)

| 메서드 | 경로 | 설명 |
|--------|------|------|
| `POST` | `/api/v1/devices/{deviceId}/actions` | body: `{ "controlId": "power" }` — MQTT/IR 즉시 실행 |

---

## 9. Drogon 라우트 등록 예시 (C++)

경로 prefix만 참고용입니다.

```cpp
// GET
REGISTER_HANDLER("/api/v1/dashboard/summary", ...);
REGISTER_HANDLER("/api/v1/radar/status", ...);
REGISTER_HANDLER("/api/v1/gestures/history", ...);
REGISTER_HANDLER("/api/v1/gesture-sets", ...);
REGISTER_HANDLER("/api/v1/gesture-sets/{setId}", ...);
REGISTER_HANDLER("/api/v1/gestures/catalog", ...);
REGISTER_HANDLER("/api/v1/devices", ...);
REGISTER_HANDLER("/api/v1/devices/{deviceId}", ...);
REGISTER_HANDLER("/api/v1/bindings", ...);

// PUT / DELETE / POST
REGISTER_HANDLER_PUT("/api/v1/gesture-sets/active", ...);
REGISTER_HANDLER_PUT("/api/v1/bindings", ...);
REGISTER_HANDLER_DELETE("/api/v1/bindings", ...);
REGISTER_HANDLER_DELETE("/api/v1/bindings/all", ...);
```

정적 파일(`site/dist`)과 API 경로가 겹치지 않도록 `/api/` prefix를 유지합니다.

---

## 10. 제스처 클래스 ↔ UI 이름 매핑 (참고)

| gestureClassId | gestureName (UI) |
|----------------|------------------|
| 1 | 만세 후 왼쪽으로 스윙 |
| 2 | 만세 후 오른쪽으로 스윙 |
| 3 | 만세 후 앞으로 숙이기 |
| 4 | 왼팔을 옆으로 뻗고 날갯짓 |
| 5 | 오른팔을 옆으로 뻗고 날갯짓 |
| 6 | 오른팔로 부채질 |
| 7 | 몸 앞에서 팔 빙글빙글 |
| 8 | 양팔 앞으로 뻗고 위로 스윙 |
| 9 | 양팔 옆으로 뻗고 위로 날갯짓 |
| 10 | 양팔 옆으로 뻗고 아래로 날갯짓 |
| 11 | 왼팔을 들고 팔 돌리기 |
| 12 | 양팔을 내리고 왼쪽으로 스윙 |
| 13 | 양팔을 내리고 오른쪽으로 스윙 |

6-class 모델 단계에서는 `set-1`만 `classId 1~5` + `0`(없음)을 사용하고, 나머지는 `503` 또는 `inactive` 처리하면 됩니다.

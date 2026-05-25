# LAN에서 wave-server 접속 (모바일 등)

서버는 기본적으로 **0.0.0.0:8500** 에 바인딩되어 같은 LAN의 다른 기기에서 접속할 수 있어야 합니다.

## 확인 순서

1. **서버 PC의 실제 IP 확인** (`.2`가 맞는지)
   ```bash
   ip -4 addr show scope global
   ```
2. **포트 리슨 확인**
   ```bash
   ss -tlnp | grep 8500
   ```
   `0.0.0.0:8500` 또는 `*:8500` 이어야 합니다.
3. **같은 PC에서 LAN IP로 테스트**
   ```bash
   curl -s -o /dev/null -w "%{http_code}" http://192.168.0.2:8500/
   ```
   `200`이면 서버는 정상입니다.
4. **방화벽**
   ```bash
   sudo ufw allow 8500/tcp
   ```
5. **WSL2에서 실행 중인 경우**  
   WSL 내부 포트는 Windows LAN에 자동 노출되지 않을 수 있습니다. Windows에서 `netsh interface portproxy`로 포워딩하거나, 네이티브 Linux/RPi에서 서버를 실행하세요.
6. **공유기 게스트 Wi‑Fi**  
   게스트 네트워크는 기기 간 통신이 막혀 있을 수 있습니다. 메인 Wi‑Fi와 동일한지 확인하세요.

## 실행 예

```bash
./bin/wave-server --site-root site --set-root gesture_set
```

모바일 브라우저: `http://<서버_IP>:8500`

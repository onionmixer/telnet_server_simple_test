# Telnet Echo Server

텔넷 에코 서버 구현 - Line Mode와 Character Mode 지원

## 개요

두 가지 모드의 텔넷 에코 서버를 C로 구현했습니다:

1. **Line Mode Server** (포트 9091) - 줄 단위로 입력을 받아 에코
2. **Character Mode Server** (포트 9092) - 문자 단위로 즉시 에코

## 빌드 방법

### 모든 서버 빌드
```bash
make
```

### 개별 빌드
```bash
make line_mode_server  # Line mode 서버만 빌드
make char_mode_server  # Character mode 서버만 빌드
```

## 실행 방법

### Line Mode 서버 실행 (포트 9091)
```bash
./line_mode_server
# 또는
make run-line
```

### Character Mode 서버 실행 (포트 9092)
```bash
./char_mode_server
# 또는
make run-char
```

## 서버 접속 방법

### Line Mode 서버 접속
```bash
telnet localhost 9091
```

### Character Mode 서버 접속
```bash
telnet localhost 9092
```

## 서버 특징

### Line Mode Server (포트 9091)
- 한 줄을 입력하고 Enter를 누르면 에코됩니다
- 입력한 줄이 완성될 때까지 기다렸다가 전체 줄을 에코합니다
- `quit` 입력 시 연결 종료
- 여러 클라이언트 동시 접속 지원 (fork 사용)
- Telnet 프로토콜 협상 처리

### Character Mode Server (포트 9092)
- 키를 누르는 즉시 문자가 에코됩니다
- 실시간 문자 단위 입력/출력
- Backspace/Delete 키 지원
- Ctrl+C: 현재 줄 지우기
- Ctrl+D 또는 `quit` + Enter: 연결 종료
- 여러 클라이언트 동시 접속 지원 (fork 사용)
- Telnet 프로토콜 협상 처리

## 주요 기능

- **멀티 클라이언트 지원**: fork()를 사용하여 여러 클라이언트 동시 처리
- **Telnet 프로토콜 지원**: IAC 명령어 및 옵션 협상 처리
- **안전한 종료**: Ctrl+C로 서버를 안전하게 종료 가능
- **클라이언트 로깅**: 연결/해제 및 에코된 메시지 로깅

## 테스트 예시

### Line Mode 서버 테스트
```bash
# 터미널 1: 서버 실행
./line_mode_server

# 터미널 2: 클라이언트 접속
telnet localhost 9091
# "Hello World" 입력 후 Enter
# "ECHO: Hello World" 출력됨
# "quit" 입력 후 Enter로 종료
```

### Character Mode 서버 테스트
```bash
# 터미널 1: 서버 실행
./char_mode_server

# 터미널 2: 클라이언트 접속
telnet localhost 9092
# 키를 누르는 즉시 화면에 표시됨
# Enter를 누르면 전체 줄이 "ECHO: ..." 형식으로 출력됨
# Ctrl+D로 종료
```

## 정리

```bash
make clean
```

## 파일 구조

```
.
├── line_mode_server.c    # Line mode 서버 소스
├── char_mode_server.c    # Character mode 서버 소스
├── Makefile              # 빌드 스크립트
└── README.md             # 이 파일
```

## 기술 스택

- **언어**: C
- **네트워크**: BSD Socket API
- **프로토콜**: Telnet (RFC 854)
- **멀티프로세싱**: fork()
- **I/O 다중화**: select()

## 참고사항

- 서버는 INADDR_ANY로 바인딩되어 모든 네트워크 인터페이스에서 접속 가능합니다
- SO_REUSEADDR 옵션으로 빠른 재시작이 가능합니다
- 좀비 프로세스 방지를 위해 SIGCHLD 핸들링이 필요할 수 있습니다 (현재는 간단한 구현)

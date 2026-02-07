add_library(trading_core STATIC)

target_sources(trading_core
        PRIVATE
        # 1. 공통 컨트롤러
        src/trading/controllers/CompositeController.cpp

        # 2. 설치/조립 (Installers)
        src/trading/controllers/client/Install.cpp
        src/trading/controllers/exchange/Install.cpp
        src/trading/controllers/gateway/Install.cpp

        # 3. 기능 구현체 (Order, MarketData는 제거됨)
        # --- Benchmark ---
        src/trading/feature/benchmark/client/BenchmarkClientController.cpp
        src/trading/feature/benchmark/exchange/BenchmarkExchangeController.cpp
        src/trading/feature/benchmark/gateway/BenchmarkGatewayController.cpp

        # --- Handshake ---
        src/trading/feature/handshake/client/RoleHelloClientController.cpp
        src/trading/feature/handshake/exchange/RoleHelloExchangeController.cpp
        src/trading/feature/handshake/gateway/RoleHelloGatewayController.cpp
        
        # (UpstreamDirectory.cpp 제거됨 - 확인 완료)
)

target_include_directories(trading_core
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(trading_core
        PUBLIC
        hyperapp_core  # [주의] 기존 이름 유지 (만약 런타임 이름을 hyperapp으로 바꿨다면 hyperapp으로 변경)
)
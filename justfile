# MeshAccessProtocol justfile

# Default recipe - show available commands
default:
    @just --list

# Run WAP request tests (native build)
test:
    g++ -std=c++11 -I. -Ilib/wap test/test_wap_request.cpp lib/wap/wap_request.cpp lib/wap/wap_response.cpp lib/wap/wmlc_decompiler.cpp -o test_wap_request
    ./test_wap_request
    rm -f test_wap_request

# Run tests with verbose output
test-verbose:
    g++ -std=c++11 -I. -Ilib/wap -g test/test_wap_request.cpp lib/wap/wap_request.cpp lib/wap/wap_response.cpp lib/wap/wmlc_decompiler.cpp -o test_wap_request
    ./test_wap_request
    rm -f test_wap_request

# Run end-to-end WAP test (sends real request to WAPBOX)
test-e2e:
    g++ -std=c++11 -I. -Ilib/wap test/test_wap_e2e.cpp lib/wap/wap_request.cpp lib/wap/wap_response.cpp lib/wap/wmlc_decompiler.cpp -o test_wap_e2e
    ./test_wap_e2e
    rm -f test_wap_e2e

# Run end-to-end WAP test in offline mode (no network)
test-e2e-offline:
    g++ -std=c++11 -I. -Ilib/wap test/test_wap_e2e.cpp lib/wap/wap_request.cpp lib/wap/wap_response.cpp lib/wap/wmlc_decompiler.cpp -o test_wap_e2e
    ./test_wap_e2e --offline
    rm -f test_wap_e2e

# Run all tests
test-all: test test-e2e

# Build test binary without running
build-test:
    g++ -std=c++11 -I. -Ilib/wap -g test/test_wap_request.cpp lib/wap/wap_request.cpp lib/wap/wap_response.cpp lib/wap/wmlc_decompiler.cpp -o test_wap_request

# Build e2e test binary without running
build-e2e:
    g++ -std=c++11 -I. -Ilib/wap -g test/test_wap_e2e.cpp lib/wap/wap_request.cpp lib/wap/wap_response.cpp lib/wap/wmlc_decompiler.cpp -o test_wap_e2e

# Clean build artifacts
clean:
    rm -f test_wap_request
    rm -rf .pio/build

# Build ESP32 firmware with PlatformIO
build:
    platformio run

# Upload firmware to device
upload:
    platformio run --target upload

# Monitor serial output
monitor:
    platformio device monitor

# Build and upload
flash: build upload

# Full development cycle: build, upload, and monitor
dev: build upload monitor

"""Test the real library with deferred UART interrupts. Requires host g++.

Run: python tests/test_serial.py [--cxx path/to/g++]
Build artifacts are created only in a temporary directory.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

HAL = r'''
#pragma once
#include <cstdint>
enum HAL_StatusTypeDef { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT };
enum { HAL_UART_STATE_RESET, HAL_UART_STATE_READY, HAL_UART_STATE_BUSY_TX,
       HAL_UART_STATE_BUSY_RX };
constexpr uint32_t HAL_MAX_DELAY = UINT32_MAX;
struct UART_HandleTypeDef {
    void* Instance = nullptr;
    int gState = HAL_UART_STATE_RESET, RxState = HAL_UART_STATE_RESET;
    const uint8_t* pTxBuffPtr = nullptr;
    uint8_t* pRxBuffPtr = nullptr;
};
extern "C" {
uint32_t __get_PRIMASK();
void __disable_irq();
void __set_PRIMASK(uint32_t);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef*, const uint8_t*, uint16_t);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef*, uint8_t*, uint16_t);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef*, const uint8_t*, uint16_t, uint32_t);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef*);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef*);
void HAL_UART_ErrorCallback(UART_HandleTypeDef*);
}
'''

TEST = r'''
#include "STM32BufferedSerial.hpp"
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
static uint32_t mask;
static std::function<void()> pending, duringBlocking;
static HAL_StatusTypeDef nextTxStatus = HAL_OK;
static int receiveCalls;
static std::string transmitted;
extern "C" uint32_t __get_PRIMASK() { return mask; }
extern "C" void __disable_irq() { mask = 1; }
extern "C" void __set_PRIMASK(uint32_t value) {
    mask = value;
    if (!mask && pending) {
        auto callback = std::move(pending);
        pending = nullptr;
        callback();
    }
}
extern "C" HAL_StatusTypeDef HAL_UART_Transmit_IT(
        UART_HandleTypeDef* h, const uint8_t* data, uint16_t size) {
    assert(mask == 1 && size == 1);
    if (nextTxStatus != HAL_OK) {
        auto status = nextTxStatus;
        nextTxStatus = HAL_OK;
        return status;
    }
    if (h->gState != HAL_UART_STATE_READY) return HAL_BUSY;
    h->pTxBuffPtr = data; // Read later, like the real HAL TX interrupt.
    h->gState = HAL_UART_STATE_BUSY_TX;
    return HAL_OK;
}
extern "C" HAL_StatusTypeDef HAL_UART_Receive_IT(
        UART_HandleTypeDef* h, uint8_t* data, uint16_t size) {
    assert(mask == 1 && size == 1);
    ++receiveCalls;
    if (h->RxState != HAL_UART_STATE_READY) return HAL_BUSY;
    h->pRxBuffPtr = data;
    h->RxState = HAL_UART_STATE_BUSY_RX;
    return HAL_OK;
}
extern "C" HAL_StatusTypeDef HAL_UART_Transmit(
        UART_HandleTypeDef* h, const uint8_t* data, uint16_t size, uint32_t) {
    assert(size != 0);
    if (h->gState != HAL_UART_STATE_READY) return HAL_BUSY;
    h->gState = HAL_UART_STATE_BUSY_TX;
    transmitted.append(reinterpret_cast<const char*>(data), size);
    if (duringBlocking) duringBlocking();
    h->gState = HAL_UART_STATE_READY;
    return HAL_OK;
}
static void complete(UART_HandleTypeDef& h) {
    assert(mask == 0 && h.gState == HAL_UART_STATE_BUSY_TX);
    transmitted += static_cast<char>(*h.pTxBuffPtr);
    h.gState = HAL_UART_STATE_READY;
    HAL_UART_TxCpltCallback(&h);
}
static void drain(UART_HandleTypeDef& h) {
    int limit = 1000;
    while (h.gState == HAL_UART_STATE_BUSY_TX) {
        assert(--limit > 0);
        complete(h);
    }
}
static void receive(UART_HandleTypeDef& h, uint8_t byte) {
    assert(h.RxState == HAL_UART_STATE_BUSY_RX);
    *h.pRxBuffPtr = byte;
    h.RxState = HAL_UART_STATE_READY;
    HAL_UART_RxCpltCallback(&h);
}
static void initialize(UART_HandleTypeDef& h) {
    h.Instance = &h;
    h.gState = h.RxState = HAL_UART_STATE_READY;
}
int main() {
    UART_HandleTypeDef h;
    STM32BufferedSerial serial(&h, 4);
    assert(STM32BufferedSerial::fromHandle(&h) == &serial); // Instance still zero.
    initialize(h);
    serial.begin();
    assert(h.RxState == HAL_UART_STATE_BUSY_RX);
    const int calls = receiveCalls;
    serial.begin();
    assert(receiveCalls == calls); // Do not abort a live reception.

    // Deferred reads, a full ring, wraparound and exact byte ordering.
    assert(serial.write(reinterpret_cast<const uint8_t*>("ABCDE"), 5) == 4);
    complete(h);
    assert(serial.write('E') == 1);
    drain(h);
    assert(transmitted == "ABCDE");
    for (int i = 0; i < 20; ++i) {
        assert(serial.write(static_cast<uint8_t>('a' + i)) == 1);
        drain(h);
    }
    assert(transmitted == "ABCDEabcdefghijklmnopqrst");
    for (auto status : {HAL_BUSY, HAL_ERROR}) {
        transmitted.clear();
        nextTxStatus = status;
        assert(serial.write('X') == 1 && h.gState == HAL_UART_STATE_READY);
        assert(serial.write('Y') == 1);
        drain(h);
        assert(transmitted == "XY"); // Rejected starts must not discard a byte.
    }
    transmitted.clear();
    assert(serial.write(reinterpret_cast<const uint8_t*>("ABC"), 3) == 3);
    pending = [&] { complete(h); };
    serial.flushTx();
    assert(!pending && transmitted == "A"); // Only the in-flight byte survives.
    assert(serial.write('D') == 1);
    drain(h);
    assert(transmitted == "AD");

    for (uint8_t byte : {'a', 'b', 'c', 'd'}) receive(h, byte);
    assert(serial.readable_len() == 3 && serial.read() == 'a');
    receive(h, 'e');
    assert(serial.read() == 'b' && serial.read() == 'c' && serial.read() == 'e');
    assert(serial.read() == -1 && !serial.available());
    receive(h, 'f');
    pending = [&] { receive(h, 'g'); };
    serial.flushRx();
    assert(serial.readable_len() == 1 && serial.read() == 'g');
    h.RxState = HAL_UART_STATE_READY; // HAL aborts RX on ORE before error callback.
    HAL_UART_ErrorCallback(&h);
    assert(h.RxState == HAL_UART_STATE_BUSY_RX);
    receive(h, 'z');
    assert(serial.read() == 'z');
    const int beforeNonBlockingError = receiveCalls;
    HAL_UART_ErrorCallback(&h);
    assert(receiveCalls == beforeNonBlockingError);
    mask = 1;
    serial.flushTx(); serial.flushRx(); serial.available(); serial.readable_len(); serial.read();
    assert(serial.write('M') == 1 && mask == 1);
    mask = 0;
    drain(h);

    transmitted.clear();
    const std::string longText(300, 'L');
    assert(serial.printf("%s:%d", longText.c_str(), 42) == 303);
    assert(transmitted == longText + ":42");
    assert(serial.printf("%s", "") == 0);
    assert(serial.printf("%065536d", 1) == -1);
    assert(transmitted == longText + ":42");
    assert(serial.write('B') == 1 && serial.printf("busy") == -1);
    drain(h);
    transmitted.clear();
    duringBlocking = [&] { assert(serial.write('Q') == 1); };
    assert(serial.printf("hello") == 5);
    duringBlocking = nullptr;
    drain(h);
    assert(transmitted == "helloQ");
    for (uint16_t size : {0, 1}) {
        UART_HandleTypeDef smallHandle;
        initialize(smallHandle);
        STM32BufferedSerial small(&smallHandle, size);
        small.begin();
        receive(smallHandle, 's');
        assert(small.read() == 's');
        assert(small.write('t') == 1 && small.write('u') == 1 && small.write('v') == -1);
        drain(smallHandle);
        STM32BufferedSerial::registerInstance(&smallHandle, nullptr);
    }
    assert(STM32BufferedSerial::fromHandle(nullptr) == nullptr);
    STM32BufferedSerial::registerInstance(&h, nullptr);
    assert(STM32BufferedSerial::fromHandle(&h) == nullptr);
    std::cout << "PASS: registration, deferred TX, rejection, flush, RX, recovery, masks, printf, sizes\n";
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="stm32-buffered-serial-") as tmp:
        build = Path(tmp)
        (build / "stm32f3xx_hal.h").write_text(HAL, encoding="utf-8")
        test = build / "test.cpp"
        test.write_text(TEST, encoding="utf-8")
        for optimization in ("-O0", "-O2"):
            executable = build / ("test" + optimization + ".exe")
            subprocess.run([
                args.cxx, "-std=c++14", optimization, "-static", "-Wall", "-Wextra", "-Werror",
                "-fno-exceptions", "-fno-rtti", "-I" + str(build), "-I" + str(root / "src"),
                str(test), str(root / "src/source/STM32BufferedSerial.cpp"),
                str(root / "src/source/HAL_UART_Callback_Setup.cpp"), "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()

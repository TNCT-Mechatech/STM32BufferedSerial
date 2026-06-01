# STM32BufferedSerial

**An interrupt-driven, buffered UART class for STM32 HAL**  
**STM32 HAL を用いた割り込み駆動型バッファ付き UART 通信クラス**

---

## 🌐 Language / 言語
- [English](#english)
- [日本語](#日本語)

---

## English

### Overview

`STM32BufferedSerial` provides a non-blocking, interrupt-driven UART interface for STM32 HAL.  
It uses circular buffers for both RX and TX, allowing smooth serial communication without blocking the CPU.  
Multiple UART instances are supported.

---

### 🚀 Features

- Interrupt-driven, non-blocking communication  
- Circular buffers for RX and TX  
- Supports up to 6 UART instances  
- Works with HAL UART callbacks (`HAL_UART_RxCpltCallback`, `HAL_UART_TxCpltCallback`)  
- Automatically re-arms RX to handle HAL busy states  
- Drop-in replacement for `HAL_UART_Transmit_IT()` / `HAL_UART_Receive_IT()`
- Blocking `print()` / `printf()` output support

---

### 💡 Example

```cpp
#include "STM32BufferedSerial.hpp"
extern UART_HandleTypeDef huart2;

STM32BufferedSerial serial(&huart2, 256);

void setup() {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_UART_Init();

    serial.begin();
    STM32BufferedSerial::registerInstance(&huart2, &serial);

    const char* msg = "STM32 Buffered Serial Example\r\n";
    serial.write(reinterpret_cast<const uint8_t*>(msg), strlen(msg));
}

void loop() {
    if (serial.available()) {
        int c = serial.read();
        serial.write((uint8_t)c);  // Echo back
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart) {
    if (auto inst = STM32BufferedSerial::fromHandle(huart))
        inst->handleRxComplete();
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart) {
    if (auto inst = STM32BufferedSerial::fromHandle(huart))
        inst->handleTxComplete();
}
````

---

### 🧠 Usage Notes

* Call `STM32BufferedSerial::registerInstance()` **after** `MX_USARTx_UART_Init()`.
* Implement `HAL_UART_RxCpltCallback()` and `HAL_UART_TxCpltCallback()` as shown.
* RX interrupt automatically restarts internally.
* Sending data:

  ```cpp
  serial.write((uint8_t*)"Hello\r\n", 7);
  ```
* Blocking string output:

  ```cpp
  serial.print("Hello\r\n");
  serial.printf("count=%d\r\n", count);
  ```
  `print()` is not named `printf()`, and `begin()` is not required when using only blocking output.
* Reading data:

  ```cpp
  if (serial.available()) {
      int c = serial.read();
  }
  ```

---

### ⚙️ Integration (STM32CubeIDE)

1. Copy `STM32BufferedSerial.hpp` and `example.cpp` into your project (e.g., `Core/Inc` and `Core/Src`).
2. Include the header in your source file:

   ```cpp
   #include "STM32BufferedSerial.hpp"
   ```
3. Ensure the following HAL headers are available:

   * `stm32f4xx_hal.h`
   * `usart.h`
   * `main.h`

---

### 🧑‍💻 Author

**shoyo**
Date: 2025-10-14

---

### 📜 License

MIT License (or another open-source license of your choice)

---

### Development Environment

STM32CubeIDE 1.19.0
NUCLEO-F446RE

---

## 日本語

### 概要

`STM32BufferedSerial` は、STM32 HAL を利用した割り込み駆動型の UART 通信クラスです。
リングバッファ（循環バッファ）を使用することで、CPU をブロックせずに安定したシリアル通信を実現します。
複数の UART インスタンスを同時に扱うことができます。

---

### 🚀 特徴

* 割り込み駆動による非ブロッキング通信
* RX / TX 両方にリングバッファを採用
* 最大 6 個の UART インスタンスに対応
* HAL の UART コールバック関数（`HAL_UART_RxCpltCallback`, `HAL_UART_TxCpltCallback`）に対応
* HAL の busy 状態を安全に回避して自動で受信再開
* `HAL_UART_Transmit_IT()` / `HAL_UART_Receive_IT()` の代替として利用可能
* ブロッキング送信の `print()` / `printf()` に対応

---

### 💡 使用例

```cpp
#include "STM32BufferedSerial.hpp"
extern UART_HandleTypeDef huart2;

// UART2 を使用するインスタンス（バッファサイズ 256 バイト）
STM32BufferedSerial serial(&huart2, 256);

void setup() {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_UART_Init();

    serial.begin();
    STM32BufferedSerial::registerInstance(&huart2, &serial);

    const char* msg = "STM32 Buffered Serial Example\r\n";
    serial.write(reinterpret_cast<const uint8_t*>(msg), strlen(msg));
}

void loop() {
    if (serial.available()) {
        int c = serial.read();
        serial.write((uint8_t)c);  // エコーバック
    }
}

// 受信完了コールバック
void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart) {
    if (auto inst = STM32BufferedSerial::fromHandle(huart))
        inst->handleRxComplete();
}

// 送信完了コールバック
void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart) {
    if (auto inst = STM32BufferedSerial::fromHandle(huart))
        inst->handleTxComplete();
}
```

---

### 🧠 使用上の注意

* `MX_USARTx_UART_Init()` の **後に**
  `STM32BufferedSerial::registerInstance()` を呼び出してください。
* HAL のコールバック関数内で `handleRxComplete()` / `handleTxComplete()` を呼び出します。
* RX 割り込みは内部で自動的に再開されます。
* データ送信例：

  ```cpp
  serial.write((uint8_t*)"Hello\r\n", 7);
  ```
* ブロッキング文字列送信例：

  ```cpp
  serial.print("Hello\r\n");
  serial.printf("count=%d\r\n", count);
  ```
  `print()` は `printf()` という名前ではありません。ブロッキング送信だけを使う場合、`begin()` は不要です。
* データ受信例：

  ```cpp
  if (serial.available()) {
      int c = serial.read();
  }
  ```

---

### ⚙️ 統合手順（STM32CubeIDE）

1. `STM32BufferedSerial.hpp` と `example.cpp` を
   プロジェクト内（例：`Core/Inc`, `Core/Src`）にコピーします。
2. 使用ファイル内でヘッダをインクルード：

   ```cpp
   #include "STM32BufferedSerial.hpp"
   ```
3. 以下の HAL 関連ヘッダが含まれていることを確認：

   * `stm32f4xx_hal.h`
   * `usart.h`
   * `main.h`

---

### 🧑‍💻 作者

**shoyo**
作成日: 2025-10-14

---

### 📜 ライセンス

MIT ライセンス（または任意のオープンソースライセンス）

---

### 🧩 補足

このクラスは、STM32 の UART 通信をより柔軟かつ安全に扱うための軽量ライブラリです。
割り込みによる処理分離で、リアルタイムタスクと UART 通信を両立させることが可能です。

---

### 開発環境

STM32CubeIDE 1.19.0
NUCLEO-F446RE

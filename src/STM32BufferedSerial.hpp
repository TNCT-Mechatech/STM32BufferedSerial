/**
 * @file STM32BufferedSerial.hpp
 * @brief Provides an interrupt-driven buffered serial communication class using STM32 HAL.
 *
 * This class implements a lightweight UART serial communication interface with internal
 * RX/TX ring buffers, designed for STM32 microcontrollers using the HAL library.
 * It supports fully interrupt-driven I/O and allows non-blocking communication
 * through software-managed buffers.
 *
 * @details
 * - Uses circular buffers for both RX and TX.
 * - Supports multiple UART instances (up to 6 by default).
 * - Designed to work with standard HAL UART interrupt callbacks.
 * - Automatically restarts reception to handle HAL busy states safely.
 *
 * Typical usage:
 * @code
 * STM32BufferedSerial serial(&huart2, 256);
 * serial.begin();
 *
 * while (1) {
 *     if (serial.available()) {
 *         int c = serial.read();
 *         serial.write((uint8_t)c);
 *     }
 * }
 * @endcode
 *
 * @note
 * Call `STM32BufferedSerial::fromHandle()` from your HAL callback to dispatch
 * to the correct serial instance:
 * @code
 * void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart) {
 *     if (auto inst = STM32BufferedSerial::fromHandle(huart))
 *         inst->handleRxComplete();
 * }
 *
 * void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart) {
 *     if (auto inst = STM32BufferedSerial::fromHandle(huart))
 *         inst->handleTxComplete();
 * }
 * @endcode
 *
 * @see HAL_UART_Receive_IT()
 * @see HAL_UART_Transmit_IT()
 * @author
 * shoyo
 * @date
 * 2025-10-14
 */

#ifndef STM32_BUFFERED_SERIAL_HPP
#define STM32_BUFFERED_SERIAL_HPP

#include "stm32f4xx_hal.h"
#include <cstdint>

/**
 * @class STM32BufferedSerial
 * @brief Interrupt-driven UART serial communication with circular buffers.
 */
class STM32BufferedSerial {
public:
    /**
     * @brief Construct a new STM32BufferedSerial object.
     * @param huart Pointer to HAL UART handle (e.g., &huart2)
     * @param bufSize Size of both TX and RX buffers (default: 256 bytes)
     */
    explicit STM32BufferedSerial(UART_HandleTypeDef* huart, uint16_t bufSize = 256);

    /** @brief Start UART communication and enable RX interrupt. */
    void begin();

    /** @brief Read a single byte from RX buffer.
     *  @return Byte (0–255), or -1 if no data available.
     */
    int read();

    /** @brief Write a single byte to TX buffer and start interrupt-driven transmission.
     *  @param data Byte to send.
     *  @return 1 if success, -1 if TX buffer is full.
     */
    int write(uint8_t data);

    /** @brief Write multiple bytes to TX buffer.
     *  @param data Pointer to data buffer.
     *  @param len Number of bytes to send.
     *  @return Number of bytes successfully queued for transmission.
     */
    int write(const uint8_t* data, uint16_t len);

    /** @brief Check if RX buffer contains data.
     *  @return true if data available.
     */
    bool available() const;

    /** @brief Get number of bytes currently in RX buffer.
     *  @return Number of readable bytes.
     */
    int readable_len() const;

    /** @brief Clear RX buffer. */
    void flushRx();

    /** @brief Clear TX buffer. */
    void flushTx();

    /** @brief Handle RX complete interrupt.
     *  Should be called from HAL_UART_RxCpltCallback().
     */
    void handleRxComplete();

    /** @brief Handle TX complete interrupt.
     *  Should be called from HAL_UART_TxCpltCallback().
     */
    void handleTxComplete();

    /** @brief Get internal UART handle. */
    UART_HandleTypeDef* getHandle() const { return _huart; }

    /**
     * @brief Register a serial instance for global callback lookup.
     * @param huart UART handle.
     * @param instance Instance pointer to register.
     */
    static void registerInstance(UART_HandleTypeDef* huart, STM32BufferedSerial* instance);

    /**
     * @brief Retrieve registered instance from UART handle.
     * @param huart UART handle.
     * @return Pointer to corresponding STM32BufferedSerial instance, or nullptr if unregistered.
     */
    static STM32BufferedSerial* fromHandle(UART_HandleTypeDef* huart);

private:
    UART_HandleTypeDef* _huart;   /**< HAL UART handle */
    uint8_t* _rxBuf;              /**< RX ring buffer */
    uint8_t* _txBuf;              /**< TX ring buffer */
    uint16_t _rxSize;             /**< RX buffer size */
    uint16_t _txSize;             /**< TX buffer size */
    volatile uint16_t _rxHead;    /**< RX buffer write index */
    volatile uint16_t _rxTail;    /**< RX buffer read index */
    volatile uint16_t _txHead;    /**< TX buffer write index */
    volatile uint16_t _txTail;    /**< TX buffer read index */
    uint8_t _rxTmp;               /**< Temporary byte for interrupt reception */

    static constexpr int MAX_UARTS = 6; /**< Max number of UART instances supported */
    static STM32BufferedSerial* instance_table_[MAX_UARTS]; /**< Global UART-to-instance map */

    /** @brief Begin receiving via interrupt. */
    void _startRxInterrupt();

    /** @brief Begin transmission via interrupt. */
    void _startTxInterrupt();

    /** @brief Push one byte into RX buffer. */
    void push(uint8_t c);

    /** @brief Pop one byte from RX buffer. */
    int pop();
};

#endif

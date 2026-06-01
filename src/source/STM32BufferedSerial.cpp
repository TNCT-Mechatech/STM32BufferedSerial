#include "../STM32BufferedSerial.hpp"
#include <cstring>
#include <cstdlib>

STM32BufferedSerial* STM32BufferedSerial::instance_table_[MAX_UARTS] = {nullptr};

STM32BufferedSerial::STM32BufferedSerial(UART_HandleTypeDef* huart, uint16_t bufSize)
    : _huart(huart),
      _rxSize(bufSize),
      _txSize(bufSize),
      _rxHead(0), _rxTail(0),
      _txHead(0), _txTail(0),
      _rxTmp(0)
{
    _rxBuf = new uint8_t[_rxSize];
    _txBuf = new uint8_t[_txSize];

    registerInstance(_huart, this);
}

void STM32BufferedSerial::begin() {
    _startRxInterrupt();
}

/*----------------------------------------
 * 受信割り込み完了ハンドラ
 *----------------------------------------*/
void STM32BufferedSerial::handleRxComplete()
{
    uint16_t next = (_rxHead + 1) % _rxSize;
    if (next != _rxTail) { // バッファに空きがあれば格納
        _rxBuf[_rxHead] = _rxTmp;
        _rxHead = next;
    }

    // 🔥 再受信を確実に開始する（HAL_BUSY対策付き）
    if (HAL_UART_Receive_IT(_huart, &_rxTmp, 1) != HAL_OK)
    {
        // 再試行が必要な場合は小delayを入れて再実行（またはErrorHandler）
        __HAL_UNLOCK(_huart);
        HAL_UART_AbortReceive(_huart);  // 念のため前回の受信をリセット
        HAL_UART_Receive_IT(_huart, &_rxTmp, 1);
    }
}


/*----------------------------------------
 * 送信割り込み完了ハンドラ
 *----------------------------------------*/
void STM32BufferedSerial::handleTxComplete() {
    if (_txTail != _txHead) {
        uint8_t data = _txBuf[_txTail];
        _txTail = (_txTail + 1) % _txSize;
        HAL_UART_Transmit_IT(_huart, &data, 1);
    }
}

/*----------------------------------------
 * 受信割り込み開始
 *----------------------------------------*/
void STM32BufferedSerial::_startRxInterrupt() {
    HAL_UART_Receive_IT(_huart, &_rxTmp, 1);
}

/*----------------------------------------
 * 送信割り込み開始
 *----------------------------------------*/
void STM32BufferedSerial::_startTxInterrupt() {
    if (_txTail == _txHead) return;   // バッファ空
    uint8_t data = _txBuf[_txTail];
    _txTail = (_txTail + 1) % _txSize;
    HAL_UART_Transmit_IT(_huart, &data, 1);
}

/*----------------------------------------
 * データ読み取り
 *----------------------------------------*/
int STM32BufferedSerial::read() {
    if (_rxTail == _rxHead) return -1;  // データなし
    uint8_t data = _rxBuf[_rxTail];
    _rxTail = (_rxTail + 1) % _rxSize;
    return data;
}

/*----------------------------------------
 * データ送信
 *----------------------------------------*/
int STM32BufferedSerial::write(uint8_t data) {
    uint16_t next = (_txHead + 1) % _txSize;
    if (next == _txTail) return -1;  // バッファ満杯

    _txBuf[_txHead] = data;
    _txHead = next;

    if (_huart->gState == HAL_UART_STATE_READY)
        _startTxInterrupt();

    return 1;
}

int STM32BufferedSerial::write(const uint8_t* data, uint16_t len) {
    int written = 0;
    for (uint16_t i = 0; i < len; i++) {
        if (write(data[i]) == 1) written++;
        else break;
    }
    return written;
}

int STM32BufferedSerial::printf(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int result = vprintf(format, args);
    va_end(args);
    return result;
}

int STM32BufferedSerial::vprintf(const char* format, va_list args)
{
    char buffer[128];

    int len = vsnprintf(buffer, sizeof(buffer), format, args);

    if (len < 0) {
        return len;
    }

    uint16_t sendLen = static_cast<uint16_t>(
        len < static_cast<int>(sizeof(buffer)) ? len : sizeof(buffer) - 1
    );

    HAL_StatusTypeDef status = HAL_UART_Transmit(
        _huart,
        reinterpret_cast<uint8_t*>(buffer),
        sendLen,
        HAL_MAX_DELAY
    );

    return (status == HAL_OK) ? len : -1;
}


void STM32BufferedSerial::push(uint8_t c)
{
    uint16_t next = (_rxHead + 1) % _rxSize;
    if (next != _rxTail) {          // バッファ満杯でなければ
        _rxBuf[_rxHead] = c;
        _rxHead = next;
    }
}

int STM32BufferedSerial::pop()
{
    if (_rxHead == _rxTail) return -1;  // データなし
    uint8_t val = _rxBuf[_rxTail];
    _rxTail = (_rxTail + 1) % _rxSize;
    return val;
}


void STM32BufferedSerial::registerInstance(UART_HandleTypeDef* huart, STM32BufferedSerial* instance)
{
    if (huart == nullptr) return;
#if defined(USART1)
    if (huart->Instance == USART1) { instance_table_[0] = instance; return; }
#endif
#if defined(USART2)
    if (huart->Instance == USART2) { instance_table_[1] = instance; return; }
#endif
#if defined(USART3)
    if (huart->Instance == USART3) { instance_table_[2] = instance; return; }
#endif
#if defined(UART4)
    if (huart->Instance == UART4) { instance_table_[3] = instance; return; }
#endif
#if defined(UART5)
    if (huart->Instance == UART5) { instance_table_[4] = instance; return; }
#endif
#if defined(USART6)
    if (huart->Instance == USART6) { instance_table_[5] = instance; return; }
#endif
}

STM32BufferedSerial* STM32BufferedSerial::fromHandle(UART_HandleTypeDef* huart)
{
    if (huart == nullptr) return nullptr;
#if defined(USART1)
    if (huart->Instance == USART1) return instance_table_[0];
#endif
#if defined(USART2)
    if (huart->Instance == USART2) return instance_table_[1];
#endif
#if defined(USART3)
    if (huart->Instance == USART3) return instance_table_[2];
#endif
#if defined(UART4)
    if (huart->Instance == UART4) return instance_table_[3];
#endif
#if defined(UART5)
    if (huart->Instance == UART5) return instance_table_[4];
#endif
#if defined(USART6)
    if (huart->Instance == USART6) return instance_table_[5];
#endif
    return nullptr;
}

/*----------------------------------------
 * バッファ状態関連
 *----------------------------------------*/
bool STM32BufferedSerial::available() const {
    return (_rxHead != _rxTail);
}

int STM32BufferedSerial::readable_len() const {
	if(_rxHead < _rxTail)
		return static_cast<int>(_rxSize-(_rxTail-_rxHead));
	else return static_cast<int>(_rxHead - _rxTail);
}

void STM32BufferedSerial::flushRx() {
    _rxHead = _rxTail = 0;
}

void STM32BufferedSerial::flushTx() {
    _txHead = _txTail = 0;
}

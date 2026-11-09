#include "../STM32BufferedSerial.hpp"
#include <memory>
#include <new>

namespace {
// Preserve the caller's interrupt mask, including when called from an ISR.
class InterruptGuard {
public:
    InterruptGuard() : savedMask(__get_PRIMASK()) { __disable_irq(); }
    ~InterruptGuard() { __set_PRIMASK(savedMask); }
    InterruptGuard(const InterruptGuard&) = delete;
    InterruptGuard& operator=(const InterruptGuard&) = delete;
private:
    uint32_t savedMask;
};
}

STM32BufferedSerial::InstanceEntry STM32BufferedSerial::instance_table_[MAX_UARTS] = {};

STM32BufferedSerial::STM32BufferedSerial(UART_HandleTypeDef* huart, uint16_t bufSize)
    : _huart(huart),
      _rxSize(bufSize < 2 ? 2 : bufSize),
      _txSize(bufSize < 2 ? 2 : bufSize),
      _rxHead(0), _rxTail(0),
      _txHead(0), _txTail(0),
      _rxTmp(0), _txTmp(0),
      _txActive(false), _rxStarted(false), _blockingTx(false)
{
    _rxBuf = new uint8_t[_rxSize];
    _txBuf = new uint8_t[_txSize];

    registerInstance(_huart, this);
}

void STM32BufferedSerial::begin() {
    InterruptGuard guard;
    if (!_huart || !_huart->Instance) return;
    registerInstance(_huart, this);
    _rxStarted = true;
    _startRxInterrupt();
}

/*----------------------------------------
 * 受信割り込み完了ハンドラ
 *----------------------------------------*/
void STM32BufferedSerial::handleRxComplete()
{
    InterruptGuard guard;
    if (!_rxStarted) return;
    push(_rxTmp);
    _startRxInterrupt();
}


/*----------------------------------------
 * 送信割り込み完了ハンドラ
 *----------------------------------------*/
void STM32BufferedSerial::handleTxComplete() {
    InterruptGuard guard;
    _txActive = false;
    _startTxInterrupt();
}

void STM32BufferedSerial::handleError() {
    InterruptGuard guard;
    // HAL ends reception on ORE before invoking the error callback.
    if (_rxStarted) _startRxInterrupt();
}

/*----------------------------------------
 * 受信割り込み開始
 *----------------------------------------*/
void STM32BufferedSerial::_startRxInterrupt() {
    if (_huart && _huart->Instance && _huart->RxState == HAL_UART_STATE_READY)
        HAL_UART_Receive_IT(_huart, &_rxTmp, 1);
}

/*----------------------------------------
 * 送信割り込み開始
 *----------------------------------------*/
void STM32BufferedSerial::_startTxInterrupt() {
    // Called with interrupts masked. Do not consume a byte until HAL accepts it.
    if (_txActive || _blockingTx || _txTail == _txHead || !_huart ||
        !_huart->Instance || _huart->gState != HAL_UART_STATE_READY) return;
    _txTmp = _txBuf[_txTail];
    _txActive = true;
    if (HAL_UART_Transmit_IT(_huart, &_txTmp, 1) == HAL_OK)
        _txTail = (_txTail + 1) % _txSize;
    else
        _txActive = false;
}

/*----------------------------------------
 * データ読み取り
 *----------------------------------------*/
int STM32BufferedSerial::read() {
    InterruptGuard guard;
    return pop();
}

/*----------------------------------------
 * データ送信
 *----------------------------------------*/
int STM32BufferedSerial::write(uint8_t data) {
    InterruptGuard guard;
    if (!_huart || !_huart->Instance) return -1;
    uint16_t next = (_txHead + 1) % _txSize;
    if (next == _txTail) return -1;  // バッファ満杯

    _txBuf[_txHead] = data;
    _txHead = next;

    _startTxInterrupt();

    return 1;
}

int STM32BufferedSerial::write(const uint8_t* data, uint16_t len) {
    if (!data && len != 0) return -1;
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
    if (!format || !_huart || !_huart->Instance) return -1;
    char buffer[128];
    va_list copy;
    va_copy(copy, args);
    int len = vsnprintf(buffer, sizeof(buffer), format, copy);
    va_end(copy);
    if (len < 0) return len;
    if (len == 0) return 0;
    if (len > UINT16_MAX) return -1;

    std::unique_ptr<char[]> expanded;
    char* output = buffer;
    if (len >= static_cast<int>(sizeof(buffer))) {
        expanded.reset(new (std::nothrow) char[static_cast<size_t>(len) + 1]);
        if (!expanded) return -1;
        va_copy(copy, args);
        const int actual = vsnprintf(expanded.get(), static_cast<size_t>(len) + 1, format, copy);
        va_end(copy);
        if (actual != len) return -1;
        output = expanded.get();
    }

    {
        InterruptGuard guard;
        if (_blockingTx || _txActive || _txHead != _txTail ||
            _huart->gState != HAL_UART_STATE_READY) return -1;
        _blockingTx = true;
    }
    HAL_StatusTypeDef status = HAL_UART_Transmit(
        _huart, reinterpret_cast<uint8_t*>(output), static_cast<uint16_t>(len), HAL_MAX_DELAY);
    {
        InterruptGuard guard;
        _blockingTx = false;
        _startTxInterrupt();
    }

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
    if (!huart) return;
    InterruptGuard guard;
    InstanceEntry* empty = nullptr;
    for (auto& entry : instance_table_) {
        if (entry.handle == huart) {
            entry = instance ? InstanceEntry{huart, instance} : InstanceEntry{};
            return;
        }
        if (!entry.handle && !empty) empty = &entry;
    }
    if (empty && instance) *empty = InstanceEntry{huart, instance};
}

STM32BufferedSerial* STM32BufferedSerial::fromHandle(UART_HandleTypeDef* huart)
{
    if (!huart) return nullptr;
    InterruptGuard guard;
    for (const auto& entry : instance_table_)
        if (entry.handle == huart) return entry.instance;
    return nullptr;
}

/*----------------------------------------
 * バッファ状態関連
 *----------------------------------------*/
bool STM32BufferedSerial::available() const {
    InterruptGuard guard;
    return (_rxHead != _rxTail);
}

int STM32BufferedSerial::readable_len() const {
    InterruptGuard guard;
    return (_rxHead + _rxSize - _rxTail) % _rxSize;
}

void STM32BufferedSerial::flushRx() {
    InterruptGuard guard;
    _rxTail = _rxHead;
}

void STM32BufferedSerial::flushTx() {
    InterruptGuard guard;
    _txTail = _txHead;
}

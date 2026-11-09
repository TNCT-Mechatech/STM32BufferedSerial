#include "../STM32BufferedSerial.hpp"

extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart)
{
    if (auto obj = STM32BufferedSerial::fromHandle(huart)) {
        obj->handleRxComplete();
    }
}

extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart)
{
    if (auto obj = STM32BufferedSerial::fromHandle(huart)) {
        obj->handleTxComplete();
    }
}

extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart)
{
    if (auto obj = STM32BufferedSerial::fromHandle(huart)) {
        obj->handleError();
    }
}

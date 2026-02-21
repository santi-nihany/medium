//===----------------------------------------------------------------------===//
///
/// \file
/// Mutex global para arbitrar acceso al bus SPI compartido.
///
//===----------------------------------------------------------------------===//

#ifndef SPI_BUS_H
#define SPI_BUS_H

#include "FreeRTOS.h"
#include "main.h"

void spiBusInit(void);
bool_t spiBusLock(TickType_t timeoutTicks);
void spiBusUnlock(void);

#endif

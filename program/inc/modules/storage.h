//===----------------------------------------------------------------------===//
///
/// \file
/// Definiciones para el manejo de almacenamiento en la microSD.
///
//===----------------------------------------------------------------------===//

#ifndef STORAGE_H
#define STORAGE_H

#include "main.h"
#include "utils/sig.h"

bool_t storageInit(void);
void storageUpdate(void);
bool_t storageIsReady(void);
bool_t storageProbe(void);
bool_t storageSigSave(const char *path, const sigRecord_t *record);
bool_t storageSigLoad(const char *path, sigRecordBuffer_t *record);

#endif

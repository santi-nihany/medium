# Médium

🔮 _comunicación invisible_ 🔮

Dispositivo para leer y emitir señales infrarrojas y de radiofrecuencia. Basado en la [EDU-CIAA-NXP](https://www.proyecto-ciaa.com.ar/devwiki/doku.php%3Fid=desarrollo:edu-ciaa:edu-ciaa-nxp.html), que cuenta con un procesador LPC4337.

## Desarrollo

Para instalar

- Utilizar [Visual Studio Code](https://code.visualstudio.com/).
  - Instalar las extensiones recomendadas: [C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools) y [Cortex Debug](https://marketplace.visualstudio.com/items?itemName=marus25.cortex-debug).
- Instalar la toolchain de [ARM para embebidos](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads).
  1. Descargar para tu sistema operativo (por ejemplo, _x86_64 Linux hosted cross toolchains_).
  2. Procurar que el target sea _bare-metal_, denominado `arm-none-eabi`.
  3. Extraer en `/opt/gcc-arm-embedded`.
  4. Añadir `/opt/gcc-arm-embedded/bin` a tu PATH.
- Instalar [OpenOCD](https://openocd.org/) v0.12.0 o superior.
  ```bash
  # Debian/Ubuntu
  sudo apt install openocd
  # Arch
  sudo pacman -S openocd
  ```

Luego, todos los comandos se encuentran en el Makefile:

```bash
make all      # para compilar
make download # para bajar a la placa
make clean    # para limpiar la salida 
```

### Scripts

En la carpeta `scripts/custom` se encuentran algunos scripts útiles para el desarrollo. Se necesita tener la herramienta [uv](https://docs.astral.sh/uv/) para manejar los paquetes de Python. Luego, se puede correr los scripts con:

```bash
cd scripts/custom
uv sync                     # para instalar las dependencias
uv run sprites.py           # genera el archivo sprites.c
uv run terminal.py --help   # abre una terminal serial con la placa
uv run signals.py --help    # analiza un archivo .sig
```

### Patches

Se utilizó la última versión del [firmware_v3](https://github.com/epernia/firmware_v3/tree/548bcbf756a6260e241ef0d6820f4e2b3c81f765) para la CIAA en el momento de iniciar el proyecto (hash 548bcbf, publicada el 8 de abril de 2025). Sobre la misma se aplicarion los siguientes parches:

- <details>
  <summary>

  `libs/lpc_fatfs_disks/sapi/src/sapi_sdcard.c`
  
  </summary>
  
  ```diff c
  case FSSDC_CardStatus_Ready:
    g_sdcard->status = SDCARD_Status_ReadyUnmounted;
  - // Automount
  - if (!sdcardMount( true ))
  - {
  -     Board_UARTPutSTR ("sapi_sdcard: Automount failed!\r\n");
  - }
  + // El montaje se realiza desde la capa de aplicación para evitar
  + // bloqueos durante la secuencia de inicialización.
    break;
  ```

  </details>
- <details>
  <summary>

  `libs/lpc_fatfs_disks/source/fssdc.c`
  
  </summary>
  
  ```diff c
  #ifndef FSSDC_SUPPORTS_HOT_INSERTION
  +   // Sin pin CD, la extracción no actualiza automáticamente STA_NOINIT.
  +   // Forzar re-inicialización en cada InitSPI para soportar reinserción.
  +   g_diskStats |= STA_NOINIT;
      g_diskStats &= ~STA_NODISK;
      Board_UARTPutSTR ("FSSDC: [InitSPI] New card status: Inserted.\r\n");
      newCardStatus (FSSDC_CardStatus_Inserted);
      FSSDC_FatFs_DiskInitialize ();
  #endif
  ```

  </details>
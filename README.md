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
```

# NX2 Port Studio

[English](README.md) · **Español** · [Français](README.fr.md) · [日本語](README.ja.md) · [简体中文](README.zh-Hans.md)

Estoy desarrollando herramientas nativas de Switch 2 para Apple Silicon. En **0.3.0** añadí la exportación de apps para Mac mediante una revisión fijada de [nxnative](https://github.com/seedds/nxnative), manteniendo separado mi backend experimental NVN2.

Actualmente admito los dumps descifrados de **Donkey Kong Bananza, actualización 458752**, y **Splatoon Raiders, actualización 65536**, únicamente cuando coinciden el ID del título y las identidades completas de sus módulos. No presento las pruebas de otros proyectos como pruebas de NX2. **Mario Kart World sigue pendiente; no exporto IPA para iPhone/iPad.**

## Crear una app para Mac

1. Selecciono la carpeta que contiene `base/exefs`, `base/romfs` y `updates/<versión>/`, con el RomFS completo de la actualización.
2. Pulso **Analizar dump** y compruebo que aparece el perfil de exportación.
3. **Preparar herramientas de exportación Mac** configura un entorno separado y descarga la revisión exacta del motor.
4. **Crear app de Mac** ejecuta el compilador real, muestra su progreso y verifica la firma. Aplicaciones es el destino predeterminado; preservo las apps existentes.

No solicito keys ni firmware para estos archivos ya descifrados. La primera compilación puede tardar una hora. Elimino el directorio temporal de esa operación al terminar o al fallar y conservo sólo su registro privado y recibo; una exportación correcta todavía necesita pruebas de juego.

La app Studio requiere macOS 14+, Apple Silicon y Swift 6 para compilarla. La exportación usa los requisitos propios del motor fijado: Xcode 27.2 y su compilador Metal, Python 3.12+, .NET y las herramientas indicadas por nxnative. No reduzco los requisitos del juego a los de la interfaz.

```sh
swift run --package-path macOS/NXPortStudio NX2PortStudio
bash scripts/package_nx2_studio.sh "/ruta/NX2 Port Studio.app"
```

Conservo la inspección del formato `exefs/` + `romfs/` y los proyectos de desarrollo privados. Mi [guía de exportación](docs/nx2/NATIVE_EXPORT.md) documenta perfiles, revisión, almacenamiento y límites; la [arquitectura](docs/nx2/ARCHITECTURE.md) describe el backend experimental.

Doy crédito a NX Port Studio, nxnative y las referencias Eden/Eglantine, preservando sus avisos y licencias en [Créditos](CREDITS.md) y [fuentes fijadas](upstreams.lock.json). No incluyo juegos, recursos de Nintendo, firmware, keys, cuentas ni partidas en el repositorio o en la distribución de Studio.

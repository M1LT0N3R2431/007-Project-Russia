# 007: From Russia with Love — Android

*[English](README.en.md)*

Port no oficial para Android de la versión original de Xbox de *007: From Russia with Love*, con controles táctiles en pantalla, soporte de mando y un objetivo de 60 FPS.

El código x86 del juego se recompila estáticamente a C++ con [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) y se compila para arm64. El dibujado va por Vulkan, y la ventana, la entrada y el audio por SDL3. No hay emulador de por medio.

El proyecto fue creado a partir del port de PC [007FRWL-XBERecomp](https://github.com/TGP482/007FRWL-XBERecomp) de TGP482 y del host Android de [Black for Android](https://github.com/RanggaCasper/Black-Android) de Rangga Casper. Ver [Créditos](#créditos).

> [!NOTE]
> Este proyecto no incluye ningún archivo del juego. Para instalarlo o compilarlo necesitas los archivos de tu propia copia legal del juego.

## Descarga

El APK está en la página de [Releases](https://github.com/M1LT0N3R2431/007-Project-Russia/releases). Va firmado con la clave de depuración, así que se instala directamente.

## Requisitos

- **Teléfono:** dispositivo arm64 con Vulkan 1.1, Android 8.0 (API 26) o superior.
- **Juego:** versión USA o Europa de la versión de Xbox, ya sea como `default.xbe` con sus carpetas de datos, o como imagen de disco (`.iso`).

## Instalación

1. Instala el APK.
2. Abre **007 FRWL** y concede **acceso a todos los archivos** cuando lo pida (en Android 10, el permiso de **almacenamiento**).
3. Copia el juego en `/storage/emulated/0/007FRWL`, o en una carpeta `007FRWL` en la raíz de tu tarjeta SD. La app crea la carpeta al conceder el acceso y arranca en cuanto encuentra los archivos.
4. Si no concedes ese acceso, también funciona `Android/data/com.frwl.recomp/files/assets`.

Los ajustes (`settings.toml`, `layout.toml`), las partidas (`hdd`), los logs y la caché de shaders se guardan en `Android/data/com.frwl.recomp/files`.

## Controles táctiles

Los controles son el mando del propio juego sobre la pantalla, y cambian según lo que esté haciendo Bond.

| Control | Acción |
| --- | --- |
| Stick izquierdo | Moverse |
| Arrastrar en la zona derecha | Mirar |
| Disparar (R) | Disparar; mantener para apuntar |
| Fijar objetivo (L) | Fijar objetivo / apuntado libre |
| Acción (A) | Saltar, trepar, usar, interactuar |
| Concentrar (X) | Concentración / vista en primera persona |
| Cubrirse (Y) | Tomar y dejar una cobertura |
| Rodar (B) | Rodar |
| Recargar | Recargar |
| < / > | Arma anterior / siguiente |
| Agacharse (L3) | Agacharse |
| Inventario (BACK) | Selección de objeto / inventario |
| Pausa (START) | Menú de pausa |
| Atrás (BACK, menús) | Salir de un menú |
| Engranaje | Panel de ajustes |

La conducción y el jetpack tienen sus propios botones en los sitios del mando original: acelerar en A, frenar en X, freno de mano y aterrizar en B, y el cortador y salir en Y. En el coche el stick solo dirige (no acelera ni frena), los dos botones de flechas de abajo a la izquierda giran a izquierda y derecha, L fija el objetivo y Y sale del vehículo de un toque o corta los neumáticos si se mantiene.

El diseño se puede mover y redimensionar: abre el panel de ajustes (engranaje) y ve a **Controles táctiles → Editar diseño**. Arrastra un control para moverlo, pellízcalo para cambiar su tamaño, y usa la barra superior para restablecer, ocultar, cancelar o guardar. El diseño se guarda en `layout.toml`.

## Compilación

### Requisitos

- Windows, con Visual Studio 2022 y la carga de trabajo **Desarrollo para el escritorio con C++** (el recompilador enlaza con sus herramientas).
- Android SDK Platform 34, NDK `28.2.13676358` y CMake `3.31.6`, instalados desde el SDK Manager de Android Studio.
- JDK 17 o superior (`JAVA_HOME`).
- Git, y Python 3 si compilas desde un `.iso`.
- Tu propia copia del juego, versión USA o Europa.

### Pasos

1. Pon `default.xbe` y sus carpetas de datos en `assets\`, o pon el `.iso` del juego en `assets\` o `iso\`. La compilación extrae el `default.xbe` de la imagen con `tools\xdvdfs.py`.
2. Ejecuta `build.bat`.
3. Instala `app\build\outputs\apk\release\app-release.apk`.

La primera compilación recompila el código del juego y tarda entre 30 y 90 minutos; las siguientes solo compilan lo que cambió. Para desarrollo, `gradlew.bat assembleDebug` genera un APK de depuración una vez que existe `generated\game`.

### Estructura del proyecto

- `native/` — el host de Android: renderizador Vulkan, controles táctiles, entrada, audio y almacenamiento.
- `src/` — los enganches del juego y los ajustes, prompts y menús portados del port de PC.
- `config/game.toml` — la entrada del recompilador: símbolos, puntos de entrada extra y enganches del host.
- `app/` — la app de Android: actividad, manifiesto, icono y build de Gradle.
- `generated/` — el código recompilado del juego, lo escribe la compilación y no forma parte del repositorio.

## Créditos

Este port no existiría sin estos proyectos:

- [007FRWL-XBERecomp](https://github.com/TGP482/007FRWL-XBERecomp) de **TGP482** — el port de PC: los enganches del juego, los ajustes y menús, y la configuración de recompilación de la que parte este port.
- [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) de **TGP482** — el recompilador estático y el runtime portátil (kernel de Xbox, librerías XDK y Direct3D 8) sobre el que corre el juego.
- [Black for Android](https://github.com/RanggaCasper/Black-Android) de **Rangga Casper** — el host de Android sobre el que se construye este port: el renderizador Vulkan, los controles táctiles y su editor, las capas de entrada y almacenamiento, y el build de Gradle/CMake.
- [SCDA-XBERecomp-android](https://github.com/mogeldev/SCDA-XBERecomp-android) de **mogeldev** — referencia para llevar un port de XboxXBE-Recomp a Android.
- [SDL](https://github.com/libsdl-org/SDL), [glslang](https://github.com/KhronosGroup/glslang), [toml++](https://github.com/marzer/tomlplusplus) y [Dear ImGui](https://github.com/ocornut/imgui) — ventana, entrada y audio; compilación de shaders; archivos de ajustes; y el panel de ajustes.
- [xemu](https://github.com/xemu-project/xemu) y [Cxbx-Reloaded](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded), que XboxXBE-Recomp usó como referencia para el comportamiento del hardware y las APIs de Xbox.

Las licencias de terceros están en [THIRD_PARTY.txt](THIRD_PARTY.txt).

## Legal

*007: From Russia with Love* es una marca de Danjaq, LLC y Metro-Goldwyn-Mayer. Este proyecto no está afiliado ni respaldado por ellas, ni por Electronic Arts, EON Productions ni ninguna de sus filiales.

No se incluyen ni se distribuyen archivos del juego. Necesitas poseer el juego para usar este proyecto. El icono de la app usa arte del juego y no está cubierto por la licencia MIT.

## Licencia

El código fuente se publica bajo la licencia MIT, ver [LICENSE](LICENSE).

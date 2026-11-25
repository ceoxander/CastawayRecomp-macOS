# The Sims 2: Castaway para macOS

![Menú de CastawayRecomp](https://raw.githubusercontent.com/ceoxander/CastawayRecomp-macOS/macos-port/runtime/ui/assets/tropical-menu.png)

**Port nativo para Mac Apple Silicon.** Necesitas proporcionar tu propia copia del juego para Wii.

[⬇ Descargar para macOS](https://github.com/ceoxander/CastawayRecomp-macOS/releases/latest) · [Ver código fuente](https://github.com/ceoxander/CastawayRecomp-macOS/tree/macos-port)

## Instalación simple

1. Descarga **CastawayRecomp-Apple-Silicon.dmg** desde [Versiones](https://github.com/ceoxander/CastawayRecomp-macOS/releases/latest).
2. Abre el DMG y ejecuta **Instalar Castaway.command**.
3. Selecciona tu imagen propia del juego: **.iso**, **.rvz** o **.wbfs**.
4. Espera mientras prepara las dependencias y compila el port. La primera compilación puede tardar unos minutos.
5. Abre **The Sims 2 Castaway.app** desde Aplicaciones.

> El juego no está incluido. La imagen se procesa localmente; usa una copia que tengas legalmente.

### Requisitos

- Mac con Apple Silicon (M1 o posterior) y macOS compatible.
- Imagen USA **R42E69** de The Sims 2: Castaway para Wii.
- Espacio libre para dependencias y archivos compilados.
- Internet durante la preparación inicial.

## Controles

| Acción | Teclado y ratón | Mando |
|---|---|---|
| Mover al Sim | W, A, S, D | Stick izquierdo |
| Apuntar | Ratón o I, J, K, L | Stick derecho |
| Aceptar | Espacio o clic izquierdo | A / botón inferior |
| Cancelar | Retroceso o clic derecho | B / botón derecho |
| Menú | Enter | Start |
| Cruceta | Flechas | Cruceta |
| Gatillos L / R | Q / E | LT / RT |

Compatible con mandos Xbox, PlayStation, Switch Pro y otros reconocidos por SDL3.

## Acerca del port

El proyecto recompila el código PowerPC del juego a C++ y emula los servicios de Wii necesarios. El disco compatible es USA (R42E69). No se distribuyen archivos del juego.

La versión de macOS está dirigida a Apple Silicon. Consulta [PROGRESS_MACOS.md](PROGRESS_MACOS.md) para el estado del port y [Versiones](https://github.com/ceoxander/CastawayRecomp-macOS/releases) para las descargas.

## Compilar desde el código

Necesitas Xcode Command Line Tools, CMake, Ninja y Python 3.10 o posterior. Desde la raíz del repositorio, ejecuta:

```bash
./tools/package_macos.sh
```

## Créditos y licencia

Basado en [BlackKnightRecomp](https://github.com/yunophilia/BlackKnightRecomp). Consulta [LICENSE](LICENSE) y los avisos de cada dependencia. The Sims 2 y sus marcas pertenecen a sus titulares. Este proyecto no está afiliado con EA ni Nintendo.

**Cada jugador debe aportar su propia copia del juego.**


![Icono de CastawayRecomp](https://raw.githubusercontent.com/ceoxander/CastawayRecomp-macOS/macos-port/runtime/ui/assets/castaway-app-icon.png)

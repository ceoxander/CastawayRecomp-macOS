# Progreso macOS ARM64

## 2026-11-25

- Se conservó el trabajo existente. `ninja -C build/macos-arm64 -j 2 CastawayRecomp` compila y enlaza el ejecutable ARM64; el paquete incluye SDL3, DXMT y vkd3d y pasa `plutil` y `codesign --verify --deep --strict`.
- El empaquetador limita la compilación a dos trabajos. Para una app situada en `Applications`, los datos escribibles se ubican en `~/Library/Application Support/CastawayRecomp`; las compilaciones existentes fuera de esa carpeta conservan su ubicación anterior.
- La ejecución gráfica requiere acceso a la sesión de macOS: bajo el sandbox el proceso abortó durante la inicialización; con acceso gráfico arrancó. El ejecutable suelto depende de rutas de bibliotecas de desarrollo; usar el ejecutable dentro del paquete.
- El proyecto contiene una prueba E2E anterior en `build/macos-arm64/qa-e2e4` con 12 comprobaciones aprobadas, incluidas partida guardada y recarga. La prueba nueva con NAND aislada en `build/macos-arm64/current-e2e` también terminó con 12 aprobadas, 0 fallidas y salida 0 (213,30 s); se realizó antes de los últimos cambios visuales.
- El código recompilado disponible corresponde a la edición USA R42E69. La interfaz en español no demuestra compatibilidad con una edición española del disco.
- Los 120 FPS independientes, la estabilidad prolongada, la audición del audio, los mandos físicos y las mejoras gráficas solicitadas todavía no están verificados. No se debe acelerar la simulación para aparentar 120 FPS.
- El SDL3 local declara mínimo macOS 27.2 mientras que el ejecutable declara 27.0; el enlazador avisa. La ejecución está en prueba en macOS 27.0.1, pero no se ha validado compatibilidad con otras versiones.

- Se instaló `~/Applications/The Sims 2 Castaway.app`; firma verificada. La app instalada usa Application Support para sus datos y requiere seleccionar los archivos originales desde el lanzador.
- Se restauró el intro de Apple durante 2,5 segundos y se ajustó la paleta del menú principal. El arranque y el menú se capturaron visualmente en `build/macos-arm64/menu-current/intro.png` y `menu-new.png`; la prueba de arranque terminó con salida 0.
- Las capturas de la prueba muestran que el HUD y el menú de pausa siguen siendo los originales; hay un recuadro negro en el retrato del menú de pausa que requiere investigación. No se ha completado el rediseño solicitado de pausa, guardado y HUD ni una validación de ausencia de artefactos.
- El menú principal se adaptó a la paleta del juego (celeste, azul y verde) y utiliza Arial Narrow Bold como aproximación local; no se ha identificado la fuente original exacta. El crédito se mantiene y se aumentó su contraste.
- El intro de Apple y el crédito se capturaron también en un arranque normal sin guion (`build/macos-arm64/native-new-check/intro-normal.png`). El ejecutable instalado coincide con el probado.
- Tras el aviso de cierre al elegir Nueva partida, una prueba de la acción del menú nativo llegó a Crear un Sim y terminó sin error (`build/macos-arm64/native-new-check/after_new_game.png`); eso no reproduce ni descarta el fallo observado por el usuario. Sigue pendiente obtener el contexto o registro de ese cierre.
- El render existente permite escala interna y filtrado lineal; no se ha implementado ni verificado un reescalado de vídeo HD adicional para las cinemáticas.

## Ajustes quirúrgicos posteriores

- Se eliminó la pantalla de Apple del arranque; se mantienen los fotogramas y cinemáticas del juego, y la confirmación automática de los avisos Wii.
- El retrato negro se debía a una copia de textura RGBA8 de 64×64 que quedaba solo en GPU; el generador de miniaturas la lee desde memoria. Se agregó la copia de vuelta a memoria para ese caso. La captura `build/macos-arm64/verify-portrait/pause.png` muestra el retrato.
- La presentación Metal usa un filtro cúbico B-spline de pesos positivos al ampliar ambos ejes, con bordes limitados, sin aplicar el realce de nitidez durante esa ampliación. La captura interna `build/macos-arm64/trace-portrait2/display.png` permite inspeccionar una escena; no demuestra ausencia de artefactos en todas las cinemáticas ni crea detalle original inexistente. En otros casos se mantiene el filtrado existente.
- `ninja -C build/macos-arm64 -j 2 CastawayRecomp_app` compiló y firmó el paquete. La prueba E2E con NAND aislada `build/macos-arm64/verify-portrait` terminó con 12 aprobadas, 0 fallidas y salida 0 en 210,40 s: creación del Sim, movimiento, guardado y carga. El usuario confirmó que escuchó correctamente el audio en una ejecución sin `--mute`.
- La captura del escritorio mediante `screencapture` falló incluso después de conceder permisos; las capturas referidas fueron producidas por la propia aplicación. Continúa el aviso del enlazador sobre el mínimo de SDL3 (27.2 frente a 27.0 del ejecutable).

## Continuación: mando y menú

- Copia de seguridad de la app instalada verificada con codesign: `build/macos-arm64/stable-app-backup-20261125.app`.
- Corregida la entrega al menú de ImGui de eventos de conexión/desconexión de mandos; se mantiene la entrega al controlador del juego. Compilado; reconexión física en caliente aún sin prueba.
- macOS y SDL detectaron un DualSense por Bluetooth; el usuario confirmó que los botones responden. SDL aceptó configurar su luz verde amarillenta. El usuario confirmó que «Nueva partida» entra en Crear un Sim tras liberar la confirmación del juego.
- Se agregó un enlace para volver al menú nuevo al cancelar el diálogo de nueva partida; la rama Sí se observó en una sesión real, la rama No aún requiere prueba. Los ajustes de controles muestran nombres PlayStation si hay uno activo.
- Se identificó la textura independiente del icono Wii B. Con un PlayStation conectado y el mapeo predeterminado se sustituye por Círculo; la captura `build/macos-arm64/verify-ps-prompt/after_new_game.png` lo demuestra. Otras indicaciones dentro del juego no están todas reemplazadas; la indicación inicial de X está implementada pero pendiente de comprobar tras la desconexión del mando.
- F3 muestra intervalos de llamadas SDL_RenderPresent, un 1 % bajo calculado de las últimas 512 muestras válidas, CPU del proceso y memoria. No mide el refresco físico ni implica 120 FPS. El render GX se produce al ritmo de los fotogramas invitados (~60 Hz); obtener 120 imágenes distintas sin acelerar la simulación requiere reconstruir/interpolar estados o comandos de render, y aún no está implementado. Tampoco existe selector de 30/60/120 ni métricas reales de 120.
- El código recompilado y la tabla de direcciones disponibles son exclusivamente R42E69 (USA); una edición española distinta requiere identificar y recompilar esa versión. La selección de idioma no demuestra compatibilidad con esa edición.
- La prueba E2E `build/macos-arm64/verify-final-p0` pasó 12 comprobaciones y terminó con código 0 en 210,45 s después de los cambios de menú, telemetría y símbolo de Círculo. El guion específico `build/macos-arm64/dialog-cancel3` seleccionó «No» y registró `new game confirmation cancelled`; la devolución llama a la restauración del menú nativo. Falta confirmación visual manual de esta rama.
- Se añadió una indicación inicial de X si hay PlayStation activo y el botón A conserva la asignación predeterminada. Se compiló, pero la captura de esa pantalla quedó pendiente porque el mando se desconectó. La sustitución de Círculo se limita a la textura B identificada; otras indicaciones originales no se han convertido.
- A petición posterior del usuario, se restauró el inicio con el logo de Apple y el crédito durante 2,5 s. La prueba aislada `build/macos-arm64/verify-apple-restored` capturó el logo, llegó al menú y terminó con código 0; el usuario confirmó que se veía. La app instalada se actualizó y su firma local se verificó.

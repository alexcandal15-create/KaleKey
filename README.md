# SampleKey Analyzer (VST3, Windows, FL Studio)

Analizador de tipo efecto: el audio pasa sin tocarse y el plugin estima la tonalidad
(24 tonalidades, notación con sostenidos) de la señal que recibe en SU entrada.
Solo ve la pista de mixer donde lo insertas, no todo el proyecto.

## Estado real (léelo)
- `Source/KeyDetector.*` (núcleo DSP) y `Tests/` SÍ se compilaron y ejecutaron (g++ 13, Linux): ver "Pruebas".
- `PluginProcessor.*`, `PluginEditor.*` y `CMakeLists.txt` (la parte JUCE/VST3) NO se han compilado
  (no había JUCE ni red en el entorno donde se escribió). Puede haber algún error de compilación menor:
  si te sale uno, pégamelo y lo arreglo. No se ha probado dentro de FL Studio.
- Las pruebas usan señales sintéticas. Samples reales (mezclas densas, voces, 808) pueden dar peores resultados.

## Requisitos (Windows 10/11 x64)
1. Visual Studio 2022 (o Build Tools) con "Desarrollo de escritorio con C++".
2. CMake >= 3.22 y Git (en el PATH).
3. Internet SOLO para compilar: CMake descarga JUCE 8.0.4. (Alternativa offline: `git clone --branch 8.0.4 https://github.com/juce-framework/JUCE.git` dentro de esta carpeta.)
4. Licencia JUCE: gratis para uso personal con las condiciones de JUCE; revisa su licencia si vas a distribuirlo.

## Compilar
En "x64 Native Tools Command Prompt for VS 2022" o PowerShell, dentro de la carpeta del proyecto:

    cmake -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release --target SampleKeyAnalyzer_VST3

Resultado: `build\SampleKeyAnalyzer_artefacts\Release\VST3\SampleKey Analyzer.vst3`

## Pruebas del algoritmo
    cmake --build build --config Release --target KeyDetectorTests
    build\Release\KeyDetectorTests.exe

(Sin CMake, en Linux/MSYS: `g++ -O2 -std=c++17 -I Source Tests/KeyDetectorTests.cpp Source/KeyDetector.cpp -o keytests && ./keytests`)

## Instalar
Copia la carpeta `SampleKey Analyzer.vst3` (o el archivo, según versión de JUCE) a
`C:\Program Files\Common Files\VST3\` (requiere permisos de administrador).

## FL Studio
1. Options > Manage plugins. Comprueba que `C:\Program Files\Common Files\VST3` está en las rutas de búsqueda.
2. Pulsa "Start scan" (o "Find installed plugins"). Debe aparecer "SampleKey Analyzer".
3. Mixer: elige la pista por la que suena tu sample (Channel Rack > selector de pista del canal).
4. En un slot de efectos de esa pista: Add > SampleKey Analyzer.
5. Dale a Play. Tarda unos segundos de audio tonal en dar resultado.

Controles: Reset (reinicia análisis), Hold (congela), Sensitivity (izquierda = más lento y estable, derecha = más rápido).
Estados: Listening (no llega audio), Analyzing, Stable Signal, Insufficient Audio (poco contenido tonal o ambiguo).
Confidence es una heurística (correlación + margen + cantidad de audio), no una probabilidad calibrada.

## Solución de errores
- No aparece en FL: repite el scan; comprueba la ruta; usa FL Studio 64 bits; mira si el scan lo marcó como fallido.
- "Listening" siempre: el medidor INPUT no se mueve => el sample no pasa por esa pista del mixer.
- "Insufficient Audio": sample demasiado corto, percusivo o sin tonos claros. Es intencional.
- Resultado relativo mayor/menor (p. ej. C Major vs A Minor): con los mismos acordes es ambiguo; mira "Alternative Keys".
- CMake no descarga JUCE: sin red/Git; clona JUCE a mano en `./JUCE`.
- Error de compilación en MSVC: pégalo completo.

## Cómo funciona
Audio thread: suma a mono -> FIFO sin bloqueos (sin asignar memoria). Hilo de análisis: FFT 16384 (Hann, salto 4096),
picos espectrales con interpolación parabólica, supresión de armónicos (3x/5x/6x/7x), rechazo de frames poco tonales
(percusión/ruido), estimación de afinación, chroma de 12 notas acumulado con memoria exponencial (4-30 s),
correlación de Pearson con perfiles Krumhansl-Kessler (mayor/menor), histéresis para no saltar entre tonalidades.
Latencia de procesado del plugin: 0 (el análisis es paralelo).

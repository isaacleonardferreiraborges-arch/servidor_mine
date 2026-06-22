# NativeTextApp

Aplicativo Android 100% em C++ usando **NativeActivity**, **OpenGL ES 2.0** e **FreeType**.

Exibe o texto **"oi amigos tudo bem"** centralizado na tela com:
- Fonte Roboto renderizada via FreeType
- Cor azul Material (#2196F3)
- Anti-aliasing via texturas GL_LUMINANCE + GL_LINEAR
- Animação de pulso suave (breathing scale)
- Toque na tela muda a cor (5 cores: azul, teal, laranja, roxo, âmbar)
- Fundo escuro (#0F0F14)

---

## Estrutura do projeto

```
NativeTextApp/
├── app/
│   ├── CMakeLists.txt              ← build NDK (baixa FreeType automaticamente)
│   ├── build.gradle
│   └── src/main/
│       ├── AndroidManifest.xml
│       ├── assets/
│       │   └── Roboto-Regular.ttf  ← fonte carregada em runtime
│       ├── cpp/
│       │   └── main.cpp            ← todo o app em C++
│       └── res/values/strings.xml
├── build.gradle
├── settings.gradle
└── gradle/wrapper/gradle-wrapper.properties
```

---

## Pré-requisitos

| Ferramenta | Versão mínima |
|---|---|
| Android Studio | Hedgehog (2023.1) ou superior |
| NDK | 25.x ou 26.x |
| CMake | 3.22.1 |
| SDK compileSdk | 34 |
| minSdk | 21 (Android 5.0) |

---

## Como compilar

### Via Android Studio

1. **Abra o projeto**: `File → Open` → selecione a pasta `NativeTextApp`
2. Aguarde o Gradle sync
3. Em `local.properties`, confirme que `sdk.dir` aponta para seu SDK
4. Instale o NDK se solicitado: `SDK Manager → SDK Tools → NDK (Side by side)`
5. Clique em **Run ▶** ou `Build → Build APK(s)`

> Na primeira compilação, o CMake irá baixar e compilar o FreeType 2.13.2
> automaticamente via `FetchContent`. Necessita de internet.

### Via linha de comando

```bash
# Clone / copie o projeto
cd NativeTextApp

# Copie e ajuste local.properties
cp local.properties.template local.properties
# edite sdk.dir=...

# Build debug
./gradlew assembleDebug

# Instalar no device conectado
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

---

## Detalhes técnicos

### Fluxo de renderização

```
android_main()
    └── loop principal
            ├── ALooper_pollAll()       ← eventos do sistema
            ├── handleCmd()             ← ciclo de vida da Activity
            │     ├── APP_CMD_INIT_WINDOW → initDisplay() + loadFont() + initGL()
            │     └── APP_CMD_TERM_WINDOW → destroyDisplay()
            ├── handleInput()           ← toque → troca de cor
            └── drawFrame()
                    ├── animação (pulse via sin)
                    ├── glClear (fundo escuro)
                    ├── ortho projection matrix
                    └── para cada glyph:
                            ├── glBindTexture (textura FT)
                            ├── upload quad (6 vértices xyzw)
                            └── glDrawArrays(GL_TRIANGLES)
```

### Por que GL_LUMINANCE para as glyphs?

O FreeType retorna bitmaps em escala de cinza (1 canal). Usando `GL_LUMINANCE`
e o fragment shader que usa esse valor como alpha, conseguimos anti-aliasing
perfeito com um único canal — eficiente e sem artefatos.

### Tamanho de fonte adaptativo

```cpp
int fontSize = std::max(32, (int)(s.screenW * 0.10f));
```

10% da largura da tela → funciona em phones compactos e tablets.

---

## Licenças

- **FreeType**: FTL (The FreeType Project License) / GPLv2
- **Roboto**: Apache License 2.0
- **android_native_app_glue**: Apache License 2.0 (NDK)

# Marcador de Softball para OBS

Fuente nativa para OBS Studio en Windows, con modos día y noche. Mantiene bolas, strikes, outs, media entrada y carreras por entrada para dos equipos.

## Instalación

1. Descarga el instalador `.exe` más reciente desde [Releases](https://github.com/somefirenoodles/OBSMarcador/releases/latest).
2. Cierra OBS, ejecuta el instalador y vuelve a abrir OBS.
3. Pulsa **+** en Fuentes y elige **Marcador de Softball**.

No requiere terminal, servidor, cuenta ni configuración de red.

## Uso

1. Agrega la fuente **Marcador de Softball**.
2. Escribe los nombres de los equipos en sus propiedades.
3. Activa **Modo día** solamente cuando necesites el fondo blanco.
4. Usa el teclado numérico:

| Tecla | Acción |
|---|---|
| `1` | Bola |
| `2` | Strike |
| `3` | Out |
| `4` | Carrera visitante |
| `5` | Siguiente media entrada |
| `6` | Carrera local |
| `7` | Quitar carrera visitante |
| `8` | Quitar carrera local |
| `9` | Media entrada anterior |
| `0` | Deshacer |

Las mismas acciones aparecen como botones en las propiedades de la fuente. OBS permite cambiar cualquier tecla desde **Ajustes → Teclas rápidas**.

## Reglas automáticas

- Cuatro bolas limpian bolas y strikes.
- Tres strikes suman un out.
- Tres outs cambian de media entrada.
- El partido usa seis entradas.

## Desarrollo (solo para programadores)

La plantilla oficial descarga las dependencias de OBS durante la configuración:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
```

Prueba mínima de la lógica, sin depender del SDK de OBS:

```powershell
gcc -std=c17 -Wall -Wextra -Werror -Isrc tests\test_scoreboard.c src\scoreboard.c -o test_scoreboard.exe
.\test_scoreboard.exe
```

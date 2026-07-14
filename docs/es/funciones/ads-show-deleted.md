---
title: AdsShowDeleted
layout: default
parent: Referencia API
nav_order: 1
permalink: /es/funciones/ads-show-deleted/
---

# AdsShowDeleted

Define si los registros eliminados son visibles (SET DELETED ON/OFF).

## Sintaxis

```c
UNSIGNED32 AdsShowDeleted(UNSIGNED16 us);
```

## Parámetros

| Parámetro | Tipo | Descripción |
|-----------|------|-------------|
| `us` | `UNSIGNED16` | `1` = mostrar registros eliminados (SET DELETED OFF, valor por defecto de Clipper); `0` = ocultar registros eliminados (SET DELETED ON). |

## Valor de Retorno

`AE_SUCCESS` (0) siempre.

## Descripción

`AdsShowDeleted` controla si la navegación (`GotoTop`, `Skip`,
recorridos por índice) y los recuentos con filtro omiten las filas
marcadas como eliminadas en el DBF.

En local, el indicador es global al proceso y se guarda también en la
`Connection` activa. En conexiones **remotas** `tcp://` (desde
v1.8.10 / M12.31), el cliente reenvía el estado a cada sesión abierta
del servidor mediante el opcode wire `ShowDeleted` (`0xDA`).

Desde v1.8.11 (M12.32) el orden de las llamadas ya no importa: una
conexión abierta **después** de `AdsShowDeleted(0)` sincroniza el
estado justo tras el handshake de conexión, por lo que el arranque
habitual de rddads / FiveWin (`SET DELETED ON` en `Main` y conectar
después) también oculta los registros eliminados en remoto. El
servidor reaplica además el indicador a la conexión ABI que crea de
forma diferida para la navegación ordenada/con ámbito.

Actualice **ambos** `openace64.dll` y `openads_serverd` a la vez; los
servidores antiguos ignoran el opcode (best-effort) y siguen
devolviendo filas eliminadas dentro de un recorrido remoto con ámbito.

## Ejemplo

```c
AdsShowDeleted(0);  // SET DELETED ON — oculta filas eliminadas
AdsGotoTop(hOrd);
while (1) {
    UNSIGNED16 eof = 0;
    AdsAtEOF(hTable, &eof);
    if (eof) break;
    /* ... procesar fila viva ... */
    AdsSkip(hOrd, 1);
}
AdsShowDeleted(1);  // restaura el valor por defecto (mostrar eliminadas)
```

## Ver También

- [AdsGetDeleted]({{ site.baseurl }}/es/funciones/ads-get-deleted/)
- [Referencia API]({{ site.baseurl }}/es/api-reference/)

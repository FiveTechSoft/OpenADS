---
title: AdsShowDeleted
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-show-deleted/
---

# AdsShowDeleted

Define se os registos eliminados são visíveis.

## Sintaxe

```c
UNSIGNED32 AdsShowDeleted(UNSIGNED16 us);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `us` | `UNSIGNED16` | 1 para mostrar registos eliminados, 0 para ocultar. |

## Valor de Retorno

`AE_SUCCESS` (0) sempre.

## Descrição

`AdsShowDeleted` define se os registos marcados como eliminados são visíveis nas operações de navegação. Por omissão, os registos eliminados são ocultados (comportamento padrão do Clipper).

Esta é uma configuração global que afeta todas as tabelas abertas. Em
ligações **remotas** `tcp://` (desde v1.8.10), o cliente envia o estado
ao servidor pelo opcode wire `ShowDeleted` (0xDA), para que percursos
com âmbito (`OrdScope`) ignorem registos eliminados como no modo local.

Desde a v1.8.11 (M12.32) a ordem das chamadas deixa de importar: uma
ligação aberta **depois** de `AdsShowDeleted(0)` sincroniza o estado
logo após o handshake de conexão, pelo que o arranque habitual de
rddads / FiveWin (`SET DELETED ON` no `Main`, conectar depois) também
oculta os registos eliminados em remoto. O servidor reaplica ainda o
indicador à conexão ABI criada preguiçosamente para a navegação
ordenada/com âmbito.

Atualize `openace64.dll` **e** `openads_serverd` em conjunto.

## Exemplo

```c
AdsShowDeleted(1);  // Mostra registos eliminados
AdsShowDeleted(0);  // Oculta registos eliminados
```

## Ver Também

- [AdsDeleteRecord]({{ site.baseurl }}/pt/funcoes/ads-delete-record/)
- [AdsIsRecordDeleted]({{ site.baseurl }}/pt/funcoes/ads-is-record-deleted/)
- [AdsRecallRecord]({{ site.baseurl }}/pt/funcoes/ads-recall-record/)

---

[AdsGetRecordCount →]({{ site.baseurl }}/pt/funcoes/ads-get-record-count/)

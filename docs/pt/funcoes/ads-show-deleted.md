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

---
title: AdsIsNull
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-is-null/
---

# AdsIsNull

Testa se um campo no registro atual é NULL.

## Sintaxe

```c
UNSIGNED32 AdsIsNull(ADSHANDLE hTable, UNSIGNED8 *pucField,
                      UNSIGNED16 *pbNull);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hTable` | `ADSHANDLE` | Handle da tabela (local, remota ou backend). |
| `pucField` | `UNSIGNED8*` | Nome do campo (string terminada em NUL) ou ordinal baseado em 1 via `ADSFIELD(n)`. |
| `pbNull` | `UNSIGNED16*` | Saída — `1` se o campo for NULL, `0` caso contrário. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso. Código de erro diferente de zero em caso de falha
(handle inválido, campo não encontrado).

## Descrição

`AdsIsNull` inspeciona o bitmap NULL do registro atual para
determinar se o campo especificado é NULL. O bitmap NULL está
presente apenas em tabelas ADT; para tabelas CDX/NTX ele está
sempre ausente, então a função sempre relata "não nulo" (0).

Para tabelas remotas e de backend, a função relata
conservatoriamente "não nulo", pois a capacidade de NULL ainda
não é exposta pelo protocolo de rede.

## Exemplo

```c
ADSHANDLE hTable;
UNSIGNED16 isNull = 0;
AdsOpenTable(&hTable, "customers.adt", NULL, NULL,
             ADS_ANSI, ADS_EXCLUSIVE, NULL, NULL);
AdsGotoTop(hTable);
AdsIsNull(hTable, (UNSIGNED8*)"email", &isNull);
if (isNull)
    printf("email is NULL\n");
else
    printf("email has a value\n");
AdsCloseTable(hTable);
```

## Ver Também

- [AdsGetField]({{ site.baseurl }}/pt/funcoes/ads-get-field/)
- [AdsSetNull]({{ site.baseurl }}/pt/funcoes/ads-set-null/)
- [AdsSetEmpty]({{ site.baseurl }}/pt/funcoes/ads-set-empty/)

---

[← AdsIsIndexUnique]({{ site.baseurl }}/pt/funcoes/ads-is-index-unique/)
[AdsIsRecordInAOF →]({{ site.baseurl }}/pt/funcoes/ads-is-record-in-aof/)

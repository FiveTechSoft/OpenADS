---
title: AdsDDSetIndexProperty
layout: default
parent: ReferÃªncia da API
nav_order: 1
permalink: /pt/funcoes/ads-dd-set-index-property/
---

# AdsDDSetIndexProperty

Define uma propriedade de um Ã­ndice no dicionÃ¡rio de dados.

## Sintaxe

```c
UNSIGNED32 ENTRYPOINT AdsDDSetIndexProperty(ADSHANDLE hConnect, UNSIGNED8* pucTableName, UNSIGNED8* pucTagName, UNSIGNED16 usPropertyID, void* pvProperty, UNSIGNED16 usPropertyLen);
```

## ParÃ¢metros

| ParÃ¢metro | Tipo | DescriÃ§Ã£o |
|-----------|------|-----------|
| `hConnect` | `ADSHANDLE` | Handle da conexÃ£o com o dicionÃ¡rio de dados. |
| `pucTableName` | `UNSIGNED8*` | Nome da tabela. |
| `pucTagName` | `UNSIGNED8*` | Nome da tag do Ã­ndice. |
| `usPropertyID` | `UNSIGNED16` | ID da propriedade a ser definida. |
| `pvProperty` | `void*` | Valor da propriedade. |
| `usPropertyLen` | `UNSIGNED16` | Comprimento do valor. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso. CÃ³digo de erro se falhar.

## DescriÃ§Ã£o

`AdsDDSetIndexProperty` define uma propriedade de um Ã­ndice ou tag dentro de uma tabela no dicionÃ¡rio de dados. As propriedades que podem ser definidas incluem expressÃ£o, Ãºnico, descendente e condiÃ§Ã£o.

## Exemplo

```c
AdsDDSetIndexProperty(hConnect, "Clientes", "IdxNome", ADS_DD_INDEX_OPTIONS, &bTrue, sizeof(UNSIGNED16));
```

## Ver TambÃ©m

- [AdsDDGetIndexProperty]({{ site.baseurl }}/pt/funcoes/ads-dd-get-index-property/)
- [AdsDDAddIndexFile]({{ site.baseurl }}/pt/funcoes/ads-dd-add-index-file/)

---

[AdsDDSetProcedureProperty â†’]({{ site.baseurl }}/pt/funcoes/ads-dd-set-procedure-property/)

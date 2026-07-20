---
title: AdsDDGetIndexProperty
layout: default
parent: ReferÃªncia da API
nav_order: 1
permalink: /pt/funcoes/ads-dd-get-index-property/
---

# AdsDDGetIndexProperty

ObtÃ©m uma propriedade de um Ã­ndice no dicionÃ¡rio de dados.

## Sintaxe

```c
UNSIGNED32 ENTRYPOINT AdsDDGetIndexProperty(ADSHANDLE hConnect, UNSIGNED8* pucTableName, UNSIGNED8* pucTagName, UNSIGNED16 usPropertyID, void* pvProperty, UNSIGNED16* pusPropertyLen);
```

## ParÃ¢metros

| ParÃ¢metro | Tipo | DescriÃ§Ã£o |
|-----------|------|-----------|
| `hConnect` | `ADSHANDLE` | Handle da conexÃ£o com o dicionÃ¡rio de dados. |
| `pucTableName` | `UNSIGNED8*` | Nome da tabela. |
| `pucTagName` | `UNSIGNED8*` | Nome da tag do Ã­ndice. |
| `usPropertyID` | `UNSIGNED16` | ID da propriedade a ser obtida. |
| `pvProperty` | `void*` | Buffer para receber o valor da propriedade. |
| `pusPropertyLen` | `UNSIGNED16*` | Comprimento do buffer; retorna o comprimento do valor. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso. CÃ³digo de erro se falhar.

## DescriÃ§Ã£o

`AdsDDGetIndexProperty` recupera uma propriedade de um Ã­ndice ou tag dentro de uma tabela no dicionÃ¡rio de dados. As propriedades incluem nome do arquivo, expressÃ£o, Ãºnico, descendente, condiÃ§Ã£o e comprimento da chave.

## Exemplo

```c
UNSIGNED16 usLen = 256;
UNSIGNED8 aucValue[256];

AdsDDGetIndexProperty(hConnect, "Clientes", "IdxNome", ADS_DD_INDEX_EXPRESSION, aucValue, &usLen);
```

## Ver TambÃ©m

- [AdsDDSetIndexProperty]({{ site.baseurl }}/pt/funcoes/ads-dd-set-index-property/)
- [AdsDDAddIndexFile]({{ site.baseurl }}/pt/funcoes/ads-dd-add-index-file/)

---

[AdsDDGetProcedureProperty â†’]({{ site.baseurl }}/pt/funcoes/ads-dd-get-procedure-property/)

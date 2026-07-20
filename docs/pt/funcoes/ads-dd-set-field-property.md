---
title: AdsDDSetFieldProperty
layout: default
parent: ReferÃªncia da API
nav_order: 1
permalink: /pt/funcoes/ads-dd-set-field-property/
---

# AdsDDSetFieldProperty

Define uma propriedade de um campo no dicionÃ¡rio de dados.

## Sintaxe

```c
UNSIGNED32 ENTRYPOINT AdsDDSetFieldProperty(ADSHANDLE hConnect, UNSIGNED8* pucTableName, UNSIGNED8* pucFieldName, UNSIGNED16 usPropertyID, void* pvProperty, UNSIGNED16 usPropertyLen);
```

## ParÃ¢metros

| ParÃ¢metro | Tipo | DescriÃ§Ã£o |
|-----------|------|-----------|
| `hConnect` | `ADSHANDLE` | Handle da conexÃ£o com o dicionÃ¡rio de dados. |
| `pucTableName` | `UNSIGNED8*` | Nome da tabela. |
| `pucFieldName` | `UNSIGNED8*` | Nome do campo. |
| `usPropertyID` | `UNSIGNED16` | ID da propriedade a ser definida. |
| `pvProperty` | `void*` | Valor da propriedade. |
| `usPropertyLen` | `UNSIGNED16` | Comprimento do valor. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso. CÃ³digo de erro se falhar.

## DescriÃ§Ã£o

`AdsDDSetFieldProperty` define uma propriedade de um campo dentro de uma tabela no dicionÃ¡rio de dados. As propriedades que podem ser definidas incluem obrigatÃ³rio, padrÃ£o, regra de validaÃ§Ã£o e comentÃ¡rios.

## Exemplo

```c
AdsDDSetFieldProperty(hConnect, "Clientes", "Email", ADS_DD_FIELD_CAN_NULL, &usZero /* 0 = NOT NULL */, sizeof(UNSIGNED16));
```

## Ver TambÃ©m

- [AdsDDGetFieldProperty]({{ site.baseurl }}/pt/funcoes/ads-dd-get-field-property/)
- [AdsDDGetTableProperty]({{ site.baseurl }}/pt/funcoes/ads-dd-get-table-property/)

---

[AdsDDSetFunctionProperty â†’]({{ site.baseurl }}/pt/funcoes/ads-dd-set-function-property/)

---
title: AdsGetHandleType
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-handle-type/
---

# AdsGetHandleType

Retorna o tipo de um handle ADS (tabela, conexão, instrução, etc.).

## Sintaxe

```c
UNSIGNED32 AdsGetHandleType(ADSHANDLE h, UNSIGNED16 *pusType);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `h` | `ADSHANDLE` | Qualquer handle ADS válido. |
| `pusType` | `UNSIGNED16*` | Saída — constante do tipo de handle. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso.

## Constantes de Tipo de Handle

| Constante | Valor | Descrição |
|-----------|-------|-----------|
| `ADS_NONE` | 0 | Handle desconhecido/inválido. |
| `ADS_TABLE` | 1 | Handle de tabela (local, remota ou backend). |
| `ADS_STATEMENT` | 2 | Handle de instrução SQL. |
| `ADS_CURSOR` | 4 | Handle de cursor SQL. |
| `ADS_DATABASE_CONNECTION` | 6 | Handle de conexão de banco de dados. |
| `ADS_SYS_ADMIN_CONNECTION` | 7 | Conexão de administrador do sistema. |

## Descrição

`AdsGetHandleType` consulta o método `kind_of()` do registro
de handles para determinar o tipo de qualquer handle ADS.
Ele distingue corretamente entre tabelas, conexões, instruções e
índices em todos os backends (local, remoto, SQLite, PostgreSQL,
MariaDB, MSSQL, ODBC, Firebird).

Isso substitui o stub anterior que sempre retornava `ADS_TABLE`.

## Exemplo

```c
ADSHANDLE h;
UNSIGNED16 hType = 0;
AdsConnect60("tcp://server:6247", NULL, NULL, NULL, 0, &h);
AdsGetHandleType(h, &hType);
if (hType == ADS_DATABASE_CONNECTION)
    printf("Handle is a connection\n");
AdsDisconnect(h);
```

## Ver Também

- [AdsGetConnectionType]({{ site.baseurl }}/pt/funcoes/ads-get-connection-type/)
- [AdsGetTableType]({{ site.baseurl }}/pt/funcoes/ads-get-table-type/)
- [AdsConnect60]({{ site.baseurl }}/pt/funcoes/ads-connect60/)

---

[← AdsGetFilter]({{ site.baseurl }}/pt/funcoes/ads-get-filter/)
[AdsGetIndexCondition →]({{ site.baseurl }}/pt/funcoes/ads-get-index-condition/)

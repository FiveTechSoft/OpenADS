---
title: Referência da API
layout: default
parent: Início (PT)
nav_order: 5
permalink: /pt/api-reference/
has_children: true
---

# Referência da API do OpenADS — v1.4.0

Referência completa das 357 funções exportadas `Ads*` em
`ace64.dll` / `ace32.dll` / `libace.so`. Todas as funções
disponíveis para aplicações Harbour / X# / Clipper / C / PHP /
.NET.

**Legenda:**
- ✅ = Totalmente implementada
- ⚠️ = Parcial / accept-and-ignore (devolve `AE_SUCCESS` mas não
  faz nada significativo)
- 🔴 = Stub que devolve `AE_FUNCTION_NOT_AVAILABLE`
- ➡️ = Reenvio para outra implementação (sobrecarga versionada)

Todas as funções devolvem `UNSIGNED32` (`AE_SUCCESS` = 0 em
sucesso, código de erro ACE em falha) salvo indicação contrária.

---

## Tabela de Conteúdos

| # | Categoria | Funções |
|---|-----------|---------|
| 1 | [Gestão de Conexões](#1-gestão-de-conexões) | 10 |
| 2 | [Operações de Tabela](#2-operações-de-tabela) | 15 |
| 3 | [Navegação de Registos](#3-navegação-de-registos) | 14 |
| 4 | [Leitura de Campos](#4-leitura-de-campos-por-tipo) | 21 |
| 5 | [Escrita de Campos](#5-escrita-de-campos) | 17 |
| 6 | [Operações de Registo](#6-operações-de-registo) | 10 |
| 7 | [Bloqueio](#7-bloqueio) | 12 |
| 8 | [Operações de Índice](#8-operações-de-índice) | 26 |
| 9 | [Seek e Âmbito](#9-seek-e-âmbito) | 13 |
| 10 | [Filtro e AOF (Rushmore)](#10-filtro-e-aof-rushmore) | 11 |
| 11 | [SQL](#11-sql) | 17 |
| 12 | [Transações (TPS)](#12-transações-tps) | 8 |
| 13 | [Memo / Binário](#13-memo--binário) | 8 |
| 14 | [Manutenção de Tabelas](#14-manutenção-de-tabelas) | 10 |
| 15 | [Cifra](#15-cifra) | 10 |
| 16 | [Data Dictionary (DD)](#16-data-dictionary-dd) | 42 |
| 17 | [Avaliação de Expresões](#17-avaliação-de-expresões) | 5 |
| 18 | [Telemetria do Servidor (AdsMg*)](#18-telemetria-do-servidor-adsmg) | 17 |
| 19 | [Pesquisa de Texto Completo](#19-pesquisa-de-texto-completo) | 3 |
| 20 | [Miscelâneos](#20-miscelâneos) | 30 |
| 21 | [Callbacks e Cache](#21-callbacks-e-cache-stubs) | 11 |
| 22 | [Integridade RI e Alavancas](#22-integridade-ri-e-alavancas) | 7 |
| 23 | [Flush Diferido](#23-flush-diferido) | 2 |
| 24 | [Relações (Stubs)](#24-relações-stubs) | 3 |
| 25 | [Legacy / Pesquisa](#25-legacy--pesquisa) | 6 |
| | [Resumo](#resumo) | **357** |

---

## 1. Gestão de Conexões

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsConnect60` | ✅ | Abrir conexão (local, `tcp://`, `tls://`, `sqlite://`, `postgresql://`, `mariadb://`, `mssql://`, `odbc://`) |
| `AdsConnect` | ✅ | Conexão simplificada (sem user/pw/options) |
| `AdsDisconnect` | ✅ | Fechar conexão e libertar handles |
| `AdsGetConnectionType` | ✅ | Devolve `ADS_LOCAL_SERVER` ou `ADS_REMOTE_SERVER` |
| `AdsIsConnectionAlive` | ✅ | Verificação de heartbeat (ping) |
| `AdsResetConnection` | ⚠️ | No-op, devolve sucesso |
| `AdsFindConnection` | 🔴 | `AE_FUNCTION_NOT_AVAILABLE` — pesquisa por caminho de servidor |
| `AdsFindConnection25` | 🔴 | Sobrecarga versionada (compat X#) |
| `AdsTestLogin` | ⚠️ | Accept-and-ignore |
| `AdsConnect26` | ➡️ | Reenvia para `AdsConnect60` |
| `AdsDisableLocalConnections` | ⚠️ | No-op, devolve sucesso |

---

## 2. Operações de Tabela

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsOpenTable` | ✅ | Abrir ficheiro DBF/CDX/NTX/ADT |
| `AdsCloseTable` | ✅ | Fechar tabela e libertar recursos |
| `AdsCloseAllTables` | ✅ | Fechar todas as tabelas abertas |
| `AdsCreateTable` | ✅ | Criar nova DBF/ADT com definições de campo |
| `AdsRestructureTable` | ✅ | Alterar estrutura (adicionar/remover/renomear campos) |
| `AdsGetTableType` | ✅ | Devolve `ADS_CDX`, `ADS_NTX`, `ADS_ADT`, etc. |
| `AdsGetTableFilename` | ✅ | Devolve caminho completo do ficheiro |
| `AdsGetTableAlias` | ✅ | Devolve o alias da tabela |
| `AdsGetTableCharType` | ✅ | Devolve `ADS_ANSI` ou `ADS_OEM` |
| `AdsGetTableConType` | ✅ | Devolve tipo de conexão da tabela |
| `AdsGetTableConnection` | ✅ | Devolve handle de conexão da tabela |
| `AdsGetTableOpenOptions` | ✅ | Devolve flags de modo de abertura |
| `AdsCheckExistence` | ✅ | Verificar se um ficheiro existe em disco |
| `AdsDeleteFile` | ✅ | Eliminar ficheiro do directório de dados |
| `AdsGetNumOpenTables` | ✅ | Devolve quantidade de tabelas abertas |
| `AdsOpenTable90` | ➡️ | Reenvia para `AdsOpenTable` |
| `AdsCreateTable71` | ➡️ | Reenvia para `AdsCreateTable` |
| `AdsCreateTable90` | ➡️ | Reenvia para `AdsCreateTable` |
| `AdsRestructureTable90` | ➡️ | Reenvia para `AdsRestructureTable` |
| `AdsGetTableHandle25` | 🔴 | `AE_FUNCTION_NOT_AVAILABLE` — pesquisa por nome |

---

## 3. Navegação de Registos

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsGotoTop` | ✅ | Mover para o primeiro registo |
| `AdsGotoBottom` | ✅ | Mover para o último registo |
| `AdsGotoRecord` | ✅ | Saltar para um recno específico |
| `AdsSkip` | ✅ | Saltar ±N registos |
| `AdsAtEOF` | ✅ | Verificar se está no fim do ficheiro |
| `AdsAtBOF` | ✅ | Verificar se está no início do ficheiro |
| `AdsIsFound` | ✅ | Verificar se o último `Seek` coincidiu |
| `AdsContinue` | ✅ | Continuar uma varredura `LOCATE` |
| `AdsGetRecordNum` | ✅ | Devolve recno actual |
| `AdsGetRecordCount` | ✅ | Devolve quantidade total de registos |
| `AdsIsRecordVisible` | ✅ | Verificar se o registo passa o filtro/AOF |
| `AdsGetBookmark` | ✅ | Obter marcador de posição (handle) |
| `AdsGetBookmark60` | ✅ | Obter marcador como array de bytes |
| `AdsGotoBookmark60` | ✅ | Restaurar posição desde marcador de bytes |

---

## 4. Leitura de Campos (por tipo)

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsGetField` | ✅ | Ler campo como texto (qualquer tipo) |
| `AdsGetFieldW` | ✅ | Ler campo como texto UTF-16 |
| `AdsGetFieldRaw` | ✅ | Ler bytes crus do disco |
| `AdsGetFieldName` | ✅ | Obter nome de campo por ordinal |
| `AdsGetFieldType` | ✅ | Obter carácter de tipo (C/N/D/L/M/…) |
| `AdsGetFieldLength` | ✅ | Obter largura do campo em bytes |
| `AdsGetFieldDecimals` | ✅ | Obter casas decimais |
| `AdsGetNumFields` | ✅ | Obter quantidade de campos |
| `AdsGetString` | ✅ | Ler como cadeia |
| `AdsGetStringW` | ✅ | Ler como cadeia larga |
| `AdsGetLong` | ✅ | Ler como inteiro de 32 bits |
| `AdsGetLongLong` | ✅ | Ler como inteiro de 64 bits |
| `AdsGetDouble` | ✅ | Ler como double |
| `AdsGetLogical` | ✅ | Ler como booleano (`.T.`/`.F.`) |
| `AdsGetJulian` | ✅ | Ler como Número de Dia Juliano |
| `AdsGetDate` | ✅ | Ler como data formatada |
| `AdsGetMemoLength` | ✅ | Obter comprimento de dados memo |
| `AdsGetMemoDataType` | ✅ | Obter tipo de memo (texto/binário) |
| `AdsGetBinaryLength` | ✅ | Obter comprimento de dados binários |
| `AdsGetBinary` | ✅ | Ler dados binários |
| `AdsIsNull` | ✅ | Verificar se o campo é NULL |

---

## 5. Escrita de Campos

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsSetString` | ✅ | Escrever cadeia em campo |
| `AdsSetStringW` | ✅ | Escrever cadeia UTF-16 em campo |
| `AdsSetLogical` | ✅ | Escrever valor booleano |
| `AdsSetDouble` | ✅ | Escrever valor double |
| `AdsSetLongLong` | ✅ | Escrever inteiro de 64 bits |
| `AdsSetJulian` | ✅ | Escrever Número de Dia Juliano |
| `AdsSetFieldRaw` | ✅ | Escrever bytes crus em campo |
| `AdsSetField` | ✅ | Atribuidor genérico (nome ou ordinal) |
| `AdsSetEmpty` | ✅ | Definir campo vazio |
| `AdsSetNull` | ✅ | Definir campo NULL |
| `AdsSetShort` | ✅ | Escrever inteiro curto |
| `AdsSetMoney` | ✅ | Escrever valor MONEY (64 bits escalado) |
| `AdsSetTime` | ✅ | Escrever valor TIME |
| `AdsSetTimeStamp` | ✅ | Escrever valor TIMESTAMP |
| `AdsSetBinary` | ✅ | Escrever dados binários |
| `AdsFileToBinary` | ✅ | Importar ficheiro para campo binário/memo |
| `AdsBinaryToFile` | ✅ | Exportar campo binário/memo para ficheiro |

---

## 6. Operações de Registo

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsAppendRecord` | ✅ | Adicionar novo registo em branco |
| `AdsWriteRecord` | ✅ | Volcar registo actual para disco |
| `AdsDeleteRecord` | ✅ | Marcar registo como eliminado |
| `AdsRecallRecord` | ✅ | Restaurar (recuperar) registo |
| `AdsRecallAllRecords` | ⚠️ | No-op, devolve sucesso |
| `AdsIsRecordDeleted` | ✅ | Verificar se o registo está eliminado |
| `AdsIsRecordLocked` | ✅ | Verificar se o registo tem bloqueio |
| `AdsRefreshRecord` | ✅ | Re-ler registo actual do disco |
| `AdsGetRecordCRC` | ✅ | Calcular CRC-32 do registo actual |
| `AdsWriteAllRecords` | ⚠️ | Devolve `AE_SUCCESS` (no-op) |

---

## 7. Bloqueio

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsLockRecord` | ✅ | Adquirir bloqueio de intervalo de bytes |
| `AdsUnlockRecord` | ✅ | Libertar bloqueio de intervalo de bytes |
| `AdsLockTable` | ✅ | Adquirir bloqueio exclusivo de tabela |
| `AdsUnlockTable` | ✅ | Libertar bloqueio de tabela |
| `AdsGetAllLocks` | ✅ | Obter array de recnos bloqueados |
| `AdsGetNumLocks` | ✅ | Contagem de bloqueios mantidos |
| `AdsIsTableLocked` | ✅ | Verificar se a tabela tem bloqueio exclusivo |
| `AdsTestRecLocks` | ⚠️ | No-op, devolve sucesso |
| `AdsSetLockCycle` | ✅ | Definir ciclo de escalonamento de bloqueio |
| `AdsGetLockCycle` | ✅ | Obter ciclo de escalonamento de bloqueio |
| `AdsSetLockRetryCount` | ✅ | Definir quantidade de tentativas de bloqueio |
| `AdsGetLockRetryCount` | ✅ | Obter quantidade de tentativas de bloqueio |

---

## 8. Operações de Índice

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsOpenIndex` | ✅ | Abrir ficheiro de índice CDX/NTX existente |
| `AdsCloseIndex` | ✅ | Fechar um índice |
| `AdsCloseAllIndexes` | ✅ | Fechar todos os índices de uma tabela |
| `AdsCreateIndex61` | ✅ | Criar índice CDX/NTX (assinatura v6.1) |
| `AdsCreateIndex` | ✅ | Criar índice (assinatura legacy) |
| `AdsDeleteIndex` | ✅ | Eliminar etiqueta de índice |
| `AdsReindex` | ✅ | Reconstruir todos os índices vinculados |
| `AdsGetNumIndexes` | ✅ | Contagem de índices abertos |
| `AdsGetIndexHandle` | ✅ | Obter handle por nome de etiqueta |
| `AdsGetIndexHandleByOrder` | ✅ | Obter handle por posição ordinal |
| `AdsGetIndexExpr` | ✅ | Obter expressão-chave do índice |
| `AdsGetIndexName` | ✅ | Obter nome da etiqueta |
| `AdsGetIndexCondition` | ✅ | Obter condição FOR do índice |
| `AdsGetIndexFilename` | ✅ | Obter nome do ficheiro do índice |
| `AdsGetIndexOrderByHandle` | ✅ | Obter posição ordinal do handle |
| `AdsSetIndexOrder` | ✅ | Definir ordem activa por nome |
| `AdsSetIndexOrderByHandle` | ✅ | Definir ordem activa por handle |
| `AdsSetIndexDirection` | ✅ | Definir direcção (ascendente/descendente) |
| `AdsIsIndexCustom` | ✅ | Verificar se o índice é personalizado |
| `AdsIsIndexDescending` | ✅ | Verificar se o índice é descendente |
| `AdsIsIndexUnique` | ✅ | Verificar se o índice é único |
| `AdsAddCustomKey` | ✅ | Adicionar chave personalizada |
| `AdsDeleteCustomKey` | ✅ | Eliminar chave personalizada |
| `AdsExtractKey` | ✅ | Extrair chave do registo actual |
| `AdsCreateFTSIndex` | ✅ | Criar índice de pesquisa de texto completo |
| `AdsCreateIndex90` | ➡️ | Reenvia para `AdsCreateIndex61` |
| `AdsReindex61` | ➡️ | Reenvia para `AdsReindex` |

---

## 9. Seek e Âmbito

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsSeek` | ✅ | Pesquisar valor de chave (exacto ou suave) |
| `AdsSeekLast` | ✅ | Pesquisar última chave coincidente |
| `AdsSkipUnique` | ✅ | Saltar para próxima chave única |
| `AdsSetScope` | ✅ | Definir âmbito de intervalo de chaves |
| `AdsClearScope` | ✅ | Limpar um âmbito |
| `AdsGetScope` | ✅ | Ler âmbito actual |
| `AdsClearAllScopes` | ⚠️ | No-op, devolve sucesso |
| `AdsGetKeyNum` | ✅ | Obter posição relativa de chave (0.0–1.0) |
| `AdsGetKeyCount` | ✅ | Contar chaves em ordem actual |
| `AdsGetKeyLength` | ✅ | Obter largura de chave em bytes |
| `AdsGetKeyType` | ✅ | Obter tipo de dados de chave |
| `AdsGetRelKeyPos` | ✅ | Obter posição relativa (fração) |
| `AdsSetRelKeyPos` | ✅ | Posicionar por fração relativa |

---

## 10. Filtro e AOF (Rushmore)

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsSetAOF` | ✅ | Instalar filtro optimizado estilo Rushmore |
| `AdsGetAOFOptLevel` | ✅ | Obter nível de optimização (FULL/PART/NONE) |
| `AdsClearAOF` | ✅ | Eliminar AOF instalado |
| `AdsRefreshAOF` | ⚠️ | No-op, devolve sucesso |
| `AdsEvalAOF` | ✅ | Avaliar expressão AOF, reportar nível |
| `AdsGetAOF` | ✅ | Obter cadeia fonte do AOF actual |
| `AdsCustomizeAOF` | ⚠️ | Stub |
| `AdsIsRecordInAOF` | ✅ | Verificar se um registo passa o AOF |
| `AdsSetFilter` | ⚠️ | No-op (filtro sem índice) |
| `AdsGetFilter` | ✅ | Obter expressão de filtro actual |
| `AdsClearFilter` | ⚠️ | No-op, devolve sucesso |
| `AdsFilterOption` | ✅ | Obter opções de optimização de filtro |

---

## 11. SQL

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsCreateSQLStatement` | ✅ | Alocar handle de instrução SQL |
| `AdsCloseSQLStatement` | ✅ | Libertar handle de instrução SQL |
| `AdsPrepareSQL` | ✅ | Preparar instrução SQL |
| `AdsGetNumParams` | ✅ | Obter quantidade de parâmetros |
| `AdsExecuteSQL` | ✅ | Executar SQL preparado, devolver cursor |
| `AdsExecuteSQLDirect` | ✅ | Executar SQL raw, devolver cursor |
| `AdsVerifySQL` | ✅ | Validar sintaxe SQL sem executar |
| `AdsClearSQLParams` | ⚠️ | No-op, devolve sucesso |
| `AdsClearSQLAbortFunc` | ⚠️ | No-op, devolve sucesso |
| `AdsStmtSetTableLockType` | ✅ | Definir tipo de bloqueio |
| `AdsStmtSetTablePassword` | ✅ | Definir palavra-passe por tabela |
| `AdsStmtSetTableReadOnly` | ✅ | Definir modo só leitura |
| `AdsStmtSetTableType` | ✅ | Definir tipo de tabela resultado |
| `AdsStmtSetTableCharType` | ✅ | Definir tipo de carácter ANSI/OEM |
| `AdsStmtSetTableCollation` | ✅ | Definir ordenação |
| `AdsStmtSetTableRights` | ✅ | Definir direitos de acesso |
| `AdsStmtDisableEncryption` | ⚠️ | No-op, devolve sucesso |
| `AdsStmtClearTablePasswords` | ⚠️ | No-op, devolve sucesso |

---

## 12. Transações (TPS)

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsBeginTransaction` | ✅ | Iniciar transação |
| `AdsCommitTransaction` | ✅ | Confirmar transação actual |
| `AdsRollbackTransaction` | ✅ | Reverter transação actual |
| `AdsInTransaction` | ✅ | Verificar se está dentro de uma transação |
| `AdsCreateSavepoint` | ✅ | Criar savepoint com nome |
| `AdsReleaseSavepoint` | ✅ | Libertar savepoint |
| `AdsRollbackTransaction80` | ✅ | Reverter para savepoint (assinatura ACE 8.0) |
| `AdsFailedTransactionRecovery` | ✅ | Recuperar de transação falhada |

---

## 13. Memo / Binário

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsGetMemoLength` | ✅ | Obter comprimento de dados memo |
| `AdsGetMemoDataType` | ✅ | Obter tipo de memo (texto/binário) |
| `AdsGetBinaryLength` | ✅ | Obter comprimento de dados binários |
| `AdsGetBinary` | ✅ | Ler dados binários |
| `AdsSetBinary` | ✅ | Escrever dados binários |
| `AdsBinaryToFile` | ✅ | Exportar memo/binário para ficheiro |
| `AdsFileToBinary` | ✅ | Importar ficheiro para campo memo/binário |
| `AdsGetMemoBlockSize` | ✅ | Obter tamanho de bloco memo |

---

## 14. Manutenção de Tabelas

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsPackTable` | ✅ | Compactar tabela (eliminar registos eliminados) |
| `AdsZapTable` | ✅ | Esvaziar tabela completamente |
| `AdsPackTable_DEFERRED` | ⚠️ | Pack diferido (stub) |
| `AdsZapTable_DEFERRED` | ⚠️ | Zap diferido (stub) |
| `AdsCopyTable` | ✅ | Copiar tabela com filtro opcional |
| `AdsCopyTableContents` | ✅ | Copiar conteúdos filtrados para outra tabela |
| `AdsCopyTableContent` | ✅ | Copiar todos os conteúdos (alias) |
| `AdsConvertTable` | ✅ | Converter entre tipos (DBF↔ADT) |
| `AdsCopyTableStructure` | ✅ | Copiar só estrutura (sem dados) |
| `AdsCloneTable` | ✅ | Clonar handle de tabela (dados partilhados) |

---

## 15. Cifra

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsEnableEncryption` | ✅ | Activar cifra na conexão |
| `AdsDisableEncryption` | ✅ | Desactivar cifra |
| `AdsIsEncryptionEnabled` | ✅ | Verificar se a cifra está activa |
| `AdsSetEncryptionPassword` | ✅ | Definir palavra-passe de cifra |
| `AdsIsTableEncrypted` | ✅ | Verificar se a tabela está cifrada |
| `AdsIsRecordEncrypted` | ✅ | Verificar se o registo está cifrado |
| `AdsEncryptTable` | ✅ | Cifrar tabela completa |
| `AdsDecryptTable` | ✅ | Decifrar tabela completa |
| `AdsEncryptRecord` | ✅ | Cifrar registo actual |
| `AdsDecryptRecord` | ✅ | Decifrar registo actual |

---

## 16. Data Dictionary (DD)

### Ciclo de Vida do Dicionário

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsDDCreate` | ✅ | Criar novo dicionário `.add` |
| `AdsDDAddTable` | ✅ | Registar alias de tabela |
| `AdsDDRemoveTable` | ✅ | Eliminar alias de tabela |
| `AdsDDAddTable90` | ➡️ | Sobrecarga versionada para X# |

### Propriedades de Tabela

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsDDGetTableProperty` | ✅ | Ler propriedade de tabela (200–216) |
| `AdsDDSetTableProperty` | ✅ | Escrever propriedade de tabela |
| `AdsDDGetFieldProperty` | ✅ | Ler propriedade de campo (301–309) |
| `AdsDDSetFieldProperty` | ✅ | Escrever propriedade de campo |
| `AdsDDGetIndexProperty` | ✅ | Ler propriedade de índice (401–408) |
| `AdsDDSetIndexProperty` | ⚠️ | Stub |

### Propriedades de Base de Dados

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsDDGetDatabaseProperty` | ✅ | Ler propriedade de BD (1–23) |
| `AdsDDSetDatabaseProperty` | ✅ | Escrever propriedade de BD |

### Utilizadores e Grupos

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsDDCreateUser` | ✅ | Criar utilizador |
| `AdsDDDeleteUser` | ✅ | Eliminar utilizador |
| `AdsDDAddUserToGroup` | ✅ | Adicionar utilizador a grupo |
| `AdsDDRemoveUserFromGroup` | ✅ | Eliminar utilizador de grupo |
| `AdsDDGetUserProperty` | ✅ | Ler propriedade de utilizador (1101–1103) |
| `AdsDDSetUserProperty` | ✅ | Escrever propriedade de utilizador |

### Permissões

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsDDGetPermissions` | ✅ | Obter permissões efectivas |
| `AdsDDGrantPermission` | ✅ | Conceder permissão |
| `AdsDDRevokePermission` | ✅ | Revogar permissão |
| `AdsDDSetUserTableRights` | ✅ | Definir direitos por tabela |
| `AdsDDGetUserTableRights` | ✅ | Obter direitos por tabela |

### Gestão de Ficheiros de Índice

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsDDAddIndexFile` | ✅ | Vincular ficheiro de índice a tabela |
| `AdsDDRemoveIndexFile` | ✅ | Desvincular ficheiro de índice |

### Vistas

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsDDCreateView` | ✅ | Criar vista SQL com nome |
| `AdsDDDropView` | ✅ | Eliminar vista |
| `AdsDDAddView` | ✅ | Alias para `AdsDDCreateView` |
| `AdsDDRemoveView` | ✅ | Alias para `AdsDDDropView` |
| `AdsDDGetViewProperty` | ✅ | Ler propriedade de vista (701–702) |
| `AdsDDSetViewProperty` | ✅ | Escrever propriedade de vista |

### Procedimentos Armazenados e Funções

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsDDCreateProcedure` | ✅ | Criar procedimento armazenado |
| `AdsDDDropProcedure` | ✅ | Eliminar procedimento armazenado |
| `AdsDDAddProcedure` | ✅ | Alias para `AdsDDCreateProcedure` |
| `AdsDDRemoveProcedure` | ✅ | Alias para `AdsDDDropProcedure` |
| `AdsDDGetProcProperty` | ✅ | Ler propriedade (601–605) |
| `AdsDDSetProcProperty` | ✅ | Escrever propriedade |
| `AdsDDGetProcedureProperty` | ✅ | Alias para `AdsDDGetProcProperty` |
| `AdsDDSetProcedureProperty` | ✅ | Alias para `AdsDDSetProcProperty` |
| `AdsDDCreateFunction` | ✅ | Registar UDF |
| `AdsDDDropFunction` | ✅ | Eliminar UDF |
| `AdsDDGetFunctionProperty` | ✅ | Ler propriedade de UDF |
| `AdsDDSetFunctionProperty` | ✅ | Escrever propriedade de UDF |

### Triggers

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsDDCreateTrigger` | ✅ | Criar trigger (BEFORE/AFTER/INSTEAD OF) |
| `AdsDDDropTrigger` | ✅ | Eliminar trigger |
| `AdsDDRemoveTrigger` | ✅ | Alias para `AdsDDDropTrigger` |
| `AdsDDGetTriggerProperty` | ✅ | Ler propriedade (501–507) |
| `AdsDDSetTriggerProperty` | ✅ | Escrever propriedade |

### Integridade Referencial

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsDDCreateRefIntegrity` | ✅ | Criar regra RI (RESTRICT/CASCADE/SETNULL) |
| `AdsDDRemoveRefIntegrity` | ✅ | Eliminar regra RI |
| `AdsDDCreateRefIntegrity62` | ➡️ | Sobrecarga versionada |
| `AdsDDGetRefIntegrityProperty` | ✅ | Ler propriedade RI (401–407) |
| `AdsDDSetRefIntegrityProperty` | ✅ | Escrever propriedade RI |

### Ligações

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsDDCreateLink` | ✅ | Criar ligação entre dicionários |
| `AdsDDDropLink` | ✅ | Eliminar ligação |
| `AdsDDModifyLink` | ✅ | Actualizar credenciais/caminho da ligação |

### Enumeração de Objectos

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsDDFindFirstObject` | ✅ | Iniciar iteração por tipo |
| `AdsDDFindNextObject` | ✅ | Continuar iteração |
| `AdsDDFindClose` | ✅ | Fechar handle de iteração |

---

## 17. Avaliação de Expresões

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsEvalLogicalExpr` | ✅ | Avaliar expressão como booleano |
| `AdsEvalNumericExpr` | ✅ | Avaliar expressão como double |
| `AdsEvalStringExpr` | ✅ | Avaliar expressão como cadeia |
| `AdsEvalTestExpr` | ⚠️ | Stub |
| `AdsIsExprValid` | ✅ | Validar sintaxe de expressão |

---

## 18. Telemetria do Servidor (AdsMg*)

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsMgConnect` | ✅ | Abrir canal de telemetria |
| `AdsMgDisconnect` | ✅ | Fechar canal de telemetria |
| `AdsMgGetActivityInfo` | ✅ | Obter instantâneo de actividade |
| `AdsMgGetCommStats` | ✅ | Obter estatísticas de comunicação |
| `AdsMgGetConfigInfo` | ✅ | Obter configuração do servidor |
| `AdsMgGetInstallInfo` | ✅ | Obter info de instalação |
| `AdsMgGetLockOwner` | ✅ | Obter proprietário de um bloqueio |
| `AdsMgGetLocks` | ✅ | Listar todos os bloqueios |
| `AdsMgGetOpenIndexes` | ✅ | Listar índices abertos |
| `AdsMgGetOpenTables` | ✅ | Listar tabelas abertas |
| `AdsMgGetOpenTables2` | ✅ | Lista estendida de tabelas |
| `AdsMgGetServerType` | ✅ | Obter tipo de servidor |
| `AdsMgGetUserNames` | ✅ | Listar utilizadores ligados |
| `AdsMgGetWorkerThreadActivity` | ✅ | Obter info de threads |
| `AdsMgKillUser` | ✅ | Desligar utilizador |
| `AdsMgResetCommStats` | ✅ | Repor contadores de comunicação |
| `AdsMgDumpInternalTables` | ✅ | Volcar metadata de tabelas internas |

---

## 19. Pesquisa de Texto Completo

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsCreateFTSIndex` | ✅ | Criar índice FTS num campo |
| `AdsFTSSearch` | ✅ | Pesquisar em índice FTS com consulta |
| `AdsGetFTSIndexes` | ⚠️ | Stub |

---

## 20. Miscelâneos

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsGetVersion` | ✅ | Obter versão ACE (major, minor, letter, desc) |
| `AdsGetLastError` | ✅ | Obter último código de erro e mensagem |
| `AdsGetErrorString` | ✅ | Obter cadeia legível do erro |
| `AdsGetServerName` | ✅ | Obter nome do servidor |
| `AdsGetServerTime` | ✅ | Obter timestamp do servidor |
| `AdsGetDateFormat` | ✅ | Obter formato de data do processo |
| `AdsSetDateFormat` | ✅ | Definir formato de data do processo |
| `AdsGetLastTableUpdate` | ✅ | Obter data de última actualização |
| `AdsGetLastAutoinc` | ✅ | Obter último valor autoincrement |
| `AdsShowDeleted` | ✅ | Alternar visibilidade `SET DELETED` |
| `AdsGetDeleted` | ✅ | Consultar estado `SET DELETED` |
| `AdsSetCollation` | ✅ | Definir ordenação |
| `AdsConvertOemToAnsi` | ✅ | Conversão OEM→ANSI |
| `AdsConvertAnsiToOem` | ✅ | Conversão ANSI→OEM |
| `AdsGetEpoch` | ✅ | Obter pivô de ano de 2 algarismos |
| `AdsSetEpoch` | ⚠️ | No-op, devolve sucesso |
| `AdsGetExact` | ✅ | Obter estado `SET EXACT` |
| `AdsSetExact` | ⚠️ | No-op, devolve sucesso |
| `AdsGetDefault` | ✅ | Obter unidade/caminho por omissão |
| `AdsSetDefault` | ⚠️ | No-op, devolve sucesso |
| `AdsGetSearchPath` | ✅ | Obter caminho de pesquisa |
| `AdsSetSearchPath` | ⚠️ | No-op, devolve sucesso |
| `AdsGetNumActiveLinks` | ✅ | Contar ligações activas |
| `AdsGetNumOpenTables` | ✅ | Contar tabelas abertas |
| `AdsApplicationExit` | ⚠️ | No-op, devolve sucesso |
| `AdsThreadExit` | ⚠️ | No-op, devolve sucesso |
| `AdsInitRawKey` | ⚠️ | No-op, devolve sucesso |
| `AdsGetRecord` | ⚠️ | Stub |
| `AdsSetRecord` | 🔴 | `AE_FUNCTION_NOT_AVAILABLE` |
| `AdsGetMilliseconds` | ⚠️ | Stub |
| `AdsSetMilliseconds` | 🔴 | `AE_FUNCTION_NOT_AVAILABLE` |
| `AdsData` | ⚠️ | No-op, devolve sucesso |

---

## 21. Callbacks e Cache (Stubs)

Estas funções são aceites por compatibilidade ABI mas não têm
efeito em OpenADS:

| Função | Devolve |
|--------|---------|
| `AdsRegisterCallbackFunction` | `AE_SUCCESS` |
| `AdsRegisterProgressCallback` | `AE_SUCCESS` |
| `AdsClearCallbackFunction` | `AE_SUCCESS` |
| `AdsClearProgressCallback` | `AE_SUCCESS` |
| `AdsCacheOpenCursors` | `AE_SUCCESS` |
| `AdsCacheOpenTables` | `AE_SUCCESS` |
| `AdsCacheRecords` | `AE_SUCCESS` |
| `AdsCloseCachedTables` | `AE_SUCCESS` |
| `AdsSetDecimals` | `AE_SUCCESS` |
| `AdsShowError` | `AE_SUCCESS` |
| `AdsSetServerType` | `AE_SUCCESS` |

---

## 22. Integridade RI e Alavancas

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsEnableRI` | ⚠️ | No-op, devolve sucesso |
| `AdsDisableRI` | ⚠️ | No-op, devolve sucesso |
| `AdsEnableUniqueEnforcement` | ⚠️ | No-op |
| `AdsDisableUniqueEnforcement` | ⚠️ | No-op |
| `AdsEnableAutoIncEnforcement` | ⚠️ | No-op |
| `AdsDisableAutoIncEnforcement` | ⚠️ | No-op |
| `AdsCancelUpdate` | ⚠️ | No-op |

---

## 23. Flush Diferido

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsSetDeferredFlush` | ✅ | Alternar flush diferido (528× inserção em massa) |
| `AdsFlushFileBuffers` | ✅ | Forçar fsync em ficheiros de tabela + índice |

---

## 24. Relações (Stubs)

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsSetRelation` | 🔴 | `AE_FUNCTION_NOT_AVAILABLE` |
| `AdsSetScopedRelation` | ⚠️ | No-op, devolve sucesso |
| `AdsClearRelation` | ⚠️ | No-op, devolve sucesso |

---

## 25. Legacy / Pesquisa

| Função | Estado | Descrição |
|--------|--------|-----------|
| `AdsFindFirstTable` | ✅ | Encontrar primeira tabela que coincida |
| `AdsFindNextTable` | ✅ | Encontrar tabela seguinte |
| `AdsFindClose` | ✅ | Fechar handle de pesquisa |
| `AdsFindFirstTable62` | ➡️ | Sobrecarga versionada |
| `AdsFindNextTable62` | ➡️ | Sobrecarga versionada |
| `AdsIsServerLoaded` | ✅ | Verificar se o servidor está local |

---

## Resumo

| Categoria | Total | ✅ | ⚠️ | 🔴 |
|-----------|------:|----:|----:|----:|
| Conexões | 11 | 7 | 2 | 2 |
| Tabela | 20 | 15 | 2 | 3 |
| Navegação | 14 | 14 | 0 | 0 |
| Leitura | 21 | 21 | 0 | 0 |
| Escrita | 17 | 17 | 0 | 0 |
| Registo | 10 | 8 | 2 | 0 |
| Bloqueio | 12 | 10 | 2 | 0 |
| Índice | 27 | 25 | 0 | 2 |
| Seek | 13 | 12 | 1 | 0 |
| Filtro/AOF | 12 | 8 | 4 | 0 |
| SQL | 18 | 12 | 6 | 0 |
| Transação | 8 | 8 | 0 | 0 |
| Memo/Binário | 8 | 8 | 0 | 0 |
| Manutenção | 10 | 8 | 2 | 0 |
| Cifra | 10 | 10 | 0 | 0 |
| Data Dictionary | 42 | 40 | 2 | 0 |
| Expresões | 5 | 4 | 1 | 0 |
| Telemetria | 17 | 17 | 0 | 0 |
| Texto Completo | 3 | 2 | 1 | 0 |
| Miscelâneos | 31 | 18 | 11 | 2 |
| Callbacks/Cache | 11 | 0 | 11 | 0 |
| Alavancas RI | 7 | 0 | 7 | 0 |
| Flush Diferido | 2 | 2 | 0 | 0 |
| Relações | 3 | 0 | 2 | 1 |
| Legacy | 6 | 4 | 0 | 2 |
| **TOTAL** | **357** | **~250** | **~56** | **~12** |

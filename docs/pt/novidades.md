---
title: Novidades
layout: default
parent: Início (PT)
nav_order: 0
permalink: /pt/novidades/
---

# Novidades (v1.0.0-rc29 → v1.8.1)

Esta página resume as mudanças mais notáveis desde a versão
v1.0.0-rc29. Para o histórico completo de commits, consulte o
[CHANGELOG](https://github.com/FiveTechSoft/OpenADS/blob/main/CHANGELOG.md).

---

## Destaques v1.8.1

### ABI — padding de chaves `OrdScope` em campos carácter

Harbour envia o âmbito com comprimento aparado, mas as chaves CDX são
preenchidas com espaços. `setScopeTop` / `setScopeBottom` num campo de
ordem de trabalho podia deixar `GotoTop` em EOF em **local e remoto**.
`AdsSetScope` agora preenche até `key_length`. Teste: `QA-D`.

---

## Destaques v1.8.0

### CDX — colação OEM NTXPL852 / PL852

`AdsSetCollation` aceita `NTXPL852` e `PL852` para ordenação de índices
CDX em CP-852 polonês. Ł (0x9D) fica entre L e M em build, seek, insert
e reindex.

### CDX — `REINDEX` em bloco

`Table::reindex()` em tags CDX usa `build_bulk()` (mesmo caminho rápido
que `CREATE INDEX`).

### Testes

19 novos testes unitários (motor, driver CDX e ABI).

---

## Destaques v1.7.0

### REMOTO — `AdsSetScope` / `OrdScope`

Os limites superior/inferior de `OrdScope` eram enviados ao servidor, mas
`GotoTop`/`Skip` ignoravam o escopo. Corrigido em `session.cpp` e
`remote_index_nav.cpp`. Requer `openace64.dll` e `openads_serverd`
atualizados.

### Docs — aviso de rollback CDX

[Migrando do ADS](migrando-do-ads/) documenta que CDX gravados pelo OpenADS
usam o header `RCHB` do Harbour e não são legíveis pelo SAP ACE (erro 7017).

---

## Destaques v1.6.5

### REMOTO — `OrdKeyCount()` e `AdsGetDate()`

- **`OrdKeyCount()` retornava 0** em aliases remotos — grades xBrowse vazios.
  Novo opcode wire `GetKeyCount`.
- **`AdsGetDate()` crashava** em campos `ADS_DATE` remotos quando o rddads
  passava handle `RemoteIndex`.

---

## Destaques v1.6.3

### REMOTO / FWH — xBrowse em `tcp://`

Correções para Harbour `ADSCDX` + FWH `xBrowse` / `TDataBase` com tags
CDX de produção (`OrdSetFocus`, ex. `CUSTNAME`):

- **Scrollbar (`AdsKeyNo`)** — posição lógica na ordem (`1..n`), não
  `RecNo` físico; `AdsGetRelKeyPos` / `AdsSetRelKeyPos` em índice remoto.
- **`DbGoto` após pintar** — o servidor sincroniza o cursor ABI do
  índice em `GotoRecord`; as linhas param de mudar a cada `Refresh`.
- **Subir no topo** — `AdsAtBOF` após `Skip(-1)` na chave #1; evita
  repetir a primeira linha.

Requer `openace64.dll` atualizado e `openads_serverd` com sync em
`GotoRecord`. Testes em `abi_remote_index_nav_test`.

---

## Destaques v1.6.2

### Nomes de tags CDX SAP e `FieldGet` na linha nova

Nomes corretos em CDX composto (`CUSTNAME`); `AdsGetField` em branco
em BOF/EOF para `td_blankrow()` do FWH.

---

## Destaques v1.5.1

### Segurança e endurecimento remoto

Path jail no `Connect` remoto, unlock aninhado coerente no `LockMgr`,
verificação TLS por defeito, e escritas remotas de memo/Unicode/data/raw.

### Harbour smoke CI

Job Windows no GitHub Actions + scripts `run_harbour_smoke.ps1` /
`bootstrap_harbour_ci.ps1`.

### ABI remoto (Fase 2)

`AdsSetRelation`, `AdsSetRecord` / `AdsGetRecord`, `AdsCustomizeAOF` em
`tcp://`. `AdsAggregate` / `AdsFetchWhere` local em tabelas DBF.

### Plus — write navegacional SQLite e MSSQL

Backends SQLite e MS SQL Server nativo (TDS) com
`AdsAppendRecord` / `AdsSetString` / `AdsWriteRecord` /
`AdsDeleteRecord` — paridade com MariaDB, PostgreSQL, Firebird e ODBC.
Padding de `ADS_STRING` no read MSSQL.

### Motor

Fixtures ADT/ADI no repositório; cabeçalho VFP `0x32` (autoinc + nullable).

---

## Novas Funcionalidades

### Driver de Tabelas com SQLite

Um driver alternativo de tabelas suportado por SQLite está
disponível por trás da flag de CMake `OPENADS_WITH_SQLITE`. Quando
habilitado, o motor pode abrir e manipular tabelas através de um
backend SQLite, fornecendo uma camada de armazenamento alternativa.
Novos arquivos fonte:

- `src/sql_backend/sqlite_backend.cpp`
- `src/sql_backend/sqlite_connection.cpp`
- `src/sql_backend/sqlite_table.h`
- `src/sql_backend/sqlite_index.h`

### Backends SQL — PostgreSQL / MariaDB / ODBC (OpenADS Plus)

OpenADS agora pode abrir tabelas em **PostgreSQL**, **MariaDB /
MySQL** e qualquer motor acessível por **ODBC** por trás da ABI
ACE, selecionado pela URI de conexão (`postgresql://` /
`mariadb://` / `odbc://`), exatamente como o backend SQLite. Do
ponto de vista da aplicação, a tabela se comporta como qualquer
outra área de trabalho — navegação, leitura de campos e SEEK por
coluna funcionam.

Esses quatro backends SQL ficam por trás de um único **registro
plugável de backend-ops**: cada um registra uma struct
`BackendTableOps` (17 ponteiros de função que espelham as ops de
tabela), de modo que as ~17 funções ABI de navegação / campos
ficam agnósticas ao backend em vez de multiplicar um bloco `if`
por backend. Adicionar outro backend (ex: Firebird, ou MSSQL /
Oracle via ODBC) é uma struct de ops mais uma linha de registro.
Os caminhos DBF / ADT local nativo e o remoto `tcp://` ficam
inalterados. Os identificadores são validados para ASCII seguro e
os valores de SEEK usam parâmetros preparados (sem concatenação
de strings). Ver `docs/OPENADS_PLUS.md`.

### Patches de Validação ADT (F1–F7, R1–R3)

A validação de tabelas ADT agora inclui um conjunto completo de
patches estruturais (F1–F7) e verificações a nível de registro
(R1–R3), reforçando as garantias de integridade para arquivos
`.adt` produzidos pelo SAP Advantage.

### Criação, Leitura, Escrita e Busca em Índice ADT/ADI Nativo

OpenADS agora pode operar de ponta a ponta com arquivos nativos
`.adt` / `.adi` / `.adm`:

- **Criar** — `AdsCreateTable(ADS_ADT)` escreve um cabeçalho de
  tabela válido, descritores de campo e um armazenamento memo
  `.adm` opcional.
- **Escrever** — `AdsAppendRecord` / `AdsWriteRecord` persistem
  linhas e payloads de memo.
- **Ler** — Reabrir, obter campos, contagem de registros, ida e
  volta de memo.
- **Índice** — `AdsCreateIndex61` constrói bolsas `.adi`
  (primeiro tag via `AdiIndex::create`, tags adicionais via
  `add_tag`).
- **Buscar** — `AdsSeek` em chaves ADI de caractere e numéricas.
- **AUTOINC** — contador semeadado a partir de linhas existentes
  ao abrir; bytes 139–143 do descritor permanecem zero em disco.
- **Layout de memo ADM** — blocos de 8 bytes com um prefixo de
  metadados de 1024 bytes.

### Caminho de Escrita ADI

O driver de índices ADI agora suporta operações de escritura —
`insert`, `erase` e `flush` — incluindo a decodificação de recno
de folha densa e busca de chaves de caractere. Isso completa o
ciclo de leitura-escrita para índices ADI.

### Despacho de Triggers (BEFORE / INSTEAD_OF / AFTER)

Os triggers agora são executados com o despacho de temporização
adequado:

- **BEFORE** — executado antes da instrução DML.
- **INSTEAD_OF** — substitui a DML em views.
- **AFTER** — executado após a execução bem-sucedida.

Suportam ordenação por prioridade, tabela `__error` para falhas,
procedimentos armazenados `sp_DisableTriggers` /
`sp_EnableTriggers` e chaves compostas de triggers em
`system.triggers`.

### Interface de Gestão DA-Web

O substituto do Data Architect baseado em navegador (**DA-Web**)
recebeu trabalho extenso:

- **Edição em linha de células** com rastreamento de alterações
  visuais.
- **Gestão de índices** — salvar e excluir índices via API.
- **CRUD de Triggers** — adicionar, excluir e editar triggers com
  validação em linha.
- **Barra de filtros AOF (Rushmore)** no explorador de tabelas.
- **Destaque de sintaxe SQL ADS** com cores semelhantes ao
  HeidiSQL.
- **Visualizador de código de procedimentos armazenados /
  funções** com parâmetros e Save-to-DD.
- **Explorador de tags de índice**, etiquetas de tipos de campo e
  scripts SQL.
- **Menu de conexão** — Novo DD, Abrir DD, Tabelas livres.
- **Abas de Permissões Efetivas** e **Membros** em painéis de
  usuário/grupo.
- **Dropdowns de tags RI** preenchidos a partir de arquivos `.add`
  binários.

### openmonitor — TUI e Painel Web

Uma nova ferramenta `openmonitor` fornece tanto uma interface de
terminal (TUI) quanto um painel web para monitorar e administrar
`openads_serverd`.

### OpenADS Studio — Responsive (telefone / tablet)

O console web Studio agora se adapta a telas pequenas — pode ser
usado a partir de um telefone ou tablet, não apenas de um
navegador de desktop. Abaixo de ~768 px a lista de tabelas se
torna um **drawer** deslizante (☰ no header, fundo atenuado,
fechamento automático ao selecionar); a barra de abas faz scroll
horizontal e formulários / modais se reorganizam em uma coluna
 com alvos táteis maiores. Um bug antigo do tema escuro — as
variáveis CSS `--panel` / `--panel-2` / `--border` eram
autorreferenciais, então painéis e bordas eram renderizados
transparentes — é corrigido como parte deste trabalho.

### Desempenho de Varredura Remota (prefetch sequencial)

Uma varredura para a frente sobre a wire `tcp://` custava mais ou
menos um round-trip TCP por registro (`Skip` + `AtEOF` +
`IsFound`). Um caminho de prefetch sequencial (negociado via flag
de capacidade Connect) agora acopla um bloco de lookahead aos acks
de `Skip` para a frente; o cliente os serve localmente e dobra a
contagem consumida no próximo passo da wire, de modo que o cursor
do servidor nunca dessincroniza. `AdsAtEOF` / `AdsAtBOF` são
respondidos a partir da linha atual cacheada e `AdsIsFound` a
partir de uma flag `Found()` cacheada. Resultado: uma varredura
de 50k registros em loopback é **~3.9× mais rápida** (só NAV) /
**~3.3×** (leitura de 3 campos), com round-trips de `IsFound`
indo a zero. A mudança é aditiva e retrocompatível — clientes
antigos (sem capacidade anunciada) mantêm o comportamento da
wire idêntico.

### Extensão Nativa PHP (`php_openads`)

Uma extensão nativa Zend PHP (`php_openads.dll`) está agora
disponível para PHP 8.x, fornecendo CRUD completo do DD (35
novos métodos `AdsDictionary`), decodificação de campos
date/timestamp e cache de nomes de campo por instrução.

### Melhorias na Importação de Dicionário de Dados SAP

- `import_dd` agora copia arquivos de memo `.am` e decodifica
  corpos de funções criptografados.
- Importação de membresia de grupos (DB:Admin, DB:Backup,
  DB:Debug) a partir de arquivos `.add` binários.
- Temporização de triggers capturada a partir de
  `system.triggers`.
- `grant_permission` e código de erro
  `AE_SAP_PERMS_NEED_IMPORT` para migração de permissões.

### Recuperação de Falhas WAL

A recuperação de falhas WAL (Write-Ahead Log) agora lida com
registros `APPEND`, completando o modelo de recuperação
ARIES-lite.

### Expansão do SQL do DD

- `CREATE DATABASE`, `GRANT` / `REVOKE`.
- Procedimentos armazenados `sp_*`.
- Tabelas virtuais `system.*` (`system.iota`, `system.columns`).
- `AdsDDGet/SetFieldProperty`, triggers, procedimentos
  armazenados, views e propriedades de índices.
- Controle de acesso por tabela com níveis de permissão de
  usuário/grupo.

### Agregação Server-Side (Tier-3)

`AdsAggregate` agora suporta `COUNT`, `SUM`, `AVG`, `MIN` e
`MAX` com push-down para backends SQL (SQLite, PostgreSQL,
MariaDB, ODBC). A spec de agregação é validada antes da execução,
e os resultados são servidos através de um result set baseado em
handles (`AdsAggregateCount` / `AdsAggregateValue` /
`AdsAggregateClose`).

### FetchWhere V2

`AdsFetchWhere` agora serve varreduras para a frente a partir de
um result set cacheado — sem round-trip por correspondência. O
cliente recebe linhas em lote e as percorre localmente, com recno
por linha opcional (flag `WANT_RECNO`). As varreduras massivas de
`SET FILTER` sem AOF são roteadas através de `AdsFetchWhere` com
ganhos significativos de desempenho.

### Driver ODBC (slice 1–3)

Um driver ODBC completo (`openads_odbc.dll`) está agora
disponível:

- **Round-trip SELECT** com cursores scrolláveis
  (`SQLFetchScroll`).
- **Acesso tipado a colunas** — `SQLDescribeCol` /
  `SQLColAttribute` / `SQLGetData` despacham através do vtable de
  ops do backend.
- **Binding posicional de parâmetros** via `SQLBindParameter`.
- **Funções de catálogo** — `SQLPrimaryKeys` /
  `system.primarykeys`.
- **Emulação de app-lock** — `rLock()`/`fLock()` via SQL Server
  `sp_getapplock`, locks advisory do PostgreSQL, locks nomeados do
  MariaDB e tabela `OPENADS$LOCKS` do Firebird.

### Caminho de Escrita Nativo (PostgreSQL / MariaDB / Firebird)

`AdsAppendRecord` / `AdsSetField` / `AdsWriteRecord` /
`AdsDeleteRecord` agora funcionam de ponta a ponta em backends
PostgreSQL, MariaDB e Firebird — sem passsthrough ODBC.

### Expansão do Push-Down SQL

As expressões de `SET FILTER` e AOF agora são empurradas para
SQLite e PostgreSQL como cláusulas `WHERE` quando a árvore de
expressões está dentro do subconjunto otimizável
(`try_emit_sql_where`). A cobertura inclui `$` (contém),
`LEFT()`, `RIGHT()`, `SUBSTR()` e `UPPER()`.

### Documentação Completa da API (Português)

Todas as **364 funções ACE** estão agora documentadas em português
(pt-BR) em `docs/pt/funcoes/`, cobrindo sintaxe, parâmetros,
valores de retorno e exemplos.

### Correção de Calling Convention x86 (32-bit)

`ENTRYPOINT` agora é `__stdcall` (WINAPI) no Win32, coincidindo
com a convenção de chamada do `rddads` do Harbour. Isso corrige a
corrupção de stack quando apps Harbour de 32-bit chamam funções ACE
através da DLL. O arquivo `.def` do x86 e a biblioteca de
importação são atualizados para coincidir. (Reportado por Jonsson /
RusSoft Ltda.)

### CI — Build Leg msvc-x86

Uma nova entrada de matrix `msvc-x86` no
`.github/workflows/ci.yml` garante que builds de 32-bit sejam
testados em cada PR, capturando breakages exclusivos do x86
(redução dependente de bitness, conflitos de assinatura `SQLLEN*`,
avisos `/WX`) antes de chegarem ao main.

---

## Correções de Erros

### Motor

- **Ordem de tags CDX** — `list_tags()` agora ordena por offset
  do tag-header (ordem de criação) em vez da ordem alfabética da
  folha. Corrige `DBSETORDER(n)` selecionando a tag errada em
  bolsas CDX escritas pelo SAP ADS. (Reportado por Jonsson /
  RusSoft Ltda.)
- **Tamanho de chave de índice de expressão CDX** — chaves de
  expressões compostas (ex: `UPPER(cName)`) agora são dimensionadas
  a partir do comprimento fixo natural da expressão, não do
  conteúdo do primeiro registro. O rtrim antigo truncava chaves,
  causando linhas fora de ordem após reindex em tabelas grandes.
  (Reportado por Jonsson / RusSoft Ltda.)
- **Caminho de folhas vazias CDX** — varreduras para a frente e
  para trás do índice agora ignoram folhas vazias deixadas por
  `erase()`. Corrige `ADSCDX/5000` em REINDEX / exclusão em massa.
  (PR #63)
- **Bits de recno em folha CDX** — `compute_layout` dimensiona o
  campo de número de registro a partir de `max_rec`, não apenas o
  comprimento da chave, para que tags com chaves largas não
  tragam mais recnos ≥ 4096. (PR #62)
- **Busca parcial CDX** — `seek_key` compara apenas o
  comprimento da chave de busca, para que buscas parciais como
  `SEEK "ART-00024800"` correspondam a chaves armazenadas
  `"ART-00024800 desc ..."`. (PR #62)
- **FOR condicional em campos lógicos** — o avaliador de
  expressões de índice agora trata campos lógicos como numéricos
  (0/1) em vez de strings truthy, para que `FOR ACTIVE` filtre
  corretamente registros `.F.`. (PR #121)
- **Corrupção por INDEX ON** — impede que `INDEX ON` corrompa
  índices da tabela fonte. (PR #118)
- **SKIP para trás MSSQL** — erro por um: `abs_n == pos` agora
  alcança a linha 0 em vez de reportar BOF. (PR #65)
- **Getters tipados ABI para backends SQL** — `AdsGetDouble`/`Long`/
  `LongLong`/`String` despacham através do vtable de ops do
  backend, para que o PostgreSQL retorne valores reais. (PR #66)
- **`AdsGetIndexHandle` para backends SQL** — resolve por nome
  para tabelas PG para que a busca por índice funcione de ponta a
  ponta. (PR #66)
- **Formato de chave numérica NTX** — campos numéricos indexados
  em uma bolsa NTX agora armazenam chaves no formato nativo
  DBFNTX (magnitude preenchida com zeros + negativos
  complementados) em vez de texto `STR()` preenchido com espaços.
  A busca nativa `dbSeek(<number>)` de um lector xBase agora
  corresponde à chave em disco. Bolsas de índice reabertas
  conservam a codificação numérica. (PR #67)
- **`dbSeek` numérico** — rddads envia tipo de chave
  `ADS_STRING` para buscas numéricas; o motor agora lida com isso
  corretamente.
- **`ALIAS->FIELD` em busca numérica** — remove o prefixo
  `ALIAS->` para que tags CDX com alias encontrem chaves.
- **Reversão de transações** — remove fisicamente os registros
  adicionados ao reverter em vez de deixar linhas fantasma.
- **Vazamento de estado LockMgr** — `held_` agora é limpo ao
  destravar para que o próximo bloqueio tome um bloqueio real do SO.
- **`AdsGetRecordCount`** — agora respeita `bFilterOption`.
- **`AdsSetRelation`** — falha honestamente quando apropriado.
- **`seek_key`** — `walk_to_last` agora honra `SET DELETED ON`.
- **Navegação de tabela vazia** — lida corretamente com tabelas
  com zero registros.
- **Navegação de registros eliminados** — estado correto após
  pular linhas eliminadas.
- **Contagem de bloqueios LockMgr** — bloqueios repetidos sobre a
  mesma chave agora são contados por referência; o bloqueio do SO
  é liberado apenas quando o último detentor destrava.
- **Verificação de limites de registros WAL** — `TxLog::read_all`
  valida o comprimento de cada campo UPDATE/APPEND antes de ler,
  evitando leituras excessivas em arquivos WAL truncados ou
  corrompidos.

### ABI

- **Leitura fora dos limites** em formatação de chave numérica —
  prevenida.
- **Bits de opção trocados** — `ADS_DESCENDING` (0x02) e
  `ADS_COMPOUND` (0x08) eram decodificados incorretamente em
  `AdsCreateIndex61`.
- **Resolução de nomes de campo insensível a maiúsculas** —
  `field_index` agora é cacheado para melhor desempenho.
- **`AE_NO_CURRENT_RECORD`** — retorna 5068 em vez de 5026.
- **`OrdListAdd`** — volta ao nome base quando um caminho relativo
  prefixa dobrado o diretório da tabela.
- **Vínculo de helpers trig** — vínculo C++ para helpers `trig_*`
  para silenciar MSVC C4190.
- **Crash de `AdsGetField` em backends SQL** — ler por ordinal de
  campo não causa mais crash.
- **AdsGetRecordCount em ORDER condicional** — conta
  correspondências FOR corretamente. (PR #100)
- **`AdsSetAOF` para filtros não otimizáveis** — agora retorna
  `AE_INVALID_EXPRESSION` em vez de sucesso, para que o rddads
  stock caia em filtrado do lado do cliente. O comportamento
  anterior desabilitava silenciosamente `SET FILTER` por completo.
  (Reportado por Jonsson / RusSoft Ltda.)

### Driver ODBC

- **Build x86** — `C4100` (parâmetro não utilizado) e `C2733`
  (conflito de assinatura `SQLLEN*` vs `SQLPOINTER`) corrigidos
  para 32-bit MSVC `/WX`. (PR #119)
- **Conformidade DM/ADO** — descritores de handle, `SQLBindCol` e
  mapeamento de tipo `BIT` corrigidos.
- **`SQL_DRIVER_ODBC_VER` / `SQL_ODBC_VER`** — agora reportados
  em `SQLGetInfo`.

### Segurança DA-Web

- Sanitização de expressões de filtro AOF.
- Correção de contenção de caminho de raiz de unidade.
- Varredura de segurança de API em todos os endpoints PHP.
- Caminhos de índice RI meta contidos sob o diretório DD.
- Limites de tamanho de frame wire para prevenir abuso.

### Plataforma / Build

- **macOS** — avisos de sign-conversion e unused-function
  corrigidos.
- **GCC** — avisos `-Werror` resolvidos (shadow, implicit
  conversion, format-truncation, stringop-truncation).
- **MSVC** — decoração `__stdcall` do x86 e crash de `_wfsopen`
  no x64 corrigidos.
- **Clang** — guarda `-Wc2y-extensions` para Apple Clang anterior.
- **Colisão de fd 0 em POSIX** — os handles de arquivo agora são
  armazenados como `(fd + 1)` para que um fd 0 real (retornado por
  `open()` quando stdin está fechado) não seja confundido com o
  sentinela "não aberto".
- **Reintento `EINTR` em POSIX** — `pread` / `pwrite` reintentam
  após interrupção por sinal em vez de falhar na E/S.
- **mmap de comprimento zero em POSIX** — `map_readonly` rejeita
  mapeamentos de comprimento zero em vez de chamar `mmap` com
  comprimento 0.

### CDX

- Bloqueio de cabeçalho compartilhado ao abrir para que
  inserções concorrentes não falhem.
- CDX estrutural nomeado por tabela, não por sessão de diretório.
- **Atualização de contagem de registros obsoleta no caminho de
  leitura** — o driver cacheia a contagem de registros do DBF ao
  abrir; em um despligue multiusuário um append de outro processo
  poderia deixar o cache defasado, de modo que uma varredura de
  índice que alcançava uma linha recém-adicionada (ex: no meio de
  `REPLACE … FOR`) falhava com erro 5000 espúrio.
  `read_record_raw` / `write_record_raw` agora relêem a contagem
  em disco sob um bloqueio de cabeçalho compartilhado antes de
  declarar um recno fora do intervalo (só caminho lento — uma
  varredura normal para a frente não paga nada).

### Remoto (Wire)

- Crash de use-after-free em consultas de tabelas virtuais via
  TCP eliminado.

---

## Documentação

- **CONTRIBUTING.md** — novo guia de contribuição com fluxo de PR,
  política de protocolo e regras de clean-room.
- **Wire Protocol DD API** — referência completa da API do
  Dicionário de Dados (§9) adicionada.
- **DA-Web GUIDE.md** — guia completo do usuário para a interface
  de navegador DA-Web.
- **README** — atualização completa do estado pós-rc29 cobrindo
  caminho de escrita ADI, criação ADT, modo AES e escopo do
  DA-Web.
- **Cookbook** — nova pasta `cookbook/` com exemplos Harbour
  executáveis e muito comentados (simples → avançado). A trilha
  `console/` é xBase puro: criar / seek por índice / transações /
  manutenção DBF (`ADSCDX`), SQL via `AdsCreateSQLStatement` +
  `AdsExecuteSQLDirect`, ADT nativo (`ADSADT` + `.adi`) e um
  cliente remoto `tcp://`. A trilha `orm/` roda o mesmo CRUD
  através de um ORM Harbour companion sobre back-ends SQLite / DBF
  / PostgreSQL / MariaDB / ODBC, terminada com um benchmark de
  todos os back-ends (`orm/complete/`) com checksum de conteúdo
  entre back-ends e manchete seek-vs-scan. Uma amostra CRUD
  `xbrowse` do FiveWin e guias de string de conexão / tipos de
  campo / resolução de problemas completam.
- **Referência da API (PT)** — todas as 364 funções ACE
  documentadas em português com sintaxe, parâmetros, valores de
  retorno e exemplos.

---

## Empacotamento

- **Instalador Windows Inno Setup** (`openads-setup.iss`).
- **Pacotes CPack** com `openace32.lib` / `openace64.lib`
  garantidos nos arquivos Windows.
- **Release CI** — `openace{32,64}.lib` incluídos automaticamente
  nos arquivos de release.

---

## Testes

- **874 testes unitários** passando em x64 e x86 (361 300+
  asserções).
- Novos arquivos de teste: `abi_cdx_tag_order_test.cpp` (ordem de
  criação de tags CDX), `abi_cdx_expr_index_scale_test.cpp`
  (tamanho de chave de índice de expressão em escala),
  `abi_multitag_order_nav_test.cpp` (navegação multi-tag),
  `abi_ntx_numeric_edge_test.cpp` (casos extremos numéricos NTX),
  `cdx_empty_tree_test.cpp` e mais.
- **CI x86** — build leg `msvc-x86` captura breakages de 32-bit
  em cada PR.
- Demo Harbour em `examples/adt-native/` (por glokcode).

---

## Contribuintes

- **Jonsson / RusSoft Ltda.** — correções de ordem de tags CDX,
  tamanho de chave de índice de expressão, filtro não otimizável
  de `AdsSetAOF` e calling convention x86.
- **Admnwk** — driver ODBC, push-down SQL, agregação,
  FetchWhere V2, caminho de escrita nativo e melhorias de CI.

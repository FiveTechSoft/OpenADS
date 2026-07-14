---
title: AdsCacheRecords
layout: default
parent: Referência da API
nav_order: 9
permalink: /pt/funcoes/ads-cache-records/
---

# AdsCacheRecords

Configura o cache de registros.

## Sintaxe

```c
UNSIGNED32 ENTRYPOINT AdsCacheRecords(ADSHANDLE hTable,
                                      UNSIGNED16 usRecCount);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hTable` | `ADSHANDLE` | Handle da tabela. |
| `usRecCount` | `UNSIGNED16` | Número de registros a manter em cache. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso. Código de erro se falhar.

## Descrição

Define quantos registos um `AdsSkip` para a frente lê antecipadamente numa ligação remota.

**Normalmente não precisa de chamar esta função.** Por omissão, o OpenADS escolhe a profundidade sozinho: um `AdsSkip` para a frente devolve um bloco de linhas antecipadas juntamente com a actual, e o cliente serve os skips seguintes a partir desse bloco sem qualquer ida e volta à rede. O servidor aumenta a profundidade ao detectar um percurso sequencial e reduz-a perante qualquer reposicionamento, escrita ou mudança de ordem — assim um browse obtém um bloco profundo, enquanto um «procurar um registo e lê-lo» não paga por linhas que nunca serão lidas. (A SAP usa um valor fixo de dez registos; o OpenADS adapta-se.)

Chame `AdsCacheRecords` quando souber algo sobre o seu padrão de acesso que o servidor não consiga inferir:

| `usNumRecords` | Efeito |
|-----------|--------|
| `0` ou `1` | **Desliga** a leitura antecipada para esta tabela. |
| `N` | Lê exactamente `N` registos por skip, ignorando o ajuste automático. |

Desligar é o mais acertado num ciclo de actualização em lote que edita quase todos os registos que visita: cada escrita descarta o bloco de qualquer forma, portanto ler antecipadamente é puro desperdício. Aumentar (o valor «agressivo» da SAP é 100) serve um percurso unidireccional que lê grande parte de uma tabela sem a editar. A SAP nota que valores acima de 100 raramente compensam; o OpenADS limita cada bloco a 512 registos de qualquer modo, para que um pedido não se torne um percurso ilimitado no servidor.

A profundidade é um tecto, não uma promessa: o bloco é também limitado por um orçamento de bytes, pelo que uma tabela com registos largos devolve menos linhas do que as pedidas em vez de uma trama de rede excessiva.

A leitura antecipada nunca serve dados obsoletos causados por si: o bloco é descartado perante qualquer escrita, procura, mudança de ordem ou `AdsRefreshRecord`. Tal como na SAP, as linhas já presentes no bloco **não** reflectem alterações feitas em simultâneo por *outros* utilizadores.

## Exemplo

```c
AdsCacheRecords(hTable, 100);
```

## Ver Também

- [AdsCacheOpenCursors]({{ site.baseurl }}/pt/funcoes/ads-cache-open-cursors/)
- [AdsCacheOpenTables]({{ site.baseurl }}/pt/funcoes/ads-cache-open-tables/)
- [AdsCloseCachedTables]({{ site.baseurl }}/pt/funcoes/ads-close-cached-tables/)

---

[AdsCloseCachedTables →]({{ site.baseurl }}/pt/funcoes/ads-close-cached-tables/)

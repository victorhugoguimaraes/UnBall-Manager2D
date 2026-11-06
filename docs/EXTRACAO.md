# Extração de dados das partidas

O Manager salva os logs desde o início, aguarda o servidor e seus descendentes encerrarem e gera os CSVs em um processo separado. Você pode iniciar outra partida enquanto a exportação anterior termina. O botão de encerramento da partida não mata o extrator.

Na aba **Histórico**, selecione uma sessão. **Abrir CSVs** abre a pasta da última exportação válida. **Reprocessar dados** cria outra versão a partir dos mesmos logs. **Ver problemas** mostra o erro da tentativa atual e uma prévia da qualidade da última exportação válida, quando existir. O diagnóstico completo fica em `qualidade.csv`.

## Arquivos gerados

- `partida.csv`: nomes anunciados e cadastrados, placar observado, tempo e cobertura.
- `bola.csv`: posição e velocidade da bola em cada quadro.
- `jogadores.csv`: posição, velocidade, orientação, estado, recursos e contadores de cada jogador em cada quadro. O lado se relaciona com os nomes em `placar.csv` e `partida.csv`.
- `arbitragem.csv`: mudanças registradas de modo de jogo; inclui as sinalizações do árbitro disponíveis no RCG.
- `placar.csv`: estados de placar registrados, incluindo campos adicionais de pênaltis quando presentes.
- `comandos_recebidos.csv`: comandos Recv do RCL, com origem, ciclo, subciclo e argumentos. Mensagens `say` com parênteses não viram comandos adicionais.
- `parametros.csv`: parâmetros de servidor e jogador e características dos tipos de jogador.
- `resumo_jogadores.csv`: contadores observados, cobertura, stamina e distância por jogador.
- `resumo_times.csv`: agregações por lado/time, com denominadores disponíveis.
- `qualidade.csv`: avisos e informações com fonte/linha quando aplicável.
- `dicionario_campos.csv`: tipos, unidades, significado e regras de ausência de cada coluna.

Os CSVs de cada exportação ficam em `exports/versions/<data_hora_id>/csv/`. O `manifest.json` fica um nível acima dos CSVs e registra versões do extrator e esquema, formatos, hashes e tamanhos das fontes, contagens de linhas e hashes dos CSVs. `latest.json`, na pasta `exports`, aponta para a última exportação concluída. Uma tentativa com erro mantém esse apontador e os arquivos anteriores.

Os dados usam UTF-8, vírgula como separador, ponto decimal e escape CSV padrão. No Excel, importe por **Dados → De Texto/CSV**, escolhendo UTF-8 e vírgula. Campos vazios indicam indisponibilidade ou cálculo sem observações suficientes; não são zeros.

## Tempo, quadros e limpeza

O RCG textual não fornece um subciclo em cada `show`. Por isso, `quadro_id` identifica cada observação na ordem do arquivo; `ciclo` pode se repetir. `ordem_no_ciclo` enumera essas repetições e não se apresenta como tempo registrado. O RCL mantém seu subciclo original.

As posições são originais, sem inversão silenciosa pela direção de ataque. Campos opcionais ainda não tabulados ficam em `campos_adicionais`. Linhas de jogador inválidas ficam fora da tabela e aparecem no diagnóstico; os quadros envolvidos afetam a cobertura. Bola ausente, estrutura temporal ambígua ou arquivo sintaticamente quebrado fazem a extração falhar.

Contadores são registros de ações, sem pressupor sucesso. A primeira observação estabelece a base. Quedas são contadas em `reinicios_contador` e não somadas como ações positivas. Cada contador tem `intervalos_contador_*` para mostrar quantos pares foram comparados. A distância usa somente ciclos consecutivos em `play_on`, com o jogador habilitado nas duas observações. Não há interpolação entre lacunas. Recursos usam as observações disponíveis de jogo corrido e exibem a quantidade de amostras.

Parâmetros textuais contendo aspas nem sempre são escapados integralmente pelo RCSS ao escrever o RCG. Quando um valor aparece dividido em vários tokens, o extrator preserva a sequência na coluna `valor` e publica um aviso. Não altera os parâmetros do servidor para resolver isso.

## Término e cobertura

O sucesso da exportação e a cobertura da partida são independentes.

- **Normal**: há um último modo `time_over` registrado.
- **Interrompido**: o Manager registrou uma solicitação de parada e não há término normal.
- **Indeterminado**: não há evidência suficiente para classificar o encerramento.

A cobertura **completa** exige RCG, nomes/placar, início observado, duração normal mínima obtida dos parâmetros, término normal e ausência de quadros incompletos ou lacunas inesperadas. Não basta contar 6000 ciclos. O extrator reconhece a omissão isolada do quadro de troca de tempo normal apenas quando coincide exatamente com os parâmetros de duração e existe `kick_off` registrado no tempo seguinte. Essa ausência continua explícita em `ciclos_ausentes_fronteira`, sem criar um quadro artificial.

`ciclos_ausentes_intervalo` conta todas as lacunas internas; `ciclos_ausentes_inesperados` exclui somente as fronteiras confirmadas. Casos de prorrogação ou configurações não confirmadas seguem a classificação conservadora. A classificação de completa não certifica todas as variáveis: recursos/contadores opcionais ausentes continuam sinalizados, e o RCL pode estar indisponível.

Uma sessão só com RCL pode exportar comandos e parâmetros de qualidade, com cobertura **indeterminada** e tabelas de posição vazias. RCL ausente não gera comandos de contagem zero.

## Uso independente

A partir da raiz do repositório:

```bash
PYTHONPATH=python python3 -m unball_extract --session /caminho/da/sessao
```

A pasta precisa conter `logs/` com os arquivos de uma partida. `match.json` é opcional para importações; acrescenta os nomes cadastrados. O processo retorna JSON no stdout e código zero em caso de sucesso; retorna código diferente de zero e diagnóstico JSON no stderr em caso de falha.

Suporte inicial: RCG ULG6 textual e registros Recv de RCL, incluindo `.rcg.gz` e `.rcl.gz`. Se houver cópias idênticas, apenas uma é processada, com aviso. Múltiplos logs diferentes do mesmo tipo ou RCG/RCL com nomes de jogo incompatíveis são recusados para evitar misturar partidas.

Um bloqueio do sistema operacional impede duas exportações simultâneas da mesma sessão. O estado de sessão do Manager impede extrair enquanto o servidor ainda grava; os hashes são conferidos novamente antes de publicar. Logs gerados fora do Manager devem estar encerrados antes da importação.

Cada tentativa é escrita em uma pasta temporária e só é publicada após concluir. Fechamento normal espera a fila. Após queda ou encerramento forçado, use **Reprocessar dados**. A última publicação válida permanece disponível; pastas temporárias deixadas por interrupção forçada não são tratadas como exportações válidas.

## Validação e limites

A suíte automatizada cobre quadros com ciclo repetido, lacunas, fronteiras de tempo, contadores reiniciados/ausentes, jogadores rejeitados, números não finitos, formatos incompatíveis, gzip, pareamento, bloqueio de sessão ativa e reprocessamento determinístico. O teste de interface cobre exportação automática, botão de reprocessamento e espera ao fechar.

A integração real foi testada com RCSSServer 19.0.0 e duas instâncias da estratégia instalada: 22 jogadores conectados, término normal e exportação de 99 quadros, 2178 observações de jogador e 5786 comandos recebidos. A ausência do quadro de troca de tempo foi registrada separadamente, preservando a cobertura completa pela regra acima.

Posse/controle, passes, finalizações, entradas na área e outras estimativas táticas ainda não são calculados nesta versão. Os CSVs observados formam a base para acrescentar essas regras com parâmetros, versões e testes próprios.

## Organização e compatibilidade

Novas sessões usam pastas por dia e confronto. Scripts, configurações e registros de execução ficam em `runtime/`. Os RCG/RCL continuam em `logs/`, e as exportações ficam em `exports/versions/`, cada uma com `csv/` e manifesto. A pasta raiz da sessão mantém apenas a identificação e essas três áreas.

O histórico reconhece sessões antigas diretamente em `runs/UnBall-Manager2D` e as novas dentro de uma pasta de data. Exportações antigas com CSVs na raiz da versão também continuam acessíveis. Reprocessar cria uma nova versão organizada, preservando a anterior. Não há exclusão automática de logs ou versões.

O formato dos CSVs continua no esquema 1.0. A disposição das pastas é identificada separadamente como `layout_version: 2`, e o extrator passou para 1.1.0. Caminhos em `manifest.json` são relativos à pasta da versão; leitores externos devem usar esses caminhos ou acrescentar `csv/` nos resultados novos.

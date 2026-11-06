# Revisão do UnBall-Manager2D

Nota de continuidade: esta revisão registra a etapa anterior à extração. A primeira versão do extrator foi implementada em seguida; veja [Extração de dados](EXTRACAO.md) para o estado atual.

Revisão da base `90c252f`, realizada em 5 e 6 de novembro de 2026. Escopo: funcionamento atual, segurança da execução local, persistência, organização do código e interface nativa. A extração automática de estatísticas será planejada em uma etapa posterior.

## Problemas encontrados e correções

### Alta prioridade: partida usava informações diferentes das escolhidas

`MainFrame::onPrepareMatch` delegava ao `auto_match.py` usando os nomes dos times. As pastas cadastradas e o executável de servidor escolhido não determinavam diretamente a execução. Isso tornava possível validar uma pasta e executar outra instalação.

Agora `Session` prepara inicializadores para as duas pastas selecionadas, e `ChildProcess` inicia o servidor configurado. O script de cada time é executado a partir de seu próprio diretório. Caminhos com espaços e apóstrofos são preservados sem interpolação de comandos fornecidos pelo usuário.

### Alta prioridade: encerramento forçado e configuração global

O encerramento usava SIGKILL imediatamente. A gravação final dos logs poderia ser interrompida. O caminho de servidor isolado não criava um grupo como o caminho de partida, prejudicando a consistência da parada. O modo Coach tentava editar `~/.rcssserver/server.conf` com `sed`.

A execução agora pertence a um grupo próprio, com parada gradual e limpeza dos descendentes. Fechar a janela aguarda essa parada. As opções de Coach e logging ficam na sessão. O servidor gera seu próprio `server.conf`, com a versão correta, e carrega as opções do Manager por `include=config/session.conf`.

### Alta prioridade: persistência podia perder o estado anterior

A leitura do cadastro apagava o conteúdo em memória antes de validar todo o documento. Na gravação, o tratamento de falha de renomeação podia remover o arquivo existente. A configuração do ambiente era aplicada à memória antes de sua validação.

A leitura agora valida uma estrutura temporária antes de substituir o cadastro. A escrita usa arquivo temporário, sincronização e renomeação atômica, removendo somente o temporário em caso de erro. O ambiente só passa a valer após validar e salvar. Erros de leitura ficam visíveis.

### Média prioridade: validação de times dava uma falsa impressão de prontidão

A validação oferecia gerar um `start.sh` genérico sem conhecer a estrutura do time. A existência de uma pasta não provava que o time podia iniciar. Pastas equivalentes por links simbólicos também podiam ser duplicadas.

A lista agora distingue inicialização pronta ou pendente. Validação e importação procuram um inicializador executável, incluindo layouts `bin` e `bins`. Nenhum arquivo do time é gerado ou alterado. O cadastro normaliza caminhos e rejeita duplicatas equivalentes.

### Média prioridade: saída e replays incompletos na interface

A leitura periódica da saída não era iniciada no caminho de servidor isolado. A tela podia descartar saída final e partir caracteres UTF-8 entre leituras; o conteúdo exibido crescia sem limite. A busca de replays não percorria as subpastas onde sessões normalmente guardam logs e aceitava arquivos comprimidos que não eram RCG.

A leitura agora acompanha todos os processos, reúne linhas, limita o trabalho por atualização e conserva a saída da sessão em `console.log`. A tela mantém um histórico limitado. A busca percorre subpastas e filtra `.rcg` e `.rcg.gz`, ordenados por modificação.

## Código e design

A revisão usou os critérios de `cpp-coding-standards`, `codebase-design`, `cpp-testing` e `frontend-design`, aplicados a uma interface wxWidgets. A opção foi preservar os controles nativos e corrigir a clareza do fluxo.

O código que gerencia arquivos, times, sessões e processos foi separado no núcleo `manager-core`. A janela fica responsável por reunir as entradas, exibir o estado e encaminhar ações. Recursos de sistema pertencem a objetos com limpeza definida, e os testes não dependem do loop gráfico para verificar regras de sessão.

Na interface, **Iniciar partida** é a ação explícita da aba Nova partida. O estado da sessão aparece junto dos seletores. Ações conflitantes ficam indisponíveis durante a execução ou encerramento, e os próprios handlers também rejeitam uma segunda sessão. Os botões se distribuem em linhas quando falta espaço. Os seletores mantêm a escolha após atualização do cadastro, e há um acesso direto à pasta da sessão.

## Validação executada

- Compilação com GCC 15, C++20 e wxWidgets 3.2 em Linux.
- CTest: cadastro, sessões e processos. Casos incluem JSON inválido sem perda do cadastro anterior, duplicatas por symlink, falha de substituição de arquivo, caminhos com espaços/apóstrofos, argumentos literais sem execução de shell, descoberta de times e replays aninhados.
- Processo que ignora SIGINT e SIGTERM: escalonamento até SIGKILL, mantendo vivo outro processo independente.
- Interface em display isolado: iniciar, rejeitar segunda sessão, bloquear controles e encerrar. Cadastro de teste separado do cadastro real.
- Partida real com RCSSServer 19.0.0 e duas instâncias da estratégia instalada: 22 jogadores conectados, término normal, RCG v6 e RCL gravados. O diretório de teste continha espaço, acento e apóstrofo. O RCG foi lido pelo `rcgvalidator` sem mensagens de invalidade.

## Limites e próxima etapa

A verificação de um inicializador confirma que ele pode ser executado, mas não garante que bibliotecas, nomes de time e argumentos internos estejam corretos. A integração real validou a estratégia instalada; outros times precisam de seus próprios testes. O monitor e o servidor usam as portas padrão no fluxo normal.

Há uma sessão ativa por janela. Ainda não há coordenação entre duas instâncias do aplicativo, indicação de prontidão baseada na conexão dos jogadores ou pipeline de estatísticas. Um processo criado por script que abandone deliberadamente o grupo não pode ser encerrado com a mesma garantia dos descendentes comuns.

A sessão já conserva os logs brutos necessários para investigar partidas. Na etapa de planejamento da captura de dados, ainda precisamos definir as métricas, fontes e versões do extrator, validação de jogos completos, tratamento de partidas interrompidas e apresentação dos resultados. Criar um arquivo não comprova que a amostra é completa ou que uma estatística está correta.

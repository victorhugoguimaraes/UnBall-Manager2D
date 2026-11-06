# Uso diário do UnBall Manager

A ferramenta é uma bancada de testes de futebol simulado para a equipe UnBall. A pessoa escolhe dois times, inicia uma partida e volta ao histórico para comparar ou investigar o que aconteceu.

A primeira leitura deve responder: quais times vão jogar e onde estão os dados das partidas anteriores? A intenção é operar: pouca variação visual, sem animação decorativa, densidade moderada e ações claras. A interface continua em wxWidgets, com fonte, cores, foco e controles do sistema. Não há uma nova paleta própria sobre o tema do desktop.

## Organização da tela

```text
Partida | Histórico | Times | Replays antigos | Eventos | Configuração

Partida
  estado da sessão
  time esquerdo / time direito
  opção Coach
  Iniciar partida | Encerrar sessão | Abrir pasta da sessão

Histórico
  buscar time, data ou identificador          filtro de estado
  data / hora | confronto | extração | cobertura
  resumo da seleção e pasta
  CSVs | replay | usar times | pasta | reprocessar | problemas | atualizar
```

O histórico mantém a seleção ao atualizar. A busca filtra a lista já carregada; não lê novamente o disco a cada tecla. Um duplo clique abre os CSVs, ou o replay quando ainda não há CSV. As ações ficam disponíveis conforme a seleção e o estado da partida.

Ctrl+Enter inicia, Ctrl+H abre o histórico e foca a busca, Ctrl+L abre os eventos e F5 atualiza o histórico e os replays. A última dupla de times é lembrada pelo caminho cadastrado. Usar os times do histórico prepara o confronto e leva à tela de partida, sem iniciar um jogo inesperadamente.

## Arquivos

Novas sessões ficam em `runs/UnBall-Manager2D/AAAA-MM-DD/hora_confronto_id/`. O usuário encontra `match.json`, `logs/`, `exports/` e `runtime/` na raiz. A pasta runtime concentra os detalhes de execução. As versões ficam em `exports/versions/`. Cada versão da exportação contém `manifest.json` e `csv/`.

Sessões e exportações antigas continuam acessíveis. A atualização não move nem apaga os arquivos existentes. Reprocessar conserva as versões anteriores e publica a nova estrutura apenas no resultado novo.

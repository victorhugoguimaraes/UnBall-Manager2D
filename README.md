# UnBall-Manager2D

Interface nativa em C++20 e wxWidgets para cadastrar times, iniciar partidas do RoboCup 2D e abrir replays. Compatível com Linux e WSL com interface gráfica.

## Compilar e executar

No Ubuntu/Debian, instale as dependências:

```bash
sudo apt install build-essential cmake libwxgtk3.2-dev nlohmann-json3-dev python3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
./build/UnBall-Manager2D
```

Instale o servidor, o monitor e os times separadamente. O [rcsssetup](https://github.com/omusymcomp/rcsssetup) é uma opção para preparar esse ambiente. A compilação do Manager e seus testes unitários não exigem a estratégia privada da UnBall.

## Preparar uma partida

1. Em **Configuração**, escolha a pasta base do RoboCup e o executável `rcssserver`. O `rcssmonitor` é opcional. Salve os caminhos.
2. Em **Times**, adicione a pasta de cada time ou importe os times de `<pasta base>/teams`. A importação procura inicializadores até cinco níveis de diretórios.
3. Use **Validar selecionado** para conferir se existe um inicializador executável. São reconhecidos `start.sh`, `start`, `start_team`, `startAll`, `startAll.sh`, `startlocal`, `startlocal.sh`, `start_team.sh` e `localStartAll`, na raiz ou em `bin`/`bins`.
4. Em **Partida**, escolha dois times e clique em **Iniciar partida**. A opção Coach permite mover bola e jogadores pelo monitor.
5. Acompanhe a saída em **Eventos**. Use **Encerrar sessão** para solicitar a parada dos processos e aguardar a gravação dos logs.

O Manager executa o inicializador de cada pasta cadastrada, usando a pasta do próprio inicializador como diretório de trabalho. Cada time precisa estar compilado e seu script deve conectar a `localhost` nas portas padrão do servidor. Os nomes anunciados pelos scripts precisam ser distintos: o nome no cadastro identifica o time, mas não reescreve argumentos de scripts de terceiros. Ajuste scripts que exigirem parâmetros ou bibliotecas específicas antes de cadastrá-los.

O campo `auto_match.py` continua disponível para compatibilidade com configurações antigas, mas não participa da execução das partidas.

## Trabalhar pelo histórico

Use **Histórico** para buscar por time, data ou identificador. Os filtros mostram todas as partidas, as que têm CSVs, as que têm avisos/falhas ou as que ainda não têm CSVs. A seleção é mantida nas atualizações.

- **Abrir CSVs** ou duplo clique abre os dados da última exportação válida.
- **Assistir replay** abre o RCG associado àquela partida.
- **Usar estes times** carrega o confronto em Partida, pronto para iniciar.
- **Abrir pasta** mostra logs, versões anteriores e os arquivos da sessão.

Os últimos times usados são lembrados entre aberturas. Atalhos: **Ctrl+Enter** inicia a partida; **Ctrl+H** abre o histórico e foca a busca; **Ctrl+L** abre os eventos; **F5** atualiza histórico e replays. O servidor isolado fica em **Configuração**.

## Arquivos de cada sessão

Cada execução cria uma pasta exclusiva dentro de:

```text
<pasta base>/runs/UnBall-Manager2D/AAAA-MM-DD/hora_confronto_id/
  match.json                       identificação, times e data da partida
  logs/                            RCG e RCL originais
  exports/
    latest.json                    aponta para a última exportação válida
    versions/data_hora_id/
      manifest.json                fontes, contagens e integridade
      csv/                         os onze arquivos CSV
  runtime/
    config/                        configurações isoladas do servidor
    launchers/                     inicializadores da sessão
    console.log                    saída do servidor
    session-state.json             estado da execução
    extraction.json                estado da extração
    extraction-console.log         saída do extrator
```

Sessões antigas continuam disponíveis, inclusive com arquivos diretamente na raiz. A atualização não move nem apaga esses arquivos. **Abrir pasta da sessão** dá acesso a esses arquivos. O Manager usa `RCSS_CONF_DIR` por processo e carrega `config/session.conf` como complemento. Assim, cada servidor gera configurações compatíveis com sua própria versão, sem editar `~/.rcssserver` ou as pastas dos times.

A parada começa com SIGINT, passa a SIGTERM após três segundos e usa SIGKILL após quatro segundos se necessário. Só os grupos iniciados pelo Manager recebem esses sinais. Scripts que deliberadamente se desligarem do grupo de processos precisam ser adaptados para permitir o gerenciamento da sessão.

Em **Replays antigos**, a busca inclui subpastas de `runs/UnBall-Manager2D`, `log` e `log_analysis/log` até doze níveis. Somente arquivos `.rcg` e `.rcg.gz` são listados. Também é possível escolher um arquivo manualmente.

Os cadastros e caminhos ficam no diretório de dados do aplicativo, normalmente `~/.UnBall-Manager2D`. A gravação usa substituição atômica; um erro ao salvar mantém o arquivo anterior.

## Extração automática para CSV

Ao encerrar uma partida, o Manager exporta os logs em segundo plano. A aba **Histórico** lista as sessões, o estado da extração e a cobertura da partida. **Abrir CSVs** abre a última exportação válida; **Reprocessar dados** cria uma nova versão; **Ver problemas** apresenta erros e avisos.

São gerados onze CSVs: partida, bola, jogadores, arbitragem, placar, comandos recebidos, parâmetros, resumos por time e jogador, qualidade e dicionário dos campos. Os dados ficam em `<sessão>/exports/versions/<versão>/csv/`; `exports/latest.json` aponta para a versão mais recente concluída.

O extrator exige Python 3.10 ou superior e usa somente a biblioteca padrão. Suporta RCG textual ULG6 e comandos Recv do RCL, inclusive comprimidos com gzip. Mantém os logs originais, distingue ausências de zeros e permite exportar partidas interrompidas com cobertura explícita. Fechar a janela normalmente aguarda a exportação; uma interrupção forçada pode ser recuperada pelo reprocessamento.

[Veja o formato dos dados, as regras de limpeza e o uso por terminal](docs/EXTRACAO.md).

## Validar mudanças

```bash
ctest --test-dir build --output-on-failure
```

Os testes de cadastro, sessões, processos, fila de exportação e leitor Python usam diretórios temporários e verificações ativas também em Release. O núcleo `manager-core` permite testar essas regras sem abrir a interface.

Teste opcional da interface em um display X11 isolado já iniciado:

```bash
cmake -S . -B build -DUNBALL_ENABLE_GUI_TESTS=ON
cmake --build build -j4
GDK_BACKEND=x11 DISPLAY=:97 ./build/ManagerUiTests
```

Defina `UNBALL_UI_SCREENSHOT_DIR` para salvar imagens durante esse teste. Ele usa cadastro e servidor fictícios próprios.

Teste opcional com servidor e estratégia reais, sem monitor:

```bash
./build/SessionSmokeTest /caminho/rcssserver /caminho/estrategia/bin /tmp/manager-smoke
```

Esse teste também exporta os CSVs com a fila usada pelo Manager. Ele exige um `bin/start.sh` que aceite `-p`, `-P` e `-t`, bibliotecas do servidor/time no ambiente e portas UDP 16000 a 16002 livres. Ele executa uma partida curta com dois nomes de time distintos, exige 22 conexões de jogadores, término normal e geração de replay.

Veja a [revisão de código e interface](docs/REVISAO.md) para os problemas encontrados, correções e limites desta atualização.

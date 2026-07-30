# UnBall-Manager2D

O **UnBall-Manager2D** é uma ferramenta gráfica desenvolvida para auxiliar a equipe **UnBall (RoboCup 2D)** nos testes e simulações de partidas.

Seu objetivo principal é permitir que os desenvolvedores da UnBall configurem partidas rapidamente, validem o código, importem adversários e visualizem os logs e monitores, tudo isso sem precisar lidar diretamente com diversos scripts e comandos complexos no terminal.

O desenvolvimento e os testes assumem que você tenha acesso prévio à base/estratégia privada da equipe UnBall.

---

## Instalação de Dependências

O UnBall-Manager2D foi desenvolvido para ser executado no Linux ou através do Windows Subsystem for Linux (WSL).

Para compilar e executar o software, instale as seguintes dependências (exemplo para Ubuntu/Debian):

```bash
sudo apt update
sudo apt install build-essential cmake libwxgtk3.2-dev nlohmann-json3-dev
```

## Preparando o Ambiente RoboCup (Recomendado)

O aplicativo precisa interagir com as ferramentas da liga RoboCup 2D. Se você ainda não as possui instaladas, a maneira recomendada de configurá-las é através do projeto oficial de instalação `rcsssetup`.

1. Instale as dependências básicas:
   ```bash
   sudo apt-get install -y git python3 python3-pip git-lfs
   ```

2. Clone e execute o `rcsssetup`:
   ```bash
   git clone https://github.com/omusymcomp/rcsssetup.git
   cd rcsssetup
   python3 setup.py
   ```

Essa instalação criará a pasta `~/rcss` contendo todas as ferramentas (`rcssserver`, `rcssmonitor`), o script `auto_match.py` e dezenas de times adversários (como Helios, Oxsy, etc) organizados na pasta `~/rcss/teams`.

---

## Compilação do UnBall-Manager2D

Após ter as dependências instaladas, compile o projeto usando o CMake:

```bash
# Entre na pasta do UnBall-Manager2D
cd UnBall-Manager2D

# Configure e construa o executável
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

---

## Como Utilizar o Software

Execute o programa a partir do terminal para abrir a interface gráfica:

```bash
./build/UnBall-Manager2D
```

### 1. Configurando o Ambiente (Aba "Configuração")
Ao iniciar pela primeira vez, você precisará informar onde o software deve procurar as ferramentas do `rcsssetup`. Vá na aba **Configuração** e defina:

- **Pasta base do RoboCup:** O diretório principal (ex: `~/rcss`).
- **rcssserver:** O executável do servidor (ex: `/usr/local/bin/rcssserver`).
- **rcssmonitor (opcional):** O monitor gráfico para visualizar o jogo (ex: `/usr/local/bin/rcssmonitor`).
- **auto_match.py:** O script Python responsável por orquestrar a partida (geralmente em `~/rcss/auto_match.py` ou dentro da pasta do repositório `rcsssetup`).

Clique em **Salvar configuração**.

### 2. Configurando o Time da UnBall e os Adversários (Aba "Times")
O software permite gerenciar a nossa estratégia e os adversários de forma prática.

**Para adicionar o nosso time:**
1. Faça o clone do repositório da nossa estratégia dentro da pasta do rcsssetup:
   ```bash
   git clone <URL_DO_REPOSITORIO_DA_UNBALL> ~/rcss/teams/UnBall
   ```
2. Na aba **Times**, clique em **Adicionar time** e selecione a pasta recém-clonada (`~/rcss/teams/UnBall`).
3. Dê o nome de exibição (ex: `UnBall Team`).
4. **Importante:** Com o time na lista, clique em **Validar selecionado**. O gerenciador verificará se a estrutura de inicialização (como o `start.sh`) está correta e oferecerá a opção de gerá-la automaticamente caso você não a tenha.

**Para adicionar os adversários:**
Para não perder tempo adicionando um por um, clique no botão **Importar da pasta 'teams'**. O UnBall-Manager2D irá vasculhar recursivamente a pasta `~/rcss/teams` do `rcsssetup` e adicionará automaticamente todos os adversários das ligas passadas (rc2024, rc2023, etc) na sua lista!

### 3. Iniciando uma Partida de Teste (Aba "Nova Partida")
1. Vá para a aba **Nova Partida**.
2. Escale a **UnBall** de um lado, e um adversário (ex: Helios ou Cyrus) do outro.
3. Se quiser testar movimentações ou arrastar a bola/jogadores pelo monitor (muito útil para debugar a estratégia), marque a caixa **Modo Coach (Teste)**.
4. Clique em **Preparar sessão**. O servidor subirá automaticamente e o `rcssmonitor` será aberto.

Quando terminar de assistir, clique no botão **Finalizar partida** para matar o servidor e todos os executáveis dos times instantaneamente, deixando o ambiente limpo para o próximo teste!

### 4. Controle Rápido (Aba "Painel")
A aba **Painel** fornece um atalho para subir o servidor manualmente e, o mais importante, possui um botão de "Encerrar processos da sessão" que age como um botão de pânico caso você precise forçar o encerramento de qualquer resquício de processos do RoboCup que estejam travando sua máquina.

---

## Links Importantes
- **Base de Instalação (rcsssetup):** [https://github.com/omusymcomp/rcsssetup](https://github.com/omusymcomp/rcsssetup)

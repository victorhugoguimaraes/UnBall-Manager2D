# Plano de extração de dados para CSV

Estado: primeira versão implementada em 06/11/2026. Consulte `EXTRACAO.md` para o comportamento entregue. Este documento preserva o plano e o escopo da primeira versão.

## Fluxo definido com o usuário

O Manager conserva RCG e RCL desde o início da sessão. Após o servidor terminar e a saída ser drenada, inicia a exportação automaticamente. Um botão **Reprocessar dados** permite repetir a extração de uma sessão encerrada.

A exportação deve funcionar mesmo em partidas interrompidas. Nesses casos, os arquivos precisam indicar cobertura parcial; não devem entrar silenciosamente em comparações de partidas completas.

## Objetivo da primeira versão

Transformar logs em dados tabulares documentados, verificáveis e reutilizáveis para limpeza, gráficos e comparação de estratégias. Preservar os originais. O CSV facilita o trabalho com os dados, mas a limpeza depende de regras explícitas e de validação.

A primeira versão exporta observações, ações registradas e resumos diretos. Posse/controle, passes e finalizações estimadas entram em uma etapa posterior, com suas próprias definições e testes. Chute detectado não é equivalente ao contador de comandos `kick`; comando recebido não comprova execução ou sucesso.

## Arquitetura proposta

Um módulo Python executável por linha de comando dentro do repositório do Manager:

```text
python3 -m unball_extract --session /caminho/da/sessao
```

A interface recebe a pasta da sessão e retorna código de saída e um resumo JSON. O módulo concentra leitura, normalização, validação e exportação. O Manager inicia esse processo de forma assíncrona, acompanha o resultado e apresenta os arquivos gerados. O núcleo da extração usa a biblioteca padrão do Python; nenhuma dependência do Excel ou do repositório de análise da RCX em tempo de execução.

O código existente em `unball-rcx-2026-analysis/outputs/base_revisada/scripts/extrair_rcx.py` serve de referência para regras e casos de teste. Não deve ser chamado diretamente: sua entrada exige Excel, assume o calendário da RCX e calcula algumas lacunas contra 6000 ciclos fixos. Também agrupa quadros por ciclo, o que exige revisão para preservar observações durante o relógio parado.

O caminho do interpretador e do módulo deve ser resolvido sem depender do diretório de trabalho do terminal. CMake deve disponibilizar os arquivos junto da execução e instalação do Manager. Falta de Python ou falha do extrator precisa produzir erro visível e permitir tentar novamente.

## Organização dos arquivos

Cada sessão mantém `logs/`, `config/`, `match.json` e `console.log`. Os dados derivados ficam em:

```text
exports/
  <id-da-exportacao>/
    partida.csv
    bola.csv
    jogadores.csv
    arbitragem.csv
    placar.csv
    comandos_recebidos.csv
    parametros.csv
    resumo_times.csv
    resumo_jogadores.csv
    qualidade.csv
    dicionario_campos.csv
    manifest.json
  latest.json
```

Cada exportação é construída em uma pasta temporária. Só recebe o nome definitivo e atualiza `latest.json` depois de terminar e validar os arquivos. Uma falha conserva a última exportação válida. Reprocessar não apaga os logs nem resultados anteriores.

`manifest.json` registra versão do esquema/extrator, horários, identificador da sessão, hashes SHA-256 e tamanhos das fontes, contagens de linhas, formatos encontrados e resultado da validação. Metadados descritivos não devem fingir comprovar qual binário de estratégia rodou: o Manager conhece os inicializadores selecionados, mas eles podem iniciar outros executáveis.

## Conjunto inicial de CSVs

- `partida.csv`: uma linha por sessão; times anunciados e selecionados, placar observado, intervalo observado, estado de encerramento e cobertura RCG/RCL.
- `bola.csv`: uma linha por quadro; identificador do quadro, ciclo, subciclo quando registrado, ordem no arquivo, modo de jogo, posição e velocidade da bola.
- `jogadores.csv`: uma linha por jogador presente em cada quadro; vínculo ao quadro, lado observado, time, camisa, tipo, posição, velocidade, orientação, estado, recursos e contadores disponíveis na versão do log.
- `arbitragem.csv`: eventos registrados de modo de jogo e arbitragem, com tempo e lado envolvidos. Coordenadas, quando acrescentadas por associação a um quadro, precisam estar identificadas como associação.
- `placar.csv`: estados de placar registrados ao longo do jogo; manter separado do resumo final observado.
- `comandos_recebidos.csv`: comandos registrados no RCL, com ciclo/subciclo, origem, jogador ou Coach, nome do comando e argumentos. Ausência de RCL não produz contagens zero.
- `parametros.csv`: parâmetros do servidor e tipos de jogador, mantendo nome, grupo e valor originais.
- `resumo_times.csv` e `resumo_jogadores.csv`: agregações diretas com denominadores e cobertura; contadores de ações, recursos e distância observada em quadros consecutivos de jogo corrido. Reinícios de contador são reportados, sem transformar diferenças negativas em ações.
- `qualidade.csv`: verificações, gravidade, fonte/linha/quadro, problema e cobertura afetada.
- `dicionario_campos.csv`: tabela/campo, tipo, unidade, significado, origem e regra de ausência ou cálculo.

Todos os arquivos têm cabeçalhos estáveis, inclusive quando não há linhas. Cada linha de dados se vincula à sessão; linhas temporais também se vinculam ao quadro ou à posição no arquivo-fonte.

## Regras de limpeza e integridade

1. Esquema versionado, UTF-8, separador vírgula, ponto decimal e escape CSV padrão. Documentar a importação em Excel com seleção de codificação/separador.
2. Ausência vira campo vazio com motivo rastreável, nunca zero automático. Distinguir zero observado, fonte ausente, campo não suportado e valor inválido.
3. Preservar quadros durante interrupções. O ciclo isolado não é uma chave única: usar identificador de quadro e ordem de origem, incluindo subciclo quando existir no formato. Não remover registros só porque compartilham ciclo.
4. Preservar coordenadas originais. Uma futura normalização pela direção de ataque deve acrescentar colunas e considerar trocas de lado; não substituir a observação original.
5. Não corrigir nomes de times com aliases específicos da RCX. Conservar nomes anunciados e tratar diferenças para o cadastro como aviso.
6. Validar números finitos, tipos, camisas, campos obrigatórios e estrutura dos registros. Não descartar linhas inválidas silenciosamente. Publicar contagens e motivos de rejeição; erro estrutural que torne a leitura ambígua falha a extração.
7. Validar duração e cobertura usando parâmetros efetivamente registrados, incluindo jogos curtos, prorrogação e configurações diferentes. Não assumir 6000 ciclos. Distinguir término normal, interrupção e término indeterminado; ausência de um marcador isolado não resolve a classificação.
8. Associar RCG/RCL pelo contexto da sessão e evidências internas. Havendo múltiplos candidatos incompatíveis, indicar ambiguidade em vez de escolher o primeiro arquivo.
9. Reconhecer explicitamente versões suportadas. Começar com RCG textual ULG6 e o RCL produzido pelo servidor instalado; aceitar essas fontes em `.gz`. Outros formatos devem produzir indicação de incompatibilidade, sem exportação aparentemente válida.
10. Ler e exportar em fluxo para evitar manter todas as posições em memória. Só guardar o estado necessário para agregações e validação.

## Integração na interface

A aba de dados deve listar sessões e seus estados: aguardando encerramento, extraindo, concluída, concluída com avisos ou falhou. Cobertura da partida é uma informação separada do sucesso técnico da exportação: exportar um log parcial pode terminar com sucesso.

Ações: **Abrir CSVs**, **Reprocessar dados** e **Ver problemas da extração**. O botão de reprocessamento fica indisponível enquanto aquela sessão estiver gravando logs ou sendo extraída. O extrator não bloqueia a interface nem impede iniciar a próxima partida após a sessão anterior ser encerrada.

A identificação do processo de extração precisa ser separada dos processos da partida. **Encerrar sessão** não deve matar uma exportação da partida anterior. Ao fechar o Manager, cancelar ou aguardar a exportação de forma definida, conservando a última publicação válida. Uma tarefa interrompida deve poder ser retomada pelo reprocessamento.

## Etapas de implementação

1. Definir esquema, estados de qualidade e fixtures sintéticas para formatos e cobertura. Registrar unidades e chaves antes do parser.
2. Implementar leitor RCG/RCL e exportação básica por linha de comando, com publicação atômica e manifestos.
3. Acrescentar validação e resumos diretos. Conferir conservação de totais e denominadores entre tabelas.
4. Integrar gatilho automático ao término da sessão e acompanhamento assíncrono no Manager.
5. Adicionar a listagem de sessões, abertura da pasta e reprocessamento. Reconhecer sessões antigas que já tenham logs.
6. Validar com uma partida real curta completa, uma interrompida e uma exportação repetida. Atualizar README e testes.

## Critérios de aceite

- Uma partida iniciada pelo Manager termina e disponibiliza CSVs sem ação adicional.
- A exportação contém bola e jogadores de todos os quadros válidos observados, sem reduzir para amostras a cada 100 ciclos.
- Contagens, placares e identificadores são consistentes entre arquivos e fontes; lacunas ficam explícitas.
- Jogos curtos e interrompidos não são classificados pela duração padrão de competição.
- Um RCL ausente permite exportar o RCG com aviso. Versão desconhecida ou fonte ambígua não passa silenciosamente.
- Reprocessar as mesmas fontes com a mesma versão produz os mesmos dados, exceto metadados de execução.
- Falha, cancelamento e fechamento da janela não corrompem exportações anteriores ou logs originais.
- Testes incluem caminhos com espaços/apóstrofos, mensagens `say` com parênteses, quadros com mesmo ciclo, jogadores ausentes, campos inválidos, contador reiniciado, `.gz`, cabeçalhos sem linhas e saída parcial.
- A interface continua respondendo durante a exportação e informa onde encontrar dados e avisos.

## Depois da primeira versão

Implementar estimativas de controle, passes, progressão, entradas na área e finalizações em um módulo separado, usando os mesmos dados observados e registrando parâmetros e versão das regras. Isso permite reproduzir os gráficos com definições claras e reprocessar partidas quando as estimativas melhorarem.

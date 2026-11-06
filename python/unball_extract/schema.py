"""Esquema estável 1.0; unidades e regras constam no dicionário exportado."""
COUNTERS = ('kick', 'dash', 'turn', 'catch', 'move', 'turn_neck', 'change_view',
            'say', 'tackle', 'pointto', 'attentionto', 'change_focus')
COMMON = ['sessao_id']
SOURCE = ['fonte', 'linha']
TIME = ['quadro_id', 'ciclo', 'ordem_no_ciclo', 'modo']
TABLES = {
    'partida': COMMON + ['batch_id', 'batch_index', 'batch_total', 'time_l', 'time_r', 'time_selecionado_l', 'time_selecionado_r',
        'gols_l', 'gols_r', 'ciclo_inicial', 'ciclo_final', 'ciclos_registrados',
        'quadros', 'ciclos_play_on', 'ciclos_ausentes_intervalo', 'ciclos_ausentes_fronteira', 'ciclos_ausentes_inesperados', 'quadros_incompletos',
        'termino', 'cobertura', 'rcg_disponivel', 'rcl_disponivel'],
    'bola': COMMON + TIME + ['x', 'y', 'vx', 'vy'] + SOURCE,
    'jogadores': COMMON + TIME + ['lado', 'camisa', 'tipo', 'estado', 'x', 'y', 'vx', 'vy',
        'corpo', 'pescoco', 'stamina', 'effort', 'recovery', 'capacity'] +
        ['contador_' + name for name in COUNTERS] + ['campos_adicionais'] + SOURCE,
    'arbitragem': COMMON + ['ciclo', 'modo', 'lado'] + SOURCE,
    'placar': COMMON + ['ciclo', 'time_l', 'time_r', 'gols_l', 'gols_r', 'penaltis_adicionais'] + SOURCE,
    'comandos_recebidos': COMMON + ['ciclo', 'subciclo', 'time', 'jogador', 'comando', 'argumentos'] + SOURCE,
    'parametros': COMMON + ['grupo', 'tipo_jogador', 'parametro', 'valor'] + SOURCE,
    'resumo_jogadores': COMMON + ['lado', 'time', 'camisa', 'quadros_observados',
        'amostras_stamina_play_on', 'stamina_media_play_on', 'stamina_minima_play_on',
        'stamina_final_observada', 'distancia_observada_play_on_m', 'intervalos_distancia',
        'reinicios_contador'] + ['acoes_observadas_' + name for name in COUNTERS] + ['intervalos_contador_' + name for name in COUNTERS],
    'resumo_times': COMMON + ['lado', 'time', 'quadros', 'ciclos_play_on',
        'distancia_observada_play_on_m', 'intervalos_distancia', 'reinicios_contador'] +
        ['acoes_observadas_' + name for name in COUNTERS] + ['intervalos_contador_' + name for name in COUNTERS],
    'qualidade': COMMON + ['gravidade', 'codigo', 'detalhe'] + SOURCE,
    'dicionario_campos': ['tabela', 'campo', 'tipo', 'unidade', 'descricao', 'ausencia'],
}
DESCRIPTIONS = {
    'batch_id': 'Identificador do lote do Manager; vazio para partidas avulsas',
    'batch_index': 'Número do jogo dentro do lote, começando em 1',
    'batch_total': 'Quantidade de jogos solicitados no lote, incluindo os não iniciados se cancelado',
    'sessao_id': 'Identificador da pasta da sessão; não identifica sozinho o conteúdo das fontes',
    'quadro_id': 'Ordem do show no RCG; chave do quadro dentro da sessão',
    'ciclo': 'Relógio simulado registrado; pode se repetir durante interrupções',
    'ordem_no_ciclo': 'Índice de observação no mesmo ciclo, começando em zero; não é subciclo registrado',
    'subciclo': 'Relógio parado registrado no RCL; não inferido do RCG',
    'modo': 'Modo de jogo registrado pelo servidor',
    'estado': 'Bitmask original do estado do jogador; zero indica desabilitado',
    'linha': 'Linha física no arquivo fonte, contando cabeçalho',
    'fonte': 'Caminho relativo do log dentro da sessão',
    'campos_adicionais': 'S-expressions originais dos campos opcionais não tabulados, em JSON',
    'distancia_observada_play_on_m': 'Soma de deslocamentos entre ciclos consecutivos play_on, com jogador habilitado nos dois quadros; não interpola lacunas',
    'intervalos_distancia': 'Quantidade de intervalos usados na distância; denominador de cobertura',
    'ciclos_play_on': 'Quantidade de ciclos distintos observados em play_on',
    'ciclos_ausentes_intervalo': 'Ciclos não registrados entre primeiro e último show; não mede duração esperada',
    'ciclos_ausentes_fronteira': 'Lacunas de um ciclo exatamente na troca de tempo normal, confirmadas pelos parâmetros e kick_off do tempo seguinte; não interpoladas',
    'ciclos_ausentes_inesperados': 'Lacunas internas excluindo apenas fronteiras de tempo confirmadas',
    'termino': 'normal: time_over observado; interrompido: Manager solicitou parada; indeterminado: sem evidência suficiente',
    'cobertura': 'completa exige término normal, parâmetros, início, duração mínima e ausência de lacunas inesperadas/incompletude; parcial ou indeterminada nos demais casos',
    'reinicios_contador': 'Quantidade de quedas em contadores acumulados, sem somar diferenças negativas',
    'gols_l': 'Gols do lado l no estado de placar observado; em partida.csv é o último estado',
    'gols_r': 'Gols do lado r no estado de placar observado; em partida.csv é o último estado',
    'argumentos': 'Argumentos do comando, preservando estrutura e strings; recebimento não comprova execução',
}


def describe(table, field):
    unit = 'm' if field in ('x', 'y', 'distancia_observada_play_on_m') else 'm/ciclo' if field in ('vx', 'vy') else 'graus' if field in ('corpo', 'pescoco') else ''
    description = DESCRIPTIONS.get(field, field.replace('_', ' '))
    kind = 'texto'
    if field.startswith('contador_'):
        description = 'Contador acumulado registrado de ' + field[9:] + '; não equivale a sucesso'
        kind = 'inteiro'
    elif field.startswith('acoes_observadas_'):
        description = 'Soma de deltas não negativos entre observações do contador ' + field[len('acoes_observadas_'):]  + '; baseline inicial excluído; cobertura pode ser parcial'
        kind = 'inteiro'
    elif field.startswith('intervalos_contador_'):
        description = 'Quantidade de pares observados com contador disponível; inclui pares com reinício, cujos deltas negativos são excluídos'
        kind = 'inteiro'
    elif field in ('x', 'y', 'vx', 'vy', 'corpo', 'pescoco', 'stamina', 'effort', 'recovery', 'capacity') or field.startswith(('stamina_', 'distancia_')):
        kind = 'numero'
    elif field in ('ciclo', 'subciclo', 'quadro_id', 'linha', 'camisa', 'tipo', 'estado', 'gols_l', 'gols_r', 'ordem_no_ciclo', 'batch_index', 'batch_total') or field.startswith(('quadros', 'ciclos_', 'intervalos_', 'amostras_', 'reinicios_')):
        kind = 'inteiro'
    return dict(tabela=table + '.csv', campo=field, tipo=kind, unidade=unit,
                descricao=description, ausencia='Campo vazio = indisponível/não calculável; consulte qualidade.csv e manifest.json')

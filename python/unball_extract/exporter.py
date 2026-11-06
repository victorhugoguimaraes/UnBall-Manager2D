"""Leitura em fluxo e publicação atômica; somente biblioteca padrão."""
from contextlib import ExitStack
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
import csv
import fcntl
import gzip
import hashlib
import json
import math
import os
import re
import shutil
import tempfile
import uuid

from . import __version__
from .schema import COUNTERS, TABLES, describe
from .sexpr import parse_many, render


class ExportError(ValueError):
    pass


def read_json(path):
    result = json.loads(path.read_text(encoding='utf-8')) if path.exists() else {}
    if not isinstance(result, dict):
        raise ExportError(f'{path.name}: esperado um objeto JSON')
    return result


def atomic_json(path, value):
    descriptor, name = tempfile.mkstemp(prefix='.' + path.name, dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8') as output:
            json.dump(value, output, ensure_ascii=False, indent=2, allow_nan=False)
            output.write('\n')
            output.flush()
            os.fsync(output.fileno())
        os.replace(name, path)
    finally:
        Path(name).unlink(missing_ok=True)


def digest(path, decompressed=False):
    opener = gzip.open if decompressed and path.suffix == '.gz' else open
    with opener(path, 'rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest() if hasattr(hashlib, 'file_digest') else _digest(stream)


def _digest(stream):
    result = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b''):
        result.update(block)
    return result.hexdigest()


def text_lines(path):
    opener = gzip.open if path.suffix == '.gz' else open
    with opener(path, 'rt', encoding='utf-8', errors='strict') as stream:
        yield from enumerate(stream, 1)


def number(value, integer=False):
    if not isinstance(value, str):
        raise ExportError('Número inválido')
    if integer:
        if not re.fullmatch(r'-?\d+', value):
            raise ExportError('Inteiro inválido: ' + value)
        return int(value)
    result = float(value)
    if not math.isfinite(result):
        raise ExportError('Número não finito: ' + value)
    return result


def counter(value):
    result = number(value, True)
    if result < 0:
        raise ExportError('Contador negativo')
    return result


def nonnegative(value):
    return counter(value)


class Tables:
    def __init__(self, directory, session_id, stack):
        self.session_id, self.counts, self.writers = session_id, Counter(), {}
        for table, fields in TABLES.items():
            output = stack.enter_context((directory / (table + '.csv')).open('w', newline='', encoding='utf-8'))
            writer = csv.DictWriter(output, fields, lineterminator='\n')
            writer.writeheader()
            self.writers[table] = writer
        self.warnings = 0

    def write(self, table, **row):
        if 'sessao_id' in TABLES[table]:
            row['sessao_id'] = self.session_id
        self.writers[table].writerow(row)
        self.counts[table] += 1

    def warn(self, code, detail, source='', line=''):
        self.warnings += 1
        self.write('qualidade', gravidade='aviso', codigo=code, detalhe=detail, fonte=source, linha=line)


class Reader:
    def __init__(self, tables, session):
        self.tables, self.session = tables, session
        self.params, self.names, self.score = {}, {'l': '', 'r': ''}, (None, None)
        self.mode, self.last_mode = 'unknown', ''
        self.first, self.last, self.frames = None, None, 0
        self.cycle_count, self.gaps, self.boundary_gaps, self.incomplete = 0, 0, 0, 0
        self.play_cycles = 0
        self.previous_play_cycle = None
        self.previous, self.players = {}, {}
        self.same_cycle, self.notices = 0, set()
        self.source = ''
        self.unknown_records = Counter()

    def warn_once(self, code, detail, line):
        if code not in self.notices:
            self.tables.warn(code, detail, self.source, line)
            self.notices.add(code)

    def read_rcg(self, path):
        self.source = str(path.relative_to(self.session))
        lines = text_lines(path)
        if next(lines, (0, ''))[1].strip() != 'ULG6':
            raise ExportError('Formato RCG não suportado; esta versão exige ULG6 textual')
        for line_number, line in lines:
            if not line.strip():
                continue
            try:
                records = parse_many(line)
                if len(records) != 1 or not records[0]:
                    raise ExportError('Esperado um registro por linha do RCG')
                record = records[0]
                kind = record[0]
                if kind in ('server_param', 'player_param', 'player_type'):
                    type_id = next((item[1] for item in record[1:] if isinstance(item, list) and len(item) == 2 and item[0] == 'id'), '')
                    for item in record[1:]:
                        if not isinstance(item, list) or len(item) < 2:
                            raise ExportError('Parâmetro malformado')
                        value = item[1] if len(item) == 2 else ' '.join(map(render, item[1:]))
                        if len(item) > 2:
                            self.warn_once('parametro_textual_composto', 'Parâmetro textual exportado como sequência original de tokens; o servidor não escapou todas as aspas', line_number)
                        self.tables.write('parametros', grupo=kind, tipo_jogador=type_id if kind == 'player_type' else '',
                                          parametro=item[0], valor=value, fonte=self.source, linha=line_number)
                        if kind == 'server_param':
                            self.params[item[0]] = value
                elif kind == 'team':
                    if len(record) < 6:
                        raise ExportError('Placar incompleto')
                    cycle = nonnegative(record[1])
                    names = {'l': str(record[2]), 'r': str(record[3])}
                    if any(self.names[side] and self.names[side] != name for side, name in names.items()):
                        self.warn_once('nome_alterado', 'Nome do time mudou no RCG; use a linha de placar para interpretar a origem', line_number)
                    self.names = names
                    self.score = (nonnegative(record[4]), nonnegative(record[5]))
                    self.tables.write('placar', ciclo=cycle, time_l=self.names['l'], time_r=self.names['r'],
                                      gols_l=self.score[0], gols_r=self.score[1], penaltis_adicionais=json.dumps(record[6:]), fonte=self.source, linha=line_number)
                elif kind == 'playmode':
                    if len(record) != 3:
                        raise ExportError('Modo incompleto')
                    self.mode = self.last_mode = str(record[2])
                    side = self.mode[-1] if self.mode.endswith(('_l', '_r')) else ''
                    self.tables.write('arbitragem', ciclo=nonnegative(record[1]), modo=self.mode, lado=side, fonte=self.source, linha=line_number)
                elif kind == 'show':
                    self.frame(record, line_number)
                elif kind not in ('msg', 'draw'):
                    self.unknown_records[str(kind)] += 1
            except (ValueError, TypeError, IndexError) as error:
                raise ExportError(f'{self.source}:{line_number}: {error}') from error
        if self.frames == 0:
            raise ExportError('RCG sem quadros show')
        for kind, count in self.unknown_records.items():
            self.tables.warn('registro_nao_exportado', f'{kind}: {count} registro(s)', self.source)

    def player(self, item):
        if len(item) < 9 or len(item[0]) != 2:
            raise ExportError('Jogador sem campos obrigatórios')
        side, uniform = item[0]
        uniform = nonnegative(uniform)
        if side not in ('l', 'r') or not 1 <= uniform <= 11:
            raise ExportError('Lado/camisa inválido')
        state = int(item[2], 0) if item[2].startswith('0x') else nonnegative(item[2])
        if state < 0:
            raise ExportError('Estado negativo')
        values = dict(lado=side, camisa=uniform, tipo=nonnegative(item[1]), estado=state)
        values.update(zip(('x', 'y', 'vx', 'vy', 'corpo', 'pescoco'), map(number, item[3:9])))
        extras = {}
        for field in item[9:]:
            if not isinstance(field, list) or not field or not isinstance(field[0], str):
                raise ExportError('Campo opcional inválido')
            if field[0] in extras:
                raise ExportError('Campo opcional duplicado')
            extras[field[0]] = field[1:]
        if 's' in extras:
            if len(extras['s']) not in (3, 4):
                raise ExportError('Recursos do jogador inválidos')
            values.update(zip(('stamina', 'effort', 'recovery', 'capacity'), map(number, extras.pop('s'))))
        if 'c' in extras:
            counters = list(map(counter, extras.pop('c')))
            values.update(zip(('contador_' + name for name in COUNTERS), counters))
            if len(counters) > len(COUNTERS):
                extras['contadores_adicionais'] = counters[len(COUNTERS):]
        values['campos_adicionais'] = json.dumps(extras, ensure_ascii=False, separators=(',', ':'))
        return values

    def frame(self, record, line):
        cycle = nonnegative(record[1])
        if self.last is not None and cycle < self.last:
            raise ExportError('Relógio do RCG retrocedeu; associação temporal ambígua')
        items = record[2:]
        balls = [item for item in items if isinstance(item, list) and item and item[0] == ['b']]
        if len(balls) != 1 or len(balls[0]) != 5:
            raise ExportError('Bola ausente ou duplicada no quadro')
        ball = list(map(number, balls[0][1:]))
        current = {}
        rejected = 0
        for item in items:
            if item is balls[0]:
                continue
            try:
                p = self.player(item)
                key = (p['lado'], p['camisa'])
                if key in current:
                    raise ExportError('Jogador duplicado no quadro')
                current[key] = p
            except (ValueError, TypeError, IndexError) as error:
                rejected += 1
                self.tables.warn('jogador_rejeitado', str(error), self.source, line)
        if rejected or len(current) != 22 or (self.mode == 'play_on' and any(not p['estado'] & (1 | 0x80000) for p in current.values())):
            self.incomplete += 1
        if cycle != self.last:
            self.cycle_count += 1
            if self.last is not None:
                self.gaps += cycle - self.last - 1
                # RCSS may omit the exact halftime show. Accept only one missing cycle,
                # at a configured normal-half boundary with a new kickoff recorded.
                if cycle == self.last + 2 and self.mode in ('kick_off_l', 'kick_off_r'):
                    try:
                        half = number(self.params['half_time']) * 1000 / number(self.params['simulator_step'])
                        halves = number(self.params['nr_normal_halfs'], True)
                        boundary = self.last + 1
                        if half > 0 and half.is_integer() and 0 < boundary < half * halves and boundary % half == 0:
                            self.boundary_gaps += 1
                    except (KeyError, ValueError, ZeroDivisionError):
                        pass
            self.same_cycle = 0
        else:
            self.same_cycle += 1
        if self.mode == 'play_on' and cycle != self.previous_play_cycle:
            self.play_cycles += 1
            self.previous_play_cycle = cycle
        self.frames += 1
        time = dict(quadro_id=self.frames, ciclo=cycle, ordem_no_ciclo=self.same_cycle, modo=self.mode,
                    fonte=self.source, linha=line)
        self.tables.write('bola', **time, **dict(zip(('x', 'y', 'vx', 'vy'), ball)))
        for key, p in current.items():
            self.tables.write('jogadores', **time, **p)
            aggregate = self.players.setdefault(key, dict(quadros=0, samples=0, total=0.0, minimum=None,
                final=None, distance=0.0, intervals=0, resets=0, actions=Counter(), measured=set(), counter_intervals=Counter()))
            aggregate['quadros'] += 1
            aggregate['final'] = p.get('stamina')
            if self.mode == 'play_on' and p['estado'] & 1 and 'stamina' in p:
                aggregate['samples'] += 1
                aggregate['total'] += p['stamina']
                aggregate['minimum'] = p['stamina'] if aggregate['minimum'] is None else min(aggregate['minimum'], p['stamina'])
            previous = self.previous.get(key)
            if previous:
                old, old_cycle, old_mode = previous
                if cycle == old_cycle + 1 and self.mode == old_mode == 'play_on' and p['estado'] & old['estado'] & 1:
                    aggregate['distance'] += math.hypot(p['x'] - old['x'], p['y'] - old['y'])
                    aggregate['intervals'] += 1
                for name in COUNTERS:
                    field = 'contador_' + name
                    if field not in p or field not in old:
                        continue
                    aggregate['measured'].add(name)
                    aggregate['counter_intervals'][name] += 1
                    delta = p[field] - old[field]
                    if delta < 0:
                        aggregate['resets'] += 1
                    else:
                        aggregate['actions'][name] += delta
            if 'stamina' not in p or 'contador_kick' not in p:
                self.warn_once('campo_opcional_ausente', 'Há observações sem recursos ou contadores; campos ficam vazios e resumos usam só dados disponíveis', line)
        self.previous = {key: (p, cycle, self.mode) for key, p in current.items()}
        self.first = cycle if self.first is None else self.first
        self.last = cycle

    def summaries(self):
        for (side, uniform), data in sorted(self.players.items()):
            self.tables.write('resumo_jogadores', lado=side, time=self.names[side], camisa=uniform,
                quadros_observados=data['quadros'], amostras_stamina_play_on=data['samples'],
                stamina_media_play_on=data['total'] / data['samples'] if data['samples'] else None,
                stamina_minima_play_on=data['minimum'], stamina_final_observada=data['final'],
                distancia_observada_play_on_m=data['distance'] if data['intervals'] else None,
                intervalos_distancia=data['intervals'], reinicios_contador=data['resets'],
                **{'acoes_observadas_' + name: data['actions'][name] if name in data['measured'] else None for name in COUNTERS},
                **{'intervalos_contador_' + name: data['counter_intervals'][name] for name in COUNTERS})
        for side in ('l', 'r'):
            own = [value for key, value in self.players.items() if key[0] == side]
            if not own:
                continue
            intervals = sum(p['intervals'] for p in own)
            self.tables.write('resumo_times', lado=side, time=self.names[side], quadros=self.frames,
                ciclos_play_on=self.play_cycles, distancia_observada_play_on_m=sum(p['distance'] for p in own) if intervals else None,
                intervalos_distancia=intervals, reinicios_contador=sum(p['resets'] for p in own),
                **{'acoes_observadas_' + name: sum(p['actions'][name] for p in own) if len(own) == 11 and all(name in p['measured'] for p in own) else None for name in COUNTERS},
                **{'intervalos_contador_' + name: sum(p['counter_intervals'][name] for p in own) for name in COUNTERS})
        resets = sum(p['resets'] for p in self.players.values())
        if resets:
            self.tables.warn('reinicios_contador', f'{resets} quedas de contadores observadas; deltas negativos excluídos', self.source)

    def read_rcl(self, path):
        source = str(path.relative_to(self.session))
        received = re.compile(r'^(\d+),(\d+)\s+Recv (.+)_(\d+|Coach):\s*(.*)$')
        known = 0
        for line_number, line in text_lines(path):
            match = received.match(line)
            if not match:
                if 'Recv ' in line:
                    self.tables.warn('comando_rejeitado', 'Registro Recv sem origem/tempo reconhecido', source, line_number)
                continue
            cycle, stopped, team, uniform, body = match.groups()
            try:
                records = parse_many(body)
                if not records:
                    raise ExportError('Comando vazio')
                for item in records:
                    if not item or not isinstance(item[0], str):
                        raise ExportError('Nome de comando inválido')
                    self.tables.write('comandos_recebidos', ciclo=int(cycle), subciclo=int(stopped),
                        time=team, jogador=uniform, comando=item[0], argumentos=' '.join(map(render, item[1:])),
                        fonte=source, linha=line_number)
                    known += 1
            except ValueError as error:
                self.tables.warn('comando_rejeitado', str(error), source, line_number)
        if known == 0:
            self.tables.warn('rcl_sem_comandos', 'Nenhum comando Recv reconhecido; verificar formato do RCL', source)

    def finish(self, metadata, lifecycle, has_rcg, has_rcl):
        ending = 'normal' if self.last_mode == 'time_over' else 'interrompido' if lifecycle.get('stop_requested') else 'indeterminado'
        expected = None
        try:
            expected = number(self.params['half_time']) * 1000 / number(self.params['simulator_step']) * number(self.params['nr_normal_halfs'], True)
            if expected <= 0:
                expected = None
        except (KeyError, ValueError, ZeroDivisionError):
            pass
        coverage = 'indeterminada'
        if has_rcg:
            coverage = 'completa' if (ending == 'normal' and expected is not None and self.first <= 1 and
                self.last >= expected - 1 and self.gaps == self.boundary_gaps and not self.incomplete and all(self.names.values()) and
                all(score is not None for score in self.score)) else 'parcial'
        if coverage != 'completa':
            self.tables.warn('cobertura_' + coverage, f'Término={ending}; quadros incompletos={self.incomplete}; lacunas internas={self.gaps}; duração normal mínima={expected}')
        if self.gaps > self.boundary_gaps:
            self.tables.warn('ciclos_ausentes', f'{self.gaps - self.boundary_gaps} ciclos sem quadro fora das fronteiras confirmadas', self.source)
        if self.boundary_gaps:
            self.tables.write('qualidade', gravidade='informacao', codigo='fronteira_de_tempo', detalhe=f'{self.boundary_gaps} ciclos sem show na troca de tempo, confirmados por parâmetros e kick_off; não interpolados', fonte=self.source)
        if self.incomplete:
            self.tables.warn('quadros_incompletos', f'{self.incomplete} quadros sem todos os jogadores esperados/habilitados em play_on', self.source)
        for side, field in (('l', 'leftTeam'), ('r', 'rightTeam')):
            if self.names[side] and metadata.get(field) and self.names[side] != metadata[field]:
                self.tables.warn('nome_diferente_cadastro', f'{side}: cadastro={metadata[field]}, anunciado={self.names[side]}')
        self.tables.write('partida', time_l=self.names['l'], time_r=self.names['r'],
            time_selecionado_l=metadata.get('leftTeam'), time_selecionado_r=metadata.get('rightTeam'),
            gols_l=self.score[0], gols_r=self.score[1], ciclo_inicial=self.first, ciclo_final=self.last,
            ciclos_registrados=self.cycle_count if has_rcg else None, quadros=self.frames if has_rcg else None,
            ciclos_play_on=self.play_cycles if has_rcg else None, ciclos_ausentes_intervalo=self.gaps if has_rcg else None,
            ciclos_ausentes_fronteira=self.boundary_gaps if has_rcg else None,
            ciclos_ausentes_inesperados=self.gaps - self.boundary_gaps if has_rcg else None,
            quadros_incompletos=self.incomplete if has_rcg else None, termino=ending, cobertura=coverage,
            rcg_disponivel=int(has_rcg), rcl_disponivel=int(has_rcl))
        return coverage, ending


def export_session(session):
    session = Path(session).resolve(strict=True)
    if not session.is_dir():
        raise ExportError('Selecione uma pasta de sessão')
    runtime = session / 'runtime' if (session / 'runtime').is_dir() else session
    lifecycle = read_json(runtime / 'session-state.json')
    if lifecycle.get('active') and lifecycle.get('pid'):
        try:
            os.kill(int(lifecycle['pid']), 0)
        except ProcessLookupError:
            pass
        else:
            raise ExportError('A sessão ainda está gravando logs')
    exports = session / 'exports'
    exports.mkdir(exist_ok=True)
    # A kernel-owned lock survives neither a crash nor cancellation; stale files are harmless.
    with (exports / '.extraction.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise ExportError('Já existe uma extração nesta sessão') from error
        return _export_locked(session, exports, lifecycle)


def _export_locked(session, exports, lifecycle):
    logs = session / 'logs'
    candidates = sorted(path for path in logs.iterdir() if path.is_file() and any(path.name.endswith(ext) for ext in ('.rcg', '.rcg.gz', '.rcl', '.rcl.gz'))) if logs.is_dir() else []
    if not candidates:
        raise ExportError('Nenhum RCG ou RCL encontrado na pasta logs da sessão')
    if any(path.is_symlink() for path in candidates):
        raise ExportError('Logs vinculados por symlink não são aceitos; copie os arquivos para a sessão')
    fingerprints = {str(path.relative_to(session)): dict(sha256=digest(path), bytes=path.stat().st_size) for path in candidates}
    selected, duplicates = {}, []
    for kind in ('rcg', 'rcl'):
        group = [p for p in candidates if p.name.removesuffix('.gz').endswith('.' + kind)]
        if group:
            if len({digest(path, True) for path in group}) != 1:
                raise ExportError(f'Múltiplos {kind.upper()} diferentes; separe cada partida em uma sessão')
            selected[kind] = group[0]
            duplicates.extend(group[1:])
    if len(selected) == 2 and selected['rcg'].name.removesuffix('.gz').removesuffix('.rcg') != selected['rcl'].name.removesuffix('.gz').removesuffix('.rcl'):
        raise ExportError('RCG e RCL têm identificadores de jogo diferentes')
    metadata = read_json(session / 'match.json')
    versions = exports / 'versions'
    versions.mkdir(exist_ok=True)
    temp = Path(tempfile.mkdtemp(prefix='.partial-', dir=versions))
    try:
        csv_directory = temp / 'csv'
        csv_directory.mkdir()
        with ExitStack() as stack:
            tables = Tables(csv_directory, session.name, stack)
            reader = Reader(tables, session)
            for kind in ('rcg', 'rcl'):
                if kind in selected:
                    getattr(reader, 'read_' + kind)(selected[kind])
                else:
                    tables.warn('fonte_ausente', kind.upper() + ' indisponível; tabelas dependentes ficam vazias')
            for path in duplicates:
                tables.warn('fonte_duplicada', 'Conteúdo idêntico; apenas uma cópia foi processada', str(path.relative_to(session)))
            reader.summaries()
            coverage, ending = reader.finish(metadata, lifecycle, 'rcg' in selected, 'rcl' in selected)
            for table, fields in TABLES.items():
                for field in fields:
                    tables.write('dicionario_campos', **describe(table, field))
        if any(digest(session / path) != info['sha256'] for path, info in fingerprints.items()):
            raise ExportError('As fontes mudaram durante a extração; tente novamente após encerrar a sessão')
        if tables.counts['bola'] != reader.frames or tables.counts['resumo_jogadores'] != len(reader.players):
            raise ExportError('Falha na conservação de linhas exportadas')
        manifest = dict(layout_version=2, schema_version='1.0', extractor_version=__version__, session_id=session.name,
            generated_at=datetime.now(timezone.utc).isoformat(), sources=fingerprints,
            formats={'rcg': 'ULG6' if 'rcg' in selected else None, 'rcl': 'Recv ciclo,subciclo' if 'rcl' in selected else None},
            selected_sources={kind: str(path.relative_to(session)) for kind, path in selected.items()},
            counts=dict(tables.counts), warnings=tables.warnings, coverage=coverage, ending=ending,
            status='completed_with_warnings' if tables.warnings else 'completed',
            files={str(p.relative_to(temp)): dict(sha256=digest(p), bytes=p.stat().st_size) for p in sorted(csv_directory.glob('*.csv'))})
        atomic_json(temp / 'manifest.json', manifest)
        destination = versions / (datetime.now().astimezone().strftime('%Y-%m-%d_%H-%M-%S') + '_' + uuid.uuid4().hex[:8])
        temp.rename(destination)
        result = dict(layout_version=2, export_directory=str(destination.relative_to(exports)), status=manifest['status'], coverage=coverage,
                      warnings=tables.warnings, generated_at=manifest['generated_at'])
        atomic_json(exports / 'latest.json', result)
        return result
    finally:
        if temp.exists():
            shutil.rmtree(temp)

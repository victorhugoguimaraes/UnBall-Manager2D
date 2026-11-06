import csv
import gzip
import json
import os
from pathlib import Path
import tempfile
import unittest

from unball_extract.exporter import export_session, ExportError
from unball_extract.sexpr import parse_many


def frame(cycle, x=0, counter=0, missing=False):
    players = []
    for side in ('l', 'r'):
        for uniform in range(1, 12):
            if missing and side == 'r' and uniform == 11:
                continue
            players.append(f'(({side} {uniform}) 0 0x1 {x} 0 0 0 0 0 (s 8000 1 1 10000) (c {counter} 0 0 0 0 0 0 0 0 0 0 0))')
    return f'(show {cycle} ((b) 0 0 0 0) ' + ' '.join(players) + ')\n'


def log(*, partial=False, gap=False, missing=False):
    text = ('ULG6\n(server_param (half_time 0.1)(simulator_step 100)(nr_normal_halfs 2))\n'
            '(team 0 Left Right 0 0)\n(playmode 0 play_on)\n' + frame(0))
    if not gap:
        text += frame(1, 1, 1) + frame(1, 2, 2)
    text += frame(2, 3, 0, missing=missing)
    if not partial:
        text += '(playmode 2 time_over)\n(team 2 Left Right 1 0)\n'
    return text


class ExtractionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="unball dados d'água ")
        self.addCleanup(self.temp.cleanup)
        self.session = Path(self.temp.name) / 'session'
        (self.session / 'logs').mkdir(parents=True)
        self.rcg = self.session / 'logs/game.rcg'
        self.rcl = self.session / 'logs/game.rcl'
        self.rcg.write_text(log(), encoding='utf-8')
        self.rcl.write_text('1,2 Recv Left_1: (say "(kick 100 0)")(dash 70)\n', encoding='utf-8')

    def output(self, result):
        return self.session / 'exports' / result['export_directory'] / 'csv'

    def rows(self, output, table):
        with (output / (table + '.csv')).open(newline='', encoding='utf-8') as stream:
            return list(csv.DictReader(stream))

    def test_full_stream_preserves_repeated_cycle_and_reconciles(self):
        original = self.rcg.read_bytes()
        result = export_session(self.session)
        output = self.output(result)
        self.assertEqual(result['coverage'], 'completa')
        self.assertEqual(self.rcg.read_bytes(), original)
        self.assertEqual(len(self.rows(output, 'bola')), 4)
        self.assertEqual(len(self.rows(output, 'jogadores')), 88)
        self.assertEqual([r['ordem_no_ciclo'] for r in self.rows(output, 'bola')], ['0', '0', '1', '0'])
        player = self.rows(output, 'resumo_jogadores')[0]
        self.assertEqual(float(player['distancia_observada_play_on_m']), 2)
        self.assertEqual(player['acoes_observadas_kick'], '2')
        self.assertEqual(player['reinicios_contador'], '1')
        self.assertEqual([r['comando'] for r in self.rows(output, 'comandos_recebidos')], ['say', 'dash'])
        self.assertEqual(self.rows(output, 'comandos_recebidos')[0]['argumentos'], '"(kick 100 0)"')
        self.assertEqual(self.rows(output, 'partida')[0]['gols_l'], '1')

    def test_partial_missing_rcl_is_not_zero_and_has_headers(self):
        self.rcg.write_text(log(partial=True, gap=True, missing=True), encoding='utf-8')
        self.rcl.unlink()
        (self.session / 'session-state.json').write_text('{"stop_requested":true}')
        output = self.output(export_session(self.session))
        game = self.rows(output, 'partida')[0]
        self.assertEqual(game['cobertura'], 'parcial')
        self.assertEqual(game['termino'], 'interrompido')
        self.assertEqual(game['ciclos_ausentes_intervalo'], '1')
        self.assertEqual(self.rows(output, 'comandos_recebidos'), [])
        self.assertIn('subciclo', (output / 'comandos_recebidos.csv').read_text())
        self.assertIn('quadros_incompletos', (output / 'qualidade.csv').read_text())

    def test_reprocess_identical_csvs_and_failures_keep_latest(self):
        first = self.output(export_session(self.session))
        second_result = export_session(self.session)
        second = self.output(second_result)
        for csv_file in first.glob('*.csv'):
            self.assertEqual(csv_file.read_bytes(), (second / csv_file.name).read_bytes(), csv_file.name)
        latest = (self.session / 'exports/latest.json').read_bytes()
        self.rcg.write_text('ULG99\n', encoding='utf-8')
        with self.assertRaises(ExportError):
            export_session(self.session)
        self.assertEqual((self.session / 'exports/latest.json').read_bytes(), latest)
        self.assertFalse(list((self.session / 'exports').rglob('.partial-*')))
        self.assertTrue((second.parent / 'manifest.json').exists())

    def test_gzip_and_ambiguity(self):
        with gzip.open(self.rcg.with_suffix('.rcg.gz'), 'wt', encoding='utf-8') as output:
            output.write(self.rcg.read_text())
        self.rcg.unlink()
        self.assertEqual(export_session(self.session)['coverage'], 'completa')
        self.rcg.write_text(log(partial=True), encoding='utf-8')
        with self.assertRaisesRegex(ExportError, 'Múltiplos'):
            export_session(self.session)

    def test_active_session_and_export_lock_are_rejected(self):
        import fcntl
        (self.session / 'session-state.json').write_text(json.dumps({'active': True, 'pid': os.getpid()}))
        with self.assertRaisesRegex(ExportError, 'gravando'):
            export_session(self.session)
        (self.session / 'session-state.json').unlink()
        exports = self.session / 'exports'
        exports.mkdir()
        with (exports / '.extraction.lock').open('a') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            with self.assertRaisesRegex(ExportError, 'Já existe'):
                export_session(self.session)

    def test_nonfinite_ball_and_malformed_records_fail_without_publish(self):
        for bad in (log().replace('((b) 0 0 0 0)', '((b) nan 0 0 0)'), log() + '(show 3 ('):
            self.rcg.write_text(bad, encoding='utf-8')
            with self.assertRaises(ExportError):
                export_session(self.session)
            self.assertFalse((self.session / 'exports/latest.json').exists())

    def test_rcl_only_and_rejected_players(self):
        self.rcg.unlink()
        result = export_session(self.session)
        self.assertEqual(result['coverage'], 'indeterminada')
        output = self.output(result)
        self.assertEqual(self.rows(output, 'bola'), [])
        self.assertEqual(self.rows(output, 'partida')[0]['gols_l'], '')
        self.rcg.write_text(log().replace('((l 1) 0 0x1 0', '((l 1) 0 0x1 nan'), encoding='utf-8')
        output = self.output(export_session(self.session))
        self.assertIn('jogador_rejeitado', (output / 'qualidade.csv').read_text())

    def test_missing_counters_remain_empty_in_summaries(self):
        import re
        self.rcg.write_text(re.sub(r'\(c [^()]*\)', '', log()), encoding='utf-8')
        output = self.output(export_session(self.session))
        player = self.rows(output, 'resumo_jogadores')[0]
        self.assertEqual(player['acoes_observadas_kick'], '')
        self.assertEqual(player['intervalos_contador_kick'], '0')
        self.assertEqual(self.rows(output, 'resumo_times')[0]['acoes_observadas_kick'], '')
        self.assertIn('campo_opcional_ausente', (output / 'qualidade.csv').read_text())

    def test_pair_mismatch_and_metadata_shape_are_rejected(self):
        self.rcl.rename(self.rcl.with_name('another.rcl'))
        with self.assertRaisesRegex(ExportError, 'identificadores'):
            export_session(self.session)
        self.rcl.with_name('another.rcl').rename(self.rcl)
        (self.session / 'match.json').write_text('[]')
        with self.assertRaisesRegex(ExportError, 'objeto JSON'):
            export_session(self.session)

    def test_halftime_boundary_requires_configured_time_and_kickoff(self):
        text = ('ULG6\n(server_param (half_time 0.2)(simulator_step 100)(nr_normal_halfs 2))\n'
                '(team 0 Left Right 0 0)\n(playmode 0 play_on)\n' + frame(0) + frame(1) +
                '(playmode 3 kick_off_r)\n' + frame(3) + frame(4) + '(playmode 4 time_over)\n')
        self.rcg.write_text(text, encoding='utf-8')
        output = self.output(export_session(self.session))
        game = self.rows(output, 'partida')[0]
        self.assertEqual(game['cobertura'], 'completa')
        self.assertEqual(game['ciclos_ausentes_fronteira'], '1')
        self.assertEqual(game['ciclos_ausentes_inesperados'], '0')
        self.rcg.write_text(text.replace('kick_off_r', 'play_on'), encoding='utf-8')
        self.assertEqual(export_session(self.session)['coverage'], 'parcial')

    def test_cancelled_export_keeps_previous_bundle_and_cleans_temporary(self):
        from unittest.mock import patch
        first = export_session(self.session)
        latest = (self.session / 'exports/latest.json').read_bytes()
        with patch('unball_extract.exporter.Reader.read_rcg', side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                export_session(self.session)
        self.assertEqual((self.session / 'exports/latest.json').read_bytes(), latest)
        self.assertTrue((self.output(first).parent / 'manifest.json').exists())
        self.assertFalse(list((self.session / 'exports').rglob('.partial-*')))

    def test_runtime_state_and_contained_csv_layout(self):
        runtime = self.session / 'runtime'
        runtime.mkdir()
        (runtime / 'session-state.json').write_text(json.dumps({'active': True, 'pid': os.getpid()}))
        with self.assertRaisesRegex(ExportError, 'gravando'):
            export_session(self.session)
        (runtime / 'session-state.json').write_text('{"active":false}')
        result = export_session(self.session)
        csv_folder = self.output(result)
        self.assertEqual(len(list(csv_folder.glob('*.csv'))), 11)
        self.assertFalse(list(csv_folder.parent.glob('*.csv')))
        manifest = json.loads((csv_folder.parent / 'manifest.json').read_text())
        self.assertEqual(manifest['layout_version'], 2)
        self.assertIn('csv/bola.csv', manifest['files'])

    def test_structure_of_say_and_unbalanced_parentheses(self):
        self.assertEqual(len(parse_many('(say "escaped \\\" (kick 1 2)")(dash 3)')), 2)
        with self.assertRaises(ValueError):
            parse_many('(say "unfinished)')


if __name__ == '__main__':
    unittest.main()

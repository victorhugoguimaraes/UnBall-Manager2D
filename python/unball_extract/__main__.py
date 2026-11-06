import argparse
import json
import sys
from .exporter import export_session


def main():
    parser = argparse.ArgumentParser(description='Exporta logs de uma sessão UnBall Manager para CSVs validados')
    parser.add_argument('--session', required=True)
    arguments = parser.parse_args()
    try:
        print(json.dumps(export_session(arguments.session), ensure_ascii=False), flush=True)
    except (OSError, ValueError, TypeError, KeyError) as error:
        print(json.dumps({'status': 'failed', 'error': str(error)}, ensure_ascii=False), file=sys.stderr, flush=True)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())

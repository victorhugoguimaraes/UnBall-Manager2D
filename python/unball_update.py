"""Atualização local do Manager a partir de uma revisão pública fixa do GitHub."""
import argparse
import fcntl
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

REPOSITORY = 'victorhugoguimaraes/UnBall-Manager2D'
PREFIX = 'UNBALL_UPDATE '
MAX_DOWNLOAD = 100 * 1024 * 1024
MAX_EXTRACTED = 500 * 1024 * 1024


def emit(state, progress, message, **extra):
    print(PREFIX + json.dumps(dict(state=state, progress=progress, message=message, **extra), ensure_ascii=False), flush=True)


def revision(value):
    if not re.fullmatch(r'[0-9a-f]{40}', value):
        raise ValueError('A revisão recebida do GitHub é inválida.')
    return value


def atomic_json(path, value):
    descriptor, name = tempfile.mkstemp(prefix='.' + path.name, dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8') as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(name, path)
    finally:
        Path(name).unlink(missing_ok=True)


def latest_revision():
    request = urllib.request.Request('https://api.github.com/repos/' + REPOSITORY + '/git/ref/heads/main',
                                     headers={'User-Agent': 'UnBall-Manager2D-Updater', 'Accept': 'application/vnd.github+json'})
    with urllib.request.urlopen(request, timeout=30) as response:
        return revision(json.loads(response.read(1024 * 1024))['object']['sha'])


def download_archive(sha, destination):
    url = 'https://codeload.github.com/' + REPOSITORY + '/zip/' + revision(sha)
    request = urllib.request.Request(url, headers={'User-Agent': 'UnBall-Manager2D-Updater'})
    size = 0
    with urllib.request.urlopen(request, timeout=30) as response, destination.open('wb') as output:
        for chunk in iter(lambda: response.read(1024 * 1024), b''):
            size += len(chunk)
            if size > MAX_DOWNLOAD:
                raise ValueError('O pacote de atualização excede o tamanho permitido.')
            output.write(chunk)


def unpack(archive, destination):
    with zipfile.ZipFile(archive) as package:
        members = package.infolist()
        if not members or sum(item.file_size for item in members) > MAX_EXTRACTED:
            raise ValueError('Pacote de atualização vazio ou grande demais.')
        roots = set()
        for item in members:
            path = PurePosixPath(item.filename)
            if path.is_absolute() or '..' in path.parts or '\\' in item.filename or not path.parts:
                raise ValueError('Caminho inválido no pacote de atualização.')
            if stat.S_ISLNK(item.external_attr >> 16):
                raise ValueError('O pacote contém um link simbólico inesperado.')
            roots.add(path.parts[0])
        if len(roots) != 1:
            raise ValueError('O pacote não contém uma única pasta de projeto.')
        package.extractall(destination)
    source = destination / roots.pop()
    if not (source / 'CMakeLists.txt').is_file():
        raise ValueError('O pacote não contém o projeto do Manager.')
    return source


def run(command, log):
    log.write('\n' + repr(command) + '\n')
    log.flush()
    subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=1200)


def build_release(source, build, sha, log):
    emit('working', 45, 'Preparando a nova versão…')
    run(['cmake', '-S', str(source), '-B', str(build), '-DCMAKE_BUILD_TYPE=Release',
         '-DBUILD_TESTING=ON', '-DUNBALL_ENABLE_GUI_TESTS=OFF', '-DUNBALL_SOURCE_REVISION=' + sha], log)
    emit('working', 60, 'Compilando a atualização…')
    run(['cmake', '--build', str(build), '-j2'], log)
    emit('working', 85, 'Validando a nova versão…')
    run(['ctest', '--test-dir', str(build), '--output-on-failure'], log)


def validate_bundle(directory, sha):
    installed = json.loads((directory / 'installed.json').read_text())
    if installed.get('revision') != sha or not installed.get('validated') or not os.access(directory / 'UnBall-Manager2D', os.X_OK) or not (directory / 'extractor/unball_extract/__main__.py').is_file() or not (directory / 'updater/unball_update.py').is_file():
        raise ValueError('Há uma instalação incompleta desta versão. Consulte o log da atualização.')


def activate(root, sha, current_sha):
    anchor = current_sha
    pointer = root / 'current.json'
    if pointer.exists():
        previous = json.loads(pointer.read_text())
        candidate = previous.get('base_revision', current_sha)
        if isinstance(candidate, str) and (candidate == 'unknown' or re.fullmatch(r'[0-9a-f]{40}', candidate)):
            anchor = candidate
    atomic_json(pointer, {'revision': sha, 'base_revision': anchor})


def perform_update(root, current_sha, check_only=False):
    root = Path(root).resolve()
    root.mkdir(parents=True, exist_ok=True)
    with (root / '.update.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise ValueError('Outra janela já está atualizando a aplicação.') from error
        for previous in root.glob('.preparing-*'):
            if previous.is_dir() and not previous.is_symlink():
                shutil.rmtree(previous)
        emit('working', 5, 'Verificando a versão no GitHub…')
        sha = latest_revision()
        if sha == current_sha:
            emit('up_to_date', 100, 'Sua aplicação já está atualizada.', revision=sha)
            return
        if check_only:
            emit('available', 100, 'Há uma nova versão disponível.', revision=sha)
            return
        versions = root / 'versions'
        versions.mkdir(exist_ok=True)
        if (versions / sha).exists():
            validate_bundle(versions / sha, sha)
            activate(root, sha, current_sha)
            emit('installed', 100, 'Atualização já instalada. Reinicie para usar a nova versão.', revision=sha)
            return
        # Build and temporary installation remain on the same filesystem as versions.
        with tempfile.TemporaryDirectory(prefix='.preparing-', dir=root) as temporary:
            temporary = Path(temporary)
            archive = temporary / 'source.zip'
            emit('working', 20, 'Baixando a atualização…')
            download_archive(sha, archive)
            source = unpack(archive, temporary / 'source')
            build = temporary / 'build'
            with (root / 'update.log').open('w', encoding='utf-8') as log:
                build_release(source, build, sha, log)
            emit('working', 95, 'Instalando a versão validada…')
            package = temporary / 'package'
            package.mkdir()
            executable = build / 'UnBall-Manager2D'
            if not executable.is_file() or not os.access(executable, os.X_OK):
                raise ValueError('A compilação não gerou o executável esperado.')
            shutil.copy2(executable, package / executable.name)
            for filename in ('README.md', 'assets/COPYING-UNBALL'):
                notice = source / filename
                if notice.is_file():
                    shutil.copy2(notice, package / notice.name)
            for name in ('extractor', 'updater'):
                shutil.copytree(build / name, package / name, ignore=shutil.ignore_patterns('__pycache__'))
            if not (package / 'extractor/unball_extract/__main__.py').is_file() or not (package / 'updater/unball_update.py').is_file():
                raise ValueError('A atualização não contém todos os módulos necessários.')
            atomic_json(package / 'installed.json', {'revision': sha, 'validated': True})
            destination = versions / sha
            # Never overwrite a running release. A previously validated bundle can be reused.
            if destination.exists():
                validate_bundle(destination, sha)
            else:
                package.rename(destination)
            activate(root, sha, current_sha)
        emit('installed', 100, 'Atualização instalada. Reinicie a aplicação para usar a nova versão.', revision=sha)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--current-sha', required=True)
    parser.add_argument('--check-only', action='store_true')
    arguments = parser.parse_args()
    try:
        perform_update(arguments.root, arguments.current_sha, arguments.check_only)
        return 0
    except KeyboardInterrupt:
        emit('cancelled', 0, 'Atualização cancelada. A versão anterior continua disponível.')
        return 130
    except (OSError, ValueError, TypeError, KeyError, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        arguments.root.mkdir(parents=True, exist_ok=True)
        with (arguments.root / 'update.log').open('a', encoding='utf-8') as log:
            log.write('\nFalha: ' + str(error) + '\n')
        emit('failed', 0, 'Não foi possível atualizar. Consulte o log para os detalhes.')
        return 1


if __name__ == '__main__':
    sys.exit(main())

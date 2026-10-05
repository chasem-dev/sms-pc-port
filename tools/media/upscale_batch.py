#!/usr/bin/env python3
"""Make local 6x AI movie previews (2x AI followed by 3x AI).

Requires Python 3, FFmpeg, and Real-ESRGAN ncnn Vulkan. Outputs are
previews, not THP overrides. No generated media is uploaded.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import struct
import subprocess
import sys
import time
import zlib

from cutscenes import metadata

GIB = 1024 ** 3


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def write_json(path, value):
    temp = path.with_name(path.name + '.tmp')
    temp.write_text(json.dumps(value, indent=2) + '\n')
    os.replace(temp, path)


def valid_png(path, dimensions):
    """Check dimensions and chunk CRCs, including interrupted PNG writes."""
    try:
        with path.open('rb') as stream:
            if stream.read(8) != b'\x89PNG\r\n\x1a\n':
                return False
            first, pixels = True, False
            while True:
                header = stream.read(8)
                if len(header) != 8:
                    return False
                size, kind = struct.unpack('>I4s', header)
                if size > 64 * 1024 * 1024:
                    return False
                data = stream.read(size)
                crc = stream.read(4)
                if len(data) != size or len(crc) != 4:
                    return False
                if zlib.crc32(kind + data) & 0xffffffff != struct.unpack('>I', crc)[0]:
                    return False
                if first and (kind != b'IHDR' or size != 13 or
                              struct.unpack('>II', data[:8]) != dimensions):
                    return False
                first = False
                pixels |= kind == b'IDAT' and bool(size)
                if kind == b'IEND':
                    return size == 0 and pixels and not stream.read(1)
    except OSError:
        return False


def pending_frames(folder, count, dimensions):
    return [f'frame{i:06d}.png' for i in range(1, count + 1)
            if not valid_png(folder / f'frame{i:06d}.png', dimensions)]


def canvas_size(width, height):
    content = (width * 6, height * 6)
    # A taller movie stays taller. The small portal animation stays small.
    return (3840, 2160) if content[0] == 3840 and content[1] <= 2160 else content


def video_filter(content, canvas):
    if content == canvas:
        return 'setsar=1'
    return f'pad={canvas[0]}:{canvas[1]}:(ow-iw)/2:(oh-ih)/2:black,setsar=1'


def stop_process(process):
    if process.poll() is None:
        try:
            if os.name == 'posix':
                os.killpg(process.pid, signal.SIGTERM)
            else:
                process.terminate()
        except ProcessLookupError:
            return
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            if os.name == 'posix':
                os.killpg(process.pid, signal.SIGKILL)
            else:
                process.kill()
            process.wait()


class Batch:
    def __init__(self, config):
        self.config = config
        self.output = Path(config['output'])
        self.work = Path(config['work'])
        self.publish = Path(config['publish']) if config.get('publish') else None
        self.minimum_free = config.get('minimum_free_gb', 3) * GIB
        model = Path(config['models'])
        self.recipe = {'schema': 1, 'method': 'realesr-animevideov3: 2x AI then 3x AI',
                       'scales': [2, 3], 'crf': 16, 'preset': 'medium',
                       'models': {name: sha256(model / name) for name in
                                  ('realesr-animevideov3-x2.bin', 'realesr-animevideov3-x2.param',
                                   'realesr-animevideov3-x3.bin', 'realesr-animevideov3-x3.param')},
                       'ffmpeg_sha256': sha256(Path(config['ffmpeg'])),
                       'realesrgan_sha256': sha256(Path(config['realesrgan']))}

    def check_space(self):
        for folder in (self.output, self.work):
            free = shutil.disk_usage(folder).free
            if free < self.minimum_free:
                raise ValueError(f'Only {free/GIB:.1f} GiB free on {folder}. '
                                 'Free space and rerun the same command to resume.')

    def run(self, phase, command, job, progress_folder=None):
        job['phase'] = phase
        write_json(self.job_report, job)
        start = time.monotonic()
        log_path = self.job_report.parent / (phase + '.log')
        print(f'  {phase}: starting (log: {log_path})', flush=True)
        with log_path.open('w') as log:
            process = subprocess.Popen([str(arg) for arg in command], stdout=log,
                                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                       start_new_session=(os.name == 'posix'))
            try:
                while True:
                    try:
                        result = process.wait(timeout=10)
                        break
                    except subprocess.TimeoutExpired:
                        self.check_space()
                        if progress_folder:
                            frames = len(list(progress_folder.glob('frame*.png')))
                            print(f'  {phase}: {frames}/{job["source"]["frames"]} frames', flush=True)
                        else:
                            print(f'  {phase}: {time.monotonic()-start:.0f}s elapsed', flush=True)
                if result:
                    raise ValueError(f'{phase} failed (exit {result}). Read {log_path}; '
                                     'rerun the same command to resume.')
            except BaseException:
                stop_process(process)
                raise
        job.setdefault('timings', {})[phase] = time.monotonic() - start
        write_json(self.job_report, job)

    def ai_pass(self, source, output, count, dimensions, scale, job):
        output.mkdir(exist_ok=True)
        missing = pending_frames(output, count, dimensions)
        if not missing:
            print(f'  AI {scale}x: reusing {count} validated frames', flush=True)
            return
        # ncnn would overwrite completed frames if given the full input folder.
        pending = self.job_report.parent / f'pending-{scale}x'
        shutil.rmtree(pending, ignore_errors=True)
        pending.mkdir()
        try:
            for name in missing:
                (output / name).unlink(missing_ok=True)
                os.link(source / name, pending / name)
            print(f'  AI {scale}x: {count-len(missing)} reused, {len(missing)} to enhance', flush=True)
            self.run(f'ai-{scale}x', [self.config['realesrgan'], '-i', pending, '-o', output,
                                     '-m', self.config['models'], '-n', 'realesr-animevideov3',
                                     '-s', scale, '-g', self.config.get('gpu', '0'),
                                     '-j', '2:2:2'], job, output)
            invalid = pending_frames(output, count, dimensions)
            if invalid:
                raise ValueError(f'AI {scale}x produced {len(invalid)} missing or invalid frames. '
                                 'Rerun the same command to resume.')
        finally:
            shutil.rmtree(pending, ignore_errors=True)

    def verify(self, movie, count, job):
        progress = self.job_report.parent / 'verify-progress.txt'
        progress.unlink(missing_ok=True)
        self.run('verify', [self.config['ffmpeg'], '-hide_banner', '-nostdin', '-v', 'error',
                            '-xerror', '-i', movie, '-progress', progress, '-nostats',
                            '-f', 'null', '-'], job)
        counts = [int(line[6:]) for line in progress.read_text().splitlines()
                  if line.startswith('frame=')]
        if not counts or counts[-1] != count:
            raise ValueError(f'Preview has {counts[-1] if counts else 0} frames; expected {count}.')
        job['verified_frames'] = count

    def publish_movie(self, movie):
        if not self.publish:
            return
        self.publish.mkdir(parents=True, exist_ok=True)
        dest = self.publish / movie.name
        if dest.resolve() == movie.resolve():
            return
        digest = sha256(movie)
        if dest.exists():
            if sha256(dest) == digest:
                return
            raise ValueError(f'{dest} already contains a different movie. Move it aside first.')
        if shutil.disk_usage(self.publish).free < movie.stat().st_size + self.minimum_free:
            raise ValueError(f'Not enough space to copy {movie.name} to {self.publish}. '
                             'The verified original is retained; rerun after freeing space.')
        temp = dest.with_name(dest.name + '.part')
        shutil.copyfile(movie, temp)
        os.chmod(temp, 0o644)
        if sha256(temp) != digest:
            raise ValueError('Preview copy checksum did not match.')
        os.replace(temp, dest)

    def movie(self, row):
        name = Path(row['disc_path']).name
        source = Path(self.config['originals']) / name
        actual_hash = sha256(source)
        if actual_hash != row['sha256']:
            raise ValueError(f'{name} does not match the original disc manifest.')
        meta = metadata(source.read_bytes())
        content = (meta['width'] * 6, meta['height'] * 6)
        canvas = canvas_size(meta['width'], meta['height'])
        identity = dict(self.recipe, source_sha256=actual_hash, content_dimensions=list(content),
                        canvas_dimensions=list(canvas))
        folder = self.work / name
        folder.mkdir(exist_ok=True)
        self.job_report = folder / 'job.json'
        if self.job_report.exists():
            job = json.loads(self.job_report.read_text())
            if job['identity'] != identity:
                raise ValueError(f'{folder} belongs to a different source or AI recipe. Use a new work folder.')
        else:
            job = {'identity': identity, 'source': meta, 'complete': False, 'timings': {}}
            write_json(self.job_report, job)
        movie = self.output / f'sunshine-{Path(name).stem}-6x-ai-v1.mp4'
        temp = movie.with_name(movie.stem + '.tmp.mp4')
        if movie.exists():
            if job.get('complete') and sha256(movie) == job.get('output_sha256'):
                print('  Reusing verified completed preview', flush=True)
            elif job.get('verified_frames') == meta['frames'] and not job.get('complete'):
                self.verify(movie, meta['frames'], job)
            else:
                raise ValueError(f'{movie} does not match a completed job. Move it aside first.')
        else:
            self.check_space()
            native, ai2, ai6 = folder / 'native', folder / 'ai2', folder / 'ai6'
            ai6.mkdir(exist_ok=True)
            # A crash during encoding must not force another AI pass.
            if pending_frames(ai6, meta['frames'], content):
                ai2.mkdir(exist_ok=True)
                if pending_frames(ai2, meta['frames'], (meta['width']*2, meta['height']*2)):
                    native.mkdir(exist_ok=True)
                    if pending_frames(native, meta['frames'], (meta['width'], meta['height'])):
                        self.run('extract', [self.config['ffmpeg'], '-hide_banner', '-nostdin', '-y',
                                            '-i', source, '-map', '0:v:0', '-fps_mode', 'passthrough',
                                            native / 'frame%06d.png'], job, native)
                        if pending_frames(native, meta['frames'], (meta['width'], meta['height'])):
                            raise ValueError('Source frame extraction is incomplete.')
                    self.ai_pass(native, ai2, meta['frames'], (meta['width']*2, meta['height']*2), 2, job)
                self.ai_pass(ai2, ai6, meta['frames'], content, 3, job)
            fps = '30000/1001' if abs(meta['fps']-30000/1001) < .0001 else str(meta['fps'])
            self.run('encode', [self.config['ffmpeg'], '-hide_banner', '-nostdin', '-y',
                                '-framerate', fps, '-i', ai6 / 'frame%06d.png', '-i', source,
                                '-map', '0:v:0', '-map', '1:a:0?', '-vf', video_filter(content, canvas),
                                '-c:v', 'libx264', '-preset', 'medium', '-crf', '16',
                                '-threads', self.config.get('threads', 4), '-pix_fmt', 'yuv420p',
                                '-c:a', 'aac', '-b:a', '256k', '-movflags', '+faststart',
                                '-fps_mode', 'passthrough', temp], job)
            self.verify(temp, meta['frames'], job)
            # Record verification before the atomic rename, for crash recovery.
            write_json(self.job_report, job)
            os.replace(temp, movie)
        job.update(complete=True, phase='complete', output=str(movie),
                   output_sha256=sha256(movie), output_bytes=movie.stat().st_size)
        write_json(self.job_report, job)
        self.publish_movie(movie)
        if not self.config.get('keep_frames', False):
            for name in ('native', 'ai2', 'ai6'):
                shutil.rmtree(folder / name, ignore_errors=True)
        return job


def load_config(path):
    config = json.loads(path.read_text())
    for key in ('manifest', 'originals', 'output', 'work', 'ffmpeg', 'realesrgan', 'models'):
        config[key] = str((path.parent / config[key]).resolve())
    if config.get('publish'):
        config['publish'] = str((path.parent / config['publish']).resolve())
    for key in ('ffmpeg', 'realesrgan'):
        if not os.access(config[key], os.X_OK):
            raise ValueError(f'{key} is missing or not executable: {config[key]}')
    if Path(config['output']) == Path(config['work']):
        raise ValueError('Output and temporary work folders must be different.')
    if config.get('minimum_free_gb', 3) < 1:
        raise ValueError('minimum_free_gb must be at least 1.')
    return config


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--plan', action='store_true', help='check inputs and list scenes without processing')
    parser.add_argument('--only', help='process one named THP from the manifest')
    parser.add_argument('--background', action='store_true', help='run detached and print the progress log path')
    args = parser.parse_args()
    try:
        config = load_config(args.config.resolve())
        rows = json.loads(Path(config['manifest']).read_text())['movies']
        names = [Path(row['disc_path']).name for row in rows]
        if len(names) != len(set(names)) or any(not n.endswith('.thp') or n in ('.thp',) for n in names):
            raise ValueError('Movie names must be unique THP filenames.')
        if args.only and args.only not in names:
            raise ValueError(f'{args.only} is not in the disc manifest.')
        rows = [r for r in rows if (Path(r['disc_path']).name == args.only if args.only else
                                   Path(r['disc_path']).name not in config.get('skip', []))]
        rows.sort(key=lambda r: r['frames'])
        batch = Batch(config)
        print(f'{len(rows)} movies; 2x AI then 3x AI. Output: {batch.output}', flush=True)
        for row in rows:
            name = Path(row['disc_path']).name
            original = Path(config['originals']) / name
            if sha256(original) != row['sha256']:
                raise ValueError(f'{name} does not match the original disc manifest.')
            dimensions = canvas_size(row['width'], row['height'])
            print(f'  {name}: {row["frames"]} frames -> {dimensions[0]}x{dimensions[1]}', flush=True)
        if args.plan:
            print('Plan checked. No scenes were processed.', flush=True)
            return 0
        batch.output.mkdir(parents=True, exist_ok=True)
        batch.work.mkdir(parents=True, exist_ok=True)
        import fcntl
        if args.background:
            with (batch.work / '.batch.lock').open('a') as lock:
                try:
                    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError:
                    raise ValueError('This batch is already running. Do not start it a second time.')
            stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
            log_path = batch.work / f'batch-{stamp}-{os.getpid()}.log'
            command = [sys.executable, str(Path(__file__).resolve()), '--config', str(args.config.resolve())]
            if args.only:
                command += ['--only', args.only]
            environment = dict(os.environ, SMS_AI_BATCH_LOG=str(log_path))
            with log_path.open('w') as log:
                process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log,
                                           stderr=subprocess.STDOUT, start_new_session=True,
                                           env=environment)
            print(f'Started in background. PID: {process.pid}\nProgress log: {log_path}', flush=True)
            return 0
        def interrupted(signum, frame):
            raise KeyboardInterrupt
        signal.signal(signal.SIGTERM, interrupted)
        if hasattr(signal, 'SIGHUP'):
            signal.signal(signal.SIGHUP, interrupted)
        # The OS releases this lock after a crash, without a stale PID file.
        with (batch.work / '.batch.lock').open('a') as lock:
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                raise ValueError('This batch is already running. Do not start it a second time.')
            if os.environ.get('SMS_AI_BATCH_LOG'):
                write_json(batch.work / 'background.json', {'pid': os.getpid(),
                           'log': os.environ['SMS_AI_BATCH_LOG']})
            state = {'complete': False, 'recipe': batch.recipe, 'completed': [],
                     'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()}
            for index, row in enumerate(rows, 1):
                print(f'\n[{index}/{len(rows)}] {Path(row["disc_path"]).name}', flush=True)
                state['current'] = row['disc_path']
                write_json(batch.output / 'batch.json', state)
                job = batch.movie(row)
                state['completed'].append({'movie': row['disc_path'], 'output': job['output'],
                                           'sha256': job['output_sha256']})
                write_json(batch.output / 'batch.json', state)
            state.pop('current', None)
            state['complete'] = True
            write_json(batch.output / 'batch.json', state)
        print(f'\nFinished: {len(rows)} verified previews in {batch.output}', flush=True)
        return 0
    except (ValueError, OSError, KeyError) as error:
        print(f'Cannot continue: {error}', file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print('\nStopped. Rerun the same command to resume.', file=sys.stderr)
        return 130


if __name__ == '__main__':
    sys.exit(main())

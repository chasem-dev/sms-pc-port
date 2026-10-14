#!/usr/bin/env python3
"""Multiplayer screenshots: one hosting game, headless and scripted into
Delfino Plaza, with test bots (mp_bot) as the other players, and captures
of the host's screen converted to PNG.

usage: ext/multiplayer/tools/mp_shots.py --disc GMSE01.iso [--bots 3]
           [--shots 3800,4400,...] [--out DIR] [--build build/linux-64]
           [--autopress SPEC] [--warp 1,0,1] [--seconds 300] [--client]

--client also runs a second game that joins (instead of the first bot), and
captures its screen too (its captures go to OUT/client).
The game runs under a 2.5 GB memory cap (systemd-run), like the other test
tools on a small machine."""
import argparse, os, random, shutil, signal, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
PLAZA_AP = 'START@1400,STICK_LEFT@2000,A@2400,STICK_LEFT@3100,A@3250,A@3450,A@3800,STICK_UP@4400+60,B@4800'


def game_env(save_dir, shot_dir, shots, extra):
    env = {k: v for k, v in os.environ.items() if not k.startswith('SMS_')}
    env.update({
        'SMS_SETTINGS': os.path.join(ROOT, 'tools', 'regress', 'empty-settings.txt'),
        'SMS_TEXTURE_PACKS': '0', 'SMS_FRAME_RATE': '30', 'SMS_HEADLESS': '1', 'SMS_AUDIO': '0',
        'SMS_SKIP_MOVIES': '1', 'SMS_QUIET_STUBS': '1', 'SMS_SAVE_DIR': save_dir,
        'SMS_SHOT_DIR': shot_dir, 'SMS_SHOTS': shots,
    })
    env.update(extra)
    return env


def capped(cmd, mem='2500M'):
    if shutil.which('systemd-run'):
        return ['systemd-run', '--user', '--scope', '-q', '-p', 'MemoryMax=' + mem, '-p', 'MemorySwapMax=0', '--'] + cmd
    return cmd


def to_png(raw, out):
    from PIL import Image
    os.makedirs(out, exist_ok=True)
    made = []
    for f in sorted(os.listdir(raw)):
        if f.endswith('.ppm'):
            png = os.path.join(out, f[:-4] + '.png')
            Image.open(os.path.join(raw, f)).convert('RGB').save(png)
            made.append(png)
    return made


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--disc', required=True)
    ap.add_argument('--build', default=os.path.join(ROOT, 'build', 'linux-64'))
    ap.add_argument('--bots', type=int, default=3)
    ap.add_argument('--shots', default='3800,4300,4500,4700,4900,5200')
    ap.add_argument('--autopress', default=PLAZA_AP)
    ap.add_argument('--warp', default='1,0,1')
    ap.add_argument('--seconds', type=float, default=300)
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'multiplayer-shots'))
    ap.add_argument('--client', action='store_true')
    ap.add_argument('--client-shots', default=None)
    ap.add_argument('--client-autopress', default=None)
    ap.add_argument('--turn', action='store_true', help='bots face the camera')
    ap.add_argument('--offsets', default='130,-60;-130,-60;0,-170', help='bots\' places beside the host, X,Z;...')
    args = ap.parse_args()

    port = str(30000 + random.randrange(20000))
    work = tempfile.mkdtemp(prefix='mp-shots-')
    bot = os.path.join(ROOT, 'build-multiplayer-test', 'mp_bot')
    if args.bots and not os.path.exists(bot):
        subprocess.check_call(['make', '-C', HERE])
    procs = []
    try:
        raw = os.path.join(work, 'host-raw')
        os.makedirs(raw)
        host_save = os.path.join(work, 'host-save')
        os.makedirs(host_save)
        env = game_env(host_save, raw, args.shots, {
            'SMS_MULTIPLAYER': 'host', 'SMS_MULTIPLAYER_PORT': port, 'SMS_MULTIPLAYER_NAME': 'Host',
            'SMS_WARP': args.warp, 'SMS_AUTOPRESS': args.autopress, 'SMS_VI_DETERMINISTIC': '1',
            'SMS_MULTIPLAYER_DEBUG': '1'})
        host_log = open(os.path.join(work, 'host.log'), 'w')
        host = subprocess.Popen(capped([os.path.join(args.build, 'sms'), args.disc]), cwd=args.build, env=env,
                                stdout=host_log, stderr=subprocess.STDOUT, start_new_session=True)
        procs.append(host)
        time.sleep(3)
        client = None
        names = ['Luigi', 'Peach', 'Metal']
        offsets = args.offsets.split(';')
        first_bot = 0
        if args.client:
            craw = os.path.join(work, 'client-raw')
            os.makedirs(craw)
            csave = os.path.join(work, 'client-save')
            os.makedirs(csave)
            cenv = game_env(csave, craw, args.client_shots or args.shots, {
                'SMS_MULTIPLAYER': 'join', 'SMS_MULTIPLAYER_ADDRESS': '127.0.0.1:' + port,
                'SMS_MULTIPLAYER_NAME': 'Client', 'SMS_VI_DETERMINISTIC': '1', 'SMS_MULTIPLAYER_DEBUG': '1',
                **({'SMS_AUTOPRESS': args.client_autopress} if args.client_autopress else {})})
            client_log = open(os.path.join(work, 'client.log'), 'w')
            client = subprocess.Popen(capped([os.path.join(args.build, 'sms'), args.disc]), cwd=args.build,
                                      env=cenv, stdout=client_log, stderr=subprocess.STDOUT, start_new_session=True)
            procs.append(client)
            first_bot = 1
        for i in range(first_bot, args.bots):
            log = open(os.path.join(work, 'bot%d.log' % i), 'w')
            benv = dict(os.environ, MP_BOT_DEBUG='1', SMS_MULTIPLAYER_DEBUG='1')
            procs.append(subprocess.Popen([bot, '127.0.0.1:' + port, '--name', names[i % 3], '--offset', offsets[i % 3],
                                           '--seconds', str(args.seconds), '--coins', '3' if i == first_bot else '0']
                                          + (['--turn'] if args.turn else []),
                                          stdout=log, stderr=subprocess.STDOUT, start_new_session=True, env=benv))
            time.sleep(0.5)
        deadline = time.time() + args.seconds
        while time.time() < deadline and host.poll() is None:
            # done once the last capture is written
            last = max(int(x) for x in args.shots.split(','))
            if os.path.exists(os.path.join(raw, 'field%05d.ppm' % last)) and (
                    not client or os.path.exists(os.path.join(work, 'client-raw', 'field%05d.ppm' % max(
                        int(x) for x in (args.client_shots or args.shots).split(','))))):
                time.sleep(1)
                break
            time.sleep(1)
    finally:
        for p in procs:
            try:
                os.killpg(p.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        time.sleep(1)
        for p in procs:
            try:
                os.killpg(p.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            p.wait()
    os.makedirs(args.out, exist_ok=True)
    made = to_png(os.path.join(work, 'host-raw'), args.out)
    if args.client:
        made += to_png(os.path.join(work, 'client-raw'), os.path.join(args.out, 'client'))
    for f in os.listdir(work):
        if f.endswith('.log'):
            shutil.copy(os.path.join(work, f), os.path.join(args.out, f))
    shutil.rmtree(work, ignore_errors=True)
    for m in made:
        print(m)
    print('logs in', args.out)


if __name__ == '__main__':
    main()

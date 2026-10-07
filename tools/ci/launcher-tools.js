'use strict';

// Runs CI steps with the build tools SMS Launcher downloads for players
// (chasem-dev/sms-launcher, src/build-tools.js) instead of the runner's own.
//
//   node tools/ci/launcher-tools.js prepare           download and unpack them
//   node tools/ci/launcher-tools.js shell SCRIPT      run a bash script with them
//   node tools/ci/launcher-tools.js check BUILD...    fail unless CMake used them
//
// As a workflow's shell: `shell: node tools/ci/launcher-tools.js shell {0}`.
// SMS_LAUNCHER_DIR is an sms-launcher checkout after `npm ci --ignore-scripts`.
// The tools go in SMS_LAUNCHER_TOOLS, by default a folder with spaces in
// RUNNER_TEMP. SMS_ARCH=32 selects the launcher's 32-bit Linux SDK.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const launcher = path.resolve(process.env.SMS_LAUNCHER_DIR || 'launcher');
const userData = path.resolve(process.env.SMS_LAUNCHER_TOOLS ||
  path.join(process.env.RUNNER_TEMP || os.tmpdir(), 'SMS Launcher tools'));
const buildTools = require(path.join(launcher, 'src', 'build-tools'));
const { run, runMain } = require(path.join(launcher, 'scripts', 'tool-run'));

// Like a launcher opened from the desktop: only the private tools (and, on
// Mac, Apple's) provide compilers. Linux keeps /usr/bin for X and Mesa.
const systemPath = { win32: path.join(process.env.SystemRoot || 'C:\\Windows', 'System32'),
  darwin: '/usr/bin:/bin:/usr/sbin:/sbin', linux: '/usr/bin:/bin' }[process.platform];

function base() {
  // Windows names are case-insensitive: drop "Path" too.
  const dropped = ['PATH', 'HOMEBREW_PREFIX', 'CC', 'CXX', 'SMS_LLVM_BIN', 'MSYS2_ROOT', 'MSYSTEM'];
  const env = Object.fromEntries(Object.entries(process.env).filter(([name]) => !dropped.includes(name.toUpperCase())));
  return { ...env, PATH: systemPath };
}

async function environment() {
  // The Mac check finds Apple's SDK and fails without Command Line Tools or Rosetta.
  if (process.platform === 'darwin') await buildTools.check(userData, { env: base(), refresh: true, archives: true });
  if (buildTools.status(userData).mode !== 'private')
    throw new Error(`The launcher's build tools are not ready in ${userData}. Run "prepare" first.`);
  const env = buildTools.environment(userData, base());
  if (process.platform === 'darwin') env.PATH = `${env.SMS_BUILD_TOOLS_BIN}:${systemPath}`;
  return env;
}

async function prepare() {
  await buildTools.prepare(userData, { run, archives: true,
    progress(percent, detail) {
      if (detail) process.stdout.write(`${detail}\n`);
      else if (percent !== null && percent % 10 === 0) process.stdout.write(`Download tools: ${percent}%\n`);
    }
  });
  await environment();
  process.stdout.write(`Launcher build tools ${buildTools.toolsetFor()} are in ${buildTools.rootFor(userData)}\n`);
}

async function shell(script) {
  const env = await environment();
  const root = buildTools.rootFor(userData);
  const bash = process.platform === 'win32'
    ? path.join(env.MSYS2_ROOT, 'usr', 'bin', 'bash.exe') : path.join(root, 'env', 'bin', 'bash');
  // GitHub's own bash shell options.
  const result = spawnSync(bash, ['--noprofile', '--norc', '-eo', 'pipefail', script.replaceAll('\\', '/')],
    { env, stdio: 'inherit' });
  if (result.error) throw result.error;
  process.exitCode = result.status ?? 1;
}

async function check(builds) {
  const canonical = file => fs.realpathSync.native(file).replaceAll('\\', '/').toLowerCase();
  const root = `${canonical(buildTools.rootFor(userData))}/`;
  for (const build of builds) {
    const cache = fs.readFileSync(path.join(build, 'CMakeCache.txt'), 'utf8');
    for (const name of ['CMAKE_C_COMPILER', 'CMAKE_CXX_COMPILER']) {
      const tool = cache.match(new RegExp(`^${name}:[^=]*=(.+)$`, 'm'))?.[1];
      if (!tool || !canonical(tool).startsWith(root))
        throw new Error(`${build} used ${name}=${tool || 'unknown'}, not the launcher's build tools.`);
      process.stdout.write(`${build}: ${name}=${tool}\n`);
    }
  }
}

const [command, ...args] = process.argv.slice(2);
const commands = { prepare, shell: () => shell(args[0]), check: () => check(args) };
if (!commands[command]) {
  process.stderr.write('Usage: node tools/ci/launcher-tools.js prepare | shell SCRIPT | check BUILD...\n');
  process.exit(2);
}
runMain(commands[command]);

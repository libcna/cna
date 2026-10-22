// SPDX-License-Identifier: MS-PL
// Node 22+, Chrome; invoke through tools/platform/run_gpu_tests_private.sh --exec.
// Arguments: compiled probe .js, output report .json (must not already exist).
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import {spawn} from 'node:child_process';

const js = path.resolve(process.argv[2]);
const report = path.resolve(process.argv[3]);
if (!js.endsWith('.js') || !fs.existsSync(js) || fs.existsSync(report))
    throw Error('Provide an existing probe .js and a new report path');
if (process.platform === 'linux' && !process.env.XDG_RUNTIME_DIR?.startsWith('/tmp/cna-wl-'))
    throw Error('Run through the private GPU runner');
const name = path.basename(js);
const profile = fs.mkdtempSync(path.join(path.dirname(js), 'notification-chrome-'));
const log = [];
const exceptions = [];
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const server = http.createServer((req, res) => {
    if (req.url === '/') {
        res.setHeader('Content-Type', 'text/html');
        res.end(`<!doctype html><meta charset="utf-8"><script>
            var Module = {print: text => console.log(text), printErr: text => console.error(text),
                onExit: code => console.log('NOTIFICATION_EXIT ' + code),
                onAbort: text => console.error('NOTIFICATION_ABORT ' + text)};
            </script><script src="${name}"></script>`);
        return;
    }
    const file = path.basename(new URL(req.url, 'http://localhost').pathname);
    if (![name, name.replace(/\.js$/, '.wasm')].includes(file)) {
        res.writeHead(404); res.end(); return;
    }
    res.setHeader('Content-Type', file.endsWith('.wasm') ? 'application/wasm' : 'text/javascript');
    fs.createReadStream(path.join(path.dirname(js), file)).pipe(res);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const chromeLog = fs.openSync(report + '.chrome.log', 'wx');
const chrome = spawn(process.env.CNA_CHROME ?? 'google-chrome', [
    '--headless=new', '--no-sandbox', '--disable-gpu', '--no-first-run', '--no-default-browser-check',
    '--remote-debugging-port=0', '--user-data-dir=' + profile, 'about:blank'
], {stdio: ['ignore', chromeLog, chromeLog]});
let socket;
let error = null;
let exitCode = null;
try {
    const deadline = Date.now() + 60000;
    const activePort = path.join(profile, 'DevToolsActivePort');
    while (!fs.existsSync(activePort)) {
        if (chrome.exitCode !== null || Date.now() >= deadline) throw Error('Chrome startup failed');
        await pause(100);
    }
    const port = fs.readFileSync(activePort, 'utf8').split('\n')[0];
    const tabs = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
    socket = new WebSocket(tabs.find(tab => tab.type === 'page').webSocketDebuggerUrl);
    await new Promise((resolve, reject) => {
        socket.addEventListener('open', resolve, {once: true});
        socket.addEventListener('error', reject, {once: true});
    });
    let serial = 0;
    const pending = new Map();
    const call = (method, params = {}) => new Promise((resolve, reject) => {
        const id = ++serial;
        const timer = setTimeout(() => { pending.delete(id); reject(Error(method + ' timed out')); }, 10000);
        pending.set(id, {resolve, reject, timer});
        socket.send(JSON.stringify({id, method, params}));
    });
    socket.addEventListener('message', event => {
        const message = JSON.parse(event.data);
        if (message.id) {
            const handler = pending.get(message.id);
            pending.delete(message.id);
            clearTimeout(handler?.timer);
            if (message.error) handler?.reject(Error(JSON.stringify(message.error)));
            else handler?.resolve(message.result);
        }
        if (message.method === 'Runtime.consoleAPICalled') {
            const line = message.params.args.map(arg => arg.value ?? arg.description).join(' ');
            log.push(line);
            const exit = line.match(/^NOTIFICATION_EXIT (\d+)$/);
            if (exit) exitCode = Number(exit[1]);
        }
        if (message.method === 'Runtime.exceptionThrown')
            exceptions.push(message.params.exceptionDetails);
    });
    await call('Runtime.enable');
    await call('Page.enable');
    await call('Page.navigate', {url: `http://127.0.0.1:${server.address().port}/`});
    while (exitCode === null && !exceptions.length && Date.now() < deadline) await pause(100);
    if (exitCode === null) throw Error('No completed browser probe');
} catch (failure) {
    error = failure.stack;
} finally {
    socket?.close();
    chrome.kill('SIGTERM');
    await new Promise(resolve => chrome.exitCode !== null ? resolve() : chrome.once('exit', resolve));
    fs.closeSync(chromeLog);
    await new Promise(resolve => server.close(resolve));
}
const passed = !error && !exceptions.length && exitCode === 0 &&
    log.includes('NOTIFICATION_BROWSER raw=0 toast=0') &&
    log.includes('NOTIFICATION_OPEN errors=1 uris=0 closed=1 queued=1 correct=1') &&
    !log.some(line => line.includes('NOTIFICATION_ABORT'));
fs.writeFileSync(report, JSON.stringify({passed, error, exitCode, exceptions, log, profile}, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({passed, error, exitCode, report}));
process.exitCode = passed ? 0 : 1;

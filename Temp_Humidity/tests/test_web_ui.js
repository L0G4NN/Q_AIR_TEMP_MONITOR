// Run from any directory with: node Temp_Humidity/tests/test_web_ui.js
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../main/network/web_server.c'), 'utf8');
const pageSource = source.split('static const char s_home_page[] =')[1].split('\n\nstatic void')[0];
const html = [...pageSource.matchAll(/"(?:\\.|[^"\\])*"/g)].map(m => JSON.parse(m[0])).join('');
const script = html.split('<script>')[1].split('</script>')[0]
  .replace('updateClock();loadReading();loadHistory();setInterval(updateClock,1000);', '');

const elements = new Map();
const drawCalls = [];
const drawing = new Proxy({}, { get: (_, key) => (...args) => drawCalls.push([key, ...args]), set: () => true });
function element(id) {
  if (!elements.has(id)) elements.set(id, {
    innerHTML: '', textContent: '', setAttribute() {}, addEventListener() {},
    getContext: () => drawing, getBoundingClientRect: () => ({ width: 900, height: 340, left: 0, top: 0 }),
  });
  return elements.get(id);
}
let nextTimer = 0;
const timers = new Map();
let fetchImpl;
const context = vm.createContext({
  document: { getElementById: element }, window: { addEventListener() {}, devicePixelRatio: 1 },
  performance: { now: () => 1000 }, AbortController,
  fetch: (...args) => fetchImpl(...args),
  setTimeout: (callback, delay) => { timers.set(++nextTimer, { callback, delay }); return nextTimer; },
  clearTimeout: id => timers.delete(id), setInterval() { throw Error('Unexpected repeating polling timer'); },
});
vm.runInContext(script, context);
const run = code => vm.runInContext(code, context);

(async () => {
  // A reboot before the next successful sensor read must clear the old displayed values.
  fetchImpl = async () => ({ ok: true, json: async () => ({
    blink_enabled: true, has_reading: true, valid: true, temperature_c: 21,
    temperature_f: 69.8, humidity_percent: 50, read_count: 1, failure_count: 0, last_result: 'ESP_OK',
  }) });
  await run('loadReading()');
  assert.match(element('temp-c').innerHTML, /21\.0/);
  timers.clear();
  fetchImpl = async () => ({ ok: true, json: async () => ({ has_reading: false, valid: false }) });
  await run('loadReading()');
  assert.equal(element('temp-c').innerHTML, '--.- &deg;C');
  assert.equal(element('humidity').textContent, '--.- %');
  assert.deepEqual([...timers.values()].map(t => t.delay), [5000]);

  // While the request is in flight, only its timeout exists: no new poll can overlap it.
  timers.clear();
  fetchImpl = (_, options) => new Promise((resolve, reject) => {
    options.signal.addEventListener('abort', () => reject(Error('timed out')), { once: true });
  });
  const pending = run('loadReading()');
  assert.deepEqual([...timers.values()].map(t => t.delay), [8000]);
  [...timers.values()][0].callback();
  await pending;
  assert.equal(element('reading-note').textContent, 'Unable to update live reading.');
  assert.deepEqual([...timers.values()].map(t => t.delay), [5000]);

  // A failed POST must re-enable the button without launching another polling chain.
  timers.clear();
  fetchImpl = async () => ({ ok: false, status: 500 });
  await run('toggleBlink()');
  assert.equal(element('blink-toggle').disabled, false);
  assert.equal(timers.size, 0);

  // Missing minutes split each sensor's line instead of implying continuous readings.
  run("clockSynced=false;graphNow=240;historyInterval=60;samples=[{seconds_since_boot:60,temperature_c:20,humidity_percent:50},{seconds_since_boot:120,temperature_c:21,humidity_percent:51},{seconds_since_boot:240,temperature_c:22,humidity_percent:52}];draw();");
  const chart = drawCalls.filter(c => (c[0] === 'moveTo' || c[0] === 'lineTo') && c[1] !== 48 && c[1] !== 852);
  // Each series connects 60 -> 120, then starts a new subpath at 240.
  assert.equal(chart.filter(c => c[0] === 'lineTo').length, 2);
  drawCalls.length = 0;
  run('samples=samples.slice(0,1);draw();');
  assert.equal(drawCalls.filter(c => c[0] === 'arc').length, 2, 'single samples must be visible');
  drawCalls.length = 0;
  run("samples=[{seconds_since_boot:60,temperature_c:20,humidity_percent:50},{seconds_since_boot:180,temperature_c:21,humidity_percent:51},{seconds_since_boot:360,temperature_c:22,humidity_percent:52}];graphNow=360;draw();");
  assert.equal(drawCalls.filter(c => c[0] === 'arc').length, 6, 'every isolated sample needs a marker for each series');
  drawCalls.length = 0;
  run("samples=[{seconds_since_boot:60,temperature_c:20,humidity_percent:50},{seconds_since_boot:120,temperature_c:21,humidity_percent:51},{seconds_since_boot:300,temperature_c:22,humidity_percent:52}];graphNow=300;draw();");
  assert.equal(drawCalls.filter(c => c[0] === 'arc').length, 2, 'only the isolated point needs markers beside a continuous segment');
  console.log('PASS: reboot reset, bounded sequential polling, failed toggle, graph gaps, isolated samples');
})().catch(error => { console.error(error); process.exitCode = 1; });

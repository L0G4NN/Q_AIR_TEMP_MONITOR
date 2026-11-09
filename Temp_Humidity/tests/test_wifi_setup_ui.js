const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { test } = require('node:test');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../main/network/wifi_setup.c'), 'utf8');
const page = source.split('static const char setup_page[] =')[1].split('\nbool wifi_setup_request')[0]
    .match(/"(?:\\.|[^"\\])*"/g).map(literal => JSON.parse(literal)).join('');
const script = page.match(/<script>(.*?)<\/script>/s)[1];

async function openSetup() {
    const elements = new Map();
    const model = {
        status: { busy: false, saved: false, message: 'Choose a network.', address: '' },
        scans: '4c6162\n636166e9\n2020\n',
        submitted: [],
    };
    function element(id) {
        if (!elements.has(id)) {
            elements.set(id, {
                value: '',
                options: [],
                events: {},
                get firstChild() { return this.options[0]; },
                get selectedIndex() { return this.options.findIndex(option => option.value === this.value); },
                addEventListener(name, handler) { this.events[name] = handler; },
                replaceChildren(...children) {
                    this.options = children;
                    if (id === 'networks') this.value = children[0]?.value ?? '';
                },
                add(option) { this.options.push(option); },
            });
        }
        return elements.get(id);
    }
    let poll;
    vm.runInNewContext(script, {
        document: { getElementById: element, createElement: () => ({}) },
        TextEncoder, TextDecoder, Uint8Array,
        Option: function (textContent, value) { Object.assign(this, { textContent, value }); },
        setTimeout(callback) { poll = callback; },
        async fetch(url, options) {
            if (url === '/setup/status') return { ok: true, json: async () => model.status };
            if (url === '/setup/networks') return { ok: true, text: async () => model.scans };
            assert.equal(url, '/setup/connect');
            model.submitted.push(Buffer.from(options.body));
            return { ok: true };
        },
    });
    await new Promise(setImmediate);
    return {
        model,
        element,
        async poll() {
            assert.equal(typeof poll, 'function', 'the portal should have scheduled a status check');
            const run = poll;
            poll = undefined;
            await run();
        },
        scan: () => element('scan').events.click(),
        select(value) {
            element('networks').value = value;
            element('networks').events.change();
        },
        submit: () => element('setup').events.submit({ preventDefault() {} }),
    };
}

test('saved setup keeps checking for an address, removes a lost link, and shows the replacement', async () => {
    const setup = await openSetup();
    const { model, element } = setup;
    model.status = { busy: false, saved: true, message: 'Waiting for an address.', address: '' };
    await setup.poll();
    assert.equal(element('controls').disabled, true);
    assert.equal(element('message').textContent, 'Waiting for an address.');
    assert.equal(element('address').firstChild, undefined);

    model.status.address = '192.168.1.7';
    await setup.poll();
    assert.equal(element('address').firstChild.href, 'http://192.168.1.7/');

    model.status.address = '';
    await setup.poll();
    assert.equal(element('address').firstChild, undefined);

    model.status.address = '192.168.1.9';
    await setup.poll();
    assert.equal(element('address').firstChild.href, 'http://192.168.1.9/');
});

test('scan preserves raw SSID bytes through selection, rescan and manual edits', async () => {
    const setup = await openSetup();
    const { element, model } = setup;
    await setup.scan();
    assert.equal(element('networks').options.length, 4);

    setup.select('636166e9');
    assert.equal(element('ssid').value, 'SSID 0x636166e9');
    await setup.scan();
    await setup.submit();
    assert.deepEqual(model.submitted[0], Buffer.from('04636166e9', 'hex'));

    setup.select('2020');
    await setup.submit();
    assert.deepEqual(model.submitted[1], Buffer.from('022020', 'hex'));

    model.scans = '4c6162\n';
    await setup.scan();
    assert.equal(element('ssid').value, '');
    await setup.submit();
    assert.equal(model.submitted.length, 2);

    setup.select('4c6162');
    element('ssid').value = 'café';
    element('ssid').events.input();
    await setup.submit();
    assert.deepEqual(model.submitted[2], Buffer.from('05636166c3a9', 'hex'));
});
